let TILE_WIDTH = 16u;
let TILE_HEIGHT = 4u;
let TILE_WIDTH_SHIFT = 4u;
let TILE_HEIGHT_SHIFT = 2u;

let BLOCK_LEN = 64u;
let BLOCK_SHIFT = 6u;
let BLOCK_MASK = 63u;
let QUEUES_LEN = 128u;
let QUEUES_MASK = 127u;

let PIXEL_WIDTH = 16;
let PIXEL_AREA_RECIP = 0.00390625;

let LAYER_ID_NONE = 4294967295u;

struct PixelSegment {
    lo: u32;
    hi: u32;
};

fn pixelSegmentTileX(seg: PixelSegment) -> i32 {
    return (i32(seg.hi) << (16u - TILE_HEIGHT_SHIFT) >>
        (16u + TILE_WIDTH_SHIFT)) - 1;
}

fn pixelSegmentTileY(seg: PixelSegment) -> i32 {
    return i32(seg.hi) << 1u >> (17u + TILE_HEIGHT_SHIFT);
}

fn pixelSegmentLayerId(seg: PixelSegment) -> u32 {
    let mask = (1u << 16u) - 1u;
    return (seg.hi << (16u - TILE_WIDTH_SHIFT - TILE_HEIGHT_SHIFT)) & mask |
            (seg.lo >> (16u + TILE_WIDTH_SHIFT + TILE_HEIGHT_SHIFT));
}

fn pixelSegmentLocalX(seg: PixelSegment) -> u32 {
    let mask = (1u << TILE_WIDTH_SHIFT) - 1u;
    return (seg.lo >> 16u) & mask;
}

fn pixelSegmentLocalY(seg: PixelSegment) -> u32 {
    let mask = (1u << TILE_HEIGHT_SHIFT) - 1u;
    return (seg.lo >> (16u + TILE_WIDTH_SHIFT)) & mask;
}

fn pixelSegmentArea(seg: PixelSegment) -> i32 {
    return i32(seg.lo << 16u) >> 22u;
}

fn pixelSegmentCover(seg: PixelSegment) -> i32 {
    return i32(seg.lo << 26u) >> 26u;
}

struct OptimizedSegment {
    lo: u32;
    hi: u32;
};

fn optimizedSegment(
    tile_x: i32,
    layer_id: u32,
    local_x: u32,
    local_y: u32,
    area: i32,
    cover: i32,
) -> OptimizedSegment {
    let mask = (1u << 6u) - 1u;
    return OptimizedSegment(
        u32(area << 22u) | ((u32(cover) & mask) << 16u) |
            (local_x << TILE_HEIGHT_SHIFT) | local_y,
        u32(tile_x << 16u) | layer_id,
    );
}

fn optimizedSegmentTileX(seg: OptimizedSegment) -> i32 {
    return i32(seg.hi) >> 16u;
}

fn optimizedSegmentLayerId(seg: OptimizedSegment) -> u32 {
    let mask = (1u << 16u) - 1u;
    return seg.hi & mask;
}

fn optimizedSegmentLocalX(seg: OptimizedSegment) -> u32 {
    let mask = (1u << TILE_WIDTH_SHIFT) - 1u;
    return (seg.lo >> TILE_HEIGHT_SHIFT) & mask;
}

fn optimizedSegmentLocalY(seg: OptimizedSegment) -> u32 {
    let mask = (1u << TILE_HEIGHT_SHIFT) - 1u;
    return seg.lo & mask;
}

fn optimizedSegmentArea(seg: OptimizedSegment) -> i32 {
    return i32(seg.lo) >> 22u;
}

fn optimizedSegmentCover(seg: OptimizedSegment) -> i32 {
    return i32(seg.lo << 10u) >> 26u;
}

struct Color {
    r: f32;
    g: f32;
    b: f32;
    a: f32;
};

[[block]]
struct Config {
    width: u32;
    height: u32;
    clear_color: Color;
};

