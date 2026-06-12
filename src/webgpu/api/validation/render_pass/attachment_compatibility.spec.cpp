// Ported from gpuweb/cts src/webgpu/api/validation/render_pass/attachment_compatibility.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

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

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pass,attachment_compatibility",
    "Validation for attachment compatibility between render passes, bundles, and pipelines\n\n"
    "TODO(#3363): Make this into a MaxLimitTest and increase kMaxColorAttachments.");

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

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

std::vector<Value> colorAttachmentCountValues() {
    std::vector<Value> values;
    for (uint32_t i = 1; i <= kDefaultMaxColorAttachments; ++i) values.emplace_back(uint64_t(i));
    return values;
}

std::vector<std::vector<bool>> colorAttachmentPatterns() {
    std::vector<std::vector<bool>> result;
    for (uint32_t count = 1; count <= kDefaultMaxColorAttachments; ++count) {
        if (count == 1) {
            result.push_back({true});
        } else if (count == 2) {
            result.push_back({true, true});
            result.push_back({false, true});
            result.push_back({true, false});
        } else {
            result.emplace_back(count, true);
            for (uint32_t i = 0; i < count; ++i) {
                std::vector<bool> p(count, true);
                p[i] = false;
                result.push_back(p);
            }
            if (count <= 4) {
                for (uint32_t i = 0; i + 1 < count; ++i) {
                    for (uint32_t j = i + 1; j < count; ++j) {
                        std::vector<bool> p(count, true);
                        p[i] = false;
                        p[j] = false;
                        result.push_back(p);
                    }
                }
            }
        }
    }
    return result;
}

const std::vector<std::vector<bool>>& patterns() {
    static const std::vector<std::vector<bool>> p = colorAttachmentPatterns();
    return p;
}

std::vector<Value> patternIndicesForCount(const ParamRecord& record, const char* key) {
    const uint32_t count = static_cast<uint32_t>(valueAs<uint64_t>(*findParam(record, key)));
    std::vector<Value> values;
    const auto& p = patterns();
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i].size() == count) values.emplace_back(uint64_t(i));
    }
    return values;
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

