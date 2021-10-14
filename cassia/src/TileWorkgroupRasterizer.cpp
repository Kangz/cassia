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

            ///////////////////////////////////////////////////////////////////
            //  Tile ranges
            ///////////////////////////////////////////////////////////////////

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

            ///////////////////////////////////////////////////////////////////
            //  Misc styling and output
            ///////////////////////////////////////////////////////////////////

            [[block]] struct Stylings {
                data: array<Styling>;
            };

            [[group(0), binding(3)]] var<storage> stylings : Stylings;
            [[group(0), binding(4)]] var out : texture_storage_2d<rgba16float, write>;

            fn accumulate(accumulator: ptr<function, vec4<f32>,read_write>, layer: u32, cover: i32, area: i32) {
                var styling = stylings.data[layer];
                var pixelCoverage = area + cover * PIXEL_SIZE;
                *accumulator = styling_accumulate_layer(*accumulator, pixelCoverage, styling);
            }

            ///////////////////////////////////////////////////////////////////
            // Carry queues
            ///////////////////////////////////////////////////////////////////

            // We need constexprs....
            let TILE_WIDTH = 8;
            let TILE_WIDTH_PLUS_ONE = 9;
            let INVALID_LAYER = 0xFFFFu;
            let WORKGROUP_SIZE = 8u;
            type CarryCovers = array<i32, WORKGROUP_SIZE>;

            let WORKGROUP_CARRIES = 10u;
            struct LayerCarry {
                layer: u32;
                rows: CarryCovers;
            };
            struct LayerCarryQueue {
                count: u32;
                data: array<LayerCarry, WORKGROUP_CARRIES>;
            };

            var<private> storeCarryIndex : u32 = 0u;
            var<private> readLayerIndex : u32 = 0u;
            var<workgroup> carries : array<LayerCarryQueue, 2>;

            fn flip_carry_stores() {
                storeCarryIndex = 1u - storeCarryIndex;
                carries[storeCarryIndex].count = 0u;
                readLayerIndex = 0u;
            }
            fn append_output_layer_carry(layer: u32, covers: CarryCovers) {
                // Really??? Can we get rid of var vs. let already?
                var localCovers = covers;
                var needsStore = false;
                for (var i = 0u; i < WORKGROUP_SIZE; i = i + 1u) {
                    if (localCovers[i] != 0) {
                        needsStore = true;
                    }
                }
                if (!needsStore) {
                    return;
                }

                if (carries[storeCarryIndex].count >= WORKGROUP_CARRIES) {
                    // TODO fallback to memory storage.
                    return;
                }

                carries[storeCarryIndex].data[carries[storeCarryIndex].count].rows = covers;
                carries[storeCarryIndex].data[carries[storeCarryIndex].count].layer = layer;
                carries[storeCarryIndex].count = carries[storeCarryIndex].count + 1u;
            }

            fn consume_input_layer_carry(localY: u32) -> i32 {
                if (readLayerIndex >= WORKGROUP_CARRIES) {
                    // TODO fallback to memory storage.
                    return 0;
                }

                var readIndex = 1u - storeCarryIndex;
                readLayerIndex = readLayerIndex + 1u;
                return carries[readIndex].data[readLayerIndex - 1u].rows[localY];
            }

            fn peek_layer_for_next_input_layer_carry() -> u32 {
                var readIndex = 1u - storeCarryIndex;
                if (readLayerIndex < carries[readIndex].count) {
                    return carries[readIndex].data[readLayerIndex].layer;
                }
                return INVALID_LAYER;
            }

            var<workgroup> subgroupAllBool : bool;
            fn fakeSubgroupAll(b: bool) -> bool {
                subgroupAllBool = false;
                workgroupBarrier();
                if (b) {
                    subgroupAllBool = true;
                }
                workgroupBarrier();
                return subgroupAllBool;
            }

            var<workgroup> foo : atomic<i32>;
            fn append_output_layer_carry2(layer: u32, localY: u32, cover: i32) {
                // very bad SubgroupAll because the one below doesn't work.
                workgroupBarrier();
                atomicStore(&foo, 0);
                workgroupBarrier();
                ignore(atomicAdd(&foo, abs(cover)));
                workgroupBarrier();
                if (atomicLoad(&foo) == 0) {
                    return;
                }

                // if (fakeSubgroupAll(cover == 0)) {
                //     return;
                // }

                if (carries[storeCarryIndex].count >= WORKGROUP_CARRIES) {
                    // TODO fallback to memory storage.
                    return;
                }

                carries[storeCarryIndex].data[carries[storeCarryIndex].count].rows[localY] = cover;
                carries[storeCarryIndex].data[carries[storeCarryIndex].count].layer = layer;
                carries[storeCarryIndex].count = carries[storeCarryIndex].count + 1u;
            }

            ///////////////////////////////////////////////////////////////////
            //  Main tile rasterization
            ///////////////////////////////////////////////////////////////////

            var<workgroup> areas : array<array<atomic<i32>, WORKGROUP_SIZE>, TILE_WIDTH>;
            var<workgroup> covers : array<array<atomic<i32>, WORKGROUP_SIZE>, TILE_WIDTH_PLUS_ONE>;
            var<workgroup> accumulators : array<array<vec4<f32>, WORKGROUP_SIZE>, TILE_WIDTH>;

            fn accumulate_layer_and_save_carry(layer: u32, localY: u32) {
                workgroupBarrier();
                var cover = 0;
                for (var x = 0; x < TILE_WIDTH; x = x + 1) {
                    var area = atomicExchange(&areas[x][localY], 0);
                    cover = cover + atomicExchange(&covers[x][localY], 0);

                    var localAccumulator = accumulators[x][localY];
                    accumulate(&localAccumulator, layer, cover, area);
                    accumulators[x][localY] = localAccumulator;
                }
                cover = cover + atomicExchange(&covers[TILE_WIDTH][localY], 0);
                append_output_layer_carry2(layer, localY, cover);
                workgroupBarrier();
            }

            fn rasterizeTile(tileId: vec2<i32>, localY: u32) {
                var tileRange = tileRanges.data[tile_index(tileId.x, tileId.y)];

                var currentLayer : u32 = INVALID_LAYER;
                var segmentIndex = tileRange.start;

                loop {
                    var carryLayer = peek_layer_for_next_input_layer_carry();
                    var segmentLayer = INVALID_LAYER;
                    if (segmentIndex < tileRange.end) {
                        segmentLayer = psegment_layer(segments.data[segmentIndex]);
                    }

                    if (segmentLayer == INVALID_LAYER && carryLayer == INVALID_LAYER) {
                        break;
                    }

                    var minLayer = min(carryLayer, segmentLayer);
                    if (minLayer != currentLayer) {
                        if (currentLayer != INVALID_LAYER) {
                            accumulate_layer_and_save_carry(currentLayer, localY);
                        }
                        currentLayer = minLayer;
                    }

                    if (carryLayer == minLayer){
                        atomicStore(&covers[0][localY], consume_input_layer_carry(localY));
                        continue;
                    }

                    if (segmentLayer == minLayer) {
                        // TODO rework completely to load multiple segments at once
                        var segment = segments.data[segmentIndex];
                        segmentIndex = segmentIndex + 1u;

                        if (psegment_local_y(segment) != localY) {
                            continue;
                        }

                        var segmentLocalX = psegment_local_x(segment);
                        var segmentCover = psegment_cover(segment);
                        var segmentArea = psegment_area(segment);

                        ignore(atomicAdd(&covers[segmentLocalX + 1u][localY], segmentCover));
                        ignore(atomicAdd(&areas[segmentLocalX][localY], segmentArea));
                        continue;
                    }
                }

                if (currentLayer != INVALID_LAYER) {
                    accumulate_layer_and_save_carry(currentLayer, localY);
                }

                for (var x = 0; x < TILE_WIDTH; x = x + 1) {
                    textureStore(out, tileId * 8 + vec2<i32>(x, i32(localY)), accumulators[x][localY]);
                    accumulators[x][localY] = vec4<f32>(0.0);
                }
            }

            [[stage(compute), workgroup_size(WORKGROUP_SIZE)]]
            fn rasterizeTileRow([[builtin(workgroup_id)]] WorkgroupId : vec3<u32>,
                                [[builtin(local_invocation_id)]] LocalId : vec3<u32>) {
                flip_carry_stores();

                var localY = LocalId.x;
                // TODO make parallel over whole subgroup
                if (localY == 0u) {
                    var tileRange = tileRanges.data[tile_index(-1, i32(WorkgroupId.x))];

                    var currentCovers : CarryCovers;
                    var currentLayer = INVALID_LAYER;

                    for (var i = tileRange.start; i < tileRange.end; i = i + 1u) {
                        var segment = segments.data[i];
                        var segmentLayer = psegment_layer(segment);

                        if (currentLayer != segmentLayer) {
                            append_output_layer_carry(currentLayer, currentCovers);
                            currentCovers = CarryCovers();
                            currentLayer = segmentLayer;
                        }

                        var segmentLocalY = psegment_local_y(segment);
                        var cover = psegment_cover(segment);
                        currentCovers[segmentLocalY] = currentCovers[segmentLocalY] + cover;
                    }
                    append_output_layer_carry(currentLayer, currentCovers);
                }

                workgroupBarrier();
                flip_carry_stores();

                var tileId = vec2<i32>(0, i32(WorkgroupId.x));
                for (; tileId.x < i32(config.widthInTiles); tileId.x = tileId.x + 1) {
                    rasterizeTile(tileId, localY);

                    workgroupBarrier(); // TODO not needed? or put in the flipping of carry stores?
                    flip_carry_stores();
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
