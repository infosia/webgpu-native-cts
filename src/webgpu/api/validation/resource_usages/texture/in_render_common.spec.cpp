// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/resource_usages/texture/in_render_common.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

constexpr uint32_t kTextureSize = 16;
constexpr uint32_t kTextureLevels = 3;
constexpr uint32_t kTextureLayers = 3;

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,resource_usages,texture,in_render_common",
    R"(
Texture Usages Validation Tests in Same or Different Render Pass Encoders.
)");

std::vector<Value> textureBindingTypes() {
    return {
        std::string("sampled-texture"),
        std::string("writeonly-storage-texture"),
        std::string("readonly-storage-texture"),
        std::string("readwrite-storage-texture"),
    };
}

std::vector<Value> ranges() {
    return {std::string("base=0,count=1"), std::string("base=1,count=1"), std::string("base=1,count=2")};
}

std::vector<Value> layerRanges() {
    return {
        std::string("base=0,count=1"),
        std::string("base=1,count=1"),
        std::string("base=1,count=2"),
        std::string("base=0,count=3"),
    };
}

struct Range {
    uint32_t base;
    uint32_t count;
};

Range parseRange(const std::string& value) {
    if (value == "base=0,count=1") return {0, 1};
    if (value == "base=1,count=1") return {1, 1};
    if (value == "base=1,count=2") return {1, 2};
    if (value == "base=0,count=3") return {0, 3};
    std::abort();
}

bool isReadOnlyTextureBindingType(const std::string& type) {
    return type == "sampled-texture" || type == "readonly-storage-texture";
}

bool isRangeNotOverlapped(uint32_t start0, uint32_t end0, uint32_t start1, uint32_t end1) {
    return end0 < start1 || end1 < start0;
}

WGPUTextureAspect aspectFor(const std::string& aspect) {
    if (aspect == "all") return WGPUTextureAspect_All;
    if (aspect == "depth-only") return WGPUTextureAspect_DepthOnly;
    if (aspect == "stencil-only") return WGPUTextureAspect_StencilOnly;
    std::abort();
}

WGPUTextureSampleType sampleTypeFor(const std::string& sampleType) {
    if (sampleType == "unfilterable-float") return WGPUTextureSampleType_UnfilterableFloat;
    if (sampleType == "depth") return WGPUTextureSampleType_Depth;
    if (sampleType == "uint") return WGPUTextureSampleType_Uint;
    std::abort();
}

WGPUStorageTextureAccess storageAccessFor(const std::string& type) {
    if (type == "writeonly-storage-texture") return WGPUStorageTextureAccess_WriteOnly;
    if (type == "readonly-storage-texture") return WGPUStorageTextureAccess_ReadOnly;
    if (type == "readwrite-storage-texture") return WGPUStorageTextureAccess_ReadWrite;
    std::abort();
}

WGPUTexture createTexture(AllFeaturesMaxLimitsGpuTest& t,
                          WGPUTextureFormat format,
                          WGPUTextureUsage usage,
                          uint32_t layers = 1,
                          uint32_t levels = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kTextureSize, kTextureSize, layers};
    desc.mipLevelCount = levels;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t,
                           WGPUTexture texture,
                           WGPUTextureViewDimension dimension,
                           uint32_t baseLayer,
                           uint32_t layerCount,
                           uint32_t baseLevel,
                           uint32_t levelCount,
                           WGPUTextureAspect aspect = WGPUTextureAspect_All) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.dimension = dimension;
    desc.baseArrayLayer = baseLayer;
    desc.arrayLayerCount = layerCount;
    desc.baseMipLevel = baseLevel;
    desc.mipLevelCount = levelCount;
    desc.aspect = aspect;
    return t.createViewTracked(texture, desc);
}

WGPURenderPassColorAttachment colorAttachment(WGPUTextureView view) {
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
    return attachment;
}

