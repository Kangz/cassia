#include "CommonWGSL.h"

namespace cassia {

    const char kPSegmentWGSL[] = R"(
        // This is the definition of a PSegment, a definition of the form name: number of bits:
        //
        //   TILE_WIDTH_SHIFT = 3
        //   TILE_HEIGHT_SHIFT = 3
        //
        //   is_none: 1bit
        //   tile_y: 15 - TILE_HEIGHT_SHIFT
        //   tile_x_sign: 1
        //
        //   if (tile_x_sign == 0) {
        //     tile_x_rest: 16 - TILE_WIDTH_SHIFT - 1 (for the sign bit)
        //     layer: 16
        //   } else {
        //     layer: 16
        //     tile_x_rest: 16 - TILE_WIDTH_SHIFT - 1 (for the sign bit)
        //   }
        //
        //   local_y: TILE_HEIGHT_SHIFT
        //   local_x: TILE_WIDTH_SHIFT
        //   area: 10
        //   cover: 6

        struct PSegment {
            lo: u32;
            hi: u32;
        };

        // Also keep the constants in CommonWGSL.h in sync.
        let TILE_WIDTH_SHIFT = 3u;
        let TILE_HEIGHT_SHIFT = 3u;
        let COVER_DIVISOR = 16.0;
        let AREA_DIVISOR = 256.0;

        fn psegment_is_none(s : PSegment) -> bool {
            return bool(s.hi & (1u << 31u));
        }
        fn psegment_tile_x_negative(s : PSegment) -> bool {
            return bool(s.hi & (1u << (31u - 15u + TILE_HEIGHT_SHIFT - 1u)));
        }

        fn psegment_layer_for_positive_tile_x(s : PSegment) -> u32 {
            var mask = (1u << 16u) - 1u;
            return ((s.hi << (16u - TILE_WIDTH_SHIFT - TILE_HEIGHT_SHIFT)) & mask) |
                   (s.lo >> (16u + TILE_WIDTH_SHIFT + TILE_HEIGHT_SHIFT));
        }
        fn psegment_layer_for_negative_tile_x(s : PSegment) -> u32 {
            return ((s.hi << (17u - TILE_HEIGHT_SHIFT)) >> 16u);
        }
        fn psegment_layer(s : PSegment) -> u32 {
            if (psegment_tile_x_negative(s)) {
                return psegment_layer_for_negative_tile_x(s);
            } else {
                return psegment_layer_for_positive_tile_x(s);
            }
            // TODO remove when Tint is fixed.
            return 0u;
        }

        fn psegment_tile_x_for_positive_tile_x(s : PSegment) -> i32 {
            return (i32(s.hi) << (16u - TILE_HEIGHT_SHIFT)) >> (16u + TILE_WIDTH_SHIFT);
        }
        fn psegment_tile_x_for_negative_tile_x(s : PSegment) -> i32 {
            var kTotalBits = 15u - TILE_WIDTH_SHIFT; // without the sign bit
            var kBitsHigh = TILE_HEIGHT_SHIFT - 1u;
            var kBitsLow = kTotalBits - kBitsHigh;

            var kHighMask = (1u << (kBitsHigh)) - 1u;
            return i32((1u << 31u) |
                // ((s.hi & kHighMask) << (kBitsLow)) |
                ((s.lo >> (32u - kBitsLow)))
            );
        }
        fn psegment_tile_x(s : PSegment) -> i32 {
            if (psegment_tile_x_negative(s)) {
                return psegment_tile_x_for_negative_tile_x(s);
            } else {
                return psegment_tile_x_for_positive_tile_x(s);
            }
            // TODO remove when Tint is fixed.
            return 0;
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

    const char kSylingWGSL[] = R"(
        let LAST_BYTE_MASK: i32 = 255;
        let LAST_BIT_MASK: i32 = 1;

        fn from_area(area: i32, fill_rule: u32) -> f32 {
            // NonZero
            if (fill_rule == 0u) {
                return clamp(abs(f32(area) / 256.0), 0.0, 1.0);
            // EvenOdd
            } else {
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

        fn blend(dst: vec4<f32>, src: vec4<f32>, blend_mode: u32) -> vec4<f32> {
            let alpha = src.w;
            let inv_alpha = 1.0 - alpha;

            var color: vec3<f32>;
            let dst_color = dst.xyz;
            let src_color = src.xyz;

            // Over
            if (blend_mode == 0u) {
                color = src_color;
            // Multiply
            } elseif (blend_mode == 1u) {
                color = dst_color * src_color;
            // Screen
            } elseif (blend_mode == 2u) {
                color = fma(dst_color, -src_color, src_color);
            // Overlay
            } elseif (blend_mode == 3u) {
                color = 2.0 * select(
                    (dst_color + src_color - fma(dst_color, src_color, vec3<f32>(0.5))),
                    dst_color * src_color,
                    src_color <= vec3<f32>(0.5),
                );
            // Darken
            } elseif (blend_mode == 4u) {
                color = min(dst_color, src_color);
            // Lighten
            } elseif (blend_mode == 5u) {
                color = max(dst_color, src_color);
            // ColorDodge
            } elseif (blend_mode == 6u) {
                color = select(
                    min(vec3<f32>(1.0), src_color / (vec3<f32>(1.0) - dst_color)),
                    vec3<f32>(0.0),
                    src_color == vec3<f32>(0.0),
                );
            // ColorBurn
            } elseif (blend_mode == 7u) {
                color = select(
                    vec3<f32>(1.0) - min(vec3<f32>(1.0), (vec3<f32>(1.0) - src_color) / dst_color),
                    vec3<f32>(1.0),
                    src_color == vec3<f32>(1.0),
                );
            // HardLight
            } elseif (blend_mode == 8u) {
                color = 2.0 * select(
                    dst_color + src_color - fma(dst_color, src_color, vec3<f32>(0.5)),
                    dst_color * src_color,
                    dst_color <= vec3<f32>(0.5),
                );
            // SoftLight
            } elseif (blend_mode == 9u) {
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
            // Difference
            } elseif (blend_mode == 10u) {
                color = abs(dst_color - src_color);
            // Exclusion
            } elseif (blend_mode == 11u) {
                color = fma(
                    dst_color,
                    fma(vec3<f32>(-2.0), src_color, vec3<f32>(1.0)),
                    src_color,
                );
            }

            return fma(dst, vec4<f32>(inv_alpha), vec4<f32>(color, alpha));
        }
    )";

} // namespace
