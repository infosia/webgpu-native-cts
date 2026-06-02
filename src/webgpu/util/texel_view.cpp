// Ported from gpuweb/cts src/webgpu/util/texture/texel_view.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "webgpu/util/texel_view.h"

#include <stdexcept>

#include "webgpu/texture_format.h"

namespace cts {

TexelView TexelView::fromTextureDataByReference(
    WGPUTextureFormat format,
    const uint8_t* data,
    size_t len,
    TexelViewConfig config) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    if (block.blockWidth != 1 || block.blockHeight != 1) {
        throw std::runtime_error("TexelView only supports uncompressed color formats");
    }
    TexelView view;
    view.format_ = format;
    view.data_ = data;
    view.len_ = len;
    view.config_ = config;
    return view;
}

std::vector<uint8_t> TexelView::bytes(uint32_t x, uint32_t y, uint32_t z) const {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format_);
    if (x < config_.subrectOrigin.x || y < config_.subrectOrigin.y || z < config_.subrectOrigin.z
        || x >= config_.subrectOrigin.x + config_.subrectSize.width
        || y >= config_.subrectOrigin.y + config_.subrectSize.height
        || z >= config_.subrectOrigin.z + config_.subrectSize.depthOrArrayLayers) {
        throw std::runtime_error("texel coordinate out of bounds");
    }

    const uint64_t localX = x - config_.subrectOrigin.x;
    const uint64_t localY = y - config_.subrectOrigin.y;
    const uint64_t localZ = z - config_.subrectOrigin.z;
    const uint64_t offset = (localZ * config_.rowsPerImage + localY) * config_.bytesPerRow
        + localX * block.bytesPerBlock;
    if (offset + block.bytesPerBlock > len_) {
        throw std::runtime_error("texel data offset out of bounds");
    }
    return std::vector<uint8_t>(data_ + offset, data_ + offset + block.bytesPerBlock);
}

TexelComponents TexelView::color(uint32_t x, uint32_t y, uint32_t z) const {
    const std::vector<uint8_t> texelBytes = bytes(x, y, z);
    const TexelRepresentation& repr = texelRepresentation(format_);
    return repr.bitsToNumber(repr.unpackBits(texelBytes.data(), texelBytes.size()));
}

WGPUTextureFormat TexelView::format() const {
    return format_;
}

} // namespace cts
