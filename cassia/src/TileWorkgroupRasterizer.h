#ifndef CASSIA_TILEWORKGROUPRASTERIZER_H
#define CASSIA_TILEWORKGROUPRASTERIZER_H

#include "Rasterizer.h"

namespace cassia {

    class TileWorkgroupRasterizer final : public Rasterizer {
      public:
        TileWorkgroupRasterizer(wgpu::Device device);
        ~TileWorkgroupRasterizer() override = default;

        wgpu::Texture Rasterize(EncodingContext* context,
            wgpu::Buffer sortedPsegments, wgpu::Buffer stylingsBuffer,
            const Config& config) override;

      private:
        wgpu::Device mDevice;
        wgpu::ComputePipeline mTileRangePipeline;
        wgpu::ComputePipeline mRasterPipeline;
    };

} // namespace cassia

#endif // CASSIA_TILEWORKGROUPRASTERIZER_H
