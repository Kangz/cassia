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
        let LAST_BYTE_MASK: i32 = 255;
        let LAST_BIT_MASK: i32 = 1;

        struct Styling {
            fill: vec4<f32>;
            fill_rule: u32;
            blend_mode: u32;
        };

        fn from_area(area: i32, fill_rule: u32) -> f32 {
            // NonZero
            switch (fill_rule) {
                // NonZero
                case 0u: {
                    return clamp(abs(f32(area) / 256.0), 0.0, 1.0);
                }
                // EvenOdd
                default: {
                    let number = area >> 8u;
                    let masked = f32(area & LAST_BYTE_MASK);
                    let capped = masked / 256.0;

                    if ((number & LAST_BIT_MASK) == 0) {
                        return capped;
                    } else {
                        return 1.0 - capped;
                    }
                }
            }

            // TODO remove when Tint is fixed
            return 0.0;
        }

        fn blend(dst: vec4<f32>, src: vec4<f32>, blend_mode: u32) -> vec4<f32> {
            let alpha = src.w;
            let inv_alpha = 1.0 - alpha;

            var color: vec3<f32>;
            let dst_color = dst.xyz;
            let src_color = src.xyz * alpha;

            switch (blend_mode) {
                // Over
                case 0u: {
                    color = src_color;

                    break;
                }
                // Multiply
                case 1u: {
                    color = dst_color * src_color;

                    break;
                }
                // Screen
                case 2u: {
                    color = fma(dst_color, -src_color, src_color);

                    break;
                }
                // Overlay
                case 3u: {
                    color = 2.0 * select(
                        (dst_color + src_color - fma(dst_color, src_color, vec3<f32>(0.5))),
                        dst_color * src_color,
                        src_color <= vec3<f32>(0.5),
                    );

                    break;
                }
                // Darken
                case 4u: {
                    color = min(dst_color, src_color);

                    break;
                }
                // Lighten
                case 5u: {
                    color = max(dst_color, src_color);

                    break;
                }
                // ColorDodge
                case 6u: {
                    color = select(
                        min(vec3<f32>(1.0), src_color / (vec3<f32>(1.0) - dst_color)),
                        vec3<f32>(0.0),
                        src_color == vec3<f32>(0.0),
                    );

                    break;
                }
                // ColorBurn
                case 7u: {
                    color = select(
                        vec3<f32>(1.0) - min(vec3<f32>(1.0), (vec3<f32>(1.0) - src_color) / dst_color),
                        vec3<f32>(1.0),
                        src_color == vec3<f32>(1.0),
                    );

                    break;
                }
                // HardLight
                case 8u: {
                    color = 2.0 * select(
                        dst_color + src_color - fma(dst_color, src_color, vec3<f32>(0.5)),
                        dst_color * src_color,
                        dst_color <= vec3<f32>(0.5),
                    );

                    break;
                }
                // SoftLight
                case 9u: {
                    let d = select(
                        sqrt(src_color),
                        src_color * fma(
                            fma(vec3<f32>(16.0), src_color, vec3<f32>(-12.0)),
                            src_color,
                            vec3<f32>(4.0),
                        ),
                        src_color <= vec3<f32>(0.25),
                    );

                    color = 2.0 * select(
                        fma(
                            d - src_color,
                            fma(vec3<f32>(2.0), dst_color, vec3<f32>(-1.0)),
                            src_color
                        ),
                        src_color * (vec3<f32>(1.0) - src_color),
                        dst_color <= vec3<f32>(0.5),
                    );

                    break;
                }
                // Difference
                case 10u: {
                    color = abs(dst_color - src_color);

                    break;
                }
                // Exclusion
                case 11u: {
                    color = fma(
                        dst_color,
                        fma(vec3<f32>(-2.0), src_color, vec3<f32>(1.0)),
                        src_color,
                    );

                    break;
                
                }
                default: { break; }
            }

            return fma(dst, vec4<f32>(inv_alpha), vec4<f32>(color, alpha));
        }
    )";

} // namespace
