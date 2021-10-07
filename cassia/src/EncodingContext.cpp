#include "EncodingContext.h"

namespace cassia {

    // EncodingContext

    EncodingContext::EncodingContext(wgpu::Device device) {
        mEncoder = device.CreateCommandEncoder();
    }

    const wgpu::CommandEncoder& EncodingContext::GetEncoder() const {
        return mEncoder;
    }

    wgpu::CommandBuffer EncodingContext::Finish() {
        wgpu::CommandBuffer commands = mEncoder.Finish();
        mEncoder = nullptr;
        return commands;
    }

    void EncodingContext::OnStartComputePass(const char* name) {
        mEncoder.PushDebugGroup(name);
    }

    void EncodingContext::OnEndComputePass() {
        mEncoder.PopDebugGroup();
    }

    void EncodingContext::OnStartRenderPass(const char* name) {
        mEncoder.PushDebugGroup(name);
    }

    void EncodingContext::OnEndRenderPass() {
        mEncoder.PopDebugGroup();
    }

    // ScopedComputePass

    ScopedComputePass::ScopedComputePass(EncodingContext* context, const char* name): mParentContext(context) {
        mParentContext->OnStartComputePass(name);
        mPass = mParentContext->GetEncoder().BeginComputePass();
    }

    ScopedComputePass::~ScopedComputePass() {
        mPass.EndPass();
        mParentContext->OnEndComputePass();
    }

    const wgpu::ComputePassEncoder* ScopedComputePass::operator->() const {
        return &mPass;
    }

    // ScopedRenderPass

    ScopedRenderPass::ScopedRenderPass(EncodingContext* context, const wgpu::RenderPassDescriptor& desc, const char* name): mParentContext(context) {
        mParentContext->OnStartRenderPass(name);
        mPass = mParentContext->GetEncoder().BeginRenderPass(&desc);
    }

    ScopedRenderPass::~ScopedRenderPass() {
        mPass.EndPass();
        mParentContext->OnEndRenderPass();
    }

    const wgpu::RenderPassEncoder* ScopedRenderPass::operator->() const {
        return &mPass;
    }

}
