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
        uint32_t carrySpillsPerRow;
    };
    static_assert(sizeof(ConfigUniforms) == 28, "");

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
                carrySpillsPerRow: u32;
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

                        if (!psegment_is_none(segment) && tile_in_bounds(tileX, tileY)) {
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

                if (!psegment_is_none(segment0) && (tileX0 != tileX1 || tileY0 != tileY1 || psegment_is_none(segment1))) {
                    if (tile_in_bounds(tileX0, tileY0)) {
                        var tileIndex0 = tile_index(tileX0, tileY0);
                        tileRanges.data[tileIndex0].end = GlobalId.x + 1u;
                    }

                    if (!psegment_is_none(segment1) && tile_in_bounds(tileX1, tileY1)) {
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

            [[group(0), binding(4)]] var<storage> stylings : Stylings;
            [[group(0), binding(5)]] var out : texture_storage_2d<rgba16float, write>;

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
            let TILE_HEIGHT = 8u;
            let INVALID_LAYER = 0xFFFFu;
            
            let WORKGROUP_SIZE = 32u;
            let WORKGROUP_HEIGHT_IN_ROWS = 4; // (TILE_WIDTH * TILE_HEIGHT) / WORKGROUP_SIZE

            //let WORKGROUP_SIZE = 64u;
            //let WORKGROUP_HEIGHT_IN_ROWS = 8; // (TILE_WIDTH * TILE_HEIGHT) / WORKGROUP_SIZE

            type CarryCovers = array<i32, TILE_HEIGHT>;

            let WORKGROUP_CARRIES = 10u;
            struct LayerCarry {
                layer: u32;
                rows: CarryCovers;
            };
            struct LayerCarryQueue {
                count: u32;
                data: array<LayerCarry, WORKGROUP_CARRIES>;
            };

            [[block]] struct CarrySpill {
                spills: array<LayerCarry>;
            };
            [[group(0), binding(3)]] var<storage, read_write> carrySpills : CarrySpill;
            var<private> storeCarryIndex : u32 = 0u;
            var<private> readLayerIndex : u32 = 0u;
            var<workgroup> carries : array<LayerCarryQueue, 2>;

            fn flip_carry_stores() {
                storeCarryIndex = 1u - storeCarryIndex;
                carries[storeCarryIndex].count = 0u;
                readLayerIndex = 0u;
            }
            fn compute_carry_spill_index(out: ptr<function, u32, read_write>,
                                         carryFlip: u32, tileY: i32, index: u32) -> bool {
                if (index> config.carrySpillsPerRow) {
                    return false;
                }
                *out = (index - config.carrySpillsPerRow) +
                       u32(tileY) * config.carrySpillsPerRow +
                       carryFlip * config.carrySpillsPerRow * u32(config.heightInTiles);
                return true;
            }

            fn append_output_layer_carry(tileY: i32, layer: u32, covers: CarryCovers) {
                // Really??? Can we get rid of var vs. let already?
                var localCovers = covers;
                var needsStore = false;
                for (var i = 0u; i < TILE_HEIGHT; i = i + 1u) {
                    if (localCovers[i] != 0) {
                        needsStore = true;
                    }
                }
                if (!needsStore) {
                    return;
                }

                if (carries[storeCarryIndex].count >= WORKGROUP_CARRIES) {
                    var spillIndex : u32;
                    if (!compute_carry_spill_index(&spillIndex,
                            storeCarryIndex, tileY, carries[storeCarryIndex].count)) {
                        return;
                    }
                    carrySpills.spills[spillIndex].rows = covers;
                    carrySpills.spills[spillIndex].layer = layer;
                    carries[storeCarryIndex].count = carries[storeCarryIndex].count + 1u;
                    return;
                }

                carries[storeCarryIndex].data[carries[storeCarryIndex].count].rows = covers;
                carries[storeCarryIndex].data[carries[storeCarryIndex].count].layer = layer;
                carries[storeCarryIndex].count = carries[storeCarryIndex].count + 1u;
            }

            fn consume_input_layer_carry(tileY: i32, thredIdx: u32) -> i32 {
                var readIndex = 1u - storeCarryIndex;
                var localLayerIndex = readLayerIndex;
                readLayerIndex = readLayerIndex + 1u;

                if (thredIdx >= TILE_HEIGHT) {
                    return 0;
                }

                if (localLayerIndex >= WORKGROUP_CARRIES) {
                    var spillIndex : u32;
                    if (!compute_carry_spill_index(&spillIndex, readIndex, tileY, localLayerIndex)) {
                        // Should never happen.
                        return 0;
                    }
                    return carrySpills.spills[spillIndex].rows[thredIdx];
                }

                return carries[readIndex].data[localLayerIndex].rows[thredIdx];
            }

            fn peek_layer_for_next_input_layer_carry(tileY: i32) -> u32 {
                var readIndex = 1u - storeCarryIndex;
                if (readLayerIndex < carries[readIndex].count) {
                    if (readLayerIndex >= WORKGROUP_CARRIES) {
                        var spillIndex : u32;
                        if (!compute_carry_spill_index(&spillIndex, readIndex, tileY, readLayerIndex)) {
                            // Should never happen.
                            return 0u;
                        }
                        return carrySpills.spills[spillIndex].layer;
                    }
                    return carries[readIndex].data[readLayerIndex].layer;
                }
                return INVALID_LAYER;
            }

            var<workgroup> subgroupAnyBool : bool;
            fn fakeSubgroupAny(b: bool) -> bool {
                var targetBool = !subgroupAnyBool;
                if (b) {
                    subgroupAnyBool = targetBool;
                }
                workgroupBarrier();
                return (subgroupAnyBool == targetBool);
            }

            var<workgroup> foo : atomic<i32>;
            fn append_output_layer_carry2(tileY: i32, layer: u32, threadIdx: u32, cover: i32) {
                if (!fakeSubgroupAny(cover != 0 && threadIdx < TILE_HEIGHT)) {
                    return;
                }

                if (threadIdx < TILE_HEIGHT) {
                    var prevCarryCount = carries[storeCarryIndex].count;

                    if (prevCarryCount >= WORKGROUP_CARRIES) {
                        var spillIndex : u32;
                        if (!compute_carry_spill_index(&spillIndex,
                                storeCarryIndex, tileY, prevCarryCount)) {
                            return;
                        }
                        carrySpills.spills[spillIndex].rows[threadIdx] = cover;
                        carrySpills.spills[spillIndex].layer = layer;
                        carries[storeCarryIndex].count = prevCarryCount + 1u;
                        return;
                    }

                    carries[storeCarryIndex].data[prevCarryCount].rows[threadIdx] = cover;
                    carries[storeCarryIndex].data[prevCarryCount].layer = layer;
                    carries[storeCarryIndex].count = prevCarryCount + 1u;
                }
            }

            ///////////////////////////////////////////////////////////////////
            //  Main tile rasterization
            ///////////////////////////////////////////////////////////////////

            var<workgroup> areas : array<array<atomic<i32>, TILE_HEIGHT>, TILE_WIDTH_PLUS_ONE>;
            var<workgroup> covers : array<array<atomic<i32>, TILE_HEIGHT>, TILE_WIDTH_PLUS_ONE>;
            var<workgroup> accumulators : array<array<vec4<f32>, TILE_HEIGHT>, TILE_WIDTH_PLUS_ONE>;

            var<workgroup> psegmentsProcessed : atomic<u32>;
            var<workgroup> nextPsegmentIndex : u32;

            fn accumulate_layer_and_save_carry(tileY: i32, layer: u32, threadIdx: u32) {
                workgroupBarrier();
                var cover = 0;

                if (threadIdx < TILE_HEIGHT) {
                    for (var x = 0; x < TILE_WIDTH; x = x + 1) {
                        cover = cover + atomicLoad(&covers[x][threadIdx]);
                        atomicStore(&covers[x][threadIdx], cover);
                    }
                    cover = cover + atomicExchange(&covers[TILE_WIDTH][threadIdx], 0);
                }

                append_output_layer_carry2(tileY, layer, threadIdx, cover);

                workgroupBarrier();

                for (var y = 0; y < i32(TILE_HEIGHT); y = y + WORKGROUP_HEIGHT_IN_ROWS) {
                    var tx = i32(threadIdx & 7u);
                    var ty = i32(threadIdx >> TILE_WIDTH_SHIFT) + y;

                    var tarea = atomicExchange(&areas[tx][ty], 0);
                    var tcover = atomicExchange(&covers[tx][ty], 0);

                    var localAccumulator = accumulators[tx][ty];
                    accumulate(&localAccumulator, layer, tcover, tarea);
                    accumulators[tx][ty] = localAccumulator;
                }

                workgroupBarrier();
            }

            fn rasterizeTile(tileId: vec2<i32>, threadIdx: u32) {
                var tileRange = tileRanges.data[tile_index(tileId.x, tileId.y)];

                var currentLayer : u32 = INVALID_LAYER;
                if (threadIdx == 0u) {
                    nextPsegmentIndex = tileRange.start;
                    atomicStore(&psegmentsProcessed, 0u);
                }

                loop {
                    workgroupBarrier();
                    var carryLayer = peek_layer_for_next_input_layer_carry(tileId.y);
                    var segmentLayer = INVALID_LAYER;
                    if (nextPsegmentIndex < tileRange.end) {
                        segmentLayer = psegment_layer(segments.data[nextPsegmentIndex]);
                    }

                    if (segmentLayer == INVALID_LAYER && carryLayer == INVALID_LAYER) {
                        break;
                    }

                    var minLayer = min(carryLayer, segmentLayer);
                    if (minLayer != currentLayer) {
                        if (currentLayer != INVALID_LAYER) {
                            accumulate_layer_and_save_carry(tileId.y, currentLayer, threadIdx);
                        }
                        currentLayer = minLayer;
                    }

                    if (carryLayer == minLayer){
                        var carry = consume_input_layer_carry(tileId.y, threadIdx);
                        if (threadIdx < TILE_HEIGHT) {
                            atomicStore(&covers[0][threadIdx], carry);
                        }
                    }

                    if (segmentLayer == minLayer) {
                        var segmentLocalIndex = nextPsegmentIndex + threadIdx;
                        if (segmentLocalIndex < tileRange.end) {
                            var segment = segments.data[segmentLocalIndex];
                            if (psegment_layer(segment) == segmentLayer) {
                                ignore(atomicAdd(&psegmentsProcessed, 1u));

                                var segmentLocalX = psegment_local_x(segment);
                                var segmentLocalY = psegment_local_y(segment);
                                var segmentCover = psegment_cover(segment);
                                var segmentArea = psegment_area(segment);

                                ignore(atomicAdd(&covers[segmentLocalX + 1u][segmentLocalY], segmentCover));
                                ignore(atomicAdd(&areas[segmentLocalX][segmentLocalY], segmentArea));
                            }
                        }

                        workgroupBarrier();
                        if (threadIdx == 0u) {
                            nextPsegmentIndex = nextPsegmentIndex + atomicExchange(&psegmentsProcessed, 0u);
                        }
                        continue;
                    }
                }

                if (currentLayer != INVALID_LAYER) {
                    accumulate_layer_and_save_carry(tileId.y, currentLayer, threadIdx);
                }

                var tx = i32(threadIdx & 7u);
                var ty = i32(threadIdx >> TILE_WIDTH_SHIFT);

                for (var y = 0; y < i32(TILE_HEIGHT); y = y + WORKGROUP_HEIGHT_IN_ROWS) {
                    textureStore(out, tileId * 8 + vec2<i32>(tx, y + ty), accumulators[tx][y + ty]);
                    accumulators[tx][y + ty] = vec4<f32>(0.0);
                }
            }

            [[stage(compute), workgroup_size(WORKGROUP_SIZE)]]
            fn rasterizeTileRow([[builtin(workgroup_id)]] WorkgroupId : vec3<u32>,
                                [[builtin(local_invocation_id)]] LocalId : vec3<u32>) {
                flip_carry_stores();

                var tileY = i32(WorkgroupId.x) % config.heightInTiles;
                var threadIdx = LocalId.x;

                // TODO make parallel over whole subgroup
                if (threadIdx == 0u) {
                    var tileRange = tileRanges.data[tile_index(-1, tileY)];

                    var currentCovers : CarryCovers;
                    var currentLayer = INVALID_LAYER;

                    for (var i = tileRange.start; i < tileRange.end; i = i + 1u) {
                        var segment = segments.data[i];
                        var segmentLayer = psegment_layer(segment);

                        if (currentLayer != segmentLayer) {
                            append_output_layer_carry(tileY, currentLayer, currentCovers);
                            currentCovers = CarryCovers();
                            currentLayer = segmentLayer;
                        }

                        var segmentLocalY = psegment_local_y(segment);
                        var cover = psegment_cover(segment);
                        currentCovers[segmentLocalY] = currentCovers[segmentLocalY] + cover;
                    }
                    append_output_layer_carry(tileY, currentLayer, currentCovers);
                }

                workgroupBarrier();
                flip_carry_stores();

                var tileId = vec2<i32>(0, tileY);
                for (; tileId.x < i32(config.widthInTiles); tileId.x = tileId.x + 1) {
                    rasterizeTile(tileId, threadIdx);

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
        constexpr uint64_t kCarrySpillsPerRow = 100;

        ConfigUniforms uniformData = {
            config.width,
            config.height,
            widthInTiles,
            heightInTiles,
            config.segmentCount,
            tileRangeCount,
            kCarrySpillsPerRow,
        };
        wgpu::Buffer uniforms = utils::CreateBufferFromData(
                mDevice, &uniformData, sizeof(uniformData), wgpu::BufferUsage::Uniform);

        wgpu::BufferDescriptor tileRangeDesc;
        tileRangeDesc.size = tileRangeCount *  sizeof(TileRange);
        tileRangeDesc.usage = wgpu::BufferUsage::Storage;
        wgpu::Buffer tileRangeBuffer = mDevice.CreateBuffer(&tileRangeDesc);

        constexpr uint64_t kSizeofCarry = sizeof(uint32_t) + 8 * sizeof(int32_t);
        wgpu::BufferDescriptor tileCarrySpillDesc;
        tileCarrySpillDesc.size = 2 * kSizeofCarry * kCarrySpillsPerRow * heightInTiles;
        tileCarrySpillDesc.usage = wgpu::BufferUsage::Storage;
        wgpu::Buffer tileCarrySpillBuffer = mDevice.CreateBuffer(&tileCarrySpillDesc);

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
                {3, tileCarrySpillBuffer},
                {4, stylingsBuffer},
                {5, outTexture.CreateView()}
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
