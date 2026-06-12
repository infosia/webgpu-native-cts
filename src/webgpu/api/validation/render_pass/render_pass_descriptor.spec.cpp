// Ported from gpuweb/cts src/webgpu/api/validation/render_pass/render_pass_descriptor.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

constexpr uint32_t kDefaultMaxColorAttachments = 8;
constexpr uint32_t kArrayLayerCount = 10;

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pass,render_pass_descriptor",
    "render pass descriptor validation tests.\n\n"
    "TODO(#3363): Make this into a MaxLimitTest and increase kMaxColorAttachments.\n"
    "TODO: review for completeness");

struct ColorRenderInfo {
    WGPUTextureFormat format;
    uint32_t byteCost;
    uint32_t alignment;
};

static constexpr std::array<ColorRenderInfo, 42> kColorRenderInfoTable = {{
    {WGPUTextureFormat_R8Unorm, 1, 1}, {WGPUTextureFormat_R8Uint, 1, 1},
    {WGPUTextureFormat_R8Sint, 1, 1}, {WGPUTextureFormat_RG8Unorm, 2, 1},
    {WGPUTextureFormat_RG8Uint, 2, 1}, {WGPUTextureFormat_RG8Sint, 2, 1},
    {WGPUTextureFormat_RGBA8Unorm, 8, 1}, {WGPUTextureFormat_RGBA8UnormSrgb, 8, 1},
    {WGPUTextureFormat_RGBA8Uint, 4, 1}, {WGPUTextureFormat_RGBA8Sint, 4, 1},
    {WGPUTextureFormat_BGRA8Unorm, 8, 1}, {WGPUTextureFormat_BGRA8UnormSrgb, 8, 1},
    {WGPUTextureFormat_R16Unorm, 2, 2}, {WGPUTextureFormat_R16Snorm, 2, 2},
    {WGPUTextureFormat_R16Uint, 2, 2}, {WGPUTextureFormat_R16Sint, 2, 2},
    {WGPUTextureFormat_R16Float, 2, 2}, {WGPUTextureFormat_RG16Unorm, 4, 2},
    {WGPUTextureFormat_RG16Snorm, 4, 2}, {WGPUTextureFormat_RG16Uint, 4, 2},
    {WGPUTextureFormat_RG16Sint, 4, 2}, {WGPUTextureFormat_RG16Float, 4, 2},
    {WGPUTextureFormat_RGBA16Unorm, 8, 4}, {WGPUTextureFormat_RGBA16Snorm, 8, 2},
    {WGPUTextureFormat_RGBA16Uint, 8, 2}, {WGPUTextureFormat_RGBA16Sint, 8, 2},
    {WGPUTextureFormat_RGBA16Float, 8, 2}, {WGPUTextureFormat_R32Uint, 4, 4},
    {WGPUTextureFormat_R32Sint, 4, 4}, {WGPUTextureFormat_R32Float, 4, 4},
    {WGPUTextureFormat_RG32Uint, 8, 4}, {WGPUTextureFormat_RG32Sint, 8, 4},
    {WGPUTextureFormat_RG32Float, 8, 4}, {WGPUTextureFormat_RGBA32Uint, 16, 4},
    {WGPUTextureFormat_RGBA32Sint, 16, 4}, {WGPUTextureFormat_RGBA32Float, 16, 4},
    {WGPUTextureFormat_RGB10A2Uint, 8, 4}, {WGPUTextureFormat_RGB10A2Unorm, 8, 4},
    {WGPUTextureFormat_RG11B10Ufloat, 8, 4}, {WGPUTextureFormat_R8Snorm, 1, 1},
    {WGPUTextureFormat_RG8Snorm, 2, 1}, {WGPUTextureFormat_RGBA8Snorm, 4, 1},
}};

static constexpr std::array<WGPUTextureFormat, 39> kPossibleColorRenderableTextureFormats = {{
    WGPUTextureFormat_R8Unorm, WGPUTextureFormat_R8Uint, WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm, WGPUTextureFormat_RG8Uint, WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb,
    WGPUTextureFormat_RGBA8Uint, WGPUTextureFormat_RGBA8Sint, WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb, WGPUTextureFormat_R16Unorm, WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Uint, WGPUTextureFormat_R16Sint, WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Unorm, WGPUTextureFormat_RG16Snorm, WGPUTextureFormat_RG16Uint,
    WGPUTextureFormat_RG16Sint, WGPUTextureFormat_RG16Float, WGPUTextureFormat_RGBA16Unorm,
    WGPUTextureFormat_RGBA16Snorm, WGPUTextureFormat_RGBA16Uint, WGPUTextureFormat_RGBA16Sint,
    WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_R32Uint, WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float, WGPUTextureFormat_RG32Uint, WGPUTextureFormat_RG32Sint,
    WGPUTextureFormat_RG32Float, WGPUTextureFormat_RGBA32Uint, WGPUTextureFormat_RGBA32Sint,
    WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RGB10A2Uint, WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_RG11B10Ufloat,
}};

std::vector<Value> possibleColorRenderableFormatValues() {
    return formatIdentifierValues(kPossibleColorRenderableTextureFormats);
}

