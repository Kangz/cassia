#include "CommonWGSL.h"

namespace cassia {

    const char kPSegmentWGSL[] = R"(
        // This is the definition of a PSegment in mold
        //
        // const TILE_WIDTH: usize = 8;
        // const TILE_WIDTH_SHIFT: usize = 3
        // const TILE_WIDTH_MASK: usize = 7;
        //
        // pub const TILE_HEIGHT: usize = 64;
        // const TILE_HEIGHT_SHIFT: usize = 6;
        // const TILE_HEIGHT_MASK: usize = 63;
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

        let TILE_WIDTH_SHIFT = 3u;
        let TILE_HEIGHT_SHIFT = 6u;
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
            return (i32(s.hi) << (16u - TILE_HEIGHT_SHIFT)) >> (16u + TILE_WIDTH_SHIFT);
        }
        fn psegment_tile_y(s : PSegment) -> i32{
            return (i32(s.hi) << 1u) >> (17u + TILE_HEIGHT_SHIFT);
        }
        fn psegment_layer_id(s : PSegment) -> u32 {
            let mask = (1u << 16u) - 1u;
            return (s.hi << (16u - TILE_WIDTH_SHIFT - TILE_HEIGHT_SHIFT)) &
                mask | (s.lo >> (16u + TILE_WIDTH_SHIFT + TILE_HEIGHT_SHIFT));
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

} // namespace
