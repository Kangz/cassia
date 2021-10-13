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

            struct LayerCarry {
                layer: u32;
                rows: array<i32, 8>;
            };
            fn layer_carry_init(carry: ptr<function, LayerCarry, read_write>, layer: u32) {
                (*carry).layer = layer;
                for (var i = 0u; i < 8u; i = i + 1u) {
                    (*carry).rows[i] = 0;
                }
            }

            let INVALID_LAYER = 0xFFFFu;
            let WORKGROUP_CARRIES = 10u;
            struct LayerCarryQueue {
                count: u32;
                data: array<LayerCarry, WORKGROUP_CARRIES>;
            };

            var<workgroup> carries : array<LayerCarryQueue, 2>;

            [[group(0), binding(3)]] var<storage> stylings : Stylings;
            [[group(0), binding(4)]] var out : texture_storage_2d<rgba16float, write>;

            fn accumulate(accumulator: ptr<function, vec4<f32>,read_write>, layer: u32, cover: i32, area: i32) {
                var styling = stylings.data[layer];
                var pixelCoverage = area + cover * PIXEL_SIZE;
                *accumulator = styling_accumulate_layer(*accumulator, pixelCoverage, styling);
            }

            fn store_layer_carry(carryFlip: u32, carry: LayerCarry) {
                // TODO maybe don't check that and instead early out in various places if we have no psegment?
                if (carry.layer == INVALID_LAYER) {
                    return;
                }

                // Really??? Can we get rid of var vs. let already?
                var localCarry = carry;
                var needsStore = false;
                for (var i = 0u; i < 8u; i = i + 1u) {
                    if (localCarry.rows[i] != 0) {
                        needsStore = true;
                    }
                }
                if (!needsStore) {
                    return;
                }

                if (carries[carryFlip].count >= WORKGROUP_CARRIES) {
                    // TODO fallback to memory storage.
                    return;
                }

                carries[carryFlip].data[carries[carryFlip].count] = carry;
                carries[carryFlip].count = carries[carryFlip].count + 1u;
            }

            fn load_layer_carry(carryFlip: u32, index: u32, maxLayer: u32,
                                out: ptr<function, LayerCarry, read_write>) -> bool {
                if (index >= carries[carryFlip].count) {
                    return false;
                }

                if (carries[carryFlip].data[index].layer > maxLayer) {
                    return false;
                }

                if (carries[carryFlip].count >= WORKGROUP_CARRIES) {
                    // TODO fallback to memory storage.
                    return false;
                }

                (*out) = carries[carryFlip].data[index];
                return true;
            }

            var<workgroup> wgCovers: array<i32, 8>;
            fn store_tile_and_cover(carryFlip: u32, localId: vec2<u32>, layer: u32, cover: i32) {
                if (localId.x == 7u) {
                    wgCovers[localId.y] = cover;
                }
                workgroupBarrier();

                if (all(localId == vec2<u32>(0u))) {
                    var carry : LayerCarry;
                    carry.layer = layer;
                    carry.rows = wgCovers;
                    store_layer_carry(carryFlip, carry);
                }
                workgroupBarrier();
            }

            fn rasterizeTile(tileId: vec2<i32>, localId: vec2<u32>, carryFlip: u32) {
                var tileRange = tileRanges.data[tile_index(tileId.x, tileId.y)];

                var currentCarry: LayerCarry;
                currentCarry.layer = INVALID_LAYER;

                var accumulator = vec4<f32>(0.0, 0.0, 0.0, 1.0);

                var area = 0;
                var cover = 0;

                var inputCarryIndex = 0u;
                var segmentIndex = tileRange.start;

                loop {
                    var segmentLayer = INVALID_LAYER;
                    var carryLayer = INVALID_LAYER;
                    if (segmentIndex < tileRange.end) {
                        segmentLayer = psegment_layer(segments.data[segmentIndex]);
                    }
                    if (inputCarryIndex < carries[1u - carryFlip].count) {
                        carryLayer = carries[1u - carryFlip].data[inputCarryIndex].layer;
                    }
                    if (segmentLayer == INVALID_LAYER && carryLayer == INVALID_LAYER) {
                        break;
                    }

                    var minLayer = min(carryLayer, segmentLayer);
                    if (minLayer != currentCarry.layer) {
                        if (currentCarry.layer != INVALID_LAYER) {
                            accumulate(&accumulator, currentCarry.layer, cover, area);
                            store_tile_and_cover(carryFlip, localId, currentCarry.layer, cover);
                        }

                        area = 0;
                        cover = 0;
                        layer_carry_init(&currentCarry, minLayer);
                    }

                    if (carryLayer == minLayer){
                        ignore(load_layer_carry(1u - carryFlip, inputCarryIndex, minLayer, &currentCarry));
                        inputCarryIndex = inputCarryIndex + 1u;

                        cover = currentCarry.rows[localId.y];
                        continue;
                    }

                    if (segmentLayer == minLayer) {
                        var segment = segments.data[segmentIndex];
                        segmentIndex = segmentIndex + 1u;

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

                        continue;
                    }
                }

                if (currentCarry.layer != INVALID_LAYER) {
                    accumulate(&accumulator, currentCarry.layer, cover, area);
                    store_tile_and_cover(carryFlip, localId, currentCarry.layer, cover);
                }

                textureStore(out, tileId * 8 + vec2<i32>(localId.xy), accumulator);
            }

            [[stage(compute), workgroup_size(8, 8)]]
            fn rasterizeTileRow([[builtin(workgroup_id)]] WorkgroupId : vec3<u32>,
                                [[builtin(local_invocation_id)]] LocalId : vec3<u32>) {
                var carryFlip = 0u;
                if (all(LocalId == vec3<u32>(0u))) {
                    carries[0].count = 0u;
                    carries[1].count = 0u;

                    var tileRange = tileRanges.data[tile_index(-1, i32(WorkgroupId.x))];

                    var currentCarry : LayerCarry;
                    currentCarry.layer = INVALID_LAYER;

                    for (var i = tileRange.start; i < tileRange.end; i = i + 1u) {
                        var segment = segments.data[i];
                        var segmentLayer = psegment_layer(segment);

                        if (currentCarry.layer != segmentLayer) {
                            store_layer_carry(carryFlip, currentCarry);
                            layer_carry_init(&currentCarry, segmentLayer);
                        }

                        var localY = psegment_local_y(segment);
                        var cover = psegment_cover(segment);
                        currentCarry.rows[localY] = currentCarry.rows[localY] + cover;
                    }
                    store_layer_carry(carryFlip, currentCarry);
                }

                carryFlip = 1u - carryFlip;
                workgroupBarrier();

                var tileId = vec2<i32>(0, i32(WorkgroupId.x));
                for (; tileId.x < i32(config.widthInTiles); tileId.x = tileId.x + 1) {
                    rasterizeTile(tileId, LocalId.xy, carryFlip);
                    workgroupBarrier();
                    carryFlip = 1u - carryFlip;
                    carries[carryFlip].count = 0u;
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
