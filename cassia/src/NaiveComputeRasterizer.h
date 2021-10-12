#ifndef CASSIA_NAIVECOMPUTERASTERIZER_H
#define CASSIA_NAIVECOMPUTERASTERIZER_H

#include "webgpu/webgpu_cpp.h"

namespace cassia {

    class EncodingContext;

    class NaiveComputeRasterizer {
      public:
        NaiveComputeRasterizer(wgpu::Device device);

        struct Config {
            uint32_t width;
            uint32_t height;
            uint32_t segmentCount;
            uint32_t stylingCount;
        };
        wgpu::Texture Rasterize(EncodingContext* context,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
            const Config& config);

      private:
        wgpu::Device mDevice;
        wgpu::ComputePipeline mPipeline;
    };

} // namespace cassia

#endif // CASSIA_NAIVECOMPUTERASTERIZER_H