std::vector<Value> depthStencilFormatValues() {
    return formatIdentifierValues(kDepthStencilFormats);
}

const ColorRenderInfo* colorRenderInfo(WGPUTextureFormat format) {
    for (const ColorRenderInfo& info : kColorRenderInfoTable) {
        if (info.format == format) return &info;
    }
    return nullptr;
}

uint32_t computeBytesPerSampleFromFormats(const std::vector<WGPUTextureFormat>& formats) {
    uint32_t bytesPerSample = 0;
    for (WGPUTextureFormat format : formats) {
        const ColorRenderInfo* info = colorRenderInfo(format);
        if (info == nullptr) continue;
        bytesPerSample = (bytesPerSample + info->alignment - 1u) & ~(info->alignment - 1u);
        bytesPerSample += info->byteCost;
    }
    return bytesPerSample;
}

bool isDepthFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_Depth16Unorm || format == WGPUTextureFormat_Depth24Plus ||
           format == WGPUTextureFormat_Depth24PlusStencil8 ||
           format == WGPUTextureFormat_Depth32Float ||
           format == WGPUTextureFormat_Depth32FloatStencil8;
}

bool isStencilFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_Stencil8 ||
           format == WGPUTextureFormat_Depth24PlusStencil8 ||
           format == WGPUTextureFormat_Depth32FloatStencil8;
}

bool isTextureFormatResolvable(WGPUTextureFormat format) {
    // Mirrors upstream kTextureFormatInfo[format].colorRender.resolve for the
    // kPossibleColorRenderableTextureFormats set used by this test.
    switch (format) {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
            return true;
        default:
            return false;
    }
}

bool isCompatModeUnsupportedMultisampledFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_R32Float:
            return true;
        default:
            return false;
    }
}

WGPUTexture createTestTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureDimension dimension = WGPUTextureDimension_2D,
    uint32_t width = 16,
    uint32_t height = 16,
    uint32_t arrayLayerCount = 1,
    uint32_t mipLevelCount = 1,
    uint32_t sampleCount = 1,
    WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.dimension = dimension;
    desc.size = WGPUExtent3D{width, height, arrayLayerCount};
    desc.format = format;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = sampleCount;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

WGPURenderPassColorAttachment colorAttachment(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    std::optional<WGPUTextureViewDescriptor> viewDesc = std::nullopt) {
    WGPUTextureViewDescriptor defaultDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc.has_value() ? *viewDesc : defaultDesc);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    return attachment;
}

WGPURenderPassDepthStencilAttachment depthStencilAttachment(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    std::optional<WGPUTextureViewDescriptor> viewDesc = std::nullopt) {
    WGPUTextureViewDescriptor defaultDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc.has_value() ? *viewDesc : defaultDesc);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthClearValue = 1.0f;
    attachment.depthLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    attachment.stencilClearValue = 0;
    attachment.stencilLoadOp = WGPULoadOp_Clear;
    attachment.stencilStoreOp = WGPUStoreOp_Store;
    return attachment;
}

void tryRenderPass(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    std::vector<WGPURenderPassColorAttachment>& colorAttachments,
    WGPURenderPassDepthStencilAttachment* depthStencil = nullptr,
    WGPUQuerySet occlusionQuerySet = nullptr,
    WGPUPassTimestampWrites* timestampWrites = nullptr) {
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = colorAttachments.size();
    desc.colorAttachments = colorAttachments.empty() ? nullptr : colorAttachments.data();
    desc.depthStencilAttachment = depthStencil;
    desc.occlusionQuerySet = occlusionQuerySet;
    desc.timestampWrites = timestampWrites;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Native validation for render-pass attachment compatibility is deferred to finish().
    t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
}

WGPUTextureView getErrorTextureView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_None;
    WGPUTexture texture = t.createTextureWithState(ResourceState::Invalid, desc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = nullptr;
    t.expectValidationError([&] { view = t.createViewTracked(texture, viewDesc); }, true);
    return view;
}

CTS_TEST(g, "attachments,one_color_attachment")
    .desc("Test that a render pass works with only one color attachment.")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bindTextureResource", {false, true}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture color = createTestTexture(t);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, color)};
        tryRenderPass(t, true, colors);
    });

CTS_TEST(g, "attachments,one_depth_stencil_attachment")
    .desc("Test that a render pass works with only one depthStencil attachment.")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bindTextureResource", {false, true}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture depth = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8);
        std::vector<WGPURenderPassColorAttachment> colors;
        WGPURenderPassDepthStencilAttachment ds = depthStencilAttachment(t, depth);
        tryRenderPass(t, true, colors, &ds);
    });

CTS_TEST(g, "color_attachments,empty")
    .desc("Test that when colorAttachments is empty, depthStencilAttachment must not be undefined.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("unclampedColorAttachments", {std::string("empty"), std::string("one-null"),
                                                   std::string("two-null"), std::string("eight-null"),
                                                   std::string("one-color")})
            .combine("hasDepthStencilAttachment", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string key = t.param<std::string>("unclampedColorAttachments");
        const bool hasDepth = t.param<bool>("hasDepthStencilAttachment");
        std::vector<WGPURenderPassColorAttachment> colors;
        if (key == "one-color") colors.push_back(colorAttachment(t, createTestTexture(t)));
        WGPURenderPassDepthStencilAttachment ds = depthStencilAttachment(
            t, createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8));
        tryRenderPass(t, !colors.empty() || hasDepth, colors, hasDepth ? &ds : nullptr);
    });

