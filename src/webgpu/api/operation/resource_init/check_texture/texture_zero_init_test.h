// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/check_texture/texture_zero_init_test.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
//
// Header-only (the build globs *.spec.cpp only); included by texture_zero.spec.cpp and the sibling
// check_texture/*.h checkers.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texture_layout.h"

namespace cts::texture_zero {

inline WGPUStringView tzSv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

enum class UninitializeMethod {
    Creation,      // The texture was just created. It is uninitialized.
    StoreOpClear,  // The texture was rendered to with GPUStoreOp "clear"
};

enum class ReadMethod {
    Sample,         // The texture is sampled from
    CopyToBuffer,   // The texture is copied to a buffer
    CopyToTexture,  // The texture is copied to another texture
    DepthTest,      // The texture is read as a depth buffer
    StencilTest,    // The texture is read as a stencil buffer
    ColorBlending,  // Read the texture by blending as a color attachment
    Storage,        // Read the texture as a storage texture
};

inline UninitializeMethod parseUninitializeMethod(std::string_view s) {
    if (s == "Creation") {
        return UninitializeMethod::Creation;
    }
    if (s == "StoreOpClear") {
        return UninitializeMethod::StoreOpClear;
    }
    std::abort();
}

inline ReadMethod parseReadMethod(std::string_view s) {
    if (s == "Sample") {
        return ReadMethod::Sample;
    }
    if (s == "CopyToBuffer") {
        return ReadMethod::CopyToBuffer;
    }
    if (s == "CopyToTexture") {
        return ReadMethod::CopyToTexture;
    }
    if (s == "DepthTest") {
        return ReadMethod::DepthTest;
    }
    if (s == "StencilTest") {
        return ReadMethod::StencilTest;
    }
    if (s == "ColorBlending") {
        return ReadMethod::ColorBlending;
    }
    if (s == "Storage") {
        return ReadMethod::Storage;
    }
    std::abort();
}

/// Upstream `BeginEndRange`.
struct BeginEndRange {
    uint32_t begin = 0;
    uint32_t end = 0;
};

struct LevelLayer {
    uint32_t level = 0;
    uint32_t layer = 0;
};

/// Upstream `SubresourceRange` (util/texture/subresource.ts).
struct SubresourceRange {
    BeginEndRange mipRange;
    BeginEndRange layerRange;

