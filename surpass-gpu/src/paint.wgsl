let TILE_WIDTH: u32 = 8u;
let TILE_HEIGHT: u32 = 8u;
let TILE_WIDTH_SHIFT = 3u;
let TILE_HEIGHT_SHIFT = 3u;

let PIXEL_WIDTH: i32 = 16;
let PIXEL_AREA: f32 = 256.0;

let LAYER_ID_NONE: u32 = 4294967295u;

struct PixelSegment {
    lo: u32;
    hi: u32;
};

fn pixelSegmentTileX(seg: PixelSegment) -> i32 {
    return i32(seg.hi) << (16u - TILE_HEIGHT_SHIFT) >> (16u + TILE_WIDTH_SHIFT);
}

fn pixelSegmentTileY(seg: PixelSegment) -> i32{
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
    clear_color: u32;
};

[[block]]
struct PixelSegments {
    data: array<PixelSegment>;
};

[[block]]
struct Styles {
    data: array<Color>;
};

[[group(0), binding(0)]]
var<uniform> config: Config;
[[group(0), binding(1)]]
var<storage> segments: PixelSegments;
[[group(0), binding(2)]]
var<storage> styles: Styles;
[[group(0), binding(3)]]
var image: texture_storage_2d<rgba16float, write>;

var<workgroup> segment_block: array<PixelSegment, 64>;
var<private> segment_index: u32;
var<private> block_index: u32;

fn getSeg(i: u32, segments_len: u32, local_index: u32) -> PixelSegment {
    segment_index = i & 63u;

    let new_block_index = i >> 6u;
    if (block_index != new_block_index) {
        block_index = new_block_index;
        segment_block[local_index] = segments.data[(block_index << 6u) + local_index];
    }

    workgroupBarrier();

    return segment_block[segment_index];
}

fn loadSegments(segments_len: u32, local_index: u32) -> bool {
    if ((block_index << 6u) >= segments_len) {
        return false;
    }

    segment_block[local_index] = segments.data[(block_index << 6u) + local_index];
    block_index = block_index + 1u;

    return true;
}

fn nextSeg(
    seg: ptr<function, PixelSegment>,
    tile: vec2<i32>,
    segments_len: u32,
    local_index: u32,
) -> bool {
    if (segment_index == 64u) {
        if (!loadSegments(segments_len, local_index)) {
            return false;
        }

        segment_index = 0u;
    }

    workgroupBarrier();

    let next_seg = segment_block[segment_index];

    let current_tile = vec2<i32>(
        pixelSegmentTileX(next_seg),
        pixelSegmentTileY(next_seg),
    );

    if (!all(current_tile == tile)) {
        return false;
    }

    *seg = next_seg;
    segment_index = segment_index + 1u;

    return true;
}

fn fromArea(area: i32) -> f32 {
    return clamp(abs(f32(area) / PIXEL_AREA), 0.0, 1.0);
}

fn blend(dst: vec4<f32>, src: vec4<f32>) -> vec4<f32> {
    let alpha = src.w;
    let inv_alpha = 1.0 - alpha;

    let color = src.xyz * alpha;

    return fma(dst, vec4<f32>(inv_alpha), vec4<f32>(color, alpha));
}

struct Painter {
    areas: array<i32, 8>;
    covers: array<i32, 9>;
    color: vec4<f32>;
};

fn clear(painter: ptr<function, Painter>, local_id: vec2<u32>) {
    var x = 0u;
    loop {
        if (x != 0u) {
            (*painter).areas[x - 1u] = 0;
        }

        (*painter).covers[x] = 0;

        x = x + 1u;
        if (x > TILE_WIDTH) { break; }
    }
}

fn paint_layer(
    painter: ptr<function, Painter>,
    layer_id: u32,
    local_id: vec2<u32>,
) {
    var x = 0u;
    var area: i32;
    var cover = 0;
    loop {
        if (x != 0u) {
            if (x - 1u == local_id.x) {
                area = (*painter).areas[x - 1u] + cover * PIXEL_WIDTH;
            }
        }

        cover = cover + (*painter).covers[x];

        x = x + 1u;
        if (x > TILE_WIDTH) { break; }
    }

    let fill = styles.data[layer_id];
    let src = vec4<f32>(fill.r, fill.g, fill.b, fill.a * fromArea(area));

    (*painter).color = blend((*painter).color, src);

    clear(painter, local_id);
}

fn paint_tile(
    painter: ptr<function, Painter>,
    tile: vec2<i32>,
    segments_len: u32,
    local_index: u32,
    local_id: vec2<u32>,
) {
    var seg: PixelSegment;
    var layer_id = LAYER_ID_NONE;
    loop {
        if (!nextSeg(&seg, tile, segments_len, local_index)) {
            if (layer_id != LAYER_ID_NONE) {
                paint_layer(painter, layer_id, local_id);
            }

            break;
        }

        let current_layer_id = pixelSegmentLayerId(seg);

        if (current_layer_id != layer_id) {
            if (layer_id != LAYER_ID_NONE) {
                paint_layer(painter, layer_id, local_id);
            }

            layer_id = current_layer_id;
        }

        let local_x = pixelSegmentLocalX(seg);
        let local_y = pixelSegmentLocalY(seg);

        if (local_id.y == local_y) {
            (*painter).areas[local_x] = (*painter).areas[local_x] + pixelSegmentArea(seg);
            (*painter).covers[local_x + 1u] = (*painter).covers[local_x + 1u] + pixelSegmentCover(seg);
        }
    }
}

fn find_start_of_tile_row(tile_y: i32, segments_len: u32, local_index: u32) {
    var len = segments_len;

    if (len == 0u) {
        return;
    }

    var start = 0u;
    loop {
        let half_len = len >> 1u;
        let mid = start + half_len;

        workgroupBarrier();

        if (pixelSegmentTileY(getSeg(mid, segments_len, local_index)) < tile_y) {
            start = mid;
        }

        len = len - half_len;
        if (len <= 1u) { break; }
    }
}

[[stage(compute), workgroup_size(8, 8)]]
fn paint(
    [[builtin(local_invocation_id)]] local_id_vec: vec3<u32>,
    [[builtin(local_invocation_index)]] local_index: u32,
    [[builtin(workgroup_id)]] workgroup_id_vec: vec3<u32>,
) {
    let local_id = local_id_vec.xy;
    var tile = vec2<i32>(0, i32(workgroup_id_vec.x));
    let tile_row_len = (config.width + TILE_WIDTH - 1u) / TILE_WIDTH;
    let segments_len = arrayLength(&segments.data);

    segment_index = 64u;
    block_index = 0u;

    // find_start_of_tile_row(tile.y, segments_len, local_index);

    var painter: Painter;

    loop {
        painter.color = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        paint_tile(&painter, tile, segments_len, local_index, local_id);
        
        textureStore(image, vec2<i32>(local_id) + tile * i32(TILE_WIDTH), painter.color);

        tile.x = tile.x + 1;
        if (u32(tile.x) > tile_row_len) { break; }
    }
}