CTS_TEST(g, "color_attachments,limits,maxColorAttachments")
    .desc("Test that color attachments must not exceed maxColorAttachments.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({ParamRecord{{"colorAttachmentsCountVariant_mult", int64_t(1)},
                                                {"colorAttachmentsCountVariant_add", int64_t(0)},
                                                {"_success", true}},
                                    ParamRecord{{"colorAttachmentsCountVariant_mult", int64_t(1)},
                                                {"colorAttachmentsCountVariant_add", int64_t(1)},
                                                {"_success", false}}});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t count = limits.maxColorAttachments *
            static_cast<uint32_t>(t.param<int64_t>("colorAttachmentsCountVariant_mult")) +
            static_cast<uint32_t>(t.param<int64_t>("colorAttachmentsCountVariant_add"));
        std::vector<WGPURenderPassColorAttachment> colors;
        for (uint32_t i = 0; i < count; ++i) {
            colors.push_back(colorAttachment(t, createTestTexture(t, WGPUTextureFormat_R8Unorm)));
        }
        tryRenderPass(t, t.param<bool>("_success"), colors);
    });

CTS_TEST(g, "color_attachments,limits,maxColorAttachmentBytesPerSample,aligned")
    .desc("Test maxColorAttachmentBytesPerSample using the same format.")
    .params([](ParamsBuilder u) {
        std::vector<Value> counts;
        for (uint32_t i = 1; i <= kDefaultMaxColorAttachments; ++i) counts.emplace_back(uint64_t(i));
        return u.combine("format", possibleColorRenderableFormatValues())
            .beginSubcases()
            .combine("attachmentCount", counts);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t attachmentCount = static_cast<uint32_t>(t.param<uint64_t>("attachmentCount"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(format);
        const WGPULimits limits = t.getLimits();
        if (attachmentCount > limits.maxColorAttachments) t.skip("attachmentCount > maxColorAttachments");
        std::vector<WGPURenderPassColorAttachment> colors;
        for (uint32_t i = 0; i < attachmentCount; ++i) {
            colors.push_back(colorAttachment(t, createTestTexture(t, format)));
        }
        const std::vector<WGPUTextureFormat> formats(attachmentCount, format);
        const bool success =
            computeBytesPerSampleFromFormats(formats) <= limits.maxColorAttachmentBytesPerSample;
        tryRenderPass(t, success, colors);
    });

CTS_TEST(g, "color_attachments,limits,maxColorAttachmentBytesPerSample,unaligned")
    .desc("Test maxColorAttachmentBytesPerSample using potentially unaligned formats.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"formats", std::string("r8unorm,r32float,rgba8unorm,rgba32float,r8unorm")}},
            ParamRecord{{"formats", std::string("r32float,rgba8unorm,rgba32float,r8unorm,r8unorm")}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string key = t.param<std::string>("formats");
        std::vector<WGPUTextureFormat> formats =
            key[1] == '8' ? std::vector<WGPUTextureFormat>{WGPUTextureFormat_R8Unorm,
                                                           WGPUTextureFormat_R32Float,
                                                           WGPUTextureFormat_RGBA8Unorm,
                                                           WGPUTextureFormat_RGBA32Float,
                                                           WGPUTextureFormat_R8Unorm}
                          : std::vector<WGPUTextureFormat>{WGPUTextureFormat_R32Float,
                                                           WGPUTextureFormat_RGBA8Unorm,
                                                           WGPUTextureFormat_RGBA32Float,
                                                           WGPUTextureFormat_R8Unorm,
                                                           WGPUTextureFormat_R8Unorm};
        const WGPULimits limits = t.getLimits();
        if (formats.size() > limits.maxColorAttachments) t.skip("numColorAttachments > maxColorAttachments");
        std::vector<WGPURenderPassColorAttachment> colors;
        for (WGPUTextureFormat format : formats) colors.push_back(colorAttachment(t, createTestTexture(t, format)));
        tryRenderPass(t, computeBytesPerSampleFromFormats(formats) <= limits.maxColorAttachmentBytesPerSample, colors);
    });

CTS_TEST(g, "color_attachments,depthSlice,definedness")
    .desc("Test depthSlice definedness for 2d and 3d color attachments.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", {std::string("2d"), std::string("3d")})
            .beginSubcases()
            .combine("depthSlice", {Value::undef(), Value(int64_t(0)), Value(int64_t(0xffffffffu))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool is3d = t.param<std::string>("dimension") == "3d";
        WGPUTexture texture = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm,
                                                is3d ? WGPUTextureDimension_3D : WGPUTextureDimension_2D);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, texture)};
        const bool depthSliceDefined =
            !t.paramIsUndefined("depthSlice") && t.param<int64_t>("depthSlice") != int64_t(0xffffffffu);
        if (depthSliceDefined) {
            colors[0].depthSlice = static_cast<uint32_t>(t.param<int64_t>("depthSlice"));
        }
        // WGPU uses 0xffffffff as the native undefined sentinel, so that JS-defined value cannot
        // be represented distinctly in C. Treat it as undefined for native timing.
        const bool success = (!is3d && !depthSliceDefined) ||
                             (is3d && depthSliceDefined && t.param<int64_t>("depthSlice") == 0);
        tryRenderPass(t, success, colors);
    });

