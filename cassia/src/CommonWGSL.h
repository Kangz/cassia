#ifndef CASSIA_COMMONWGSL_H_
#define CASSIA_COMMONWGSL_H_

#include <cstdint>

namespace cassia {

    // Also keep the constants in kPSegmentWGSL in sync.
    constexpr uint32_t TILE_WIDTH_SHIFT = 3u;
    constexpr uint32_t TILE_HEIGHT_SHIFT = 3u;
    constexpr uint32_t TILE_X_OFFSET = 256;

    struct PSegment {
        int64_t cover: 6;
        int64_t area: 10;
        uint64_t local_x: TILE_WIDTH_SHIFT;
        uint64_t local_y: TILE_HEIGHT_SHIFT;
        uint64_t layer: 16;
        int64_t tile_x: (16 - TILE_WIDTH_SHIFT);
        int64_t tile_y: (15 - TILE_HEIGHT_SHIFT);
        uint64_t is_none: 1;
    };

    extern const char kPSegmentWGSL[];
    extern const char kStylingWGSL[];

} // namespace cassia

#endif // CASSIA_COMMONWGSL_H_
