#include "TileWorkgroupRasterizer.h"

#include "CommonWGSL.h"
#include "EncodingContext.h"

#include "utils/WGPUHelpers.h"

#include <string>

#include <iostream>

namespace cassia {

    struct ConfigUniforms {
        uint32_t width;
        uint32_t height;
        uint32_t widthInTiles;
        uint32_t heightInTiles;
        uint32_t segmentCount;
        uint32_t tileRangeCount;
    };
    static_assert(sizeof(ConfigUniforms) == 24, "");

    struct TileRange {
        uint32_t start;
        uint32_t end;
    };

    TileWorkgroupRasterizer::TileWorkgroupRasterizer(wgpu::Device device)
        : mDevice(std::move(device)) {
        std::string code = std::string(kPSegmentWGSL) + std::string(kStylingWGSL) + R"(
            [[block]] struct Config {
                width: u32;
                height: u32;
                widthInTiles: i32;
                heightInTiles: i32;
                segmentCount: u32;
                tileRangeCount: u32;
            };
            [[group(0), binding(0)]] var<uniform> config : Config;

            [[block]] struct PSegments {
                data: array<PSegment>;
            };
            [[group(0), binding(1)]] var<storage> segments : PSegments;

            struct Range {
                start: u32;
                end: u32; // Exclusive
            };
            [[block]] struct TileRanges {
                data: array<Range>;
            };
            [[group(0), binding(2)]] var<storage, read_write> tileRanges : TileRanges;

            fn tile_index(tileX: i32, tileY: i32) -> u32 {
                return u32(tileX + tileY * (config.widthInTiles + 1));
            }

            fn tile_in_bounds(tileX: i32, tileY: i32) -> bool {
                // Note that tileX is always >= -1
                return tileX < config.widthInTiles && tileY >= 0 && tileY < config.heightInTiles;
            }

            // Large workgroup size to not run into the max dispatch limitation.
            [[stage(compute), workgroup_size(256)]]
            fn computeTileRanges([[builtin(global_invocation_id)]] GlobalId : vec3<u32>) {
                if (GlobalId.x >= config.segmentCount - 1u) {
                    // This is the last segment of the last referenced tile so we can mark the end of it.
                    if (GlobalId.x == config.segmentCount - 1u) {
                        var segment = segments.data[GlobalId.x];

                        var tileX = psegment_tile_x(segment);
                        var tileY = psegment_tile_y(segment);

                        if (tile_in_bounds(tileX, tileY)) {
                            var tileIndex = tile_index(tileX, tileY);
                            tileRanges.data[tileIndex].end = GlobalId.x + 1u;
                        }
                    }

                    return;
                }

                // TODO we could share segments with the workgroup to avoid redundant load

                var segment0 = segments.data[GlobalId.x];
                var segment1 = segments.data[GlobalId.x + 1u];

                // TODO handle none segments
                var tileX0 = psegment_tile_x(segment0);
                var tileX1 = psegment_tile_x(segment1);
                var tileY0 = psegment_tile_y(segment0);
                var tileY1 = psegment_tile_y(segment1);

                if (tileX0 != tileX1 || tileY0 != tileY1) {
                    if (tile_in_bounds(tileX0, tileY0)) {
                        var tileIndex0 = tile_index(tileX0, tileY0);
                        tileRanges.data[tileIndex0].end = GlobalId.x + 1u;
                    }

                    if (tile_in_bounds(tileX1, tileY1)) {
                        var tileIndex1 = tile_index(tileX1, tileY1);
                        tileRanges.data[tileIndex1].start = GlobalId.x + 1u;
                    }
                }
            }

            [[block]] struct Stylings {
                data: array<Styling>;
            };

            [[group(0), binding(3)]] var<storage> stylings : Stylings;
            [[group(0), binding(4)]] var out : texture_storage_2d<rgba16float, write>;

            var<workgroup> coverCarry : array<i32, 8>;
            fn rasterizeTile(tileId: vec2<i32>, localId: vec2<u32>) {
                var tileRange = tileRanges.data[tile_index(tileId.x, tileId.y)];

                // TODO do these computations in fixed point
                var cover = coverCarry[localId.y];
                var area = 0;
                for (var i = tileRange.start; i < tileRange.end; i = i + 1u) {
                    var segment = segments.data[i];

                    if (psegment_local_y(segment) != localId.y) {
                        continue;
                    }

                    var segmentLocalX = psegment_local_x(segment);
                    if (segmentLocalX <= localId.x) {
                        var segmentCover = psegment_cover(segment);
                        cover = cover + segmentCover;
                        if (segmentLocalX == localId.x) {
                            area = area + psegment_area(segment);
                            // Remove the extra coverage we counted
                            area = area - PIXEL_SIZE * segmentCover;
                        }
                    }
                }

                var layer = psegment_layer(segments.data[0]);

                var coverage = f32(area + PIXEL_SIZE * cover) / AREA_DIVISOR;

                var fill = stylings.data[layer].fill;
                var color = vec3<f32>(fill[0], fill[1], fill[2]);
                var accumulator = vec4<f32>(color * coverage, fill[3]);

                textureStore(out, tileId * 8 + vec2<i32>(localId.xy), accumulator);

                if (localId.x == 7u) {
                    coverCarry[localId.y] = cover;
                }
            }

            // TODO doesn't handle stuff outside the screen.
            [[stage(compute), workgroup_size(8, 8)]]
            fn rasterizeTileRow([[builtin(workgroup_id)]] WorkgroupId : vec3<u32>,
                             [[builtin(local_invocation_id)]] LocalId : vec3<u32>) {
                if (all(LocalId == vec3<u32>(0u))) {
                    var tileRange = tileRanges.data[tile_index(-1, i32(WorkgroupId.x))];
                    for (var i = tileRange.start; i < tileRange.end; i = i + 1u) {
                        var segment = segments.data[i];
                        var segmentLocalY = psegment_local_y(segment);
                        coverCarry[segmentLocalY] = coverCarry[segmentLocalY] + psegment_cover(segment);
                    }
                }

                workgroupBarrier();

                var tileId = vec2<i32>(0, i32(WorkgroupId.x));
                for (; tileId.x < i32(config.widthInTiles); tileId.x = tileId.x + 1) {
                    rasterizeTile(tileId, LocalId.xy);
                    workgroupBarrier();
                }
            }
        )";