CTS_TEST(g, "color_attachments,depthSlice,bound_check")
    .desc("Test depthSlice bounds for 3d texture subresources.")
    .params([](ParamsBuilder u) {
        return u.combine("mipLevel", {0, 1, 2, 3, 4})
            .beginSubcases()
            .expand("depthSlice", [](const ParamRecord& p) {
                const uint32_t mip = static_cast<uint32_t>(valueAs<int64_t>(*findParam(p, "mipLevel")));
                const uint32_t depth = std::max(kArrayLayerCount >> mip, 1u);
                std::vector<uint32_t> raw = {0, 1, depth - 1u, depth};
                std::vector<Value> values;
                for (uint32_t v : raw) {
                    bool seen = false;
                    for (const Value& existing : values) {
                        if (valueAs<int64_t>(existing) == static_cast<int64_t>(v)) seen = true;
                    }
                    if (!seen) values.emplace_back(int64_t(v));
                }
                return values;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t mip = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        const uint32_t slice = static_cast<uint32_t>(t.param<int64_t>("depthSlice"));
        WGPUTexture texture = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_3D,
                                                16, 1, kArrayLayerCount, mip + 1);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.baseMipLevel = mip;
        viewDesc.mipLevelCount = 1;
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, texture, viewDesc)};
        colors[0].depthSlice = slice;
        tryRenderPass(t, slice < std::max(kArrayLayerCount >> mip, 1u), colors);
    });

CTS_TEST(g, "color_attachments,depthSlice,overlaps,same_miplevel")
    .desc("Test depth slices of 3d color attachments have no overlaps in one render pass.")
    .params([](ParamsBuilder u) {
        return u.combine("sameDepthSlice", {true, false})
            .beginSubcases()
            .combine("sameTexture", {true, false})
            .combine("samePass", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool sameDepthSlice = t.param<bool>("sameDepthSlice");
        const bool sameTexture = t.param<bool>("sameTexture");
        const bool samePass = t.param<bool>("samePass");
        constexpr uint32_t count = 4;
        WGPUTexture texture = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_3D,
                                                16, 16, count);
        std::vector<WGPURenderPassColorAttachment> colors;
        for (uint32_t i = 0; i < count; ++i) {
            WGPUTexture tex = sameTexture ? texture : createTestTexture(t, WGPUTextureFormat_RGBA8Unorm,
                                                                        WGPUTextureDimension_3D, 16, 16, count);
            colors.push_back(colorAttachment(t, tex));
            colors.back().depthSlice = sameDepthSlice ? 0 : i;
        }
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (samePass) {
            WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            desc.colorAttachmentCount = colors.size();
            desc.colorAttachments = colors.data();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        } else {
            for (WGPURenderPassColorAttachment& color : colors) {
                WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                desc.colorAttachmentCount = 1;
                desc.colorAttachments = &color;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }
        }
        t.expectValidationError([&] { t.finishTracked(encoder); }, sameDepthSlice && sameTexture && samePass);
    });

CTS_TEST(g, "color_attachments,depthSlice,overlaps,diff_miplevel")
    .desc("Test same depth slice from different mip levels of a 3d texture.")
    .params([](ParamsBuilder u) { return u.combine("sameMipLevel", {true, false}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool sameMipLevel = t.param<bool>("sameMipLevel");
        constexpr uint32_t mipCount = 4;
        WGPUTexture texture = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_3D,
                                                1, 1, 1u << mipCount, mipCount);
        std::vector<WGPURenderPassColorAttachment> colors;
        for (uint32_t i = 0; i < mipCount; ++i) {
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            viewDesc.baseMipLevel = sameMipLevel ? 0 : i;
            viewDesc.mipLevelCount = 1;
            colors.push_back(colorAttachment(t, texture, viewDesc));
            colors.back().depthSlice = 0;
        }
        tryRenderPass(t, !sameMipLevel, colors);
    });

CTS_TEST(g, "attachments,same_size")
    .desc("Test that attachments have the same size.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture c1a = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 1, 1);
        WGPUTexture c1b = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 1, 1);
        WGPUTexture c2 = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 2, 2);
        WGPUTexture d1 = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureDimension_2D, 1, 1);
        WGPUTexture d2 = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureDimension_2D, 2, 2);
        std::vector<WGPURenderPassColorAttachment> good = {colorAttachment(t, c1a), colorAttachment(t, c1b)};
        WGPURenderPassDepthStencilAttachment ds1 = depthStencilAttachment(t, d1);
        tryRenderPass(t, true, good, &ds1);
        std::vector<WGPURenderPassColorAttachment> badColor = {colorAttachment(t, c1a), colorAttachment(t, c2)};
        tryRenderPass(t, false, badColor);
        std::vector<WGPURenderPassColorAttachment> badDepth = {colorAttachment(t, c1a), colorAttachment(t, c1b)};
        WGPURenderPassDepthStencilAttachment ds2 = depthStencilAttachment(t, d2);
        tryRenderPass(t, false, badDepth, &ds2);
    });