std::vector<Value> depthStencilAttachmentFormatValues() {
    std::vector<Value> values;
    values.push_back(Value::undef());
    for (WGPUTextureFormat format : kDepthStencilFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

WGPUTextureView createAttachmentTextureView(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    uint32_t sampleCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{16, 16, 1};
    desc.format = format;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.sampleCount = sampleCount;
    WGPUTexture texture = t.createTextureTracked(desc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

WGPURenderPassColorAttachment colorAttachment(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    uint32_t sampleCount = 1) {
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = createAttachmentTextureView(t, format, sampleCount);
    attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    return attachment;
}

WGPURenderPassDepthStencilAttachment depthAttachment(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    uint32_t sampleCount = 1,
    bool depthReadOnly = false,
    bool stencilReadOnly = false) {
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = createAttachmentTextureView(t, format, sampleCount);
    attachment.depthReadOnly = depthReadOnly ? WGPU_TRUE : WGPU_FALSE;
    attachment.stencilReadOnly = stencilReadOnly ? WGPU_TRUE : WGPU_FALSE;
    if (isDepthFormat(format) && !depthReadOnly) {
        attachment.depthClearValue = 0.0f;
        attachment.depthLoadOp = WGPULoadOp_Clear;
        attachment.depthStoreOp = WGPUStoreOp_Discard;
    }
    if (isStencilFormat(format) && !stencilReadOnly) {
        attachment.stencilClearValue = 1;
        attachment.stencilLoadOp = WGPULoadOp_Clear;
        attachment.stencilStoreOp = WGPUStoreOp_Discard;
    }
    return attachment;
}

WGPURenderBundle makeBundle(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUDevice device,
    const std::vector<WGPUTextureFormat>& colorFormats,
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined,
    uint32_t sampleCount = 1,
    bool depthReadOnly = false,
    bool stencilReadOnly = false) {
    WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    desc.colorFormatCount = colorFormats.size();
    desc.colorFormats = colorFormats.empty() ? nullptr : colorFormats.data();
    desc.depthStencilFormat = depthStencilFormat;
    desc.sampleCount = sampleCount;
    desc.depthReadOnly = depthReadOnly ? WGPU_TRUE : WGPU_FALSE;
    desc.stencilReadOnly = stencilReadOnly ? WGPU_TRUE : WGPU_FALSE;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(device, &desc);
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(encoder, nullptr);
    wgpuRenderBundleEncoderRelease(encoder);
    (void)t;
    return bundle;
}

void executeBundleInPass(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    std::vector<WGPURenderPassColorAttachment>& colors,
    WGPURenderPassDepthStencilAttachment* ds,
    WGPURenderBundle bundle) {
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colors.size();
    passDesc.colorAttachments = colors.empty() ? nullptr : colors.data();
    passDesc.depthStencilAttachment = ds;
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
}

const char* plainTypeForFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGB10A2Uint:
            return "u32";
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA32Sint:
            return "i32";
        default:
            return "f32";
    }
}

struct PipelineHolder {
    std::string vertexCode =
        "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 0.0, 0.0, 0.0);\n"
        "}\n";
    std::string fragmentCode;
    WGPUShaderModule vertexModule = nullptr;
    WGPUShaderModule fragmentModule = nullptr;
    WGPUPipelineLayout layout = nullptr;
    std::vector<WGPUColorTargetState> targets;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

std::string fragmentShaderForTargets(const std::vector<std::optional<WGPUTextureFormat>>& formats) {
    std::string fields;
    std::string values;
    uint32_t location = 0;
    for (const auto& format : formats) {
        if (!format.has_value()) {
            ++location;
            continue;
        }
        const char* plain = plainTypeForFormat(*format);
        fields += "  @location(" + std::to_string(location) + ") o" + std::to_string(location) +
            " : vec4<" + plain + ">,\n";
        if (!values.empty()) values += ", ";
        if (std::string_view(plain) == "u32") {
            values += "vec4<u32>(0u, 0u, 0u, 0u)";
        } else if (std::string_view(plain) == "i32") {
            values += "vec4<i32>(0, 0, 0, 0)";
        } else {
            values += "vec4<f32>(0.0, 0.0, 0.0, 0.0)";
        }
        ++location;
    }
    if (fields.empty()) return "@fragment fn main() {}\n";
    return "struct Outputs {\n" + fields + "}\n"
           "@fragment fn main() -> Outputs {\n"
           "  return Outputs(" + values + ");\n"
           "}\n";
}

PipelineHolder makePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::vector<std::optional<WGPUTextureFormat>>& targetFormats,
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined,
    uint32_t sampleCount = 1,
    bool depthWriteEnabled = false,
    uint32_t stencilWriteMask = 0,
    WGPUCullMode cullMode = WGPUCullMode_None,
    bool stencilWrites = false) {
    PipelineHolder h;
    h.fragmentCode = fragmentShaderForTargets(targetFormats);
    h.vertexModule = t.createShaderModuleTracked(h.vertexCode);
    h.fragmentModule = t.createShaderModuleTracked(h.fragmentCode);
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    h.layout = t.createPipelineLayoutTracked(layoutDesc);
    for (const auto& format : targetFormats) {
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format.value_or(WGPUTextureFormat_Undefined);
        target.writeMask = WGPUColorWriteMask_None;
        h.targets.push_back(target);
    }
    h.fragment.module = h.fragmentModule;
    h.fragment.entryPoint = sv("main");
    h.fragment.targetCount = h.targets.size();
    h.fragment.targets = h.targets.empty() ? nullptr : h.targets.data();
    h.desc.layout = h.layout;
    h.desc.vertex.module = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.fragment = &h.fragment;
    h.desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    h.desc.primitive.cullMode = cullMode;
    h.desc.multisample.count = sampleCount;
    if (depthStencilFormat != WGPUTextureFormat_Undefined) {
        h.depthStencil.format = depthStencilFormat;
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;
        h.depthStencil.depthWriteEnabled = depthWriteEnabled ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        h.depthStencil.stencilWriteMask = stencilWriteMask;
        if (stencilWrites) {
            h.depthStencil.stencilFront.failOp = WGPUStencilOperation_Zero;
            h.depthStencil.stencilFront.depthFailOp = WGPUStencilOperation_Zero;
            h.depthStencil.stencilFront.passOp = WGPUStencilOperation_Zero;
            h.depthStencil.stencilBack.failOp = WGPUStencilOperation_Zero;
            h.depthStencil.stencilBack.depthFailOp = WGPUStencilOperation_Zero;
            h.depthStencil.stencilBack.passOp = WGPUStencilOperation_Zero;
        }
        h.desc.depthStencil = &h.depthStencil;
    }
    return h;
}

struct EncoderCase {
    WGPUCommandEncoder commandEncoder = nullptr;
    WGPURenderPassEncoder pass = nullptr;
    WGPURenderBundleEncoder bundle = nullptr;
};

EncoderCase makeEncoderCase(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType,
    std::vector<WGPURenderPassColorAttachment>& colors,
    WGPURenderPassDepthStencilAttachment* ds,
    const std::vector<WGPUTextureFormat>& bundleFormats,
    WGPUTextureFormat depthStencilFormat,
    uint32_t sampleCount,
    bool depthReadOnly = false,
    bool stencilReadOnly = false) {
    EncoderCase c;
    if (encoderType == "render pass") {
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = colors.size();
        passDesc.colorAttachments = colors.empty() ? nullptr : colors.data();
        passDesc.depthStencilAttachment = ds;
        c.commandEncoder = t.createCommandEncoderTracked();
        c.pass = wgpuCommandEncoderBeginRenderPass(c.commandEncoder, &passDesc);
    } else {
        WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        desc.colorFormatCount = bundleFormats.size();
        desc.colorFormats = bundleFormats.empty() ? nullptr : bundleFormats.data();
        desc.depthStencilFormat = depthStencilFormat;
        desc.sampleCount = sampleCount;
        desc.depthReadOnly = depthReadOnly ? WGPU_TRUE : WGPU_FALSE;
        desc.stencilReadOnly = stencilReadOnly ? WGPU_TRUE : WGPU_FALSE;
        c.bundle = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
    }
    return c;
}

void setPipelineAndValidate(
    AllFeaturesMaxLimitsGpuTest& t,
    EncoderCase& c,
    WGPURenderPipeline pipeline,
    bool success) {
    if (c.pass != nullptr) {
        wgpuRenderPassEncoderSetPipeline(c.pass, pipeline);
        wgpuRenderPassEncoderEnd(c.pass);
        wgpuRenderPassEncoderRelease(c.pass);
        t.expectValidationError([&] { t.finishTracked(c.commandEncoder); }, !success);
    } else {
        wgpuRenderBundleEncoderSetPipeline(c.bundle, pipeline);
        t.expectValidationError([&] {
            WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(c.bundle, nullptr);
            if (bundle != nullptr) wgpuRenderBundleRelease(bundle);
        }, !success);
        wgpuRenderBundleEncoderRelease(c.bundle);
    }
}

std::vector<WGPURenderPassColorAttachment> makeColorAttachments(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::vector<std::optional<WGPUTextureFormat>>& formats,
    uint32_t sampleCount = 1) {
    std::vector<WGPURenderPassColorAttachment> colors;
    for (const auto& format : formats) {
        if (format.has_value()) colors.push_back(colorAttachment(t, *format, sampleCount));
        else colors.push_back(WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT);
    }
    return colors;
}

std::vector<WGPUTextureFormat> concreteFormats(const std::vector<std::optional<WGPUTextureFormat>>& formats) {
    std::vector<WGPUTextureFormat> out;
    for (const auto& format : formats) out.push_back(format.value_or(WGPUTextureFormat_Undefined));
    return out;
}

CTS_TEST(g, "render_pass_and_bundle,color_format")
    .desc("Test that color attachment formats in render passes and bundles must match.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("passFormat", possibleColorRenderableFormatValues())
            .combine("bundleFormat", possibleColorRenderableFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat passFormat = parseTextureFormat(t.param<std::string>("passFormat"));
        const WGPUTextureFormat bundleFormat = parseTextureFormat(t.param<std::string>("bundleFormat"));
        t.skipIfTextureFormatNotSupported(passFormat);
        t.skipIfTextureFormatNotSupported(bundleFormat);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(passFormat);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(bundleFormat);
        std::vector<WGPUTextureFormat> bundleFormats = {bundleFormat};
        WGPURenderBundle bundle = makeBundle(t, t.device(), bundleFormats);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, passFormat)};
        executeBundleInPass(t, passFormat == bundleFormat, colors, nullptr, bundle);
        wgpuRenderBundleRelease(bundle);
    });

