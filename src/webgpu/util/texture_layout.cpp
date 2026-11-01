// Ported from gpuweb/cts src/webgpu/util/texture/layout.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
#include "webgpu/util/texture_layout.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "webgpu/texture_format.h"

namespace cts {
namespace {

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUExtent3D mipSizeFor(WGPUExtent3D baseSize, WGPUTextureDimension dimension, uint32_t mipLevel) {
    WGPUExtent3D size = baseSize;
    size.width = std::max(1u, baseSize.width >> mipLevel);
    if (dimension != WGPUTextureDimension_1D) {
        size.height = std::max(1u, baseSize.height >> mipLevel);
    }
    if (dimension == WGPUTextureDimension_3D) {
        size.depthOrArrayLayers = std::max(1u, baseSize.depthOrArrayLayers >> mipLevel);
    }
    return size;
}

uint32_t layoutBytesPerRow(TexelCopyBufferLayout layout, WGPUTextureFormat format, WGPUExtent3D size) {
    if (layout.bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED) {
        return layout.bytesPerRow;
    }
    return static_cast<uint32_t>(alignTo(bytesInACompleteRow(size.width, format), kBytesPerRowAlignment));
}

uint32_t layoutRowsPerImage(TexelCopyBufferLayout layout, uint32_t heightBlocks) {
    if (layout.rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED) {
        return layout.rowsPerImage;
    }
    return heightBlocks;
}

} // namespace

uint32_t bytesInACompleteRow(uint32_t width, WGPUTextureFormat format) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    if (width % info.blockWidth != 0) {
        throw std::runtime_error("copy width must be a multiple of block width");
    }
    return info.bytesPerBlock * width / info.blockWidth;
}

WGPUExtent3D physicalMipSize(WGPUExtent3D baseSize, WGPUTextureDimension dimension, uint32_t mipLevel) {
    return mipSizeFor(baseSize, dimension, mipLevel);
}

WGPUExtent3D physicalMipSize(
    WGPUExtent3D baseSize,
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    uint32_t mipLevel) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    WGPUExtent3D size = mipSizeFor(baseSize, dimension, mipLevel);
    size.width = static_cast<uint32_t>(alignTo(size.width, info.blockWidth));
    size.height = static_cast<uint32_t>(alignTo(size.height, info.blockHeight));
    return size;
}

TextureCopyLayout getTextureCopyLayout(
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevel) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    const WGPUExtent3D mipSize = physicalMipSize(baseSize, format, dimension, mipLevel);
    const uint32_t widthBlocks = mipSize.width / info.blockWidth;
    const uint32_t heightBlocks = mipSize.height / info.blockHeight;
    const uint32_t depth = mipSize.depthOrArrayLayers;
    const uint32_t minBytesPerRow = widthBlocks * info.bytesPerBlock;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(minBytesPerRow, kBytesPerRowAlignment));
    const uint32_t rowsPerImage = heightBlocks;
    const uint64_t bytesPerSlice = static_cast<uint64_t>(bytesPerRow) * rowsPerImage;
    const uint64_t sliceSize = static_cast<uint64_t>(bytesPerRow) * (heightBlocks - 1) + minBytesPerRow;
    const uint64_t byteLength = alignTo(bytesPerSlice * (depth - 1) + sliceSize, kBufferCopyAlignment);
    return TextureCopyLayout{info.bytesPerBlock, byteLength, minBytesPerRow, bytesPerRow, rowsPerImage, mipSize};
}

