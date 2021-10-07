#ifndef CASSIA_TILEWORKGROUPRASTERIZER_H
#define CASSIA_TILEWORKGROUPRASTERIZER_H

#include "webgpu/webgpu_cpp.h"

namespace cassia {

    class EncodingContext;

    class TileWorkgroupRasterizer {
      public:
        TileWorkgroupRasterizer(wgpu::Device device);

        struct Config {
            uint32_t width;
            uint32_t height;
            uint32_t segmentCount;
        };
        wgpu::Texture Rasterize(EncodingContext* context,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
            const Config& config);

      private:
        wgpu::Device mDevice;
    };

} // namespace cassia

#endif // CASSIA_TILEWORKGROUPRASTERIZER_H