CTS_TEST(g, "render_pass_and_bundle,color_count")
    .desc("Test that the number of color attachments in render passes and bundles must match.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("passCount", colorAttachmentCountValues()).combine("bundleCount", colorAttachmentCountValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t passCount = static_cast<uint32_t>(t.param<uint64_t>("passCount"));
        const uint32_t bundleCount = static_cast<uint32_t>(t.param<uint64_t>("bundleCount"));
        const WGPULimits limits = t.getLimits();
        if (passCount > limits.maxColorAttachments || bundleCount > limits.maxColorAttachments) t.skip("count > maxColorAttachments");
        std::vector<WGPUTextureFormat> bundleFormats(bundleCount, WGPUTextureFormat_RGBA8Uint);
        WGPURenderBundle bundle = makeBundle(t, t.device(), bundleFormats);
        std::vector<WGPURenderPassColorAttachment> colors;
        for (uint32_t i = 0; i < passCount; ++i) colors.push_back(colorAttachment(t, WGPUTextureFormat_RGBA8Uint));
        executeBundleInPass(t, passCount == bundleCount, colors, nullptr, bundle);
        wgpuRenderBundleRelease(bundle);
    });

CTS_TEST(g, "render_pass_and_bundle,color_sparse")
    .desc("Test that each color attachment in render passes and bundles must match.")
    .params([](ParamsBuilder u) {
        return u.combine("attachmentCount", colorAttachmentCountValues())
            .beginSubcases()
            .expand("iPass", [](const ParamRecord& p) { return patternIndicesForCount(p, "attachmentCount"); })
            .expand("iBundle", [](const ParamRecord& p) { return patternIndicesForCount(p, "attachmentCount"); });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const auto& passPattern = patterns()[static_cast<size_t>(t.param<uint64_t>("iPass"))];
        const auto& bundlePattern = patterns()[static_cast<size_t>(t.param<uint64_t>("iBundle"))];
        if (passPattern.size() > t.getLimits().maxColorAttachments) t.skip("pass count > maxColorAttachments");
        std::vector<std::optional<WGPUTextureFormat>> passFormats;
        std::vector<WGPUTextureFormat> bundleFormats;
        for (bool used : passPattern) passFormats.push_back(used ? std::optional<WGPUTextureFormat>(WGPUTextureFormat_RGBA8Uint) : std::nullopt);
        for (bool used : bundlePattern) bundleFormats.push_back(used ? WGPUTextureFormat_RGBA8Uint : WGPUTextureFormat_Undefined);
        WGPURenderBundle bundle = makeBundle(t, t.device(), bundleFormats);
        std::vector<WGPURenderPassColorAttachment> colors = makeColorAttachments(t, passFormats);
        executeBundleInPass(t, passPattern == bundlePattern, colors, nullptr, bundle);
        wgpuRenderBundleRelease(bundle);
    });

