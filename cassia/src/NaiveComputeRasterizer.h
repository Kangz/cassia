#ifndef CASSIA_NAIVECOMPUTERASTERIZER_H
#define CASSIA_NAIVECOMPUTERASTERIZER_H

#include "Rasterizer.h"

namespace cassia {

    class NaiveComputeRasterizer final : public Rasterizer {
      public:
        NaiveComputeRasterizer(wgpu::Device device);
        ~NaiveComputeRasterizer() override = default;

        wgpu::Texture Rasterize(EncodingContext* context,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
            const Config& config) override;

      private:
        wgpu::Device mDevice;
        wgpu::ComputePipeline mPipeline;
    };

} // namespace cassia

#endif // CASSIA_NAIVECOMPUTERASTERIZER_H
