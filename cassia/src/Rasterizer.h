#ifndef CASSIA_RASTERIZER_H
#define CASSIA_RASTERIZER_H

#include "webgpu/webgpu_cpp.h"

namespace cassia {

    class EncodingContext;

    class Rasterizer {
      public:
        struct Config {
            uint32_t width;
            uint32_t height;
            uint32_t segmentCount;
            uint32_t stylingCount;
        };

        virtual ~Rasterizer() = default;

        virtual wgpu::Texture Rasterize(EncodingContext* context,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
            const Config& config) = 0;
    };

} // namespace cassia

#endif // CASSIA_RASTERIZER_H