CTS_TEST(g, "attachments,color_depth_mismatch")
    .desc("Test that attachments match whether they are used for color or depth stencil.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture color = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm);
        WGPUTexture depth = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8);
        std::vector<WGPURenderPassColorAttachment> badColor = {colorAttachment(t, depth)};
        tryRenderPass(t, false, badColor);
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassDepthStencilAttachment badDs = depthStencilAttachment(t, color);
        tryRenderPass(t, false, empty, &badDs);
    });

CTS_TEST(g, "attachments,layer_count")
    .desc("Test layer counts for color and depth stencil attachments.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({ParamRecord{{"arrayLayerCount", int64_t(5)}, {"baseArrayLayer", int64_t(0)}, {"_success", false}},
                                    ParamRecord{{"arrayLayerCount", int64_t(1)}, {"baseArrayLayer", int64_t(0)}, {"_success", true}},
                                    ParamRecord{{"arrayLayerCount", int64_t(1)}, {"baseArrayLayer", int64_t(9)}, {"_success", true}}});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t layers = static_cast<uint32_t>(t.param<int64_t>("arrayLayerCount"));
        const uint32_t base = static_cast<uint32_t>(t.param<int64_t>("baseArrayLayer"));
        const bool success = t.param<bool>("_success");
        WGPUTexture color = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 32, 32, 10);
        WGPUTexture depth = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureDimension_2D, 32, 32, 10);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = WGPUTextureViewDimension_2DArray;
        viewDesc.baseArrayLayer = base;
        viewDesc.arrayLayerCount = layers;
        viewDesc.mipLevelCount = 1;
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, color, viewDesc)};
        tryRenderPass(t, success, colors);
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassDepthStencilAttachment ds = depthStencilAttachment(t, depth, viewDesc);
        tryRenderPass(t, success, empty, &ds);
    });

CTS_TEST(g, "attachments,mip_level_count")
    .desc("Test mip level counts for color and depth stencil attachments.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({ParamRecord{{"mipLevelCount", int64_t(2)}, {"baseMipLevel", int64_t(0)}, {"_success", false}},
                                    ParamRecord{{"mipLevelCount", int64_t(1)}, {"baseMipLevel", int64_t(0)}, {"_success", true}},
                                    ParamRecord{{"mipLevelCount", int64_t(1)}, {"baseMipLevel", int64_t(3)}, {"_success", true}}});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t mips = static_cast<uint32_t>(t.param<int64_t>("mipLevelCount"));
        const uint32_t base = static_cast<uint32_t>(t.param<int64_t>("baseMipLevel"));
        const bool success = t.param<bool>("_success");
        WGPUTexture color = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 32, 32, 1, 4);
        WGPUTexture depth = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureDimension_2D, 32, 32, 1, 4);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = base;
        viewDesc.mipLevelCount = mips;
        viewDesc.arrayLayerCount = 1;
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, color, viewDesc)};
        tryRenderPass(t, success, colors);
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassDepthStencilAttachment ds = depthStencilAttachment(t, depth, viewDesc);
        tryRenderPass(t, success, empty, &ds);
    });

CTS_TEST(g, "color_attachments,loadOp_storeOp")
    .desc("Test transient color attachment load/store requirements.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleColorRenderableFormatValues())
            .beginSubcases()
            .combine("transientTexture", {true, false})
            .combine("loadOp", {std::string("clear"), std::string("load")})
            .combine("storeOp", {std::string("discard"), std::string("store")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool transient = t.param<bool>("transientTexture");
        const std::string loadOp = t.param<std::string>("loadOp");
        const std::string storeOp = t.param<std::string>("storeOp");
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(format);
        if (transient) t.skipIfTransientAttachmentNotSupported();
        const WGPUTextureUsage usage = transient ? (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment)
                                                 : WGPUTextureUsage_RenderAttachment;
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, format, WGPUTextureDimension_2D, 16, 16, 1, 1, 1, usage))};
        colors[0].loadOp = loadOp == "clear" ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colors[0].storeOp = storeOp == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
        tryRenderPass(t, !transient || (loadOp == "clear" && storeOp == "discard"), colors);
    });

CTS_TEST(g, "color_attachments,non_multisampled")
    .desc("Test setting a resolve target is invalid if the color attachment is not multisampled.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 1))};
        colors[0].resolveTarget = createView(t, createTestTexture(t));
        tryRenderPass(t, false, colors);
    });

CTS_TEST(g, "color_attachments,sample_count")
    .desc("Test multisampled color attachment sample count matching.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture single = createTestTexture(t);
        WGPUTexture multi = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4);
        std::vector<WGPURenderPassColorAttachment> ok = {colorAttachment(t, multi)};
        tryRenderPass(t, true, ok);
        std::vector<WGPURenderPassColorAttachment> bad = {colorAttachment(t, single), colorAttachment(t, multi)};
        tryRenderPass(t, false, bad);
    });