CTS_TEST(g, "render_pass_and_bundle,depth_format")
    .desc("Test that the depth attachment format in render passes and bundles must match.")
    .params([](ParamsBuilder u) {
        return u.combine("passFormat", depthStencilAttachmentFormatValues())
            .beginSubcases()
            .combine("bundleFormat", depthStencilAttachmentFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat passFormat = t.paramIsUndefined("passFormat") ? WGPUTextureFormat_Undefined : parseTextureFormat(t.param<std::string>("passFormat"));
        const WGPUTextureFormat bundleFormat = t.paramIsUndefined("bundleFormat") ? WGPUTextureFormat_Undefined : parseTextureFormat(t.param<std::string>("bundleFormat"));
        if (passFormat != WGPUTextureFormat_Undefined) t.skipIfTextureFormatNotSupported(passFormat);
        if (bundleFormat != WGPUTextureFormat_Undefined) t.skipIfTextureFormatNotSupported(bundleFormat);
        std::vector<WGPUTextureFormat> bundleFormats = {WGPUTextureFormat_RGBA8Unorm};
        WGPURenderBundle bundle = makeBundle(t, t.device(), bundleFormats, bundleFormat);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, WGPUTextureFormat_RGBA8Unorm)};
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        WGPURenderPassDepthStencilAttachment* dsPtr = nullptr;
        if (passFormat != WGPUTextureFormat_Undefined) {
            ds = depthAttachment(t, passFormat);
            dsPtr = &ds;
        }
        executeBundleInPass(t, passFormat == bundleFormat, colors, dsPtr, bundle);
        wgpuRenderBundleRelease(bundle);
    });

