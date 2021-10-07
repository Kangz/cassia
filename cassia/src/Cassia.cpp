#include "Cassia.h"

#include "EncodingContext.h"
#include "NaiveComputeRasterizer.h"

#include <webgpu/webgpu_cpp.h>
#include <dawn/dawn_proc.h>
#include <dawn_native/DawnNative.h>
#include "GLFW/glfw3.h"
#include "utils/GLFWUtils.h"
#include "utils/WGPUHelpers.h"
#include "utils/ComboRenderPipelineDescriptor.h"

#include <algorithm>
#include <iostream>
#include <memory>

namespace cassia {

    class Cassia {
      public:
        Cassia(uint32_t width, uint32_t height): mWidth(width), mHeight(height) {
            // Setup dawn native and its instance
            mInstance = std::make_unique<dawn_native::Instance>();
            DawnProcTable nativeProcs = dawn_native::GetProcs();
            dawnProcSetProcs(&nativeProcs);

            // Create a device, set it up to print errors.
            mInstance->DiscoverDefaultAdapters();
            // TODO choose an adapter that we like instead of the first one?
            dawn_native::Adapter adapter = mInstance->GetAdapters()[0];
            wgpu::AdapterProperties adapterProperties;
            adapter.GetProperties(&adapterProperties);
            std::cout << "Using adapter " << adapterProperties.name << std::endl;

            // Check for timestamp support.
            mTimestampsSupported = false;
            for (const char* extension : adapter.GetSupportedFeatures()) {
                if (extension == std::string("timestamp_query")) { // ewwww
                    std::cout << "Timestamps are supported!" << std::endl;
                    mTimestampsSupported = true;
                    break;
                }
            }

            // Create the device
            dawn_native::DeviceDescriptor deviceDesc;
            if (mTimestampsSupported) {
                deviceDesc.requiredFeatures.push_back("timestamp_query");
                deviceDesc.forceDisabledToggles.push_back("disallow_unsafe_apis");
            }
            mDevice = wgpu::Device::Acquire(adapter.CreateDevice(&deviceDesc));
            mDevice.SetUncapturedErrorCallback([](WGPUErrorType, const char* message, void*) {
                std::cerr << "Dawn error: " << message;
            }, nullptr);
            mQueue = mDevice.GetQueue();

            // Create the GLFW window
            glfwSetErrorCallback([](int code, const char* message) {
                std::cerr << "GLFW error: " << code << " - " << message << std::endl;
            });
            if (!glfwInit()) {
                return;
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
            mWindow = glfwCreateWindow(width, height, "Paths!", nullptr, nullptr);

            // Create the swapchain
            mSurface = utils::CreateSurfaceForWindow(mInstance->Get(), mWindow);
            wgpu::SwapChainDescriptor swapchainDesc;
            swapchainDesc.label = "cassia swapchain";
            swapchainDesc.usage = wgpu::TextureUsage::RenderAttachment;
            swapchainDesc.format = wgpu::TextureFormat::BGRA8Unorm;
            swapchainDesc.width = width;
            swapchainDesc.height = height;
            swapchainDesc.presentMode = wgpu::PresentMode::Mailbox;
            mSwapchain = mDevice.CreateSwapChain(mSurface, &swapchainDesc);

            // Create the pipeline used to blit on the screen
            wgpu::ShaderModule blitModule = utils::CreateShaderModule(mDevice, R"(
                struct VertexOutput {
                    [[builtin(position)]] Position : vec4<f32>;
                    [[location(0)]] fragUV : vec2<f32>;
                };
                [[stage(vertex)]]
                fn vsMain([[builtin(vertex_index)]] index : u32) -> VertexOutput {
                    var positions = array<vec2<f32>, 4>(
                        vec2<f32>(1.0, 1.0),
                        vec2<f32>(1.0, -1.0),
                        vec2<f32>(-1.0, 1.0),
                        vec2<f32>(-1.0, -1.0),
                    );
                    var pos = positions[index];

                    var output : VertexOutput;
                    output.Position = vec4<f32>(pos, 0.0, 1.0);
                    output.fragUV = (pos + vec2<f32>(1.0)) / 2.0;
                    return output;
                }

                [[group(0), binding(0)]] var s : sampler;
                [[group(0), binding(1)]] var t : texture_2d<f32>;
                [[stage(fragment)]]
                fn fsMain([[location(0)]] uv : vec2<f32>) -> [[location(0)]] vec4<f32> {
                    return textureSample(t, s, uv);
                }
            )");
            utils::ComboRenderPipelineDescriptor pDesc;
            pDesc.label = "blit pipeline";
            pDesc.vertex.module = blitModule;
            pDesc.vertex.entryPoint = "vsMain";
            pDesc.cFragment.module = blitModule;
            pDesc.cFragment.entryPoint = "fsMain";
            pDesc.cTargets[0].format = wgpu::TextureFormat::BGRA8Unorm;
            pDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
            pDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Uint32;
            mBlitPipeline = mDevice.CreateRenderPipeline(&pDesc);

            // Create sub components
            mRasterizer = std::make_unique<NaiveComputeRasterizer>(mDevice);
        }

        void Render(
            const uint64_t* psegmentsIn,
            size_t psegmentCount,
            const CassiaStyling* stylings,
            size_t stylingCount
        ) {
            glfwPollEvents();

            // Sort using the CPU for now.
            std::vector<uint64_t> psegments(psegmentsIn, psegmentsIn + psegmentCount);
            std::sort(psegments.begin(), psegments.end());

            wgpu::Buffer sortedPsegments = utils::CreateBufferFromData(
                    mDevice, psegments.data(), psegments.size() * sizeof(uint64_t),
                    wgpu::BufferUsage::Storage);
            wgpu::Buffer stylingsBuffer = utils::CreateBufferFromData(
                    mDevice, stylings, stylingCount * sizeof(CassiaStyling),
                    wgpu::BufferUsage::Storage);

            // Run all the steps of the algorithm.
            EncodingContext context(mDevice, mTimestampsSupported);

            NaiveComputeRasterizer::Config config = {mWidth, mHeight, static_cast<uint32_t>(psegmentCount)};
            wgpu::Texture picture = mRasterizer->Rasterize(&context, sortedPsegments, stylingsBuffer, config);

            // Do the blit into the swapchain.
            {
                wgpu::BindGroup blitBindGroup = utils::MakeBindGroup(
                        mDevice, mBlitPipeline.GetBindGroupLayout(0), {
                    {0, mDevice.CreateSampler()},
                    {1, picture.CreateView()},
                });

                utils::ComboRenderPassDescriptor rpDesc({{mSwapchain.GetCurrentTextureView()}});
                rpDesc.cColorAttachments[0].loadOp = wgpu::LoadOp::Clear;
                rpDesc.cColorAttachments[0].storeOp = wgpu::StoreOp::Store;
                rpDesc.cColorAttachments[0].clearColor = {0.0, 0.0, 0.0, 0.0};
                ScopedRenderPass pass(&context, rpDesc, "Cassia::BlitToSwapChain");

                pass->SetPipeline(mBlitPipeline);
                pass->SetBindGroup(0, blitBindGroup);
                pass->Draw(4);
            }

            // Submit all the commands!
            context.SubmitOn(mQueue);
            mSwapchain.Present();
        }

        ~Cassia() {
            mRasterizer = nullptr;

            mBlitPipeline = nullptr;
            mWindow = nullptr;
            mSwapchain = nullptr;
            mQueue = nullptr;
            mDevice = nullptr;
            mInstance = nullptr;
            if (mWindow) {
                glfwDestroyWindow(mWindow);
            }
        }

      private:
        std::unique_ptr<NaiveComputeRasterizer> mRasterizer;

        wgpu::RenderPipeline mBlitPipeline;
        wgpu::Queue mQueue;
        wgpu::Device mDevice;
        wgpu::SwapChain mSwapchain;
        wgpu::Surface mSurface;
        std::unique_ptr<dawn_native::Instance> mInstance;
        GLFWwindow* mWindow = nullptr;

        uint32_t mWidth, mHeight;
        bool mTimestampsSupported;
    };

    static std::unique_ptr<Cassia> sCassia;

} // namespace cassia

void cassia_init(uint32_t width, uint32_t height) {
    cassia::sCassia = std::make_unique<cassia::Cassia>(width, height);
}

void cassia_render(
    const uint64_t* psegments,
    size_t psegmentCount,
    const CassiaStyling* stylings,
    size_t stylingCount
) {
    cassia::sCassia->Render(psegments, psegmentCount, stylings, stylingCount);
}

void cassia_shutdown() {
    cassia::sCassia = nullptr;
}