    /// Iterates the `{ level, layer }` rectangle, level-major.
    std::vector<LevelLayer> each() const {
        std::vector<LevelLayer> out;
        for (uint32_t level = mipRange.begin; level < mipRange.end; ++level) {
            for (uint32_t layer = layerRange.begin; layer < layerRange.end; ++layer) {
                out.push_back(LevelLayer{level, layer});
            }
        }
        return out;
    }
};

// For each mip level count (1 | 5), the mip ranges to leave uninitialized.
inline std::vector<BeginEndRange> uninitializedMipRangesToTest(uint32_t mipLevelCount) {
    if (mipLevelCount == 1) {
        return {BeginEndRange{0, 1}};  // Test the only mip
    }
    return {BeginEndRange{0, 2}, BeginEndRange{3, 4}};  // Test a range and a single mip
}

// For each layer count (1 | 7), the layers to leave uninitialized.
inline std::vector<BeginEndRange> uninitializedLayerRangesToTest(uint32_t layerCount) {
    if (layerCount == 1) {
        return {BeginEndRange{0, 1}};  // Test the only layer
    }
    return {BeginEndRange{2, 4}, BeginEndRange{6, 7}};  // Test a range and a single layer
}

// Enums to abstract over color / depth / stencil values in textures.
enum class InitializedState {
    Canary,  // Set on initialized subresources. It should stay the same. On discarded resources, we should observe zero.
    Zero,    // We check that uninitialized subresources are in this state when read back.
};

inline const char* initializedStateName(InitializedState state) {
    return state == InitializedState::Zero ? "Zero" : "Canary";
}

/// Upstream `PerTexelComponent<number>` for the two states: R, G, B, A, Depth, Stencil.
struct PerTexelComponents {
    double R = 0;
    double G = 0;
    double B = 0;
    double A = 0;
    double Depth = 0;
    double Stencil = 0;
};

inline std::array<double, 4> initializedStateAsColor(InitializedState state, WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    double value = 0;
    if (id.find("uint") != std::string_view::npos) {
        value = state == InitializedState::Zero ? 0 : 1;
    } else if (id.find("sint") != std::string_view::npos) {
        value = state == InitializedState::Zero ? 0 : -1;
    } else {
        value = state == InitializedState::Zero ? 0 : 1;
    }
    return {value, value, value, value};
}

inline double initializedStateAsDepth(InitializedState state) {
    return state == InitializedState::Zero ? 0 : 0.8;
}

inline uint32_t initializedStateAsStencil(InitializedState state) {
    return state == InitializedState::Zero ? 0 : 42;
}

/// Upstream format_info `canCopyToAllAspectsOfTextureFormat` (uncompressed formats): every color
/// format is a copy destination; depth is only for depth16unorm; stencil always is.
inline bool canCopyToAllAspectsOfTextureFormat(WGPUTextureFormat format) {
    if (isDepthTextureFormat(format) && format != WGPUTextureFormat_Depth16Unorm) {
        return false;
    }
    return true;
}

/// Upstream format_info `isTextureFormatPossiblyMultisampled`.
inline bool isTextureFormatPossiblyMultisampled(WGPUTextureFormat format) {
    return textureFormatInfo(format).multisample || isTier1BlendableMultisampleTextureFormat(format);
}

/// Upstream `resolvePerAspectFormat`.
inline WGPUTextureFormat resolvePerAspectFormat(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    if (aspect == WGPUTextureAspect_StencilOnly && isStencilTextureFormat(format)) {
        return WGPUTextureFormat_Stencil8;
    }
    if (aspect == WGPUTextureAspect_DepthOnly) {
        switch (format) {
            case WGPUTextureFormat_Depth24PlusStencil8:
                return WGPUTextureFormat_Depth24Plus;
            case WGPUTextureFormat_Depth32FloatStencil8:
                return WGPUTextureFormat_Depth32Float;
            default:
                break;
        }
    }
    return format;
}

/// Upstream `kTexelRepresentationInfo[format].pack(rep.encode(components))` for the encodable
/// uncompressed formats: color formats use the shared texel representation; the per-aspect
/// depth/stencil formats (depth16unorm, depth32float, stencil8) are encoded here.
inline std::vector<uint8_t> packTexel(WGPUTextureFormat format, const PerTexelComponents& c) {
    switch (format) {
        case WGPUTextureFormat_Depth16Unorm: {
            const uint16_t v = static_cast<uint16_t>(std::llround(c.Depth * 65535.0));
            return {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>(v >> 8)};
        }
        case WGPUTextureFormat_Depth32Float: {
            const float f = static_cast<float>(c.Depth);
            std::vector<uint8_t> out(4);
            std::memcpy(out.data(), &f, 4);
            return out;
        }
        case WGPUTextureFormat_Stencil8:
            return {static_cast<uint8_t>(c.Stencil)};
        default:
            break;
    }
    const TexelRepresentation& rep = texelRepresentation(format);
    TexelComponents numbers;
    numbers.values = {c.R, c.G, c.B, c.A};
    return rep.packBits(rep.numberToBits(numbers));
}

/// Upstream `virtualMipSize`.
inline std::array<uint32_t, 3> virtualMipSize(WGPUTextureDimension dimension,
                                              std::array<uint32_t, 3> size,
                                              uint32_t mipLevel) {
    auto shiftMinOne = [mipLevel](uint32_t n) { return std::max(1u, n >> mipLevel); };
    switch (dimension) {
        case WGPUTextureDimension_1D:
            return {shiftMinOne(size[0]), size[1], size[2]};
        case WGPUTextureDimension_2D:
            return {shiftMinOne(size[0]), shiftMinOne(size[1]), size[2]};
        case WGPUTextureDimension_3D:
            return {shiftMinOne(size[0]), shiftMinOne(size[1]), shiftMinOne(size[2])};
        default:
            std::abort();
    }
}

inline WGPUTextureUsage getRequiredTextureUsage(WGPUTextureFormat format,
                                                uint32_t sampleCount,
                                                UninitializeMethod uninitializeMethod,
                                                ReadMethod readMethod) {
    WGPUTextureUsage usage = WGPUTextureUsage_CopyDst;

    switch (uninitializeMethod) {
        case UninitializeMethod::Creation:
            break;
        case UninitializeMethod::StoreOpClear:
            usage |= WGPUTextureUsage_RenderAttachment;
            break;
    }

    switch (readMethod) {
        case ReadMethod::CopyToBuffer:
        case ReadMethod::CopyToTexture:
            usage |= WGPUTextureUsage_CopySrc;
            break;
        case ReadMethod::Sample:
            usage |= WGPUTextureUsage_TextureBinding;
            break;
        case ReadMethod::Storage:
            usage |= WGPUTextureUsage_StorageBinding;
            break;
        case ReadMethod::DepthTest:
        case ReadMethod::StencilTest:
        case ReadMethod::ColorBlending:
            usage |= WGPUTextureUsage_RenderAttachment;
            break;
    }

    if (sampleCount > 1) {
        // Copies to multisampled textures are not allowed. We need OutputAttachment to initialize
        // canary data in multisampled textures.
        usage |= WGPUTextureUsage_RenderAttachment;
    }

    if (!canCopyToAllAspectsOfTextureFormat(format)) {
        // Copies are not possible. We need OutputAttachment to initialize canary data.
        // (Upstream asserts color formats here are possibly color-renderable; every color format
        // is a copy destination, so the branch is depth/stencil only.)
        usage |= WGPUTextureUsage_RenderAttachment;
    }

    return usage;
}

/// The parsed parameters of one `uninitialized_texture_is_zero` subcase (upstream `TextureZeroParams`).
struct TextureZeroParams {
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    ReadMethod readMethod = ReadMethod::CopyToBuffer;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    uint32_t mipLevelCount = 1;
    uint32_t sampleCount = 1;
    UninitializeMethod uninitializeMethod = UninitializeMethod::Creation;
    uint32_t layerCount = 1;
    bool nonPowerOfTwo = false;
    bool canaryOnCreation = false;
};

class TextureZeroInitTest : public AllFeaturesMaxLimitsGpuTest {
  public:
    /// Parses the case + subcase parameters (upstream does this in the constructor).
    void loadParams() {
        p.dimension = parseTextureDimension(param<std::string>("dimension"));
        p.readMethod = parseReadMethod(param<std::string>("readMethod"));
        p.format = parseTextureFormat(param<std::string>("format"));
        p.aspect = parseTextureAspect(param<std::string>("aspect"));
        p.mipLevelCount = static_cast<uint32_t>(param<int>("mipLevelCount"));
        p.sampleCount = static_cast<uint32_t>(param<int>("sampleCount"));
        p.uninitializeMethod = parseUninitializeMethod(param<std::string>("uninitializeMethod"));
        p.layerCount = static_cast<uint32_t>(param<int>("layerCount"));
        p.nonPowerOfTwo = param<bool>("nonPowerOfTwo");
        p.canaryOnCreation = param<bool>("canaryOnCreation");
    }

