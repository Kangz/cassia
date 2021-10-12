#include "CommonWGSL.h"

namespace cassia {

    const char kPSegmentWGSL[] = R"(
        // This is the definition of a PSegment in mold
        //
        // pub const TILE_WIDTH: usize = 8;
        // const TILE_WIDTH_SHIFT: usize = TILE_WIDTH.trailing_zeros() as usize;
        // const TILE_WIDTH_MASK: usize = TILE_WIDTH - 1;
        //
        // pub const TILE_HEIGHT: usize = 8;
        // const TILE_HEIGHT_SHIFT: usize = TILE_HEIGHT.trailing_zeros() as usize;
        // const TILE_HEIGHT_MASK: usize = TILE_HEIGHT - 1;/
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

        // Also keep the constants in CommonWGSL.h in sync.
        let TILE_WIDTH_SHIFT = 3u;
        let TILE_HEIGHT_SHIFT = 3u;
        let TILE_X_OFFSET = 256;
        let PIXEL_SIZE = 16;
        let PIXEL_AREA = 256;

        // TODO remove when all rasterizers use StylingWGSL
        let COVER_DIVISOR = 16.0;
        let AREA_DIVISOR = 256.0;

        fn psegment_is_none(s : PSegment) -> bool {
            return bool(s.hi & (1u << 31u));
        }
        fn psegment_layer(s : PSegment) -> u32 {
            var mask = (1u << 16u) - 1u;
            return ((s.hi << (16u - TILE_WIDTH_SHIFT - TILE_HEIGHT_SHIFT)) & mask) |
                   (s.lo >> (16u + TILE_WIDTH_SHIFT + TILE_HEIGHT_SHIFT));
        }
        fn psegment_tile_x(s : PSegment) -> i32 {
            return ((i32(s.hi) << (16u - TILE_HEIGHT_SHIFT)) >> (16u + TILE_WIDTH_SHIFT)) - TILE_X_OFFSET;
        }
        fn psegment_tile_y(s : PSegment) -> i32{
            return (i32(s.hi) << 1u) >> (17u + TILE_HEIGHT_SHIFT);
        }
        fn psegment_local_x(s : PSegment) -> u32 {
            var mask = (1u << TILE_WIDTH_SHIFT) - 1u;
            return (s.lo >> 16u) & mask;
        }
        fn psegment_local_y(s : PSegment) -> u32{
            var mask = (1u << TILE_HEIGHT_SHIFT) - 1u;
            return (s.lo >> (16u + TILE_WIDTH_SHIFT)) & mask;
        }
        fn psegment_area(s : PSegment) -> i32{
            return i32(s.lo << 16u) >> 22u;
        }
        fn psegment_cover(s : PSegment) -> i32{
            return i32(s.lo << 26u) >> 26u;
        }
    )";

    const char kStylingWGSL[] = R"(
        let LAST_BYTE_MASK: i32 = 255; // PIXEL_AREA - 1

        struct Styling {
            fill: vec4<f32>;
            fillRule: u32;
            blendMode: u32;
        };

        fn styling_coverage_to_alpha(area: i32, fillRule: u32) -> f32 {
            // NonZero
            switch (fillRule) {
                // NonZero
                case 0u: {
                    return clamp(abs(f32(area) / f32(PIXEL_AREA)), 0.0, 1.0);
                }
                // EvenOdd
                default: {
                    let windingNumber = area >> 8u;
                    let fractionalPart = f32(area & LAST_BYTE_MASK) / f32(PIXEL_AREA);

                    if ((windingNumber & 1) == 0) {
                        return fractionalPart;
                    } else {
                        return 1.0 - fractionalPart;
                    }
                }
            }

            // TODO remove when Tint is fixed
            return 0.0;
        }

        fn styling_do_blend(dst: vec4<f32>, src: vec4<f32>, blendMode: u32) -> vec4<f32> {
            let alpha = src.w;
            let inverseAlpha = 1.0 - alpha;

            var color: vec3<f32>;
            let dstColor = dst.xyz;
            let srcColor = src.xyz * alpha;

            switch (blendMode) {
                // Over
                case 0u: {
                    color = srcColor;
                    break;
                }

                // Multiply
                case 1u: {
                    color = dstColor * srcColor;
                    break;
                }

                // Screen
                case 2u: {
                    color = fma(dstColor, -srcColor, srcColor);
                    break;
                }

                // Overlay
                case 3u: {
                    color = 2.0 * select(
                        (dstColor + srcColor - fma(dstColor, srcColor, vec3<f32>(0.5))),
                        dstColor * srcColor,
                        srcColor <= vec3<f32>(0.5),
                    );
                    break;
                }

                // Darken
                case 4u: {
                    color = min(dstColor, srcColor);
                    break;
                }

                // Lighten
                case 5u: {
                    color = max(dstColor, srcColor);
                    break;
                }

                // ColorDodge
                case 6u: {
                    color = select(
                        min(vec3<f32>(1.0), srcColor / (vec3<f32>(1.0) - dstColor)),
                        vec3<f32>(0.0),
                        srcColor == vec3<f32>(0.0),
                    );
                    break;
                }

                // ColorBurn
                case 7u: {
                    color = select(
                        vec3<f32>(1.0) - min(vec3<f32>(1.0), (vec3<f32>(1.0) - srcColor) / dstColor),
                        vec3<f32>(1.0),
                        srcColor == vec3<f32>(1.0),
                    );
                    break;
                }

                // HardLight
                case 8u: {
                    color = 2.0 * select(
                        dstColor + srcColor - fma(dstColor, srcColor, vec3<f32>(0.5)),
                        dstColor * srcColor,
                        dstColor <= vec3<f32>(0.5),
                    );
                    break;
                }

                // SoftLight
                case 9u: {
                    let d = select(
                        sqrt(srcColor),
                        srcColor * fma(
                            fma(vec3<f32>(16.0), srcColor, vec3<f32>(-12.0)),
                            srcColor,
                            vec3<f32>(4.0),
                        ),
                        srcColor <= vec3<f32>(0.25),
                    );

                    color = 2.0 * select(
                        fma(
                            d - srcColor,
                            fma(vec3<f32>(2.0), dstColor, vec3<f32>(-1.0)),
                            srcColor
                        ),
                        srcColor * (vec3<f32>(1.0) - srcColor),
                        dstColor <= vec3<f32>(0.5),
                    );
                    break;
                }

                // Difference
                case 10u: {
                    color = abs(dstColor - srcColor);
                    break;
                }

                // Exclusion
                case 11u: {
                    color = fma(
                        dstColor,
                        fma(vec3<f32>(-2.0), srcColor, vec3<f32>(1.0)),
                        srcColor,
                    );
                    break;
                }

                default: { break; }
            }

            return fma(dst, vec4<f32>(inverseAlpha), vec4<f32>(color, alpha));
        }

        fn styling_accumulate_layer(previousLayers: vec4<f32>, pixelCoverage: i32, styling: Styling) -> vec4<f32> {
            var coverageAlpha = styling_coverage_to_alpha(pixelCoverage, styling.fillRule);
            var currentLayer = vec4<f32>(styling.fill.xyz, styling.fill.w * coverageAlpha);
            return styling_do_blend(previousLayers, currentLayer, styling.blendMode);
        }
    )";

} // namespace