CTS_TEST(g, "resolveTarget,sample_count")
    .desc("Test using multisampled resolve target is invalid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = createView(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4));
        tryRenderPass(t, false, colors);
    });

CTS_TEST(g, "resolveTarget,array_layer_count")
    .desc("Test resolve target arrayLayerCount > 1 is invalid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = WGPUTextureViewDimension_2DArray;
        colors[0].resolveTarget = t.createViewTracked(createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 2), viewDesc);
        tryRenderPass(t, false, colors);
    });

CTS_TEST(g, "resolveTarget,mipmap_level_count")
    .desc("Test resolve target mip level count > 1 is invalid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = createView(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 2));
        tryRenderPass(t, false, colors);
    });

CTS_TEST(g, "resolveTarget,usage")
    .desc("Test resolve target must have RENDER_ATTACHMENT usage.")
    .params([](ParamsBuilder u) {
        return u.combine("usage", {std::string("copy"), std::string("storage-texture"),
                                   std::string("storage-storage"), std::string("render-texture")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string key = t.param<std::string>("usage");
        WGPUTextureUsage usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        if (key == "storage-texture" || key == "storage-storage") usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;
        if (key == "render-texture") usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = createView(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 1, usage));
        tryRenderPass(t, (usage & WGPUTextureUsage_RenderAttachment) != 0, colors);
    });

CTS_TEST(g, "resolveTarget,error_state")
    .desc("Test that a resolve target in an error state is invalid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = getErrorTextureView(t);
        tryRenderPass(t, false, colors);
    });

CTS_TEST(g, "resolveTarget,single_sample_count")
    .desc("Test multisampled color attachment with single-sampled resolve target is valid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = createView(t, createTestTexture(t));
        tryRenderPass(t, true, colors);
    });

CTS_TEST(g, "resolveTarget,different_format")
    .desc("Test resolve target with a different format is invalid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = createView(t, createTestTexture(t, WGPUTextureFormat_BGRA8Unorm));
        tryRenderPass(t, false, colors);
    });

CTS_TEST(g, "resolveTarget,different_size")
    .desc("Test resolve target size must match color attachment subresource size.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t size = 16;
        WGPUTexture src = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, size, size, 1, 1, 4);
        WGPUTexture dst = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, size * 2, size * 2, 1, 2);
        WGPUTextureViewDescriptor mip0 = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        mip0.baseMipLevel = 0;
        mip0.mipLevelCount = 1;
        std::vector<WGPURenderPassColorAttachment> bad = {colorAttachment(t, src)};
        bad[0].resolveTarget = t.createViewTracked(dst, mip0);
        tryRenderPass(t, false, bad);
        WGPUTextureViewDescriptor mip1 = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        mip1.baseMipLevel = 1;
        std::vector<WGPURenderPassColorAttachment> good = {colorAttachment(t, src)};
        good[0].resolveTarget = t.createViewTracked(dst, mip1);
        tryRenderPass(t, true, good);
    });

CTS_TEST(g, "depth_stencil_attachment,sample_counts_mismatch")
    .desc("Test depth stencil attachment sample counts must match color attachments.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture multiDs = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureDimension_2D, 16, 16, 1, 1, 4);
        WGPUTexture singleDs = createTestTexture(t, WGPUTextureFormat_Depth24PlusStencil8);
        WGPUTexture singleColor = createTestTexture(t);
        WGPUTexture multiColor = createTestTexture(t, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, 16, 16, 1, 1, 4);
        std::vector<WGPURenderPassColorAttachment> c0 = {colorAttachment(t, multiColor)};
        WGPURenderPassDepthStencilAttachment ds0 = depthStencilAttachment(t, singleDs);
        tryRenderPass(t, false, c0, &ds0);
        std::vector<WGPURenderPassColorAttachment> c1 = {colorAttachment(t, singleColor)};
        WGPURenderPassDepthStencilAttachment ds1 = depthStencilAttachment(t, multiDs);
        tryRenderPass(t, false, c1, &ds1);
        std::vector<WGPURenderPassColorAttachment> c2 = {colorAttachment(t, multiColor)};
        WGPURenderPassDepthStencilAttachment ds2 = depthStencilAttachment(t, multiDs);
        tryRenderPass(t, true, c2, &ds2);
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassDepthStencilAttachment ds3 = depthStencilAttachment(t, multiDs);
        tryRenderPass(t, true, empty, &ds3);
    });

