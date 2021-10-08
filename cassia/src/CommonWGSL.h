#ifndef CASSIA_COMMONWGSL_H_
#define CASSIA_COMMONWGSL_H_

#include <cstdint>

namespace cassia {

    // Also keep the constants in kPSegmentWGSL in sync.
    constexpr uint32_t TILE_WIDTH_SHIFT = 3u;
    constexpr uint32_t TILE_HEIGHT_SHIFT = 3u;

    extern const char kPSegmentWGSL[];
    extern const char kStylingWGSL[];

} // namespace cassia

#endif // CASSIA_COMMONWGSL_H_
