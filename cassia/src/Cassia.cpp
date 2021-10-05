#include <webgpu/webgpu_cpp.h>
#include <dawn/dawn_proc.h>
#include <dawn_native/DawnNative.h>
#include "GLFW/glfw3.h"
#include "utils/GLFWUtils.h"
#include "utils/WGPUHelpers.h"

#include <iostream>
#include <algorithm>

wgpu::Buffer CreateBufferFromData(const wgpu::Device& device, wgpu::BufferUsage usage, const void* data, size_t size) {
    wgpu::BufferDescriptor bufDesc;
    bufDesc.size = size;
    bufDesc.usage = usage;
    bufDesc.mappedAtCreation = true;
    wgpu::Buffer buffer = device.CreateBuffer(&bufDesc);
    memcpy(buffer.GetMappedRange(), data, size);
    buffer.Unmap();
    return buffer;
}

class StupidComputeRenderer {
    struct Uniforms {
        uint32_t width;
        uint32_t height;
        uint32_t segmentCount;
    };
    static_assert(sizeof(Uniforms) == 12, "");

  public:
    StupidComputeRenderer(wgpu::Device device, uint32_t width, uint32_t height) : mDevice(device), mWidth(width), mHeight(height) {
        wgpu::ShaderModule module = utils::CreateShaderModule(mDevice, R"(
            [[block]] struct Config {
                width: u32;
                height: u32;
                count: u32;
            };
            [[group(0), binding(0)]] var<uniform> config : Config;

            struct PSegments {
                u32: low;
                u32: high;
            };
            // TODO define helpers to extract data from the PSegment

            [[block]] struct PSegments {
                array<PSegment> segments;
            };
            [[group(0), binding(1)]] var<storage> in : PSegments;
            [[group(0), binding(2)]] var out : texture_storage_2d<rgba16floati, write>;

            [[stage(compute), workgroup_size(8, 8)]]
            fn main([[builtin(global_invocation_id)]] GlobalId : vec3<u32>) {
                if (GlobalId.x >= config.width || GlobalId.y >= config.height) {
                    return;
                }

                var accum = vec4<f32>(0.0);
                for (var i = 0u; i < config.count; i = i + 1u) {
                    var segment = in.segments[i];
                    // TODO Computations, accumulator, if on our line and before us
                }

                textureStore(out, vec<i32>(GlobalId.xy), accum);
            }
        )");

        wgpu::ComputePipelineDescriptor pDesc;
        pDesc.compute.module = module;
        pDesc.compute.entryPoint = "main";
        mPipeline = mDevice.CreateComputePipeline(&pDesc);
    }

    wgpu::Texture Render(wgpu::Buffer sortedPsegments, uint32_t segmentCount) {
        Uniforms u = {mWidth, mHeight, segmentCount};
        wgpu::Buffer uniforms = CreateBufferFromData(mDevice, wgpu::BufferUsage::Uniform, &u, sizeof(u));

        wgpu::TextureDescriptor texDesc;
        texDesc.size = {mWidth, mHeight};
        texDesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
        texDesc.format = wgpu::TextureFormat::RGBA16Float;
        wgpu::Texture outTexture = mDevice.CreateTexture(&texDesc);

        wgpu::BindGroup bg = utils::MakeBindGroup(mDevice, mPipeline.GetBindGroupLayout(0), {
            {0, uniforms},
            {1, sortedPsegments},
            {2, outTexture.CreateView()}
        });

        wgpu::CommandEncoder encoder = mDevice.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetBindGroup(0, bg);
        pass.SetPipeline(mPipeline);
        pass.Dispatch(mWidth / 8 + 1, mHeight / 8 + 1);
        pass.EndPass();

        wgpu::CommandBuffer commands = encoder.Finish();
        mDevice.GetQueue().Submit(1, &commands);

        return outTexture;
    }

  private:
    wgpu::ComputePipeline mPipeline;
    wgpu::Device mDevice;
    uint32_t mWidth, mHeight;
};

class Cassia {
  public:
    Cassia(uint32_t width, uint32_t height) {
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
        mDevice = wgpu::Device::Acquire(adapter.CreateDevice());

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
        swapchainDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        swapchainDesc.width = width;
        swapchainDesc.height = height;
        swapchainDesc.presentMode = wgpu::PresentMode::Mailbox;
        mSwapchain = mDevice.CreateSwapChain(mSurface, &swapchainDesc);
    }

    void Render(const uint64_t* psegmentsIn, size_t psegmentCount) {
        std::vector<uint64_t> psegments(psegmentsIn, psegmentsIn + psegmentCount);
        std::sort(psegments.begin(), psegments.end());

        wgpu::Buffer sortedPsegments =
            CreateBufferFromData(mDevice, wgpu::BufferUsage::Storage, psegments.data(), psegments.size() * sizeof(uint64_t));

        // TODO: Make a stupid renderer and call it to get a float16 storage texture from the pSegments.

        // TODO Do a manual blit from the storage texture to the swapchain renderable texture.
        // Use utils/ComboRenderPipelineDescriptor.h and the vertex_index trick where we index a constant
        // array of positions in the vertex shader to make this easy to write.
    }

    ~Cassia() {
        mWindow = nullptr;
        mSwapchain = nullptr;
        mDevice = nullptr;
        mInstance = nullptr;
        if (mWindow) {
            glfwDestroyWindow(mWindow);
        }
    }

  private:
    wgpu::Device mDevice;
    wgpu::SwapChain mSwapchain;
    wgpu::Surface mSurface;
    std::unique_ptr<dawn_native::Instance> mInstance;
    GLFWwindow* mWindow = nullptr;
};

extern "C" {
    static Cassia* sCassia = nullptr;


    // TODO make an actual header for this? So we can have a standalone program taking
    // a psegment file and renders it to the screen?
    __attribute__((visibility("default"))) void cassia_init(uint32_t width, uint32_t height) {
        sCassia = new Cassia(width, height);
    }

    __attribute__((visibility("default"))) void cassia_render(const uint64_t* psegments, size_t psegmentCount) {
    }

    __attribute__((visibility("default"))) void cassia_shutdown() {
        delete sCassia;
        sCassia = nullptr;
    }
}
