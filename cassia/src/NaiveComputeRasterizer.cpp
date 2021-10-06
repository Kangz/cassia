#include "NaiveComputeRasterizer.h"

#include "utils/WGPUHelpers.h"

namespace cassia {

    NaiveComputeRasterizer::NaiveComputeRasterizer(wgpu::Device device)
        : mDevice(std::move(device)) {

        // The config struct can be used directly to lay out a uniform buffer.
        static_assert(sizeof(Config) == 12, "");

        wgpu::ShaderModule module = utils::CreateShaderModule(mDevice, R"(
            [[block]] struct Config {
                width: u32;
                height: u32;
                count: u32;
            };
            [[group(0), binding(0)]] var<uniform> config : Config;

            struct PSegment {
                low: u32;
                high: u32;
            };
            // TODO define helpers to extract data from the PSegment

            [[block]] struct PSegments {
                segments: array<PSegment>;
            };
            [[group(0), binding(1)]] var<storage> in : PSegments;
            [[group(0), binding(2)]] var out : texture_storage_2d<rgba16float, write>;

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

                textureStore(out, vec2<i32>(GlobalId.xy), accum);
            }
        )");

        wgpu::ComputePipelineDescriptor pDesc;
        pDesc.label = "naive rasterizer pipeline";
        pDesc.compute.module = module;
        pDesc.compute.entryPoint = "main";
        mPipeline = mDevice.CreateComputePipeline(&pDesc);
    }

    wgpu::Texture NaiveComputeRasterizer::Rasterize(const wgpu::ComputePassEncoder& pass,
            wgpu::Buffer sortedPsegments, const Config& config) {
        wgpu::Buffer uniforms = utils::CreateBufferFromData(
                mDevice, &config, sizeof(Config), wgpu::BufferUsage::Uniform);

        wgpu::TextureDescriptor texDesc;
        texDesc.label = "rasterized paths";
        texDesc.size = {config.width, config.height};
        texDesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
        texDesc.format = wgpu::TextureFormat::RGBA16Float;
        wgpu::Texture outTexture = mDevice.CreateTexture(&texDesc);

        wgpu::BindGroup bg = utils::MakeBindGroup(mDevice, mPipeline.GetBindGroupLayout(0), {
                {0, uniforms},
                {1, sortedPsegments},
                {2, outTexture.CreateView()}
                });

        pass.PushDebugGroup("naive raster");
        pass.SetBindGroup(0, bg);
        pass.SetPipeline(mPipeline);
        pass.Dispatch((config.width + 7) / 8, (config.height + 7) / 8);
        pass.PopDebugGroup();

        return outTexture;
    }

} // namespace cassia