[[block]]
struct PixelSegments {
    data: array<PixelSegment>;
};

struct Style {
    fill_rule: u32;
    color: Color;
    blend_mode: u32;
};

[[block]]
struct Styles {
    data: array<Style>;
};

[[group(0), binding(0)]]
var<uniform> config: Config;
[[group(0), binding(1)]]
var<storage> segments: PixelSegments;
[[group(0), binding(2)]]
var<storage> styles: Styles;
[[group(0), binding(3)]]
var image: texture_storage_2d<rgba16float, write>;

var<workgroup> segment_block: array<OptimizedSegment, BLOCK_LEN>;
var<private> segment_index: u32;
var<private> block_index: u32;

fn loadSegments(tile_y: i32, segments_len: u32, local_index: u32) -> bool {
    if (block_index > (segments_len >> BLOCK_SHIFT)) {
        return false;
    }

    let i = block_index * BLOCK_LEN + local_index;
    var opt_seg = optimizedSegment(
        -2,
        0u,
        0u,
        0u,
        0,
        0,
    );

    workgroupBarrier();

    if (i < segments_len) {
        let seg = segments.data[i];

        if (pixelSegmentTileY(seg) == tile_y) {
            opt_seg = optimizedSegment(
                pixelSegmentTileX(seg),
                pixelSegmentLayerId(seg),
                pixelSegmentLocalX(seg),
                pixelSegmentLocalY(seg),
                pixelSegmentArea(seg),
                pixelSegmentCover(seg),
            );
        }
    }

    segment_block[local_index] = opt_seg;

    workgroupBarrier();

    block_index = block_index + 1u;

    return true;
}

fn clearColor() -> vec4<f32> {
    return vec4<f32>(
        config.clear_color.r,
        config.clear_color.g,
        config.clear_color.b,
        config.clear_color.a,
    );
}

var<workgroup> queues_layer_id_buffer: array<u32, QUEUES_LEN>;
var<workgroup> queues_cover_buffer: array<atomic<u32>, QUEUES_LEN>;

struct Queues {
    start0: u32;
    end0: u32;
    start1: u32;
};

struct Painter {
    queues: Queues;
    area: i32;
    cover: i32;
    color: vec4<f32>;
};

fn areaToCoverage(area: i32, fill_rule: u32) -> f32 {
    switch (fill_rule) {
        // NonZero
        case 0u: {
            return clamp(abs(f32(area) * PIXEL_AREA_RECIP), 0.0, 1.0);
        }
        // EvenOdd
        default: {
            let winding_number = area >> 8u;
            let norm = f32(area & 255) * PIXEL_AREA_RECIP;

            return select(
                1.0 - norm,
                norm,
                (winding_number & 1) == 0,
            );
        }
    }
}

fn blend(dst: vec4<f32>, src: vec4<f32>, blend_mode: u32) -> vec4<f32> {
    let alpha = src.w;
    let inverse_alpha = 1.0 - alpha;

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
                (dst_color + src_color -
                    fma(dst_color, src_color, vec3<f32>(0.5))),
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
                vec3<f32>(1.0) - min(
                    vec3<f32>(1.0),
                    (vec3<f32>(1.0) - src_color) / dst_color,
                ),
                vec3<f32>(1.0),
                src_color == vec3<f32>(1.0),
            );
            break;
        }
        // HardLight
        case 8u: {
            color = 2.0 * select(
                dst_color + src_color -
                    fma(dst_color, src_color, vec3<f32>(0.5)),
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
        default: {
            color = fma(
                dst_color,
                fma(vec3<f32>(-2.0), src_color, vec3<f32>(1.0)),
                src_color,
            );
            break;
        }
    }

    return fma(dst, vec4<f32>(inverse_alpha), vec4<f32>(color, alpha));
}

