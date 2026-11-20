// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/storeOp.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2020 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texture_test_utils.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pass,storeOp",
    R"(API Operation Tests for RenderPass StoreOp.

  Test Coverage:

  - Tests that color and depth-stencil store operations {'discard', 'store'} work correctly for a
    render pass with both a color attachment and depth-stencil attachment.
      TODO: use depth24plus-stencil8

  - Tests that store operations {'discard', 'store'} work correctly for a render pass with multiple
    color attachments.
      TODO: test with more interesting loadOp values

  - Tests that store operations {'discard', 'store'} work correctly for a render pass with a color
    attachment for:
      - All renderable color formats
      - mip level set to {'0', mip > '0'}
      - array layer set to {'0', layer > '1'} for 2D textures
      TODO: depth slice set to {'0', slice > '0'} for 3D textures

  - Tests that store operations {'discard', 'store'} work correctly for a render pass with a
    depth-stencil attachment for:
      - All renderable depth-stencil formats
      - mip level set to {'0', mip > '0'}
      - array layer set to {'0', layer > '1'} for 2D textures
      TODO: test depth24plus and depth24plus-stencil8 formats
      TODO: test that depth and stencil aspects are set separately
      TODO: depth slice set to {'0', slice > '0'} for 3D textures
      TODO: test with more interesting loadOp values)");

// Test with a zero and non-zero mip.
constexpr uint32_t kMipLevelCount = 2;

constexpr uint32_t kHeight = 2;
constexpr uint32_t kWidth = 2;

std::vector<Value> storeOpValues() {
    return {Value("discard"), Value("store")};
}

WGPUStoreOp parseStoreOp(std::string_view value) {
    if (value == "store") {
        return WGPUStoreOp_Store;
    }
    if (value == "discard") {
        return WGPUStoreOp_Discard;
    }
    std::abort();
}

bool isSintOrUintFormat(WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    return id.find("sint") != std::string_view::npos || id.find("uint") != std::string_view::npos;
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUTexture createAttachmentTexture(AllFeaturesMaxLimitsGpuTest& t,
                                    WGPUTextureFormat format,
                                    uint32_t depthOrArrayLayers = 1,
                                    uint32_t mipLevelCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.size = WGPUExtent3D{kWidth, kHeight, depthOrArrayLayers};
    desc.mipLevelCount = mipLevelCount;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPUTextureView createDefaultView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, desc);
}

WGPUTextureView createSingleSubresourceView(AllFeaturesMaxLimitsGpuTest& t,
                                            WGPUTexture texture,
                                            uint32_t mipLevel,
                                            uint32_t arrayLayer) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.baseArrayLayer = arrayLayer;
    desc.baseMipLevel = mipLevel;
    desc.mipLevelCount = 1;
    desc.arrayLayerCount = 1;
    return t.createViewTracked(texture, desc);
}

void encodeEmptyPass(AllFeaturesMaxLimitsGpuTest& t,
                     const std::vector<WGPURenderPassColorAttachment>& colorAttachments,
                     const WGPURenderPassDepthStencilAttachment* depthStencilAttachment) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colorAttachments.size();
    passDesc.colorAttachments = colorAttachments.empty() ? nullptr : colorAttachments.data();
    passDesc.depthStencilAttachment = depthStencilAttachment;
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);
}

TexelValue depthValue(double depth) {
    TexelValue value;
    value.Depth = depth;
    return value;
}

TexelValue stencilValue(double stencil) {
    TexelValue value;
    value.Stencil = stencil;
    return value;
}

