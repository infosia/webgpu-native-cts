// Ported from gpuweb/cts src/webgpu/util/texture/texture_ok.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "webgpu/util/texture_ok.h"

#include <sstream>
#include <stdexcept>

#include "webgpu/texture_format.h"

namespace cts {

void iterateBlockRows(WGPUExtent3D size, WGPUTextureFormat format, const BlockRowCallback& callback) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    if (info.blockWidth != 1 || info.blockHeight != 1) {
        throw std::runtime_error("iterateBlockRows only supports uncompressed color formats");
    }
    for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < size.height; ++y) {
            callback(BlockRow{0, y, z, size.width});
        }
    }
}

uint64_t getTexelOffsetInBytes(
    TexelCopyBufferLayout layout,
    WGPUTextureFormat format,
    WGPUOrigin3D texelBlock,
    WGPUOrigin3D origin) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    const uint64_t x = texelBlock.x - origin.x;
    const uint64_t y = texelBlock.y - origin.y;
    const uint64_t z = texelBlock.z - origin.z;
    return layout.offset
        + z * layout.rowsPerImage * layout.bytesPerRow
        + y * layout.bytesPerRow
        + x * info.bytesPerBlock;
}

std::optional<std::string> findFailedPixels(
    WGPUTextureFormat format,
    WGPUOrigin3D origin,
    WGPUExtent3D size,
    const TexelView& actual,
    const TexelView& expected,
    double maxFractionalDiff) {
    const TexelRepresentation& repr = texelRepresentation(format);
    for (uint32_t z = origin.z; z < origin.z + size.depthOrArrayLayers; ++z) {
        for (uint32_t y = origin.y; y < origin.y + size.height; ++y) {
            for (uint32_t x = origin.x; x < origin.x + size.width; ++x) {
                const TexelComponents actualColor = actual.color(x, y, z);
                const TexelComponents expectedColor = expected.color(x, y, z);
                for (TexelComponent component : repr.componentOrder) {
                    const uint32_t index = static_cast<uint32_t>(component);
                    if (!texelComponentEqual(actualColor.values[index], expectedColor.values[index], maxFractionalDiff)) {
                        std::ostringstream message;
                        message << "pixel mismatch at " << x << "," << y << "," << z
                                << " component " << index
                                << ": expected " << expectedColor.values[index]
                                << ", got " << actualColor.values[index];
                        return message.str();
                    }
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace cts