fn painterPushCover(
    painter: ptr<function, Painter>,
    layer_id: u32,
    fill_rule: u32,
    local_id: vec2<u32>,
) {
    var mask: u32;
    switch (fill_rule) {
        // NonZero
        case 0u: {
            mask = 4294967295u;
        }
        // EvenOdd
        default: {
            mask = 522133279u;
        }
    }

    queues_layer_id_buffer[(*painter).queues.start1] = layer_id;

    if (local_id.x == 0u && local_id.y == 0u) {
        atomicStore(&queues_cover_buffer[(*painter).queues.start1], 0u);
    }

    workgroupBarrier();

    if (local_id.x == (TILE_WIDTH - 1u)) {
        let tmp = atomicOr(
            &queues_cover_buffer[(*painter).queues.start1],
            u32(((*painter).cover & 255) << (local_id.y << 3u)),
        );
    }

    workgroupBarrier();

    (*painter).queues.start1 = ((*painter).queues.start1 + select(
        0u,
        1u,
        (atomicLoad(&queues_cover_buffer[(*painter).queues.start1]) & mask) != 0u,
    )) & QUEUES_MASK;
}

fn painterBlendLayer(
    painter: ptr<function, Painter>,
    layer_id: u32,
    local_id: vec2<u32>,
) {
    let style = styles.data[layer_id];

    painterPushCover(painter, layer_id, style.fill_rule, local_id);

    let src = vec4<f32>(
        style.color.r,
        style.color.g,
        style.color.b,
        style.color.a * areaToCoverage((*painter).area, style.fill_rule),
    );

    (*painter).area = 0;
    (*painter).cover = 0;
    (*painter).color = blend((*painter).color, src, style.blend_mode);
}

fn painterPopQueueUntil(
    painter: ptr<function, Painter>,
    layer_id: u32,
    local_id: vec2<u32>,
) {
    loop {
        if ((*painter).queues.start0 == (*painter).queues.end0) { break; }

        let current_layer_id =
            queues_layer_id_buffer[(*painter).queues.start0];
        if (current_layer_id > layer_id) { break; }

        let shift = local_id.y << 3u;
        let cover = i32(queues_cover_buffer[(*painter).queues.start0]) <<
            (24u - shift) >> 24u;

        (*painter).area = (*painter).area + cover * PIXEL_WIDTH;
        (*painter).cover = (*painter).cover + cover;

        if (current_layer_id < layer_id) {
            painterBlendLayer(painter, current_layer_id, local_id);
        }

        (*painter).queues.start0 = ((*painter).queues.start0 + 1u) &
            QUEUES_MASK;
    }
}

fn painterNegativeCovers(
    painter: ptr<function, Painter>,
    tile: vec2<i32>,
    segments_len: u32,
    local_index: u32,
    local_id: vec2<u32>,
) {
    var seg: OptimizedSegment;
    var layer_id = LAYER_ID_NONE;
    loop {
        var should_break = false;
        loop {
            seg = segment_block[segment_index];

            should_break = optimizedSegmentTileX(seg) != tile.x;

            if (should_break || segment_index == BLOCK_LEN) { break; }

            segment_index = segment_index + 1u;

            let current_layer_id = optimizedSegmentLayerId(seg);

            if (current_layer_id != layer_id) {
                if (layer_id != LAYER_ID_NONE) {
                    let style = styles.data[layer_id];
                    painterPushCover(painter, layer_id, style.fill_rule, local_id);
                    (*painter).cover = 0;
                }

                layer_id = current_layer_id;
            }

            let cover = select(
                0,
                optimizedSegmentCover(seg),
                optimizedSegmentLocalY(seg) == local_id.y,
            );

            (*painter).cover = (*painter).cover + cover;
        }

        if (segment_index == BLOCK_LEN) {
            should_break = !loadSegments(tile.y, segments_len, local_index);
            segment_index = 0u;
        }

        if (should_break) {
            if (layer_id != LAYER_ID_NONE) {
                let style = styles.data[layer_id];
                painterPushCover(painter, layer_id, style.fill_rule, local_id);
                (*painter).cover = 0;
            }

            break;
        }
    }
}

