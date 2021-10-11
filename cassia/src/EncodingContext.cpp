#include "EncodingContext.h"

#include <chrono>
#include <iostream>
#include <memory>

namespace cassia {

    namespace {
        uint64_t GetNowAsNS() {
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        }
    }

    // EncodingContext

    EncodingContext::EncodingContext(wgpu::Device device, bool hasTimestamps) : mGatherTimestamps(hasTimestamps), mDevice(std::move(device)) {
        mEncoder = mDevice.CreateCommandEncoder();

        if (!mGatherTimestamps) {
            return;
        }

        wgpu::QuerySetDescriptor queryDesc;
        queryDesc.type = wgpu::QueryType::Timestamp;
        queryDesc.count = 1000;
        mGpuTimestamps = mDevice.CreateQuerySet(&queryDesc);
    }

    const wgpu::CommandEncoder& EncodingContext::GetEncoder() const {
        return mEncoder;
    }

    void EncodingContext::SubmitOn(const wgpu::Queue& queue) {
        // Resolve the queries as uint64_ts in readbackBuffer.
        wgpu::Buffer timestampReadback;
        if (mGatherTimestamps) {
            wgpu::BufferDescriptor bufDesc;
            bufDesc.size = sizeof(uint64_t) * mScopes.size() * 2;
            bufDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
            wgpu::Buffer resolveBuffer = mDevice.CreateBuffer(&bufDesc);

            bufDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            timestampReadback = mDevice.CreateBuffer(&bufDesc);

            mEncoder.ResolveQuerySet(mGpuTimestamps, 0, mScopes.size() * 2, resolveBuffer, 0);
            mEncoder.CopyBufferToBuffer(resolveBuffer, 0, timestampReadback, 0, bufDesc.size);
        }

        wgpu::CommandBuffer commands = mEncoder.Finish();
        queue.Submit(1, &commands);

        mEncoder = nullptr;
        mGpuTimestamps = nullptr;

        // Map the timestamp buffer asynchronously and print timestamp data when done.
        if (mGatherTimestamps) {
            struct Userdata {
                std::vector<Scope> scopes;
                wgpu::Buffer gpuTimestampBuffer;

                void PrintTimestamps() {
                    const uint64_t* gpuTimestamps = reinterpret_cast<const uint64_t*>(gpuTimestampBuffer.GetConstMappedRange());
                    std::cout << "Scopes:" << std::endl;
                    for (size_t i = 0; i < scopes.size(); i++) {
                        float cpuTimeMs = (scopes[i].endCpuTimeNs - scopes[i].startCpuTimeNs) / 1000'000.0;
                        float gpuTimeMs = (gpuTimestamps[i * 2 + 1] - gpuTimestamps[i * 2]) / 1000'000.0;

                        std::cout << " - " << scopes[i].name << std::endl;
                        std::cout << "   - CPU time: " << cpuTimeMs << "ms" << std::endl;
                        std::cout << "   - GPU time: " << gpuTimeMs << "ms" << std::endl;
                    }
                }
            };
            Userdata* userdata = new Userdata;
            userdata->scopes = std::move(mScopes);
            userdata->gpuTimestampBuffer = timestampReadback;

            timestampReadback.MapAsync(wgpu::MapMode::Read, 0, 0, [](WGPUBufferMapAsyncStatus, void* userdataIn) {
                std::unique_ptr<Userdata> userdata(static_cast<Userdata*>(userdataIn));
                userdata->PrintTimestamps();
            }, userdata);
        }
    }

    void EncodingContext::OnStartPass(const char* name) {
        mEncoder.PushDebugGroup(name);

        if (!mGatherTimestamps) {
            return;
        }

        mScopes.push_back({});
        mScopes.back().name = name;
        mScopes.back().startCpuTimeNs = GetNowAsNS();
        mEncoder.WriteTimestamp(mGpuTimestamps, mScopes.size() * 2 - 2);
    }

    void EncodingContext::OnEndPass() {
        mEncoder.PopDebugGroup();

        if (!mGatherTimestamps) {
            return;
        }

        mScopes.back().endCpuTimeNs = GetNowAsNS();
        mEncoder.WriteTimestamp(mGpuTimestamps, mScopes.size() * 2 - 1);
    }

    // ScopedComputePass

    ScopedComputePass::ScopedComputePass(EncodingContext* context, const char* name): mParentContext(context) {
        mParentContext->OnStartPass(name);
        mPass = mParentContext->GetEncoder().BeginComputePass();
    }

    ScopedComputePass::~ScopedComputePass() {
        mPass.EndPass();
        mParentContext->OnEndPass();
    }

    const wgpu::ComputePassEncoder* ScopedComputePass::operator->() const {
        return &mPass;
    }

    // ScopedRenderPass

    ScopedRenderPass::ScopedRenderPass(EncodingContext* context, const wgpu::RenderPassDescriptor& desc, const char* name): mParentContext(context) {
        mParentContext->OnStartPass(name);
        mPass = mParentContext->GetEncoder().BeginRenderPass(&desc);
    }

    ScopedRenderPass::~ScopedRenderPass() {
        mPass.EndPass();
        mParentContext->OnEndPass();
    }

    const wgpu::RenderPassEncoder* ScopedRenderPass::operator->() const {
        return &mPass;
    }

}