    TextureZeroParams p;

    PerTexelComponents stateToTexelComponents(InitializedState state) const {
        const std::array<double, 4> color = initializedStateAsColor(state, p.format);
        PerTexelComponents c;
        c.R = color[0];
        c.G = color[1];
        c.B = color[2];
        c.A = color[3];
        c.Depth = initializedStateAsDepth(state);
        c.Stencil = static_cast<double>(initializedStateAsStencil(state));
        return c;
    }

    uint32_t textureWidth() const {
        uint32_t width = 1u << p.mipLevelCount;
        if (p.nonPowerOfTwo) {
            width = 2 * width - 1;
        }
        return width;
    }

    uint32_t textureHeight() const {
        if (p.dimension == WGPUTextureDimension_1D) {
            return 1;
        }
        uint32_t height = 1u << p.mipLevelCount;
        if (p.nonPowerOfTwo) {
            height = 2 * height - 1;
        }
        return height;
    }

    uint32_t textureDepth() const {
        return p.dimension == WGPUTextureDimension_3D ? 11 : 1;
    }

    uint32_t textureDepthOrArrayLayers() const {
        return p.dimension == WGPUTextureDimension_2D ? p.layerCount : textureDepth();
    }

    std::array<uint32_t, 3> textureSize() const {
        return {textureWidth(), textureHeight(), textureDepth()};
    }

