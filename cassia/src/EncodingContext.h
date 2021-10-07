#ifndef CASSIA_ENCODINGCONTEXT_H
#define CASSIA_ENCODINGCONTEXT_H

#include "webgpu/webgpu_cpp.h"

namespace cassia {

    class EncodingContext {
      public:
        EncodingContext(wgpu::Device device);

        const wgpu::CommandEncoder& GetEncoder() const;
        wgpu::CommandBuffer Finish();

      private:
        friend class ScopedComputePass;
        friend class ScopedRenderPass;

        void OnStartComputePass(const char* name);
        void OnEndComputePass();

        void OnStartRenderPass(const char* name);
        void OnEndRenderPass();

        wgpu::CommandEncoder mEncoder;
        wgpu::Device mDevice;

        // TODO collect scopes with CPU timestamps
        // TODO add GPU timestamps
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