WGPUBindGroupLayout createBindGroupLayoutForTest(AllFeaturesMaxLimitsGpuTest& t,
                                                 const std::string& usage,
                                                 const std::string& sampleType) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Fragment;
    if (usage == "sampled-texture") {
        entry.texture.sampleType = sampleTypeFor(sampleType);
        entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;
    } else {
        entry.storageTexture.access = storageAccessFor(usage);
        entry.storageTexture.format = WGPUTextureFormat_R32Float;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroup createBindGroupForTest(AllFeaturesMaxLimitsGpuTest& t,
                                     WGPUTextureView view,
                                     const std::string& usage,
                                     const std::string& sampleType) {
    WGPUBindGroupLayout layout = createBindGroupLayoutForTest(t, usage, sampleType);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = view;
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPURenderPassEncoder beginRenderPass(WGPUCommandEncoder encoder,
                                      const std::vector<WGPURenderPassColorAttachment>& colors,
                                      const WGPURenderPassDepthStencilAttachment* ds = nullptr) {
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = colors.size();
    desc.colorAttachments = colors.empty() ? nullptr : colors.data();
    desc.depthStencilAttachment = ds;
    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}

CTS_TEST(testGroup, "subresources,color_attachments")
    .desc(R"(
  Test that the different subresource of the same texture are allowed to be used as color
  attachments in same / different render pass encoder, while the same subresource is only allowed
  to be used as different color attachments in different render pass encoders.)")
    .params([](ParamsBuilder u) {
        return u.combine("layer0", {0, 1})
            .combine("level0", {0, 1})
            .combine("layer1", {0, 1})
            .combine("level1", {0, 1})
            .combine("inSamePass", {true, false})
            .filter([](const ParamRecord& p) {
                return !(valueAs<bool>(*findParam(p, "inSamePass")) &&
                         valueAs<int>(*findParam(p, "level0")) != valueAs<int>(*findParam(p, "level1")));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t layer0 = static_cast<uint32_t>(t.param<int>("layer0"));
        const uint32_t level0 = static_cast<uint32_t>(t.param<int>("level0"));
        const uint32_t layer1 = static_cast<uint32_t>(t.param<int>("layer1"));
        const uint32_t level1 = static_cast<uint32_t>(t.param<int>("level1"));
        const bool inSamePass = t.param<bool>("inSamePass");

        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment, kTextureLayers, kTextureLevels);
        WGPURenderPassColorAttachment a0 = colorAttachment(createView(t, texture, WGPUTextureViewDimension_2D, layer0, 1, level0, 1));
        WGPURenderPassColorAttachment a1 = colorAttachment(createView(t, texture, WGPUTextureViewDimension_2D, layer1, 1, level1, 1));
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (inSamePass) {
            std::vector<WGPURenderPassColorAttachment> colors = {a0, a1};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderEnd(pass);
        } else {
            std::vector<WGPURenderPassColorAttachment> colors0 = {a0};
            WGPURenderPassEncoder pass0 = beginRenderPass(encoder, colors0);
            wgpuRenderPassEncoderEnd(pass0);
            std::vector<WGPURenderPassColorAttachment> colors1 = {a1};
            WGPURenderPassEncoder pass1 = beginRenderPass(encoder, colors1);
            wgpuRenderPassEncoderEnd(pass1);
        }
        const bool success = inSamePass ? layer0 != layer1 : true;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "subresources,color_attachment_and_bind_group")
    .desc(R"(
  Test that when one subresource of a texture is used as a color attachment, it cannot be used in a
  bind group simultaneously in the same render pass encoder. It is allowed when the bind group is
  used in another render pass encoder instead of the same one.)")
    .params([](ParamsBuilder u) {
        return u.combine("colorAttachmentLevel", {0, 1})
            .combine("colorAttachmentLayer", {0, 1})
            .combineWithParams({
                ParamRecord{{"bgLevel", 0}, {"bgLevelCount", 1}},
                ParamRecord{{"bgLevel", 1}, {"bgLevelCount", 1}},
                ParamRecord{{"bgLevel", 1}, {"bgLevelCount", 2}},
            })
            .combineWithParams({
                ParamRecord{{"bgLayer", 0}, {"bgLayerCount", 1}},
                ParamRecord{{"bgLayer", 1}, {"bgLayerCount", 1}},
                ParamRecord{{"bgLayer", 1}, {"bgLayerCount", 2}},
                ParamRecord{{"bgLayer", 0}, {"bgLayerCount", static_cast<int>(kTextureLayers)}},
            })
            .combine("bgUsage", textureBindingTypes())
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "bgUsage")) != "sampled-texture" &&
                         valueAs<int>(*findParam(p, "bgLevelCount")) > 1);
            })
            .combine("inSamePass", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t caLevel = static_cast<uint32_t>(t.param<int>("colorAttachmentLevel"));
        const uint32_t caLayer = static_cast<uint32_t>(t.param<int>("colorAttachmentLayer"));
        const uint32_t bgLevel = static_cast<uint32_t>(t.param<int>("bgLevel"));
        const uint32_t bgLevelCount = static_cast<uint32_t>(t.param<int>("bgLevelCount"));
        const uint32_t bgLayer = static_cast<uint32_t>(t.param<int>("bgLayer"));
        const uint32_t bgLayerCount = static_cast<uint32_t>(t.param<int>("bgLayerCount"));
        const std::string bgUsage = t.param<std::string>("bgUsage");
        const bool inSamePass = t.param<bool>("inSamePass");

        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
            kTextureLayers, kTextureLevels);
        WGPUTextureView bgView = createView(t, texture, WGPUTextureViewDimension_2DArray, bgLayer, bgLayerCount, bgLevel, bgLevelCount);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, bgView, bgUsage, "unfilterable-float");
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, texture, WGPUTextureViewDimension_2D, caLayer, 1, caLevel, 1));

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        if (inSamePass) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
        } else {
            wgpuRenderPassEncoderEnd(pass);
            WGPUTexture texture2 = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment);
            WGPURenderPassColorAttachment ca2 = colorAttachment(createView(t, texture2, WGPUTextureViewDimension_2D, 0, 1, 0, 1));
            std::vector<WGPURenderPassColorAttachment> colors2 = {ca2};
            WGPURenderPassEncoder pass2 = beginRenderPass(encoder, colors2);
            wgpuRenderPassEncoderSetBindGroup(pass2, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass2);
        }
        const bool mipNoOverlap = isRangeNotOverlapped(caLevel, caLevel, bgLevel, bgLevel + bgLevelCount - 1);
        const bool layerNoOverlap = isRangeNotOverlapped(caLayer, caLayer, bgLayer, bgLayer + bgLayerCount - 1);
        const bool success = inSamePass ? (mipNoOverlap || layerNoOverlap) : true;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "subresources,depth_stencil_attachment_and_bind_group")
    .desc(R"(
  Test that when one subresource of a texture is used as a depth stencil attachment, it cannot be
  used in a bind group simultaneously in the same render pass encoder. It is allowed when the bind
  group is used in another render pass encoder instead of the same one, or the subresource is used
  as a read-only depth stencil attachment.)")
    .params([](ParamsBuilder u) {
        return u.combine("dsLevel", {0, 1})
            .combine("dsLayer", {0, 1})
            .combineWithParams({
                ParamRecord{{"bgLevel", 0}, {"bgLevelCount", 1}},
                ParamRecord{{"bgLevel", 1}, {"bgLevelCount", 1}},
                ParamRecord{{"bgLevel", 1}, {"bgLevelCount", 2}},
            })
            .combineWithParams({
                ParamRecord{{"bgLayer", 0}, {"bgLayerCount", 1}},
                ParamRecord{{"bgLayer", 1}, {"bgLayerCount", 1}},
                ParamRecord{{"bgLayer", 1}, {"bgLayerCount", 2}},
                ParamRecord{{"bgLayer", 0}, {"bgLayerCount", static_cast<int>(kTextureLayers)}},
            })
            .beginSubcases()
            .combine("depthReadOnly", {true, false})
            .combine("stencilReadOnly", {true, false})
            .combine("bgAspect", {std::string("depth-only"), std::string("stencil-only")})
            .combine("inSamePass", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t dsLevel = static_cast<uint32_t>(t.param<int>("dsLevel"));
        const uint32_t dsLayer = static_cast<uint32_t>(t.param<int>("dsLayer"));
        const uint32_t bgLevel = static_cast<uint32_t>(t.param<int>("bgLevel"));
        const uint32_t bgLevelCount = static_cast<uint32_t>(t.param<int>("bgLevelCount"));
        const uint32_t bgLayer = static_cast<uint32_t>(t.param<int>("bgLayer"));
        const uint32_t bgLayerCount = static_cast<uint32_t>(t.param<int>("bgLayerCount"));
        const bool depthReadOnly = t.param<bool>("depthReadOnly");
        const bool stencilReadOnly = t.param<bool>("stencilReadOnly");
        const std::string bgAspect = t.param<std::string>("bgAspect");
        const bool inSamePass = t.param<bool>("inSamePass");

        WGPUTexture texture = createTexture(t, WGPUTextureFormat_Depth24PlusStencil8,
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding, kTextureLayers, kTextureLevels);
        WGPUTextureView bgView = createView(t, texture, WGPUTextureViewDimension_2DArray, bgLayer, bgLayerCount, bgLevel, bgLevelCount, aspectFor(bgAspect));
        WGPUBindGroup bindGroup = createBindGroupForTest(t, bgView, "sampled-texture", bgAspect == "depth-only" ? "depth" : "uint");
        WGPUTextureView attachmentView = createView(t, texture, WGPUTextureViewDimension_2D, dsLayer, 1, dsLevel, 1);
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = attachmentView;
        ds.depthReadOnly = depthReadOnly;
        ds.stencilReadOnly = stencilReadOnly;
        if (!depthReadOnly) {
            ds.depthLoadOp = WGPULoadOp_Load;
            ds.depthStoreOp = WGPUStoreOp_Store;
        }
        if (!stencilReadOnly) {
            ds.stencilLoadOp = WGPULoadOp_Load;
            ds.stencilStoreOp = WGPUStoreOp_Store;
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassEncoder pass = beginRenderPass(encoder, empty, &ds);
        if (inSamePass) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
        } else {
            wgpuRenderPassEncoderEnd(pass);
            WGPUTexture texture2 = createTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_RenderAttachment);
            WGPURenderPassColorAttachment ca2 = colorAttachment(createView(t, texture2, WGPUTextureViewDimension_2D, 0, 1, 0, 1));
            std::vector<WGPURenderPassColorAttachment> colors2 = {ca2};
            WGPURenderPassEncoder pass2 = beginRenderPass(encoder, colors2);
            wgpuRenderPassEncoderSetBindGroup(pass2, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass2);
        }
        const bool noOverlap = isRangeNotOverlapped(dsLevel, dsLevel, bgLevel, bgLevel + bgLevelCount - 1) ||
                               isRangeNotOverlapped(dsLayer, dsLayer, bgLayer, bgLayer + bgLayerCount - 1);
        const bool readonly = (bgAspect == "stencil-only" && stencilReadOnly) || (bgAspect == "depth-only" && depthReadOnly);
        const bool success = !inSamePass || noOverlap || readonly;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "subresources,multiple_bind_groups")
    .desc(R"(
  Test that when one color texture subresource is bound to different bind groups, its list of
  internal usages within one usage scope can only be a compatible usage list.)")
    .params([](ParamsBuilder u) {
        return u.combine("bg0Levels", ranges())
            .combine("bg0Layers", layerRanges())
            .combine("bg1Levels", ranges())
            .combine("bg1Layers", layerRanges())
            .combine("bgUsage0", textureBindingTypes())
            .combine("bgUsage1", textureBindingTypes())
            .filter([](const ParamRecord& p) {
                const Range levels0 = parseRange(valueAs<std::string>(*findParam(p, "bg0Levels")));
                const Range levels1 = parseRange(valueAs<std::string>(*findParam(p, "bg1Levels")));
                const std::string usage0 = valueAs<std::string>(*findParam(p, "bgUsage0"));
                const std::string usage1 = valueAs<std::string>(*findParam(p, "bgUsage1"));
                return !((usage0 != "sampled-texture" && levels0.count > 1) ||
                         (usage1 != "sampled-texture" && levels1.count > 1));
            })
            .beginSubcases()
            .combine("inSamePass", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const Range l0 = parseRange(t.param<std::string>("bg0Levels"));
        const Range a0 = parseRange(t.param<std::string>("bg0Layers"));
        const Range l1 = parseRange(t.param<std::string>("bg1Levels"));
        const Range a1 = parseRange(t.param<std::string>("bg1Layers"));
        const std::string usage0 = t.param<std::string>("bgUsage0");
        const std::string usage1 = t.param<std::string>("bgUsage1");
        const bool inSamePass = t.param<bool>("inSamePass");

        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding, kTextureLayers, kTextureLevels);
        WGPUBindGroup bg0 = createBindGroupForTest(t, createView(t, texture, WGPUTextureViewDimension_2DArray, a0.base, a0.count, l0.base, l0.count), usage0, "unfilterable-float");
        WGPUBindGroup bg1 = createBindGroupForTest(t, createView(t, texture, WGPUTextureViewDimension_2DArray, a1.base, a1.count, l1.base, l1.count), usage1, "unfilterable-float");
        WGPUTexture colorTexture = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment);
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, colorTexture, WGPUTextureViewDimension_2D, 0, 1, 0, 1));
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        if (inSamePass) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
        } else {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
            WGPURenderPassEncoder pass2 = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderSetBindGroup(pass2, 1, bg1, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass2);
        }
        const bool bothReadOnly = isReadOnlyTextureBindingType(usage0) && isReadOnlyTextureBindingType(usage1);
        const bool noOverlap = isRangeNotOverlapped(l0.base, l0.base + l0.count - 1, l1.base, l1.base + l1.count - 1) ||
                               isRangeNotOverlapped(a0.base, a0.base + a0.count - 1, a1.base, a1.base + a1.count - 1);
        const bool success = !inSamePass || bothReadOnly || noOverlap || usage0 == usage1;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "subresources,depth_stencil_texture_in_bind_groups")
    .desc(R"(
  Test that when one depth stencil texture subresource is bound to different bind groups, we can
  always bind these two bind groups in either the same or different render pass encoder as the depth
  stencil texture can only be bound as TEXTURE_BINDING in the bind group.)")
    .params([](ParamsBuilder u) {
        return u.combine("view0Levels", ranges())
            .combine("view0Layers", layerRanges())
            .combine("view1Levels", ranges())
            .combine("view1Layers", layerRanges())
            .combine("aspect0", {std::string("depth-only"), std::string("stencil-only")})
            .combine("aspect1", {std::string("depth-only"), std::string("stencil-only")})
            .combine("inSamePass", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const Range l0 = parseRange(t.param<std::string>("view0Levels"));
        const Range a0 = parseRange(t.param<std::string>("view0Layers"));
        const Range l1 = parseRange(t.param<std::string>("view1Levels"));
        const Range a1 = parseRange(t.param<std::string>("view1Layers"));
        const std::string aspect0 = t.param<std::string>("aspect0");
        const std::string aspect1 = t.param<std::string>("aspect1");
        const bool inSamePass = t.param<bool>("inSamePass");

        WGPUTexture texture = createTexture(t, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureUsage_TextureBinding, kTextureLayers, kTextureLevels);
        WGPUBindGroup bg0 = createBindGroupForTest(t, createView(t, texture, WGPUTextureViewDimension_2DArray, a0.base, a0.count, l0.base, l0.count, aspectFor(aspect0)), "sampled-texture", aspect0 == "depth-only" ? "depth" : "uint");
        WGPUBindGroup bg1 = createBindGroupForTest(t, createView(t, texture, WGPUTextureViewDimension_2DArray, a1.base, a1.count, l1.base, l1.count, aspectFor(aspect1)), "sampled-texture", aspect1 == "depth-only" ? "depth" : "uint");
        WGPUTexture colorTexture = createTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_RenderAttachment);
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, colorTexture, WGPUTextureViewDimension_2D, 0, 1, 0, 1));
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        if (inSamePass) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
        } else {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
            WGPURenderPassEncoder pass2 = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderSetBindGroup(pass2, 1, bg1, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass2);
        }
        t.expectValidationError([&] { t.finishTracked(encoder); }, false);
    });

}  // namespace