    // Used to iterate subresources and check that their uninitialized contents are zero when accessed.
    std::vector<SubresourceRange> iterateUninitializedSubresources() const {
        std::vector<SubresourceRange> out;
        for (const BeginEndRange& mipRange : uninitializedMipRangesToTest(p.mipLevelCount)) {
            for (const BeginEndRange& layerRange : uninitializedLayerRangesToTest(p.layerCount)) {
                out.push_back(SubresourceRange{mipRange, layerRange});
            }
        }
        return out;
    }

    // Used to iterate and initialize other subresources not checked for zero-initialization.
    // Zero-initialization of uninitialized subresources should not have side effects on already
    // initialized subresources.
    std::vector<SubresourceRange> iterateInitializedSubresources() const {
        std::vector<std::vector<bool>> uninitialized(p.mipLevelCount, std::vector<bool>(p.layerCount, false));
        for (const SubresourceRange& subresources : iterateUninitializedSubresources()) {
            for (const LevelLayer& ll : subresources.each()) {
                uninitialized[ll.level][ll.layer] = true;
            }
        }
        std::vector<SubresourceRange> out;
        for (uint32_t level = 0; level < p.mipLevelCount; ++level) {
            for (uint32_t layer = 0; layer < p.layerCount; ++layer) {
                if (!uninitialized[level][layer]) {
                    out.push_back(SubresourceRange{BeginEndRange{level, level + 1}, BeginEndRange{layer, layer + 1}});
                }
            }
        }
        return out;
    }

    std::vector<WGPUTextureViewDescriptor> generateTextureViewDescriptorsForRendering(
        WGPUTextureAspect aspect,
        const SubresourceRange& subresourceRange) const {
        std::vector<WGPUTextureViewDescriptor> out;
        for (const LevelLayer& ll : subresourceRange.each()) {
            WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            desc.dimension = WGPUTextureViewDimension_2D;
            desc.aspect = aspect;
            desc.baseMipLevel = ll.level;
            desc.mipLevelCount = 1;
            desc.baseArrayLayer = ll.layer;
            desc.arrayLayerCount = 1;
            out.push_back(desc);
        }
        return out;
    }

    void submit(WGPUCommandEncoder encoder) {
        WGPUCommandBuffer commandBuffer = finishTracked(encoder);
        wgpuQueueSubmit(queue(), 1, &commandBuffer);
    }

    void initializeTexture(WGPUTexture texture, InitializedState state, const SubresourceRange& subresourceRange) {
        if (p.sampleCount > 1 || !canCopyToAllAspectsOfTextureFormat(p.format)) {
            // Copies to multisampled textures not yet specified. Use a storeOp for now.
            if (isColorTextureFormat(p.format) && !isTextureFormatColorRenderable(p.format)) {
                fail("not implemented for non-renderable color");
            }
            initializeWithStoreOp(state, texture, subresourceRange);
        } else {
            initializeWithCopy(texture, state, subresourceRange);
        }
    }

