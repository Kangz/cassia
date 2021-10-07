#include "TileWorkgroupRasterizer2.h"

#include "CommonWGSL.h"
#include "EncodingContext.h"

#include "utils/WGPUHelpers.h"

#include <string>

#include <iostream>

namespace cassia {

    struct ConfigUniforms {
        uint32_t width;
        uint32_t height;
        uint32_t segmentCount;
        int32_t tilesPerRow;
        uint32_t tileRangeCount;
    };
    static_assert(sizeof(ConfigUniforms) == 20, "");

    struct TileRange {
        uint32_t start;
        uint32_t end;
    };

    TileWorkgroupRasterizer2::TileWorkgroupRasterizer2(wgpu::Device device)
        : mDevice(std::move(device)) {
        std::string code = std::string(kPSegmentWGSL) + kStylingWGSL + R"(
            [[block]] struct Config {
                width: u32;
                height: u32;
                segmentCount: u32;
                tilesPerRow: i32;
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

            [[block]] struct Stylings {
                data: array<Styling>;
            };

            [[group(0), binding(2)]] var<storage> stylings : Stylings;
            [[group(0), binding(3)]] var out : texture_storage_2d<rgba16float, write>;

            // We need constexprs....
            let TILE_WIDTH = 8;
            let TILE_WIDTH_PLUS_ONE = 9;
            let TILE_HEIGHT = 8;
            let WORKGROUP_SIZE = 8;

            var<workgroup> areas : array<array<atomic<i32>, TILE_HEIGHT>, TILE_WIDTH_PLUS_ONE>;
            var<workgroup> covers : array<array<atomic<i32>, TILE_HEIGHT>, TILE_WIDTH_PLUS_ONE>;
            var<workgroup> group_index : i32;

            // TODO doesn't handle stuff outside the screen.
            [[stage(compute), workgroup_size(WORKGROUP_SIZE)]]
            fn rasterizeTileRow([[builtin(workgroup_id)]] WorkgroupId : vec3<u32>,
                             [[builtin(local_invocation_id)]] LocalId : vec3<u32>) {
                
                
                var tile_y = i32(WorkgroupId.x);
                var local_y = i32(LocalId.x);

                for(var i = 0; i < TILE_WIDTH; i = i + 1) {
                    atomicStore(&areas[i][local_y], 0);
                    atomicStore(&covers[i][local_y], 0);
                }
                atomicStore(&covers[TILE_WIDTH][local_y], 0);

                // TEMP: Applying layer 0 fill to all segments for now.
                var fill = stylings.data[0].fill;
                var color = vec3<f32>(fill[0], fill[1], fill[2]);

                ///////////////////////////////////////////////////////////////
                // Locate the start of tile row's psegments
                if (local_y == 0) {
                    group_index = i32(config.segmentCount);
                    var low = 0;
                    var high = group_index - 1;

                    for (; low <= high;) {
                        var mid = (low + high) >> 1u;
                        var segment = segments.data[mid];

                        if (psegment_is_none(segment) || psegment_tile_y(segment) > tile_y) {
                            high = mid - 1;
                        }
                        elseif (psegment_tile_y(segment) < tile_y) {
                            low = mid + 1;
                        }
                        else {
                            group_index = mid;
                            high = mid - 1;
                        }
                    }
                }

                // Wait for group_index to update
                workgroupBarrier();
                   
                // Invocations look at psegnets with their own offset
                var curr_index = group_index + local_y;

                ///////////////////////////////////////////////////////////
                // Iterate tiles in row
                for (var tile_x = 0; tile_x < i32(config.width); tile_x = tile_x + TILE_WIDTH) {
                    var pos_tile_x = (tile_x >> TILE_WIDTH_SHIFT);
                
                    ///////////////////////////////////////////////////////////
                    // Cooperatively accumulate the areas & covers in the tile

                    // Wait for any prior access to areas & covers to end
                    workgroupBarrier();

                    // Loop through psegments in the current tile
                    for (;
                         curr_index < i32(config.segmentCount);
                         curr_index = curr_index + WORKGROUP_SIZE) {
                        
                        var segment = segments.data[curr_index];
                        var ps_tile_x = psegment_tile_x(segment);
                        var ps_tile_y = psegment_tile_y(segment);

                        // Stop when reaching end of tile's segments
                        if (psegment_is_none(segment) ||
                            ps_tile_x > pos_tile_x ||
                            ps_tile_y > tile_y) {
                            break;
                        }

                        // Accumulate areas & covers
                        var ps_local_x = psegment_local_x(segment);
                        var ps_local_y = psegment_local_y(segment);

                        if (ps_tile_x == pos_tile_x) {
                            ignore(atomicAdd(&areas[ps_local_x][ps_local_y], psegment_area(segment)));
                            ignore(atomicAdd(&covers[ps_local_x + 1u][ps_local_y], psegment_cover(segment)));
                        } else {
                            ignore(atomicAdd(&covers[0][ps_local_y], psegment_cover(segment)));
                        }
                    }

                    // Wait for area & cover accumulation to finish.
                    workgroupBarrier();                   

                    ///////////////////////////////////////////////////////////
                    // Output the tile
                    var cover = 0;

                    //for loc_x in range(0, TILE_WIDTH):
                    for (var loc_x = 0; loc_x < TILE_WIDTH; loc_x = loc_x + 1) {
                        var area = atomicExchange(&areas[loc_x][local_y], 0);
                        cover = cover + atomicExchange(&covers[loc_x][local_y], 0);

                        var coverage = (f32(cover) / COVER_DIVISOR) + (f32(area) / AREA_DIVISOR);
                        var accumulator = vec4<f32>(color * coverage, fill[3]);

                        textureStore(out, vec2<i32>(tile_x + loc_x, (tile_y << TILE_HEIGHT_SHIFT) + local_y), accumulator);
                    }

                    // Save output covers for next tile
                    atomicStore(&covers[0][local_y], cover + atomicExchange(&covers[TILE_WIDTH][local_y], 0));
                }
            }
        )";

        wgpu::ShaderModule module = utils::CreateShaderModule(mDevice, code.c_str());

        wgpu::ComputePipelineDescriptor pDesc;
        pDesc.label = "TileWorkgroupRasterizer2::mRasterPipeline";
        pDesc.compute.module = module;
        pDesc.compute.entryPoint = "rasterizeTileRow";
        mRasterPipeline = mDevice.CreateComputePipeline(&pDesc);
    }