CTS_TEST(g, "render_pass_and_bundle,sample_count")
    .desc("Test that the sample count in render passes and bundles must match.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("renderSampleCount", {1, 4})
            .combine("bundleSampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t renderSampleCount = static_cast<uint32_t>(t.param<int64_t>("renderSampleCount"));
        const uint32_t bundleSampleCount = static_cast<uint32_t>(t.param<int64_t>("bundleSampleCount"));
        std::vector<WGPUTextureFormat> bundleFormats = {WGPUTextureFormat_RGBA8Unorm};
        WGPURenderBundle bundle = makeBundle(t, t.device(), bundleFormats, WGPUTextureFormat_Undefined, bundleSampleCount);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, WGPUTextureFormat_RGBA8Unorm, renderSampleCount)};
        executeBundleInPass(t, renderSampleCount == bundleSampleCount, colors, nullptr, bundle);
        wgpuRenderBundleRelease(bundle);
    });

CTS_TEST(g, "render_pass_and_bundle,device_mismatch")
    .desc("Test that render passes cannot be called with bundles created from another device.")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("mismatched", {true, false}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();
        std::vector<WGPUTextureFormat> bundleFormats = {WGPUTextureFormat_R16Uint};
        WGPURenderBundle bundle = makeBundle(t, sourceDevice, bundleFormats);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, WGPUTextureFormat_R16Uint)};
        executeBundleInPass(t, !mismatched, colors, nullptr, bundle);
        wgpuRenderBundleRelease(bundle);
    });

