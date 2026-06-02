// Ported from gpuweb/cts src/webgpu/util/texture/texel_view.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cts/webgpu.h"
#include "webgpu/util/texel_data.h"

namespace cts {

struct TexelViewConfig {
    uint32_t bytesPerRow = 0;
    uint32_t rowsPerImage = 0;
    WGPUOrigin3D subrectOrigin = WGPUOrigin3D{0, 0, 0};
    WGPUExtent3D subrectSize = WGPUExtent3D{1, 1, 1};
};

class TexelView {
  public:
    static TexelView fromTextureDataByReference(
        WGPUTextureFormat format,
        const uint8_t* data,
        size_t len,
        TexelViewConfig config);

    std::vector<uint8_t> bytes(uint32_t x, uint32_t y, uint32_t z) const;
    TexelComponents color(uint32_t x, uint32_t y, uint32_t z) const;
    WGPUTextureFormat format() const;

  private:
    WGPUTextureFormat format_ = WGPUTextureFormat_Undefined;
    const uint8_t* data_ = nullptr;
    size_t len_ = 0;
    TexelViewConfig config_;
};

} // namespace cts