CTS_TEST(g, "depth_stencil_attachment,loadOp_storeOp_match_depthReadOnly_stencilReadOnly")
    .desc("Test depth/stencil load/store/read-only and transient attachment combinations.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormatValues())
            .beginSubcases()
            .combine("transientTexture", {true, false})
            .combine("depthReadOnly", {Value::undef(), Value(true), Value(false)})
            .combine("depthLoadOp", {Value::undef(), Value(std::string("clear")), Value(std::string("load"))})
            .combine("depthStoreOp", {Value::undef(), Value(std::string("discard")), Value(std::string("store"))})
            .combine("stencilReadOnly", {Value::undef(), Value(true), Value(false)})
            .combine("stencilLoadOp", {Value::undef(), Value(std::string("clear")), Value(std::string("load"))})
            .combine("stencilStoreOp", {Value::undef(), Value(std::string("discard")), Value(std::string("store"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool transient = t.param<bool>("transientTexture");
        t.skipIfTextureFormatNotSupported(format);
        if (transient) t.skipIfTransientAttachmentNotSupported();
        const WGPUTextureUsage usage = transient ? (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment)
                                                 : WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = createTestTexture(t, format, WGPUTextureDimension_2D, 1, 1, 1, 1, 1, usage);
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = createView(t, texture);
        const bool depthReadOnly = !t.paramIsUndefined("depthReadOnly") && t.param<bool>("depthReadOnly");
        const bool stencilReadOnly = !t.paramIsUndefined("stencilReadOnly") && t.param<bool>("stencilReadOnly");
        ds.depthReadOnly = depthReadOnly ? WGPU_TRUE : WGPU_FALSE;
        ds.stencilReadOnly = stencilReadOnly ? WGPU_TRUE : WGPU_FALSE;
        auto setLoad = [&](const char* key, WGPULoadOp& out) {
            if (!t.paramIsUndefined(key)) out = t.param<std::string>(key) == "clear" ? WGPULoadOp_Clear : WGPULoadOp_Load;
        };
        auto setStore = [&](const char* key, WGPUStoreOp& out) {
            if (!t.paramIsUndefined(key)) out = t.param<std::string>(key) == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
        };
        setLoad("depthLoadOp", ds.depthLoadOp);
        setStore("depthStoreOp", ds.depthStoreOp);
        setLoad("stencilLoadOp", ds.stencilLoadOp);
        setStore("stencilStoreOp", ds.stencilStoreOp);
        if (!t.paramIsUndefined("depthLoadOp") && t.param<std::string>("depthLoadOp") == "clear") ds.depthClearValue = 0.0f;
        const bool hasDepth = isDepthFormat(format);
        const bool hasStencil = isStencilFormat(format);
        const bool hasDepthLoad = !t.paramIsUndefined("depthLoadOp");
        const bool hasDepthStore = !t.paramIsUndefined("depthStoreOp");
        const bool hasStencilLoad = !t.paramIsUndefined("stencilLoadOp");
        const bool hasStencilStore = !t.paramIsUndefined("stencilStoreOp");
        const bool goodAspectSettingsPresent =
            (!(hasDepthLoad && hasDepthStore && !depthReadOnly) || hasDepth) &&
            (!(hasStencilLoad && hasStencilStore && !stencilReadOnly) || hasStencil);
        const bool goodDepthCombo = hasDepth && !depthReadOnly ? (hasDepthLoad && hasDepthStore) : (!hasDepthLoad && !hasDepthStore);
        const bool goodStencilCombo = hasStencil && !stencilReadOnly ? (hasStencilLoad && hasStencilStore) : (!hasStencilLoad && !hasStencilStore);
        const bool depthTransientOk = !hasDepth || (!t.paramIsUndefined("depthLoadOp") && !t.paramIsUndefined("depthStoreOp") &&
            t.param<std::string>("depthLoadOp") == "clear" && t.param<std::string>("depthStoreOp") == "discard");
        const bool stencilTransientOk = !hasStencil || (!t.paramIsUndefined("stencilLoadOp") && !t.paramIsUndefined("stencilStoreOp") &&
            t.param<std::string>("stencilLoadOp") == "clear" && t.param<std::string>("stencilStoreOp") == "discard");
        tryRenderPass(t, goodAspectSettingsPresent && goodDepthCombo && goodStencilCombo && (!transient || (depthTransientOk && stencilTransientOk)), empty, &ds);
    });

CTS_TEST(g, "depth_stencil_attachment,depth_clear_value")
    .desc("Test depthClearValue range when depthLoadOp is clear.")
    .params([](ParamsBuilder u) {
        return u.combine("depthLoadOp", {Value(std::string("load")), Value(std::string("clear")), Value::undef()})
            .combine("depthClearValue", {Value::undef(), Value(-1.0), Value(0.0), Value(0.5), Value(1.0), Value(1.5)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool depthOpUndef = t.paramIsUndefined("depthLoadOp");
        WGPUTextureFormat format = depthOpUndef ? WGPUTextureFormat_Stencil8 : WGPUTextureFormat_Depth24PlusStencil8;
        t.skipIfTextureFormatNotSupported(format);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t))};
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = createView(t, createTestTexture(t, format));
        ds.stencilClearValue = 0;
        ds.stencilLoadOp = WGPULoadOp_Clear;
        ds.stencilStoreOp = WGPUStoreOp_Store;
        if (!t.paramIsUndefined("depthClearValue")) ds.depthClearValue = static_cast<float>(t.param<double>("depthClearValue"));
        if (depthOpUndef) {
            ds.depthLoadOp = WGPULoadOp_Undefined;
            ds.depthStoreOp = WGPUStoreOp_Undefined;
        } else {
            ds.depthLoadOp = t.param<std::string>("depthLoadOp") == "clear" ? WGPULoadOp_Clear : WGPULoadOp_Load;
            ds.depthStoreOp = WGPUStoreOp_Store;
        }
        const bool inRange = !t.paramIsUndefined("depthClearValue") && t.param<double>("depthClearValue") >= 0.0 && t.param<double>("depthClearValue") <= 1.0;
        const bool invalid = !depthOpUndef && t.param<std::string>("depthLoadOp") == "clear" && !inRange;
        tryRenderPass(t, !invalid, colors, &ds);
    });

CTS_TEST(g, "resolveTarget,format_supports_resolve")
    .desc("Test multisample formats can resolve iff they support resolve.")
    .params([](ParamsBuilder u) { return u.combine("format", possibleColorRenderableFormatValues()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        if (!t.isTextureFormatMultisampled(format) || isCompatModeUnsupportedMultisampledFormat(format)) {
            t.skip("format is not multisampled");
        }
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t, format, WGPUTextureDimension_2D, 16, 16, 1, 1, 4))};
        colors[0].resolveTarget = createView(t, createTestTexture(t, format));
        tryRenderPass(t, isTextureFormatResolvable(format), colors);
    });