CTS_TEST(g, "render_pass_or_bundle_and_pipeline,color_format")
    .desc("Test color attachment formats in render passes or bundles match pipeline color format.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {std::string("render pass"), std::string("render bundle")})
            .beginSubcases()
            .combine("encoderFormat", possibleColorRenderableFormatValues())
            .combine("pipelineFormat", possibleColorRenderableFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const WGPUTextureFormat encoderFormat = parseTextureFormat(t.param<std::string>("encoderFormat"));
        const WGPUTextureFormat pipelineFormat = parseTextureFormat(t.param<std::string>("pipelineFormat"));
        t.skipIfTextureFormatNotSupported(encoderFormat);
        t.skipIfTextureFormatNotSupported(pipelineFormat);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(encoderFormat);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(pipelineFormat);
        auto h = makePipeline(t, {pipelineFormat});
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(h.desc);
        std::vector<std::optional<WGPUTextureFormat>> encoderFormats = {encoderFormat};
        std::vector<WGPURenderPassColorAttachment> colors = makeColorAttachments(t, encoderFormats);
        std::vector<WGPUTextureFormat> bundleFormats = concreteFormats(encoderFormats);
        EncoderCase c = makeEncoderCase(t, encoderType, colors, nullptr, bundleFormats, WGPUTextureFormat_Undefined, 1);
        setPipelineAndValidate(t, c, pipeline, encoderFormat == pipelineFormat);
    });

CTS_TEST(g, "render_pass_or_bundle_and_pipeline,color_count")
    .desc("Test number of color attachments in render passes or bundles match pipeline count.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {std::string("render pass"), std::string("render bundle")})
            .beginSubcases()
            .combine("encoderCount", colorAttachmentCountValues())
            .combine("pipelineCount", colorAttachmentCountValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const uint32_t encoderCount = static_cast<uint32_t>(t.param<uint64_t>("encoderCount"));
        const uint32_t pipelineCount = static_cast<uint32_t>(t.param<uint64_t>("pipelineCount"));
        if (encoderCount > t.getLimits().maxColorAttachments || pipelineCount > t.getLimits().maxColorAttachments) t.skip("count > maxColorAttachments");
        std::vector<std::optional<WGPUTextureFormat>> pipelineFormats(pipelineCount, WGPUTextureFormat_RGBA8Uint);
        auto h = makePipeline(t, pipelineFormats);
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(h.desc);
        std::vector<std::optional<WGPUTextureFormat>> encoderFormats(encoderCount, WGPUTextureFormat_RGBA8Uint);
        std::vector<WGPURenderPassColorAttachment> colors = makeColorAttachments(t, encoderFormats);
        std::vector<WGPUTextureFormat> bundleFormats = concreteFormats(encoderFormats);
        EncoderCase c = makeEncoderCase(t, encoderType, colors, nullptr, bundleFormats, WGPUTextureFormat_Undefined, 1);
        setPipelineAndValidate(t, c, pipeline, encoderCount == pipelineCount);
    });

CTS_TEST(g, "render_pass_or_bundle_and_pipeline,color_sparse")
    .desc("Test each color attachment in render passes or bundles matches the pipeline.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {std::string("render pass"), std::string("render bundle")})
            .combine("attachmentCount", colorAttachmentCountValues())
            .beginSubcases()
            .expand("iEncoder", [](const ParamRecord& p) { return patternIndicesForCount(p, "attachmentCount"); })
            .expand("iPipeline", [](const ParamRecord& p) { return patternIndicesForCount(p, "attachmentCount"); });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const auto& encoderPattern = patterns()[static_cast<size_t>(t.param<uint64_t>("iEncoder"))];
        const auto& pipelinePattern = patterns()[static_cast<size_t>(t.param<uint64_t>("iPipeline"))];
        std::vector<std::optional<WGPUTextureFormat>> pipelineFormats;
        std::vector<std::optional<WGPUTextureFormat>> encoderFormats;
        for (bool used : pipelinePattern) pipelineFormats.push_back(used ? std::optional<WGPUTextureFormat>(WGPUTextureFormat_RGBA8Uint) : std::nullopt);
        for (bool used : encoderPattern) encoderFormats.push_back(used ? std::optional<WGPUTextureFormat>(WGPUTextureFormat_RGBA8Uint) : std::nullopt);
        auto h = makePipeline(t, pipelineFormats);
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(h.desc);
        std::vector<WGPURenderPassColorAttachment> colors = makeColorAttachments(t, encoderFormats);
        std::vector<WGPUTextureFormat> bundleFormats = concreteFormats(encoderFormats);
        EncoderCase c = makeEncoderCase(t, encoderType, colors, nullptr, bundleFormats, WGPUTextureFormat_Undefined, 1);
        setPipelineAndValidate(t, c, pipeline, encoderPattern == pipelinePattern);
    });