        wgpu::ShaderModule module = utils::CreateShaderModule(mDevice, code.c_str());

        wgpu::ComputePipelineDescriptor pDesc;
        pDesc.label = "TileWorkgroupRasterizer::mTileRangePipeline";
        pDesc.compute.module = module;
        pDesc.compute.entryPoint = "computeTileRanges";
        mTileRangePipeline = mDevice.CreateComputePipeline(&pDesc);

        pDesc.label = "TileWorkgroupRasterizer::mRasterPipeline";
        pDesc.compute.entryPoint = "rasterizeTileRow";
        mRasterPipeline = mDevice.CreateComputePipeline(&pDesc);
    }

    wgpu::Texture TileWorkgroupRasterizer::Rasterize(EncodingContext* context,
        wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
        const Config& config) {
        uint32_t widthInTiles = (config.width + TILE_WIDTH_SHIFT - 1) >> TILE_WIDTH_SHIFT;
        uint32_t heightInTiles = (config.height + TILE_HEIGHT_SHIFT - 1) >> TILE_HEIGHT_SHIFT;
        uint32_t tileRangeCount = (widthInTiles + 1) * heightInTiles;

        ConfigUniforms uniformData = {
            config.width,
            config.height,
            widthInTiles,
            heightInTiles,
            config.segmentCount,
            tileRangeCount
        };
        wgpu::Buffer uniforms = utils::CreateBufferFromData(
                mDevice, &uniformData, sizeof(uniformData), wgpu::BufferUsage::Uniform);

        wgpu::BufferDescriptor tileRangeDesc;
        tileRangeDesc.size = tileRangeCount *  sizeof(TileRange);
        tileRangeDesc.usage = wgpu::BufferUsage::Storage;
        wgpu::Buffer tileRangeBuffer = mDevice.CreateBuffer(&tileRangeDesc);

        {
            wgpu::BindGroup bg = utils::MakeBindGroup(mDevice, mTileRangePipeline.GetBindGroupLayout(0), {
                {0, uniforms},
                {1, sortedPsegments},
                {2, tileRangeBuffer},
            });

            // TODO reuse tile range buffer instead?
            {
                ScopedComputePass pass(context, "TileWorkgroupRasterizer::FakePassToFactorOutLazyClearCost");
                pass->SetBindGroup(0, bg);
                pass->SetPipeline(mTileRangePipeline);
                pass->Dispatch(0);
            }

            ScopedComputePass pass(context, "TileWorkgroupRasterizer::TileRangeComputation");

            pass->SetBindGroup(0, bg);
            pass->SetPipeline(mTileRangePipeline);
            pass->Dispatch((config.segmentCount + 255) / 256);
        }

        wgpu::TextureDescriptor texDesc;
        texDesc.label = "rasterized paths";
        texDesc.size = {config.width, config.height};
        texDesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
        texDesc.format = wgpu::TextureFormat::RGBA16Float;
        wgpu::Texture outTexture = mDevice.CreateTexture(&texDesc);

        {
            wgpu::BindGroup bg = utils::MakeBindGroup(mDevice, mRasterPipeline.GetBindGroupLayout(0), {
                {0, uniforms},
                {1, sortedPsegments},
                {2, tileRangeBuffer},
                {3, stylingsBuffer},
                {4, outTexture.CreateView()}
            });

            {
                ScopedComputePass pass(context, "TileWorkgroupRasterizer::FakePassToFactorOutLazyClearCost");

                pass->SetBindGroup(0, bg);
                pass->SetPipeline(mRasterPipeline);
                pass->Dispatch(0);
            }

            ScopedComputePass pass(context, "TileWorkgroupRasterizer::Raster");

            pass->SetBindGroup(0, bg);
            pass->SetPipeline(mRasterPipeline);
            pass->Dispatch(heightInTiles);
        }

        return outTexture;
    }

} // namespace cassia