    void discardTexture(WGPUTexture texture, const SubresourceRange& subresourceRange) {
        WGPUCommandEncoder encoder = createCommandEncoderTracked();
        wgpuCommandEncoderPushDebugGroup(encoder, tzSv("discardTexture"));

        for (const WGPUTextureViewDescriptor& desc :
             generateTextureViewDescriptorsForRendering(WGPUTextureAspect_All, subresourceRange)) {
            WGPURenderPassDescriptor pass = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            if (isColorTextureFormat(p.format)) {
                color.view = createViewTracked(texture, desc);
                color.loadOp = WGPULoadOp_Load;
                color.storeOp = WGPUStoreOp_Discard;
                pass.colorAttachmentCount = 1;
                pass.colorAttachments = &color;
            } else {
                ds.view = createViewTracked(texture, desc);
                if (isDepthTextureFormat(p.format)) {
                    ds.depthLoadOp = WGPULoadOp_Load;
                    ds.depthStoreOp = WGPUStoreOp_Discard;
                }
                if (isStencilTextureFormat(p.format)) {
                    ds.stencilLoadOp = WGPULoadOp_Load;
                    ds.stencilStoreOp = WGPUStoreOp_Discard;
                }
                pass.depthStencilAttachment = &ds;
            }
            WGPURenderPassEncoder encoderPass = beginRenderPassTracked(encoder, &pass);
            wgpuRenderPassEncoderEnd(encoderPass);
        }

        wgpuCommandEncoderPopDebugGroup(encoder);
        submit(encoder);
    }

    void skipIfTextureFormatNotSupportedForTest() {
        skipIfTextureFormatNotSupported(p.format);
        const WGPUTextureUsage usage =
            getRequiredTextureUsage(p.format, p.sampleCount, p.uninitializeMethod, p.readMethod);
        skipIfTextureFormatDoesNotSupportUsage(usage, p.format);
        if (p.sampleCount > 1 && !isTextureFormatMultisampled(p.format)) {
            skip("texture format '" + std::string(textureFormatIdentifier(p.format))
                 + "' does not support multisampling");
        }
    }

  private:
    void initializeWithStoreOp(InitializedState state, WGPUTexture texture, const SubresourceRange& subresourceRange) {
        WGPUCommandEncoder encoder = createCommandEncoderTracked();
        wgpuCommandEncoderPushDebugGroup(encoder, tzSv("initializeWithStoreOp"));

        for (const WGPUTextureViewDescriptor& desc :
             generateTextureViewDescriptorsForRendering(WGPUTextureAspect_All, subresourceRange)) {
            WGPURenderPassDescriptor pass = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            if (isColorTextureFormat(p.format)) {
                const std::array<double, 4> clear = initializedStateAsColor(state, p.format);
                color.view = createViewTracked(texture, desc);
                color.clearValue = WGPUColor{clear[0], clear[1], clear[2], clear[3]};
                color.loadOp = WGPULoadOp_Clear;
                color.storeOp = WGPUStoreOp_Store;
                pass.colorAttachmentCount = 1;
                pass.colorAttachments = &color;
            } else {
                ds.view = createViewTracked(texture, desc);
                if (isDepthTextureFormat(p.format)) {
                    ds.depthClearValue = static_cast<float>(initializedStateAsDepth(state));
                    ds.depthLoadOp = WGPULoadOp_Clear;
                    ds.depthStoreOp = WGPUStoreOp_Store;
                }
                if (isStencilTextureFormat(p.format)) {
                    ds.stencilClearValue = initializedStateAsStencil(state);
                    ds.stencilLoadOp = WGPULoadOp_Clear;
                    ds.stencilStoreOp = WGPUStoreOp_Store;
                }
                pass.depthStencilAttachment = &ds;
            }
            WGPURenderPassEncoder encoderPass = beginRenderPassTracked(encoder, &pass);
            wgpuRenderPassEncoderEnd(encoderPass);
        }

        wgpuCommandEncoderPopDebugGroup(encoder);
        submit(encoder);
    }