fn painterPaintTile(
    painter: ptr<function, Painter>,
    tile: vec2<i32>,
    segments_len: u32,
    local_index: u32,
    local_id: vec2<u32>,
) {
    var seg: OptimizedSegment;
    var layer_id = LAYER_ID_NONE;
    loop {
        var should_break = false;
        loop {
            seg = segment_block[segment_index];

            should_break = optimizedSegmentTileX(seg) != tile.x;

            if (should_break || segment_index == BLOCK_LEN) { break; }

            segment_index = segment_index + 1u;

            let current_layer_id = optimizedSegmentLayerId(seg);

            if (current_layer_id != layer_id) {
                if (layer_id != LAYER_ID_NONE) {
                    painterBlendLayer(painter, layer_id, local_id);
                }

                painterPopQueueUntil(painter, current_layer_id, local_id);

                layer_id = current_layer_id;
            }

            let local_x = optimizedSegmentLocalX(seg);
            let local_y = optimizedSegmentLocalY(seg);

            (*painter).area = (*painter).area + select(
                0,
                optimizedSegmentArea(seg),
                local_id.x == local_x && local_id.y == local_y,
            );

            let cover = optimizedSegmentCover(seg);

            (*painter).area = (*painter).area + PIXEL_WIDTH * select(
                0,
                cover,
                local_id.x > local_x && local_id.y == local_y,
            );
            (*painter).cover = (*painter).cover + select(
                0,
                cover,
                local_id.y == local_y,
            );
        }

        if (segment_index == BLOCK_LEN) {
            should_break = !loadSegments(tile.y, segments_len, local_index);
            segment_index = 0u;
        }

        if (should_break) {
            if (layer_id != LAYER_ID_NONE) {
                painterBlendLayer(painter, layer_id, local_id);
            }

            painterPopQueueUntil(painter, LAYER_ID_NONE, local_id);

            break;
        }
    }
}

fn findStartOfTileRow(tile_y: i32, segments_len: u32) -> u32 {
    if (segments_len == 0u) {
        return 0u;
    }

    var end = segments_len - 1u;

    var start = 0u;
    loop {
        let mid = (start + end) >> 1u;

        if (pixelSegmentTileY(segments.data[mid]) < tile_y) {
            start = mid + 1u;
        } else {
            end = mid;
        }

        if (start == end) { break; }
    }

    return start;
}

[[stage(compute), workgroup_size(16, 4)]]
fn paint(
    [[builtin(local_invocation_id)]] local_id_vec: vec3<u32>,
    [[builtin(local_invocation_index)]] local_index: u32,
    [[builtin(workgroup_id)]] workgroup_id_vec: vec3<u32>,
) {
    let local_id = local_id_vec.xy;
    var tile = vec2<i32>(-1, i32(workgroup_id_vec.x));
    let tile_row_len = (config.width + TILE_WIDTH - 1u) / TILE_WIDTH;
    let segments_len = arrayLength(&segments.data);

    let start_index = findStartOfTileRow(tile.y, segments_len);
    block_index = start_index / BLOCK_LEN;

    let tmp = loadSegments(tile.y, segments_len, local_index);
    segment_index = start_index & BLOCK_MASK;

    var painter: Painter;
    painter.queues = Queues(0u, 0u, 0u);
    painter.area = 0;
    painter.cover = 0;

    painterNegativeCovers(&painter, tile, segments_len, local_index, local_id);
    
    painter.cover = 0;
    painter.queues.end0 = painter.queues.start1;
    tile.x = tile.x + 1;

    loop {
        painter.color = clearColor();
        painterPaintTile(&painter, tile, segments_len, local_index, local_id);
        
        textureStore(image, vec2<i32>(local_id) + tile * vec2<i32>(
            i32(TILE_WIDTH),
            i32(TILE_HEIGHT),
        ), painter.color);

        painter.queues.end0 = painter.queues.start1;

        tile.x = tile.x + 1;
        if (u32(tile.x) > tile_row_len) { break; }
    }
}
