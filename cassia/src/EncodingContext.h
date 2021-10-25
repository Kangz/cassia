#ifndef CASSIA_ENCODINGCONTEXT_H
#define CASSIA_ENCODINGCONTEXT_H

#include "webgpu/webgpu_cpp.h"

#include <string>
#include <vector>

namespace cassia {

    class EncodingContext {
      public:
        EncodingContext(wgpu::Device device, bool hasTimestamps);

        const wgpu::CommandEncoder& GetEncoder() const;
        void SubmitOn(const wgpu::Queue& queue);

      private:
        friend class ScopedCPUPass;
        friend class ScopedComputePass;
        friend class ScopedRenderPass;

        void OnStartPass(const char* name, bool hasGPU);
        void OnEndPass();

        wgpu::CommandEncoder mEncoder;
        wgpu::Device mDevice;

        struct Scope {
            std::string name;
            uint64_t startCpuTimeNs;
            uint64_t endCpuTimeNs;
            bool hasGPU;
        };
        std::vector<Scope> mScopes;

        bool mGatherTimestamps;
        wgpu::QuerySet mGpuTimestamps;
    };

    class ScopedCPUPass {
      public:
        ScopedCPUPass(EncodingContext* context, const char* name);
        ~ScopedCPUPass();

      private:
        EncodingContext* mParentContext;
    };

    class ScopedComputePass {
      public:
        ScopedComputePass(EncodingContext* context, const char* name);
        ~ScopedComputePass();

        const wgpu::ComputePassEncoder* operator->() const;

      private:
        wgpu::ComputePassEncoder mPass;
        EncodingContext* mParentContext;
    };

    class ScopedRenderPass {
      public:
        ScopedRenderPass(EncodingContext* context, const wgpu::RenderPassDescriptor& desc, const char* name);
        ~ScopedRenderPass();

        const wgpu::RenderPassEncoder* operator->() const;

      private:
        wgpu::RenderPassEncoder mPass;
        EncodingContext* mParentContext;
    };

} // namespace cassia

#endif // CASSIA_ENCODINGCONTEXT_H
