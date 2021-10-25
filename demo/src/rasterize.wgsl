let TILE_WIDTH: u32 = 8u;
let TILE_WIDTH_SHIFT: u32 = 3u;
let TILE_HEIGHT: u32 = 64u;
let TILE_HEIGHT_SHIFT: u32 = 6u;

struct PixelSegment {
    lo: u32;
    hi: u32;
};

// fn pixel_segment_tile_x(seg: PixelSegment) -> i32 {
//     i32(seg.hi) << (16 - TILE_HEIGHT_SHIFT) >> (16 + TILE_WIDTH_SHIFT)
// }

// fn pixel_segment_tile_y(seg: PixelSegment) -> i32 {
//     i32(seg.hi) << 1u >> (17u + TILE_HEIGHT_SHIFT)
// }

// fn pixel_segment_layer_id(seg: PixelSegment) -> u32 {
//     let mask = (1u << 16u) - 1u;
//     (seg.hi << (16u - TILE_WIDTH_SHIFT - TILE_HEIGHT_SHIFT)) &
//         mask | (seg.lo >> (16u + TILE_WIDTH_SHIFT + TILE_HEIGHT_SHIFT))
// }

// fn pixel_segment_local_x(seg: PixelSegment) -> u32 {
//     let mask = (1u << TILE_WIDTH_SHIFT) - 1u;
//     (seg.lo >> 16u) & mask
// }

// fn pixel_segment_local_y(seg: PixelSegment) -> u32 {
//     let mask = (1u << TILE_HEIGHT_SHIFT) - 1u;
//     (seg.lo >> (16u + TILE_WIDTH_SHIFT)) & mask
// }

// fn pixel_segment_area(seg: PixelSegment) -> i32 {
//     i32(seg.lo) << 16u >> 22u
// }

// fn pixel_segment_cover(seg: PixelSegment) -> i32 {
//     i32(seg.lo) << 26u >> 26u
// }

// struct Cell {
//     val: u32;
// }

// fn cell_new(area: i32, cover: i32) -> Cell {
//     let mask = (1u << 16u) - 1u;
//     Cell(u32((area << 16u) | mask & cover))
// }

// fn cell_area(cell: Cell) -> i32 {
//     i32(cell.val) >> 16u
// }

// fn cell_cover(cell: Cell) -> i32 {
//     i32(cell.val) << 16u >> 16u
// }

// fn cell_acc_seg(cell: Cell, seg: PixelSegment) -> Cell {
//     let area = cell_area(cell) + pixel_segment_area(seg);
//     let cover = cell_cover(cell) + pixel_segment_cover(seg);

//     cell_new(area, cover)
// }

// struct Painter {
//     cells: Cell[TILE_WIDTH + 1u];
//     colors: vec4[TILE_WIDTH];
// }

let LAST_BYTE_MASK: i32 = 255;
let LAST_BIT_MASK: i32 = 1;

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
}

fn blend(dst: vec4<f32>, src: vec4<f32>, blend_mode: u32) -> vec4<f32> {
    let alpha = src.w;
    let inv_alpha = 1.0 - alpha;

    var color: vec3<f32>;
    let dst_color = dst.xyz;
    let src_color = src.xyz;

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
    }

    return fma(dst, vec4<f32>(inv_alpha), vec4<f32>(color, alpha));
}