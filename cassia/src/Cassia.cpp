#include <webgpu/webgpu_cpp.h>
#include <dawn/dawn_proc.h>
#include <dawn_native/DawnNative.h>
#include "GLFW/glfw3.h"
#include "utils/GLFWUtils.h"

#include <iostream>
#include <algorithm>

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

        // Now upload the data to the GPU, render to a buffer and blit on the screen!
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