CTS_TEST(g, "timestampWrites,query_set_type")
    .desc("Test timestampWrites query set type.")
    .params([](ParamsBuilder u) { return u.combine("queryType", {std::string("occlusion"), std::string("timestamp")}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) t.skip("timestamp-query feature not available");
        const std::string queryType = t.param<std::string>("queryType");
        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = queryType == "timestamp" ? WGPUQueryType_Timestamp : WGPUQueryType_Occlusion;
        qsDesc.count = 2;
        WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);
        WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        writes.querySet = qs;
        writes.beginningOfPassWriteIndex = 0;
        writes.endOfPassWriteIndex = 1;
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t))};
        tryRenderPass(t, queryType == "timestamp", colors, nullptr, nullptr, &writes);
        wgpuQuerySetRelease(qs);
    });

CTS_TEST(g, "timestampWrite,query_index")
    .desc("Test timestamp write query indexes are in range and unique.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"beginningOfPassWriteIndex", int64_t(0)}, {"endOfPassWriteIndex", Value::undef()}},
            ParamRecord{{"beginningOfPassWriteIndex", int64_t(1)}, {"endOfPassWriteIndex", Value::undef()}},
            ParamRecord{{"beginningOfPassWriteIndex", int64_t(2)}, {"endOfPassWriteIndex", Value::undef()}},
            ParamRecord{{"beginningOfPassWriteIndex", int64_t(3)}, {"endOfPassWriteIndex", Value::undef()}},
            ParamRecord{{"beginningOfPassWriteIndex", Value::undef()}, {"endOfPassWriteIndex", int64_t(0)}},
            ParamRecord{{"beginningOfPassWriteIndex", Value::undef()}, {"endOfPassWriteIndex", int64_t(1)}},
            ParamRecord{{"beginningOfPassWriteIndex", Value::undef()}, {"endOfPassWriteIndex", int64_t(2)}},
            ParamRecord{{"beginningOfPassWriteIndex", Value::undef()}, {"endOfPassWriteIndex", int64_t(3)}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) t.skip("timestamp-query feature not available");
        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = WGPUQueryType_Timestamp;
        qsDesc.count = 2;
        WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);
        WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        writes.querySet = qs;
        writes.beginningOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
        writes.endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
        if (!t.paramIsUndefined("beginningOfPassWriteIndex")) writes.beginningOfPassWriteIndex = static_cast<uint32_t>(t.param<int64_t>("beginningOfPassWriteIndex"));
        if (!t.paramIsUndefined("endOfPassWriteIndex")) writes.endOfPassWriteIndex = static_cast<uint32_t>(t.param<int64_t>("endOfPassWriteIndex"));
        const bool beginUndef = t.paramIsUndefined("beginningOfPassWriteIndex");
        const bool endUndef = t.paramIsUndefined("endOfPassWriteIndex");
        const uint32_t begin = beginUndef ? 0 : static_cast<uint32_t>(t.param<int64_t>("beginningOfPassWriteIndex"));
        const uint32_t end = endUndef ? 0 : static_cast<uint32_t>(t.param<int64_t>("endOfPassWriteIndex"));
        const bool valid = (beginUndef != endUndef) && (beginUndef || begin < 2) && (endUndef || end < 2);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t))};
        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.colorAttachmentCount = colors.size();
        desc.colorAttachments = colors.data();
        desc.timestampWrites = &writes;
        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
            if (pass != nullptr) {
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }
            t.finishTracked(encoder);
        }, !valid);
        wgpuQuerySetRelease(qs);
    });

CTS_TEST(g, "occlusionQuerySet,query_set_type")
    .desc("Test occlusionQuerySet must have type occlusion.")
    .params([](ParamsBuilder u) { return u.combine("queryType", {std::string("occlusion"), std::string("timestamp")}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string queryType = t.param<std::string>("queryType");
        if (queryType == "timestamp" && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }
        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = queryType == "timestamp" ? WGPUQueryType_Timestamp : WGPUQueryType_Occlusion;
        qsDesc.count = 1;
        WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, createTestTexture(t))};
        tryRenderPass(t, queryType == "occlusion", colors, nullptr, qs);
        wgpuQuerySetRelease(qs);
    });

} // namespace