uint64_t dataBytesForCopyOrFail(
    TexelCopyBufferLayout layout,
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    bool requireStrictAlignment) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    if (copySize.width % info.blockWidth != 0 || copySize.height % info.blockHeight != 0) {
        throw std::runtime_error("copy size must be a multiple of block size");
    }
    const uint32_t widthBlocks = copySize.width / info.blockWidth;
    const uint32_t heightBlocks = copySize.height / info.blockHeight;
    const uint32_t depth = copySize.depthOrArrayLayers;
    const uint32_t bytesInLastRow = widthBlocks * info.bytesPerBlock;

    if (requireStrictAlignment && layout.offset % info.bytesPerBlock != 0) {
        throw std::runtime_error("copy offset is not block aligned");
    }

    uint32_t bytesPerRow = layout.bytesPerRow;
    if (bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED) {
        if (heightBlocks > 1 || depth > 1) {
            throw std::runtime_error("bytesPerRow is required");
        }
        bytesPerRow = static_cast<uint32_t>(alignTo(bytesInLastRow, kBytesPerRowAlignment));
    }
    if (bytesPerRow < bytesInLastRow || (requireStrictAlignment && bytesPerRow % kBytesPerRowAlignment != 0)) {
        throw std::runtime_error("invalid bytesPerRow");
    }

    uint32_t rowsPerImage = layout.rowsPerImage;
    if (rowsPerImage == WGPU_COPY_STRIDE_UNDEFINED) {
        if (depth > 1) {
            throw std::runtime_error("rowsPerImage is required");
        }
        rowsPerImage = heightBlocks;
    }
    if (rowsPerImage < heightBlocks) {
        throw std::runtime_error("invalid rowsPerImage");
    }

    uint64_t required = 0;
    if (depth > 1) {
        required += static_cast<uint64_t>(bytesPerRow) * rowsPerImage * (depth - 1);
    }
    if (depth > 0) {
        if (heightBlocks > 1) {
            required += static_cast<uint64_t>(bytesPerRow) * (heightBlocks - 1);
        }
        if (heightBlocks > 0) {
            required += bytesInLastRow;
        }
    }
    return layout.offset + required;
}

void updateLinearTextureDataSubBox(WGPUTextureFormat format, WGPUExtent3D copySize, const LinearTextureSubBox& copy) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    const uint32_t heightBlocks = copySize.height / info.blockHeight;
    const uint32_t srcBytesPerRow = layoutBytesPerRow(copy.srcLayout, format, copySize);
    const uint32_t srcRowsPerImage = layoutRowsPerImage(copy.srcLayout, heightBlocks);
    const uint32_t dstBytesPerRow = layoutBytesPerRow(copy.dstLayout, format, copySize);
    const uint32_t dstRowsPerImage = layoutRowsPerImage(copy.dstLayout, heightBlocks);
    const uint32_t rowBytes = bytesInACompleteRow(copySize.width, format);

    // The origins are in texels, the layouts in block rows.
    const auto rowOffset = [&info](const WGPUOrigin3D& origin,
                                   const TexelCopyBufferLayout& layout,
                                   uint32_t bytesPerRow,
                                   uint32_t rowsPerImage,
                                   uint32_t y,
                                   uint32_t z) {
        return layout.offset + static_cast<uint64_t>(origin.z + z) * rowsPerImage * bytesPerRow
            + static_cast<uint64_t>(origin.y / info.blockHeight + y) * bytesPerRow
            + static_cast<uint64_t>(origin.x / info.blockWidth) * info.bytesPerBlock;
    };

    for (uint32_t z = 0; z < copySize.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < heightBlocks; ++y) {
            const uint64_t srcOffset = rowOffset(copy.srcOrigin, copy.srcLayout, srcBytesPerRow, srcRowsPerImage, y, z);
            const uint64_t dstOffset = rowOffset(copy.dstOrigin, copy.dstLayout, dstBytesPerRow, dstRowsPerImage, y, z);
            if (srcOffset + rowBytes > copy.srcLen || dstOffset + rowBytes > copy.dstLen) {
                throw std::runtime_error("linear texture sub-box copy out of bounds");
            }
            if (rowBytes > 0) {
                std::memcpy(copy.dst + dstOffset, copy.src + srcOffset, rowBytes);
            }
        }
    }
}

} // namespace cts