    void initializeWithCopy(WGPUTexture texture, InitializedState state, const SubresourceRange& subresourceRange) {
        const std::vector<LevelLayer> subresources = subresourceRange.each();
        if (subresources.empty()) {
            fail("initializeWithCopy: empty subresource range");
        }

        const std::array<uint32_t, 3> size = textureSize();
        const std::array<uint32_t, 3> largest = virtualMipSize(p.dimension, size, subresources[0].level);

        // Upstream createTextureUploadBuffer: a COPY_SRC buffer holding the solid-color linear data
        // of the largest subresource in the range.
        const std::vector<uint8_t> texelData = packTexel(p.format, stateToTexelComponents(state));
        const TextureCopyLayout layout = getTextureCopyLayout(
            p.format, p.dimension, WGPUExtent3D{largest[0], largest[1], largest[2]}, 0);
        if (texelData.size() != layout.bytesPerBlock) {
            fail("initializeWithCopy: texel size does not match the format's block size");
        }

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = layout.byteLength;
        bufferDesc.usage = WGPUBufferUsage_CopySrc;
        bufferDesc.mappedAtCreation = WGPU_TRUE;
        WGPUBuffer buffer = createBufferTracked(bufferDesc);
        auto* mapping = static_cast<uint8_t*>(wgpuBufferGetMappedRange(buffer, 0, layout.byteLength));
        if (mapping == nullptr) {
            fail("initializeWithCopy: failed to map the upload buffer");
        }
        const std::array<uint32_t, 3> mipSize = virtualMipSize(p.dimension, largest, 0);
        for (uint32_t slice = 0; slice < mipSize[2]; ++slice) {
            for (uint32_t row = 0; row < mipSize[1]; ++row) {
                for (uint32_t col = 0; col < mipSize[0]; ++col) {
                    const uint64_t byteOffset = static_cast<uint64_t>(slice) * layout.rowsPerImage * layout.bytesPerRow
                                                + static_cast<uint64_t>(row) * layout.bytesPerRow
                                                + static_cast<uint64_t>(col) * texelData.size();
                    std::memcpy(mapping + byteOffset, texelData.data(), texelData.size());
                }
            }
        }
        wgpuBufferUnmap(buffer);

        WGPUCommandEncoder encoder = createCommandEncoderTracked();
        for (const LevelLayer& ll : subresources) {
            const std::array<uint32_t, 3> copy = virtualMipSize(p.dimension, size, ll.level);

            WGPUTexelCopyBufferInfo src = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            src.buffer = buffer;
            src.layout.offset = 0;
            src.layout.bytesPerRow = layout.bytesPerRow;
            src.layout.rowsPerImage = layout.rowsPerImage;

            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = texture;
            dst.mipLevel = ll.level;
            dst.origin = WGPUOrigin3D{0, 0, ll.layer};

            const WGPUExtent3D copySize{copy[0], copy[1], copy[2]};
            wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &copySize);
        }
        submit(encoder);
        wgpuBufferDestroy(buffer);
    }
};

