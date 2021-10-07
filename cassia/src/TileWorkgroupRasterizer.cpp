#include "TileWorkgroupRasterizer.h"

#include "CommonWGSL.h"
#include "EncodingContext.h"

#include "utils/WGPUHelpers.h"

#include <string>

namespace cassia {

    TileWorkgroupRasterizer::TileWorkgroupRasterizer(wgpu::Device device)
        : mDevice(std::move(device)) {
        std::string code = std::string(kPSegmentWGSL) +  R"(
            [[block]] struct Config {
                width: u32;
                height: u32;
                segmentCount: u32;
                tilesPerRow: u32;
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
            [[group(0), binding(2)]] var<storage, write> tileRanges : TileRanges;

            fn tile_index(tileX: i32, tileY: i32) -> u32 {
                // TODO handle tileX/Y being negative!
                return u32(tileX) + u32(config.tilesPerRow) * u32(tileY);
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
                        var tileIndex = tile_index(tileX, tileY);

                        tileRanges.data[tileIndex].end = GlobalId.x + 1u;
                    }

                    return;
                }

                // TODO we could share segments with the workgroup to avoid redundant load
                // TODO ignore tileY outside of the screen

                var segment0 = segments.data[GlobalId.x];
                var segment1 = segments.data[GlobalId.x + 1u];

                var tileX0 = psegment_tile_x(segment0);
                var tileX1 = psegment_tile_x(segment1);
                var tileY0 = psegment_tile_y(segment0);
                var tileY1 = psegment_tile_y(segment1);
                if (tileX0 != tileX1 || tileY0 != tileY1) {
                    var tileIndex0 = tile_index(tileX0, tileY0);
                    tileRanges.data[tileIndex0].end = GlobalId.x + 1u;

                    var tileIndex1 = tile_index(tileX1, tileY1);
                    tileRanges.data[tileIndex1].start = GlobalId.x + 1u;
                }
            }
        )";

        wgpu::ShaderModule module = utils::CreateShaderModule(mDevice, code.c_str());
    }

    wgpu::Texture TileWorkgroupRasterizer::Rasterize(EncodingContext* context,
        wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
        const Config& config) {
        // TODO make a buffer with per-tile psegment start

        return {}; // TODO
    }

} // namespace cassia
