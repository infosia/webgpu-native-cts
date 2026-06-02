// Ported from gpuweb/cts src/webgpu/util/texture/texture_ok.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cts/webgpu.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_layout.h"

namespace cts {

struct BlockRow {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    uint32_t width = 0;
};

using BlockRowCallback = std::function<void(BlockRow)>;

void iterateBlockRows(WGPUExtent3D size, WGPUTextureFormat format, const BlockRowCallback& callback);
uint64_t getTexelOffsetInBytes(
    TexelCopyBufferLayout layout,
    WGPUTextureFormat format,
    WGPUOrigin3D texelBlock,
    WGPUOrigin3D origin);
std::optional<std::string> findFailedPixels(
    WGPUTextureFormat format,
    WGPUOrigin3D origin,
    WGPUExtent3D size,
    const TexelView& actual,
    const TexelView& expected,
    double maxFractionalDiff);

} // namespace cts
