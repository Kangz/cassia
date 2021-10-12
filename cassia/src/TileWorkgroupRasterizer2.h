#ifndef CASSIA_TILEWORKGROUPRASTERIZER2_H
#define CASSIA_TILEWORKGROUPRASTERIZER2_H

#include "Rasterizer.h"

namespace cassia {

    class TileWorkgroupRasterizer2 final : public Rasterizer {
      public:
        TileWorkgroupRasterizer2(wgpu::Device device);
        ~TileWorkgroupRasterizer2() override = default;

        wgpu::Texture Rasterize(EncodingContext* context,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
            const Config& config) override;

      private:
        wgpu::Device mDevice;
        wgpu::ComputePipeline mRasterPipeline;
    };

} // namespace cassia

#endif // CASSIA_TILEWORKGROUPRASTERIZER2_H