    wgpu::Texture TileWorkgroupRasterizer2::Rasterize(EncodingContext* context,
        wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
        const Config& config) {
        int32_t tilesPerRow = 1 << (16 - TILE_WIDTH_SHIFT);
        uint32_t tileRowCount = (config.height + TILE_HEIGHT_SHIFT - 1) >> TILE_HEIGHT_SHIFT;
        uint32_t tileRangeCount = tilesPerRow * tileRowCount;

        ConfigUniforms uniformData = {
            config.width,
            config.height,
            config.segmentCount,
            tilesPerRow,
            tileRangeCount
        };
        wgpu::Buffer uniforms = utils::CreateBufferFromData(
                mDevice, &uniformData, sizeof(uniformData), wgpu::BufferUsage::Uniform);

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
                {2, stylingsBuffer},
                {3, outTexture.CreateView()}
            });

            {
                ScopedComputePass pass(context, "TileWorkgroupRasterizer2::FakePassToFactorOutLazyClearCost");

                pass->SetBindGroup(0, bg);
                pass->SetPipeline(mRasterPipeline);
                pass->Dispatch(0);
            }

            ScopedComputePass pass(context, "TileWorkgroupRasterizer2::Raster");

            pass->SetBindGroup(0, bg);
            pass->SetPipeline(mRasterPipeline);
            pass->Dispatch(tileRowCount);
        }

        return outTexture;
    }

} // namespace cassia