CTS_TEST(g, "render_pass_or_bundle_and_pipeline,depth_format")
    .desc("Test depth attachment format in render passes or bundles matches pipeline depth format.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {std::string("render pass"), std::string("render bundle")})
            .combine("encoderFormat", depthStencilAttachmentFormatValues())
            .beginSubcases()
            .combine("pipelineFormat", depthStencilAttachmentFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const WGPUTextureFormat encoderFormat = t.paramIsUndefined("encoderFormat") ? WGPUTextureFormat_Undefined : parseTextureFormat(t.param<std::string>("encoderFormat"));
        const WGPUTextureFormat pipelineFormat = t.paramIsUndefined("pipelineFormat") ? WGPUTextureFormat_Undefined : parseTextureFormat(t.param<std::string>("pipelineFormat"));
        if (encoderFormat != WGPUTextureFormat_Undefined) t.skipIfTextureFormatNotSupported(encoderFormat);
        if (pipelineFormat != WGPUTextureFormat_Undefined) t.skipIfTextureFormatNotSupported(pipelineFormat);
        auto h = makePipeline(t, {WGPUTextureFormat_RGBA8Unorm}, pipelineFormat);
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(h.desc);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, WGPUTextureFormat_RGBA8Unorm)};
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        WGPURenderPassDepthStencilAttachment* dsPtr = nullptr;
        if (encoderFormat != WGPUTextureFormat_Undefined) {
            ds = depthAttachment(t, encoderFormat);
            dsPtr = &ds;
        }
        std::vector<WGPUTextureFormat> bundleFormats = {WGPUTextureFormat_RGBA8Unorm};
        EncoderCase c = makeEncoderCase(t, encoderType, colors, dsPtr, bundleFormats, encoderFormat, 1);
        setPipelineAndValidate(t, c, pipeline, encoderFormat == pipelineFormat);
    });

