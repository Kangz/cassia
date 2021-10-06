#include "NaiveComputeRasterizer.h"

#include "CommonWGSL.h"

#include "utils/WGPUHelpers.h"

#include <string>

namespace cassia {

    NaiveComputeRasterizer::NaiveComputeRasterizer(wgpu::Device device)
        : mDevice(std::move(device)) {

        // The config struct can be used directly to lay out a uniform buffer.
        static_assert(sizeof(Config) == 12, "");

        std::string code = std::string(kPSegmentWGSL) +  R"(
            [[block]] struct Config {
                width: u32;
                height: u32;
                count: u32;
            };
            [[group(0), binding(0)]] var<uniform> config : Config;

            struct Styling {
                fill_rule: u32;
                fill: array<f32, 4>;
                blend_mode: u32;
            };

            // TODO define helpers to extract data from the PSegment

            [[block]] struct PSegments {
                data: array<PSegment>;
            };
            [[block]] struct Stylings {
                data: array<Styling>;
            };
            [[group(0), binding(1)]] var<storage> segments : PSegments;
            [[group(0), binding(2)]] var<storage> stylings : Stylings;
            [[group(0), binding(3)]] var out : texture_storage_2d<rgba16float, write>;

            [[stage(compute), workgroup_size(8, 8)]]
            fn main([[builtin(global_invocation_id)]] GlobalId : vec3<u32>) {
                ignore(stylings);

                if (GlobalId.x >= config.width || GlobalId.y >= config.height) {
                    return;
                }
                var pos = vec2<i32>(GlobalId.xy);

                var cover = 0.0;
                var area = 0.0;
                for (var i = 0u; i < config.count; i = i + 1u) {
                    var segment = segments.data[i];
                    if (psegment_is_none(segment)) {
                        continue;
                    }

                    var y = (psegment_tile_y(segment) << TILE_HEIGHT_SHIFT) + i32(psegment_local_y(segment));
                    if (y != pos.y) {
                        continue;
                    }

                    var x = (psegment_tile_x(segment) << TILE_WIDTH_SHIFT) + i32(psegment_local_x(segment));
                    if (x < pos.x) {
                        cover = cover + f32(psegment_cover(segment)) / COVER_DIVISOR;
                    } elseif (x == pos.x) {
                        area = area + f32(psegment_area(segment)) / AREA_DIVISOR;
                    }
                }

                var layer_id = psegment_layer_id(segments.data[0]);

                var fill = stylings.data[0].fill;
                var color = vec3<f32>(fill[0], fill[1], fill[2]);
                var greyscale = vec4<f32>(color * (cover + area), fill[3]);

                textureStore(out, vec2<i32>(GlobalId.xy), greyscale);
            }
        )";
        wgpu::ShaderModule module = utils::CreateShaderModule(mDevice, code.c_str());

        wgpu::ComputePipelineDescriptor pDesc;
        pDesc.label = "naive rasterizer pipeline";
        pDesc.compute.module = module;
        pDesc.compute.entryPoint = "main";
        mPipeline = mDevice.CreateComputePipeline(&pDesc);
    }

    wgpu::Texture NaiveComputeRasterizer::Rasterize(const wgpu::ComputePassEncoder& pass,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer, const Config& config) {
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
                {2, stylingsBuffer},
                {3, outTexture.CreateView()}
                });

        pass.PushDebugGroup("naive raster");
        pass.SetBindGroup(0, bg);
        pass.SetPipeline(mPipeline);
        pass.Dispatch((config.width + 7) / 8, (config.height + 7) / 8);
        pass.PopDebugGroup();

        return outTexture;
    }

} // namespace cassia