/// Upstream `kTestParams`.
inline ParamsBuilder makeTestParams(ParamsBuilder u) {
    auto str = [](const ParamRecord& r, std::string_view key) { return valueAs<std::string>(*findParam(r, key)); };
    auto num = [](const ParamRecord& r, std::string_view key) { return valueAs<int>(*findParam(r, key)); };
    auto fmt = [str](const ParamRecord& r) { return parseTextureFormat(str(r, "format")); };

    return u.combine("dimension", {"1d", "2d", "3d"})
        .combine("readMethod", {"CopyToBuffer", "CopyToTexture", "Sample", "DepthTest", "StencilTest"})
        // [3] compressed formats
        .combine("format", formatIdentifierValues(kUncompressedTextureFormats))
        .filter([str, fmt](const ParamRecord& r) {
            return textureFormatAndDimensionPossiblyCompatible(parseTextureDimension(str(r, "dimension")), fmt(r));
        })
        .beginSubcases()
        .combine("aspect", {"all", "depth-only", "stencil-only"})
        .filter([str, fmt](const ParamRecord& r) {
            const ReadMethod readMethod = parseReadMethod(str(r, "readMethod"));
            const WGPUTextureFormat format = fmt(r);
            const std::string aspect = str(r, "aspect");
            const bool hasColor = isColorTextureFormat(format);
            const bool hasDepth = isDepthTextureFormat(format);
            const bool hasStencil = isStencilTextureFormat(format);
            const bool drop =
                (readMethod == ReadMethod::DepthTest && (!hasDepth || aspect == "stencil-only"))
                || (readMethod == ReadMethod::StencilTest && (!hasStencil || aspect == "depth-only"))
                || (readMethod == ReadMethod::ColorBlending && !hasColor)
                // [1]: Test with depth/stencil sampling
                || (readMethod == ReadMethod::Sample && (hasDepth || hasStencil))
                || (aspect == "depth-only" && !hasDepth)
                || (aspect == "stencil-only" && !hasStencil)
                || (aspect == "all" && hasDepth && hasStencil)
                // Cannot copy from a packed depth format.
                // [2]: Test copying out of the stencil aspect.
                || ((readMethod == ReadMethod::CopyToBuffer || readMethod == ReadMethod::CopyToTexture)
                    && (format == WGPUTextureFormat_Depth24Plus || format == WGPUTextureFormat_Depth24PlusStencil8));
            return !drop;
        })
        .combine("mipLevelCount", {1, 5})
        // 1D texture can only have a single mip level
        .filter([str, num](const ParamRecord& r) {
            return !(str(r, "dimension") == "1d" && num(r, "mipLevelCount") != 1);
        })
        .combine("sampleCount", {1, 4})
        .filter([str, num](const ParamRecord& r) {
            // We can only read from multisampled textures by sampling.
            const ReadMethod readMethod = parseReadMethod(str(r, "readMethod"));
            return !(num(r, "sampleCount") > 1
                     && (readMethod == ReadMethod::CopyToBuffer || readMethod == ReadMethod::CopyToTexture));
        })
        // Multisampled textures may only have one mip
        .filter([num](const ParamRecord& r) { return !(num(r, "sampleCount") > 1 && num(r, "mipLevelCount") > 1); })
        .combine("uninitializeMethod", {"Creation", "StoreOpClear"})
        .filter([str, num, fmt](const ParamRecord& r) {
            const ReadMethod readMethod = parseReadMethod(str(r, "readMethod"));
            const bool drop = str(r, "dimension") != "2d"
                              && (num(r, "sampleCount") > 1 || isDepthOrStencilTextureFormat(fmt(r))
                                  || readMethod == ReadMethod::DepthTest || readMethod == ReadMethod::StencilTest
                                  || readMethod == ReadMethod::ColorBlending
                                  || str(r, "uninitializeMethod") == "StoreOpClear");
            return !drop;
        })
        .expand("layerCount",
                [str](const ParamRecord& r) {
                    if (str(r, "dimension") == "2d") {
                        return std::vector<Value>{1, 7};
                    }
                    return std::vector<Value>{1};
                })
        // Multisampled 3D / 2D array textures not supported.
        .filter([num](const ParamRecord& r) { return !(num(r, "sampleCount") > 1 && num(r, "layerCount") > 1); })
        .filter([str, num, fmt](const ParamRecord& r) {
            const WGPUTextureFormat format = fmt(r);
            const uint32_t sampleCount = static_cast<uint32_t>(num(r, "sampleCount"));
            const WGPUTextureUsage usage = getRequiredTextureUsage(
                format, sampleCount, parseUninitializeMethod(str(r, "uninitializeMethod")),
                parseReadMethod(str(r, "readMethod")));
            const bool drop = ((usage & WGPUTextureUsage_RenderAttachment) != 0
                               && !isTextureFormatPossiblyUsableAsRenderAttachment(format))
                              || ((usage & WGPUTextureUsage_StorageBinding) != 0
                                  && !isTextureFormatPossiblyStorageReadable(format))
                              || (sampleCount > 1 && !isTextureFormatPossiblyMultisampled(format));
            return !drop;
        })
        .combine("nonPowerOfTwo", {false, true})
        .combine("canaryOnCreation", {false, true});
}

/// Upstream `CheckContents`.
using CheckContents = void (*)(TextureZeroInitTest& t,
                               WGPUTexture texture,
                               InitializedState state,
                               const SubresourceRange& subresourceRange);

}  // namespace cts::texture_zero
