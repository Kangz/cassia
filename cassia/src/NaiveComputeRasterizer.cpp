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

            // const TILE_WIDTH: usize = 8;
            // const TILE_WIDTH_SHIFT: usize = 3
            // const TILE_WIDTH_MASK: usize = 7;
            //
            // pub const TILE_HEIGHT: usize = 64;
            // const TILE_HEIGHT_SHIFT: usize = 6;
            // const TILE_HEIGHT_MASK: usize = 63;
            //
            // pub struct CompactSegment(u64) {
            //     is_none: u8[1],
            //     tile_y: i16[15 - TILE_HEIGHT_SHIFT],
            //     tile_x: i16[16 - TILE_WIDTH_SHIFT],
            //     layer: u16[16],
            //     local_y: u8[TILE_HEIGHT_SHIFT],
            //     local_x: u8[TILE_WIDTH_SHIFT],
            //     area: i16[10],
            //     cover: i8[6],
            // }

            struct PSegment {
                lo: u32;
                hi: u32;
            };

            let TILE_WIDTH_SHIFT = 3u;
            let TILE_HEIGHT_SHIFT = 6u;
            let COVER_DIVISOR = 16.0;
            let AREA_DIVISOR = 256.0;

            fn psegment_is_none(s : PSegment) -> bool {
                return bool(s.hi & (1u << 31u));
            }
            fn psegment_tile_x(s : PSegment) -> i32 {
                return (i32(s.hi) << (16u - TILE_HEIGHT_SHIFT)) >> (16u + TILE_WIDTH_SHIFT);
            }
            fn psegment_tile_y(s : PSegment) -> i32{
                return (i32(s.hi) << 1u) >> (17u + TILE_HEIGHT_SHIFT);
            }
            fn psegment_local_x(s : PSegment) -> u32 {
                let mask = (1u << TILE_WIDTH_SHIFT) - 1u;
                return (s.lo >> 16u) & mask;
            }
            fn psegment_local_y(s : PSegment) -> u32{
                let mask = (1u << TILE_HEIGHT_SHIFT) - 1u;
                return (s.lo >> (16u + TILE_WIDTH_SHIFT)) & mask;
            }
            fn psegment_area(s : PSegment) -> i32{
                return i32(s.lo << 16u) >> 22u;
            }
            fn psegment_cover(s : PSegment) -> i32{
                return i32(s.lo << 26u) >> 26u;
            }

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
                var pos = vec2<i32>(GlobalId.xy);

                var cover = 0.0;
                var area = 0.0;
                for (var i = 0u; i < config.count; i = i + 1u) {
                    var segment = in.segments[i];
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

                var greyscale = vec4<f32>(vec3<f32>(cover + area), 1.0);

                textureStore(out, vec2<i32>(GlobalId.xy), greyscale);
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