// Tests a render pass with both a color and depth stencil attachment to ensure store operations are
// set independently.
CTS_TEST(g, "render_pass_store_op,color_attachment_with_depth_stencil_attachment")
    .params([](ParamsBuilder u) {
        return u.combine("colorStoreOperation", storeOpValues())
            .combine("depthStencilStoreOperation", storeOpValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string colorStoreOperation = t.param<std::string>("colorStoreOperation");
        const std::string depthStencilStoreOperation = t.param<std::string>("depthStencilStoreOperation");

        // Create a basic color attachment.
        constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;
        WGPUTexture colorAttachment = createAttachmentTexture(t, kColorFormat);

        // Create a basic depth/stencil attachment.
        constexpr WGPUTextureFormat kDepthStencilFormat = WGPUTextureFormat_Depth32Float;
        WGPUTexture depthStencilAttachment = createAttachmentTexture(t, kDepthStencilFormat);

        // Color load operation will clear to {1.0, 1.0, 1.0, 1.0}.
        // Depth operation will clear to 1.0.
        // Store operations are determined by test the params.
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = createDefaultView(t, colorAttachment);
        color.clearValue = WGPUColor{1.0, 1.0, 1.0, 1.0};
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = parseStoreOp(colorStoreOperation);

        WGPURenderPassDepthStencilAttachment depthStencil = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthStencil.view = createDefaultView(t, depthStencilAttachment);
        depthStencil.depthClearValue = 1.0f;
        depthStencil.depthLoadOp = WGPULoadOp_Clear;
        depthStencil.depthStoreOp = parseStoreOp(depthStencilStoreOperation);

        encodeEmptyPass(t, {color}, &depthStencil);

        // Check that the correct store operation occurred.
        const double expectedColor = colorStoreOperation == "discard" ? 0.0 : 1.0;
        expectSingleColor(t, colorAttachment, kColorFormat, {kHeight, kWidth, 1},
                          colorTexelValue({expectedColor, expectedColor, expectedColor, expectedColor}));

        // Check that the correct store operation occurred.
        const double expectedDepth = depthStencilStoreOperation == "discard" ? 0.0 : 1.0;
        expectSingleColor(t, depthStencilAttachment, kDepthStencilFormat, {kHeight, kWidth, 1},
                          depthValue(expectedDepth), WGPUTextureDimension_2D, 0, 0, WGPUTextureAspect_DepthOnly);
    });

std::vector<Value> colorRenderableFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kColorRenderableTextureFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

// Tests that render pass color attachment store operations work correctly for all renderable color
// formats, mip levels and array layers.
CTS_TEST(g, "render_pass_store_op,color_attachment_only")
    .params([](ParamsBuilder u) {
        return u.combine("colorFormat", colorRenderableFormatValues())
            .combine("storeOperation", storeOpValues())
            .beginSubcases()
            .combine("mipLevel", {0, 1})
            .combine("arrayLayer", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat colorFormat = parseTextureFormat(t.param<std::string>("colorFormat"));
        const std::string storeOperation = t.param<std::string>("storeOperation");
        const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        const uint32_t arrayLayer = static_cast<uint32_t>(t.param<int64_t>("arrayLayer"));
        t.skipIfTextureFormatNotSupported(colorFormat);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(colorFormat);

        WGPUTexture colorAttachment = createAttachmentTexture(t, colorFormat, arrayLayer + 1, kMipLevelCount);
        WGPUTextureView colorAttachmentView = createSingleSubresourceView(t, colorAttachment, mipLevel, arrayLayer);

        const TexelRepresentation& repr = texelRepresentation(colorFormat);
        std::array<bool, 4> hasComponent = {false, false, false, false};
        for (TexelComponent component : repr.componentOrder) {
            hasComponent[static_cast<size_t>(component)] = true;
        }

        // Note: for unorm/float values we specifically want values that will generate failure if
        // srgb remapping is applied so we can't choose 0 or 1 for R, G, or B.
        constexpr std::array<double, 4> kMissingValue = {0, 0, 0, 1};
        const bool isInt = isSintOrUintFormat(colorFormat);
        const std::array<double, 4> baseValue =
            isInt ? std::array<double, 4>{12, 34, 56, 3} : std::array<double, 4>{0.8, 0.75, 0.5, 1.0};
        const double maxDiff = isInt ? 0.0 : 2.0 / 255.0;

        std::array<double, 4> clearValueAsComponents{};
        for (size_t c = 0; c < 4; ++c) {
            clearValueAsComponents[c] = hasComponent[c] ? baseValue[c] : kMissingValue[c];
        }

        // Color store operation is determined by the test params.
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = colorAttachmentView;
        color.clearValue = WGPUColor{clearValueAsComponents[0], clearValueAsComponents[1], clearValueAsComponents[2],
                                     clearValueAsComponents[3]};
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = parseStoreOp(storeOperation);
        encodeEmptyPass(t, {color}, nullptr);

        // Check that the correct store operation occurred.
        const std::array<double, 4> expectedValue =
            storeOperation == "discard" ? std::array<double, 4>{0.0, 0.0, 0.0, 0.0} : clearValueAsComponents;
        expectSingleColorWithTolerance(t, colorAttachment, colorFormat, {kHeight, kWidth, 1}, expectedValue, arrayLayer,
                                       mipLevel, maxDiff);
    });

// Test with multiple color attachments to ensure each attachment's storeOp is set independently.
CTS_TEST(g, "render_pass_store_op,multiple_color_attachments")
    .params([](ParamsBuilder u) {
        return u.combine("storeOperation1", storeOpValues())
            .combine("storeOperation2", storeOpValues())
            .beginSubcases()
            .combine("colorAttachments", {1, 2, 3, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string storeOperation1 = t.param<std::string>("storeOperation1");
        const std::string storeOperation2 = t.param<std::string>("storeOperation2");
        const uint32_t colorAttachmentCount = static_cast<uint32_t>(t.param<int64_t>("colorAttachments"));
        constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;

        std::vector<WGPUTexture> colorAttachments;
        for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
            colorAttachments.push_back(createAttachmentTexture(t, kColorFormat));
        }

        // Color load operation will clear to {1.0, 1.0, 1.0, 1.0}. Color store operation is
        // determined by test params: storeOperation1 for even numbered attachments and
        // storeOperation2 for odd numbered attachments.
        std::vector<WGPURenderPassColorAttachment> renderPassColorAttachments;
        std::vector<std::string> storeOps;
        for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
            const std::string& op = i % 2 == 0 ? storeOperation1 : storeOperation2;
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = createDefaultView(t, colorAttachments[i]);
            color.clearValue = WGPUColor{1.0, 1.0, 1.0, 1.0};
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = parseStoreOp(op);
            renderPassColorAttachments.push_back(color);
            storeOps.push_back(op);
        }
        encodeEmptyPass(t, renderPassColorAttachments, nullptr);

        // Check that the correct store operation occurred.
        for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
            const double expected = storeOps[i] == "discard" ? 0.0 : 1.0;
            expectSingleColor(t, colorAttachments[i], kColorFormat, {kHeight, kWidth, 1},
                              colorTexelValue({expected, expected, expected, expected}));
        }
    });

CTS_TEST(g, "render_pass_store_op,depth_stencil_attachment_only")
    .desc(R"(
Tests that render pass depth stencil store operations work correctly for all renderable color
formats, mip levels and array layers.

- x= all (sized) depth stencil formats, all store ops, multiple mip levels, multiple array layers

TODO: Also test unsized depth/stencil formats [1]
  )")
    .params([](ParamsBuilder u) {
        return u.combine("depthStencilFormat", {"stencil8", "depth16unorm", "depth32float"}) // [1]
            .combine("storeOperation", storeOpValues())
            .beginSubcases()
            .combine("mipLevel", {0, 1})
            .combine("arrayLayer", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("depthStencilFormat"));
        const std::string storeOperation = t.param<std::string>("storeOperation");
        const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        const uint32_t arrayLayer = static_cast<uint32_t>(t.param<int64_t>("arrayLayer"));

        WGPUTexture depthStencilTexture = createAttachmentTexture(t, format, arrayLayer + 1, kMipLevelCount);
        WGPUTextureView view = createSingleSubresourceView(t, depthStencilTexture, mipLevel, arrayLayer);

        // Depth-stencil load operation will clear to depth = 1.0, stencil = 1.0.
        // Depth-stencil store operate is determined by test params.
        WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthStencilAttachment.view = view;
        if (isDepthTextureFormat(format)) {
            depthStencilAttachment.depthClearValue = 1.0f;
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.depthStoreOp = parseStoreOp(storeOperation);
        }
        if (isStencilTextureFormat(format)) {
            depthStencilAttachment.stencilClearValue = 1;
            depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.stencilStoreOp = parseStoreOp(storeOperation);
        }
        encodeEmptyPass(t, {}, &depthStencilAttachment);

        // discard: the depth/stencil components should be 0; store: they should be 1.
        const double expected = storeOperation == "discard" ? 0.0 : 1.0;
        if (isDepthTextureFormat(format)) {
            expectSingleColor(t, depthStencilTexture, format, {kHeight, kWidth, 1}, depthValue(expected),
                              WGPUTextureDimension_2D, arrayLayer, mipLevel, WGPUTextureAspect_DepthOnly);
        }
        if (isStencilTextureFormat(format)) {
            expectSingleColor(t, depthStencilTexture, format, {kHeight, kWidth, 1}, stencilValue(expected),
                              WGPUTextureDimension_2D, arrayLayer, mipLevel, WGPUTextureAspect_StencilOnly);
        }
    });

} // namespace