CTS_TEST(g, "render_pass_or_bundle_and_pipeline,depth_stencil_read_only_write_state")
    .desc("Test depth/stencil read-only state compatibility with pipeline write state.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {std::string("render pass"), std::string("render bundle")})
            .combine("format", depthStencilAttachmentFormatValues())
            .beginSubcases()
            .combine("depthReadOnly", {false, true})
            .combine("stencilReadOnly", {false, true})
            .combine("stencilWrites", {false, true})
            .combine("depthWriteEnabled", {false, true})
            .combine("stencilWriteMask", {uint64_t(0), uint64_t(0xffffffffu)})
            .combine("cullMode", {std::string("none"), std::string("front"), std::string("back")})
            .filter([](const ParamRecord& p) {
                const Value* value = findParam(p, "format");
                if (value != nullptr && !std::holds_alternative<Value::Undefined>(value->data())) {
                    const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*value));
                    if (!isDepthFormat(format) && valueAs<bool>(*findParam(p, "depthWriteEnabled"))) return false;
                    if (!isStencilFormat(format) && valueAs<bool>(*findParam(p, "stencilWrites"))) return false;
                }
                return true;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const WGPUTextureFormat format = t.paramIsUndefined("format") ? WGPUTextureFormat_Undefined : parseTextureFormat(t.param<std::string>("format"));
        const bool depthReadOnly = t.param<bool>("depthReadOnly");
        const bool stencilReadOnly = t.param<bool>("stencilReadOnly");
        const bool depthWriteEnabled = t.param<bool>("depthWriteEnabled");
        const bool stencilWrites = t.param<bool>("stencilWrites");
        const uint32_t stencilWriteMask = static_cast<uint32_t>(t.param<uint64_t>("stencilWriteMask"));
        const std::string cullModeName = t.param<std::string>("cullMode");
        if (format != WGPUTextureFormat_Undefined) t.skipIfTextureFormatNotSupported(format);
        WGPUCullMode cullMode = WGPUCullMode_None;
        if (cullModeName == "front") cullMode = WGPUCullMode_Front;
        if (cullModeName == "back") cullMode = WGPUCullMode_Back;
        auto h = makePipeline(t, {WGPUTextureFormat_RGBA8Unorm}, format, 1, depthWriteEnabled, stencilWriteMask, cullMode, stencilWrites);
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(h.desc);
        std::vector<WGPURenderPassColorAttachment> colors = {colorAttachment(t, WGPUTextureFormat_RGBA8Unorm)};
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        WGPURenderPassDepthStencilAttachment* dsPtr = nullptr;
        if (format != WGPUTextureFormat_Undefined) {
            ds = depthAttachment(t, format, 1, depthReadOnly, stencilReadOnly);
            dsPtr = &ds;
        }
        std::vector<WGPUTextureFormat> bundleFormats = {WGPUTextureFormat_RGBA8Unorm};
        EncoderCase c = makeEncoderCase(t, encoderType, colors, dsPtr, bundleFormats, format, 1, depthReadOnly, stencilReadOnly);
        bool writesStencil = false;
        if (format != WGPUTextureFormat_Undefined && stencilWriteMask != 0 && stencilWrites) {
            writesStencil = true;
        }
        bool valid = true;
        if (format != WGPUTextureFormat_Undefined && depthWriteEnabled) valid = valid && !depthReadOnly;
        if (writesStencil) valid = valid && !stencilReadOnly;
        setPipelineAndValidate(t, c, pipeline, valid);
    });

CTS_TEST(g, "render_pass_or_bundle_and_pipeline,sample_count")
    .desc("Test sample count in render passes or bundles matches pipeline sample count.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {std::string("render pass"), std::string("render bundle")})
            .combine("attachmentType", {std::string("color"), std::string("depthstencil")})
            .beginSubcases()
            .combine("encoderSampleCount", {1, 4})
            .combine("pipelineSampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string attachmentType = t.param<std::string>("attachmentType");
        const uint32_t encoderSamples = static_cast<uint32_t>(t.param<int64_t>("encoderSampleCount"));
        const uint32_t pipelineSamples = static_cast<uint32_t>(t.param<int64_t>("pipelineSampleCount"));
        const bool useColor = attachmentType == "color";
        std::vector<std::optional<WGPUTextureFormat>> pipelineFormats;
        if (useColor) pipelineFormats.push_back(WGPUTextureFormat_RGBA8Unorm);
        const WGPUTextureFormat dsFormat = useColor ? WGPUTextureFormat_Undefined : WGPUTextureFormat_Depth24PlusStencil8;
        auto h = makePipeline(t, pipelineFormats, dsFormat, pipelineSamples);
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(h.desc);
        std::vector<WGPURenderPassColorAttachment> colors;
        if (useColor) colors.push_back(colorAttachment(t, WGPUTextureFormat_RGBA8Unorm, encoderSamples));
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        WGPURenderPassDepthStencilAttachment* dsPtr = nullptr;
        if (!useColor) {
            ds = depthAttachment(t, WGPUTextureFormat_Depth24PlusStencil8, encoderSamples);
            dsPtr = &ds;
        }
        std::vector<WGPUTextureFormat> bundleFormats;
        if (useColor) bundleFormats.push_back(WGPUTextureFormat_RGBA8Unorm);
        EncoderCase c = makeEncoderCase(t, encoderType, colors, dsPtr, bundleFormats, dsFormat, encoderSamples);
        setPipelineAndValidate(t, c, pipeline, encoderSamples == pipelineSamples);
    });

} // namespace
