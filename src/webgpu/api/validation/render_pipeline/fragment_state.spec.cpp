// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/fragment_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <cstdlib>
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

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,fragment_state",
    R"(
This test dedicatedly tests validation of GPUFragmentState of createRenderPipeline.

TODO(#3363): Make this into a MaxLimitTest and increase kMaxColorAttachments.
)");

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

constexpr uint32_t kDefaultMaxColorAttachments = 8;

static constexpr const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static constexpr const char* kDefaultFragmentShaderCode =
    "@fragment fn main() -> @location(0) vec4<f32>  {\n"
    "  return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

struct ColorRenderInfo {
    WGPUTextureFormat format;
    uint32_t byteCost;
    uint32_t alignment;
    uint32_t componentCount;
    const char* sampleType;
};

static constexpr std::array<ColorRenderInfo, 42> kColorRenderInfoTable = {{
    {WGPUTextureFormat_R8Unorm, 1, 1, 1, "float"},
    {WGPUTextureFormat_R8Uint, 1, 1, 1, "uint"},
    {WGPUTextureFormat_R8Sint, 1, 1, 1, "sint"},
    {WGPUTextureFormat_RG8Unorm, 2, 1, 2, "float"},
    {WGPUTextureFormat_RG8Uint, 2, 1, 2, "uint"},
    {WGPUTextureFormat_RG8Sint, 2, 1, 2, "sint"},
    {WGPUTextureFormat_RGBA8Unorm, 8, 1, 4, "float"},
    {WGPUTextureFormat_RGBA8UnormSrgb, 8, 1, 4, "float"},
    {WGPUTextureFormat_RGBA8Uint, 4, 1, 4, "uint"},
    {WGPUTextureFormat_RGBA8Sint, 4, 1, 4, "sint"},
    {WGPUTextureFormat_BGRA8Unorm, 8, 1, 4, "float"},
    {WGPUTextureFormat_BGRA8UnormSrgb, 8, 1, 4, "float"},
    {WGPUTextureFormat_R16Unorm, 2, 2, 1, "float"},
    {WGPUTextureFormat_R16Snorm, 2, 2, 1, "float"},
    {WGPUTextureFormat_R16Uint, 2, 2, 1, "uint"},
    {WGPUTextureFormat_R16Sint, 2, 2, 1, "sint"},
    {WGPUTextureFormat_R16Float, 2, 2, 1, "float"},
    {WGPUTextureFormat_RG16Unorm, 4, 2, 2, "float"},
    {WGPUTextureFormat_RG16Snorm, 4, 2, 2, "float"},
    {WGPUTextureFormat_RG16Uint, 4, 2, 2, "uint"},
    {WGPUTextureFormat_RG16Sint, 4, 2, 2, "sint"},
    {WGPUTextureFormat_RG16Float, 4, 2, 2, "float"},
    {WGPUTextureFormat_RGBA16Unorm, 8, 4, 4, "float"},
    {WGPUTextureFormat_RGBA16Snorm, 8, 2, 4, "float"},
    {WGPUTextureFormat_RGBA16Uint, 8, 2, 4, "uint"},
    {WGPUTextureFormat_RGBA16Sint, 8, 2, 4, "sint"},
    {WGPUTextureFormat_RGBA16Float, 8, 2, 4, "float"},
    {WGPUTextureFormat_R32Uint, 4, 4, 1, "uint"},
    {WGPUTextureFormat_R32Sint, 4, 4, 1, "sint"},
    {WGPUTextureFormat_R32Float, 4, 4, 1, "float"},
    {WGPUTextureFormat_RG32Uint, 8, 4, 2, "uint"},
    {WGPUTextureFormat_RG32Sint, 8, 4, 2, "sint"},
    {WGPUTextureFormat_RG32Float, 8, 4, 2, "float"},
    {WGPUTextureFormat_RGBA32Uint, 16, 4, 4, "uint"},
    {WGPUTextureFormat_RGBA32Sint, 16, 4, 4, "sint"},
    {WGPUTextureFormat_RGBA32Float, 16, 4, 4, "float"},
    {WGPUTextureFormat_RGB10A2Uint, 8, 4, 4, "uint"},
    {WGPUTextureFormat_RGB10A2Unorm, 8, 4, 4, "float"},
    {WGPUTextureFormat_RG11B10Ufloat, 8, 4, 3, "float"},
    {WGPUTextureFormat_R8Snorm, 1, 1, 1, "float"},
    {WGPUTextureFormat_RG8Snorm, 2, 1, 2, "float"},
    {WGPUTextureFormat_RGBA8Snorm, 4, 1, 4, "float"},
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

const ColorRenderInfo* colorRenderInfo(WGPUTextureFormat format) {
    for (const ColorRenderInfo& info : kColorRenderInfoTable) {
        if (info.format == format) return &info;
    }
    return nullptr;
}

uint32_t getColorRenderByteCost(WGPUTextureFormat format) {
    const ColorRenderInfo* info = colorRenderInfo(format);
    return info == nullptr ? 0 : info->byteCost;
}

uint32_t computeBytesPerSampleFromFormats(const std::vector<WGPUTextureFormat>& formats) {
    uint32_t bytesPerSample = 0;
    for (WGPUTextureFormat format : formats) {
        const ColorRenderInfo* info = colorRenderInfo(format);
        if (info == nullptr) continue;
        const uint32_t alignment = info->alignment;
        bytesPerSample = (bytesPerSample + alignment - 1u) & ~(alignment - 1u);
        bytesPerSample += info->byteCost;
    }
    return bytesPerSample;
}

std::vector<Value> allTextureFormatValues() {
    return formatIdentifierValues(kAllTextureFormats);
}

std::vector<Value> colorTextureFormatValues() {
    return formatIdentifierValues(kColorTextureFormats);
}

std::vector<Value> possibleColorRenderableFormatValues() {
    return formatIdentifierValues(kPossibleColorRenderableTextureFormats);
}

std::vector<Value> allTextureFormatAndUndefinedValues() {
    std::vector<Value> values;
    values.push_back(Value::undef());
    const std::vector<Value> formats = possibleColorRenderableFormatValues();
    values.insert(values.end(), formats.begin(), formats.end());
    return values;
}

struct BlendFactorInfo {
    const char* name;
    WGPUBlendFactor value;
};

static constexpr std::array<BlendFactorInfo, 17> kBlendFactors = {{
    {"zero", WGPUBlendFactor_Zero},
    {"one", WGPUBlendFactor_One},
    {"src", WGPUBlendFactor_Src},
    {"one-minus-src", WGPUBlendFactor_OneMinusSrc},
    {"src-alpha", WGPUBlendFactor_SrcAlpha},
    {"one-minus-src-alpha", WGPUBlendFactor_OneMinusSrcAlpha},
    {"dst", WGPUBlendFactor_Dst},
    {"one-minus-dst", WGPUBlendFactor_OneMinusDst},
    {"dst-alpha", WGPUBlendFactor_DstAlpha},
    {"one-minus-dst-alpha", WGPUBlendFactor_OneMinusDstAlpha},
    {"src-alpha-saturated", WGPUBlendFactor_SrcAlphaSaturated},
    {"constant", WGPUBlendFactor_Constant},
    {"one-minus-constant", WGPUBlendFactor_OneMinusConstant},
    {"src1", WGPUBlendFactor_Src1},
    {"one-minus-src1", WGPUBlendFactor_OneMinusSrc1},
    {"src1-alpha", WGPUBlendFactor_Src1Alpha},
    {"one-minus-src1-alpha", WGPUBlendFactor_OneMinusSrc1Alpha},
}};

std::vector<Value> blendFactorValues() {
    std::vector<Value> values;
    values.reserve(kBlendFactors.size());
    for (const BlendFactorInfo& info : kBlendFactors) values.emplace_back(std::string(info.name));
    return values;
}

WGPUBlendFactor parseBlendFactor(std::string_view name) {
    for (const BlendFactorInfo& info : kBlendFactors) {
        if (name == info.name) return info.value;
    }
    std::abort();
}

bool isDualSourceBlendFactor(WGPUBlendFactor factor) {
    return factor == WGPUBlendFactor_Src1 || factor == WGPUBlendFactor_OneMinusSrc1 ||
        factor == WGPUBlendFactor_Src1Alpha || factor == WGPUBlendFactor_OneMinusSrc1Alpha;
}

bool is32FloatFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_R32Float || format == WGPUTextureFormat_RG32Float ||
        format == WGPUTextureFormat_RGBA32Float;
}

bool isTextureFormatBlendable(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    if (!t.isTextureFormatColorRenderable(format)) return false;
    if (format == WGPUTextureFormat_RG11B10Ufloat) {
        return wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable) != WGPU_FALSE;
    }
    if (is32FloatFormat(format)) {
        return wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Float32Blendable) != WGPU_FALSE;
    }
    const ColorRenderInfo* info = colorRenderInfo(format);
    return info != nullptr && std::string_view(info->sampleType) == "float";
}

bool blendFactorNameReadsSrcAlpha(std::string_view name) {
    return name.find("src-alpha") != std::string_view::npos ||
        name.find("src1-alpha") != std::string_view::npos;
}

WGPUBlendOperation parseBlendOperation(std::string_view name) {
    if (name == "add") return WGPUBlendOperation_Add;
    if (name == "subtract") return WGPUBlendOperation_Subtract;
    if (name == "reverse-subtract") return WGPUBlendOperation_ReverseSubtract;
    if (name == "min") return WGPUBlendOperation_Min;
    if (name == "max") return WGPUBlendOperation_Max;
    std::abort();
}

std::vector<Value> blendOperationValues() {
    return {std::string("add"), std::string("subtract"), std::string("reverse-subtract"),
            std::string("min"), std::string("max")};
}

const char* plainTypeForSampleType(std::string_view sampleType) {
    if (sampleType == "sint") return "i32";
    if (sampleType == "uint") return "u32";
    return "f32";
}

std::string wgslValue(const char* plainType, uint32_t index) {
    static constexpr std::array<const char*, 4> f32 = {"0.0000", "1.0000", "0.0000", "1.0000"};
    static constexpr std::array<const char*, 4> u32 = {"0u", "1u", "0u", "1u"};
    static constexpr std::array<const char*, 4> i32 = {"0", "1", "0", "1"};
    if (std::string_view(plainType) == "u32") return u32[index];
    if (std::string_view(plainType) == "i32") return i32[index];
    return f32[index];
}

std::string outputType(const char* plainType, uint32_t componentCount) {
    if (componentCount == 1) return std::string(plainType);
    return "vec" + std::to_string(componentCount) + "<" + plainType + ">";
}

std::string outputExpression(const char* plainType, uint32_t componentCount) {
    const std::string type = outputType(plainType, componentCount);
    if (componentCount == 1) return wgslValue(plainType, 0);
    std::string expr = type + "(";
    for (uint32_t i = 0; i < componentCount; ++i) {
        if (i != 0) expr += ", ";
        expr += wgslValue(plainType, i);
    }
    expr += ")";
    return expr;
}

std::string fragmentShaderWithOutput(
    const char* plainType,
    uint32_t componentCount,
    bool dualSourceBlending = false) {
    const std::string type = outputType(plainType, componentCount);
    const std::string expr = outputExpression(plainType, componentCount);
    if (dualSourceBlending) {
        return std::string("enable dual_source_blending;\n"
                           "struct Outputs {\n"
                           "  @location(0) @blend_src(0) o0 : ") +
            type + ",\n"
            "  @location(0) @blend_src(1) o0_blend : " + type + ",\n"
            "}\n"
            "@fragment fn main() -> Outputs {\n"
            "  return Outputs(" +
            expr + ", " + expr + ");\n"
            "}\n";
    }
    return std::string("struct Outputs {\n"
                       "  @location(0) o0 : ") +
        type + ",\n"
        "}\n"
        "@fragment fn main() -> Outputs {\n"
        "  return Outputs(" +
        expr + ");\n"
        "}\n";
}

std::string fragmentShaderWithMaybeOutput(std::optional<std::pair<std::string, uint32_t>> output) {
    if (!output.has_value()) {
        return "@fragment fn main() {}\n";
    }
    return fragmentShaderWithOutput(output->first.c_str(), output->second);
}

const char* plainTypeForFormat(WGPUTextureFormat format) {
    const ColorRenderInfo* info = colorRenderInfo(format);
    if (info != nullptr) return plainTypeForSampleType(info->sampleType);

    const std::string_view identifier = textureFormatIdentifier(format);
    if (identifier.find("uint") != std::string_view::npos) return "u32";
    if (identifier.find("sint") != std::string_view::npos) return "i32";
    return "f32";
}

std::string fragmentShaderForTargets(const std::vector<WGPUColorTargetState>& targets) {
    if (targets.empty()) return kDefaultFragmentShaderCode;

    std::string fields;
    std::string values;
    for (size_t i = 0; i < targets.size(); ++i) {
        const char* plainType = plainTypeForFormat(targets[i].format);
        fields += "  @location(" + std::to_string(i) + ") o" + std::to_string(i) +
            " : vec4<" + plainType + ">,\n";
        if (!values.empty()) values += ", ";
        values += outputExpression(plainType, 4);
    }

    return "struct Outputs {\n" + fields +
        "}\n"
        "@fragment fn main() -> Outputs {\n"
        "  return Outputs(" +
        values + ");\n"
        "}\n";
}

struct PipelineHolder {
    std::string vertexCode = kDefaultVertexShaderCode;
    std::string fragmentCode = kDefaultFragmentShaderCode;
    WGPUShaderModule vertexModule = nullptr;
    WGPUShaderModule fragmentModule = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    std::vector<WGPUBlendState> blends;
    std::vector<WGPUColorTargetState> targets;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

PipelineHolder makePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    std::vector<WGPUColorTargetState> targets,
    std::optional<std::string> fragmentCode = std::nullopt,
    bool withDepthStencil = false) {
    PipelineHolder h;
    h.targets = std::move(targets);
    h.fragmentCode = fragmentCode.has_value() ? std::move(*fragmentCode) : fragmentShaderForTargets(h.targets);
    h.vertexModule = t.createShaderModuleTracked(h.vertexCode);
    h.fragmentModule = t.createShaderModuleTracked(h.fragmentCode);
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    h.pipelineLayout = t.createPipelineLayoutTracked(layoutDesc);
    h.fragment = WGPU_FRAGMENT_STATE_INIT;
    h.fragment.module = h.fragmentModule;
    h.fragment.entryPoint = sv("main");
    h.fragment.targetCount = h.targets.size();
    h.fragment.targets = h.targets.empty() ? nullptr : h.targets.data();
    h.desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout = h.pipelineLayout;
    h.desc.vertex.module = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.fragment = &h.fragment;
    if (withDepthStencil) {
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = WGPUTextureFormat_Depth24Plus;
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;
        h.desc.depthStencil = &h.depthStencil;
    }
    return h;
}

void doCreateRenderPipelineTest(AllFeaturesMaxLimitsGpuTest& t, bool success, PipelineHolder& h) {
    // isAsync=true uses this same synchronous create path; native validation is eager.
    t.expectValidationError([&] { t.createRenderPipelineTracked(h.desc); }, !success);
}

WGPUColorTargetState colorTarget(
    WGPUTextureFormat format,
    WGPUColorWriteMask writeMask = WGPUColorWriteMask_All,
    const WGPUBlendState* blend = nullptr) {
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    target.writeMask = writeMask;
    target.blend = blend;
    return target;
}

void skipIfFormatUnsupported(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    if (info.hasRequiredFeature &&
        wgpuDeviceHasFeature(t.device(), info.requiredFeature) == WGPU_FALSE) {
        t.skip("texture format required feature is not available");
    }
}

CTS_TEST(g, "color_target_exists")
    .desc("Tests creating a complete render pipeline requires at least one color target state.")
    .params([](ParamsBuilder u) { return u.combine("isAsync", {false, true}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto good = makePipeline(t, {colorTarget(WGPUTextureFormat_RGBA8Unorm)});
        doCreateRenderPipelineTest(t, true, good);
        auto bad = makePipeline(t, {});
        doCreateRenderPipelineTest(t, false, bad);
    });

CTS_TEST(g, "targets_format_is_color_format")
    .desc("Tests that color target state format must be a color format, regardless of how the fragment shader writes to it.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormatValues())
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                return format == WGPUTextureFormat_RGBA8Unorm || !isColorTextureFormat(format);
            })
            .combine("isAsync", {false, true})
            .beginSubcases()
            .combine("fragOutType", {std::string("f32"), std::string("u32"), std::string("i32")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string fragOutType = t.param<std::string>("fragOutType");
        skipIfFormatUnsupported(t, format);
        auto h = makePipeline(t, {colorTarget(format)}, fragmentShaderWithOutput(fragOutType.c_str(), 4));
        doCreateRenderPipelineTest(t, format == WGPUTextureFormat_RGBA8Unorm && fragOutType == "f32", h);
    });

CTS_TEST(g, "targets_format_renderable")
    .desc("Tests that color target state format must have RENDER_ATTACHMENT capability (tests only color formats).")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true}).combine("format", colorTextureFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        skipIfFormatUnsupported(t, format);
        auto h = makePipeline(t, {colorTarget(format)});
        doCreateRenderPipelineTest(t, t.isTextureFormatColorRenderable(format), h);
    });

CTS_TEST(g, "limits,maxColorAttachments")
    .desc("Tests that color state targets length must not be larger than device.limits.maxColorAttachments.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combineWithParams({ParamRecord{{"targetsLengthVariant_mult", int64_t(1)}, {"targetsLengthVariant_add", int64_t(0)}},
                                ParamRecord{{"targetsLengthVariant_mult", int64_t(1)}, {"targetsLengthVariant_add", int64_t(1)}}});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t count = limits.maxColorAttachments *
            static_cast<uint32_t>(t.param<int64_t>("targetsLengthVariant_mult")) +
            static_cast<uint32_t>(t.param<int64_t>("targetsLengthVariant_add"));
        std::vector<WGPUColorTargetState> targets(count);
        for (WGPUColorTargetState& target : targets) {
            target = colorTarget(WGPUTextureFormat_RG8Unorm, WGPUColorWriteMask_None);
        }
        auto h = makePipeline(t, std::move(targets), kDefaultFragmentShaderCode, true);
        doCreateRenderPipelineTest(t, count <= limits.maxColorAttachments, h);
    });

CTS_TEST(g, "limits,maxColorAttachmentBytesPerSample,aligned")
    .desc("Tests that the total color attachment bytes per sample must not be larger than maxColorAttachmentBytesPerSample when using the same format for multiple attachments.")
    .params([](ParamsBuilder u) {
        std::vector<Value> attachmentCounts;
        for (uint32_t i = 1; i <= kDefaultMaxColorAttachments; ++i) attachmentCounts.emplace_back(uint64_t(i));
        return u.combine("format", possibleColorRenderableFormatValues())
            .beginSubcases()
            .combine("attachmentCount", attachmentCounts)
            .combine("isAsync", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t attachmentCount = static_cast<uint32_t>(t.param<uint64_t>("attachmentCount"));
        const WGPULimits limits = t.getLimits();
        skipIfFormatUnsupported(t, format);
        if (attachmentCount > limits.maxColorAttachments) t.skip("attachmentCount exceeds maxColorAttachments");
        std::vector<WGPUColorTargetState> targets(attachmentCount);
        for (WGPUColorTargetState& target : targets) {
            target = colorTarget(format, WGPUColorWriteMask_None);
        }
        auto h = makePipeline(t, std::move(targets));
        const bool shouldError = !t.isTextureFormatColorRenderable(format) ||
            getColorRenderByteCost(format) * attachmentCount > limits.maxColorAttachmentBytesPerSample;
        doCreateRenderPipelineTest(t, !shouldError, h);
    });

CTS_TEST(g, "limits,maxColorAttachmentBytesPerSample,unaligned")
    .desc("Tests that the total color attachment bytes per sample must not be larger than maxColorAttachmentBytesPerSample when using various sets of formats.")
    .params([](ParamsBuilder u) {
        return u.combine("formats", {std::string("r8unorm,r32float,rgba8unorm,rgba32float,r8unorm"),
                                     std::string("r32float,rgba8unorm,rgba32float,r8unorm,r8unorm")})
            .beginSubcases()
            .combine("isAsync", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string key = t.param<std::string>("formats");
        std::vector<WGPUTextureFormat> formats;
        if (key[0] == 'r' && key[1] == '8') {
            formats = {WGPUTextureFormat_R8Unorm, WGPUTextureFormat_R32Float, WGPUTextureFormat_RGBA8Unorm,
                       WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_R8Unorm};
        } else {
            formats = {WGPUTextureFormat_R32Float, WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA32Float,
                       WGPUTextureFormat_R8Unorm, WGPUTextureFormat_R8Unorm};
        }
        const WGPULimits limits = t.getLimits();
        if (formats.size() > limits.maxColorAttachments) t.skip("numColorAttachments exceeds maxColorAttachments");
        std::vector<WGPUColorTargetState> targets;
        targets.reserve(formats.size());
        for (WGPUTextureFormat format : formats) targets.push_back(colorTarget(format, WGPUColorWriteMask_None));
        auto h = makePipeline(t, std::move(targets));
        doCreateRenderPipelineTest(t, computeBytesPerSampleFromFormats(formats) <= limits.maxColorAttachmentBytesPerSample, h);
    });

CTS_TEST(g, "targets_format_filterable")
    .desc("Tests that color target state format must be filterable if blend is not undefined.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("format", possibleColorRenderableFormatValues())
            .beginSubcases()
            .combine("hasBlend", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool hasBlend = t.param<bool>("hasBlend");
        skipIfFormatUnsupported(t, format);
        if (!t.isTextureFormatColorRenderable(format)) t.skip("texture format is not renderable");
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        auto h = makePipeline(t, {colorTarget(format, WGPUColorWriteMask_All, hasBlend ? &blend : nullptr)});
        doCreateRenderPipelineTest(t, !hasBlend || isTextureFormatBlendable(t, format), h);
    });

CTS_TEST(g, "targets_blend")
    .desc("Tests if the combination of blend srcFactor, dstFactor and operation is valid.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("component", {std::string("color"), std::string("alpha")})
            .combine("srcFactor", blendFactorValues())
            .combine("dstFactor", blendFactorValues())
            .beginSubcases()
            .combine("operation", blendOperationValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string component = t.param<std::string>("component");
        const std::string srcName = t.param<std::string>("srcFactor");
        const std::string dstName = t.param<std::string>("dstFactor");
        const std::string opName = t.param<std::string>("operation");
        const WGPUBlendFactor src = parseBlendFactor(srcName);
        const WGPUBlendFactor dst = parseBlendFactor(dstName);
        if ((isDualSourceBlendFactor(src) || isDualSourceBlendFactor(dst)) &&
            wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DualSourceBlending) == WGPU_FALSE) {
            t.skip("dual-source-blending feature is not available on this device");
        }
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_DstAlpha;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha = blend.color;
        WGPUBlendComponent* selected = component == "color" ? &blend.color : &blend.alpha;
        selected->srcFactor = src;
        selected->dstFactor = dst;
        selected->operation = parseBlendOperation(opName);
        const bool dual = isDualSourceBlendFactor(src) || isDualSourceBlendFactor(dst);
        auto h = makePipeline(t, {colorTarget(WGPUTextureFormat_RGBA8Unorm, WGPUColorWriteMask_All, &blend)},
                              fragmentShaderWithOutput("f32", 4, dual));
        const bool success = (opName != "min" && opName != "max") ||
            (src == WGPUBlendFactor_One && dst == WGPUBlendFactor_One);
        doCreateRenderPipelineTest(t, success, h);
    });

CTS_TEST(g, "targets_write_mask")
    .desc("Tests that color target state write mask must be < 16.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("writeMask", {Value(uint64_t(0)), Value(uint64_t(0xf)), Value(uint64_t(0x10)),
                                   Value(uint64_t(0x80000001u))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t writeMask = t.param<uint64_t>("writeMask");
        auto h = makePipeline(t, {colorTarget(WGPUTextureFormat_RGBA8Unorm, static_cast<WGPUColorWriteMask>(writeMask))});
        doCreateRenderPipelineTest(t, writeMask < 16, h);
    });

CTS_TEST(g, "pipeline_output_targets")
    .desc("Pipeline fragment output types must be compatible with target color state format.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("format", allTextureFormatAndUndefinedValues())
            .beginSubcases()
            .combine("shaderOutput", {Value::undef(), std::string("f32:1"), std::string("f32:2"), std::string("f32:3"),
                                      std::string("f32:4"), std::string("u32:1"), std::string("u32:2"),
                                      std::string("u32:3"), std::string("u32:4"), std::string("i32:1"),
                                      std::string("i32:2"), std::string("i32:3"), std::string("i32:4")})
            .expand("writeMask", [](const ParamRecord& p) {
                if (findParam(p, "format")->data().index() != 3 && findParam(p, "shaderOutput")->data().index() != 3) {
                    return std::vector<Value>{uint64_t(0), uint64_t(1), uint64_t(2), uint64_t(4), uint64_t(8)};
                }
                return std::vector<Value>{uint64_t(0xf)};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<WGPUColorTargetState> targets;
        std::optional<WGPUTextureFormat> format;
        if (!t.paramIsUndefined("format")) {
            format = parseTextureFormat(t.param<std::string>("format"));
            skipIfFormatUnsupported(t, *format);
            if (!t.isTextureFormatColorRenderable(*format)) t.skip("texture format is not renderable");
            targets.push_back(colorTarget(*format, static_cast<WGPUColorWriteMask>(t.param<uint64_t>("writeMask"))));
        }
        std::optional<std::pair<std::string, uint32_t>> output;
        if (!t.paramIsUndefined("shaderOutput")) {
            const std::string shaderOutput = t.param<std::string>("shaderOutput");
            output = std::make_pair(shaderOutput.substr(0, 3), static_cast<uint32_t>(shaderOutput[4] - '0'));
        }
        auto h = makePipeline(t, std::move(targets), fragmentShaderWithMaybeOutput(output), true);
        bool success = true;
        if (format.has_value()) {
            if (output.has_value()) {
                const ColorRenderInfo* info = colorRenderInfo(*format);
                success = info != nullptr && output->first == plainTypeForSampleType(info->sampleType) &&
                    output->second >= info->componentCount;
            } else {
                success = t.param<uint64_t>("writeMask") == 0;
            }
        }
        doCreateRenderPipelineTest(t, success, h);
    });

std::vector<ParamRecord> blendSingleFactorParams() {
    std::vector<ParamRecord> records;
    for (const BlendFactorInfo& info : kBlendFactors) {
        records.push_back(ParamRecord{{"colorSrcFactor", std::string(info.name)}});
        records.push_back(ParamRecord{{"colorDstFactor", std::string(info.name)}});
        records.push_back(ParamRecord{{"alphaSrcFactor", std::string(info.name)}});
        records.push_back(ParamRecord{{"alphaDstFactor", std::string(info.name)}});
    }
    return records;
}

std::string factorOrDefault(AllFeaturesMaxLimitsGpuTest& t, const char* key, const char* defaultValue) {
    return t.hasParam(key) ? t.param<std::string>(key) : std::string(defaultValue);
}

CTS_TEST(g, "pipeline_output_targets,blend")
    .desc("When blending is enabled and alpha channel is read by a color blend factor, fragment output must be vec4.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("format", {std::string("r8unorm"), std::string("rg8unorm"), std::string("rgba8unorm"), std::string("bgra8unorm")})
            .combine("componentCount", {1, 2, 3, 4})
            .combineWithParams(blendSingleFactorParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t componentCount = static_cast<uint32_t>(t.param<int>("componentCount"));
        skipIfFormatUnsupported(t, format);
        const std::string csrc = factorOrDefault(t, "colorSrcFactor", "one");
        const std::string cdst = factorOrDefault(t, "colorDstFactor", "zero");
        const std::string asrc = factorOrDefault(t, "alphaSrcFactor", "one");
        const std::string adst = factorOrDefault(t, "alphaDstFactor", "zero");
        const WGPUBlendFactor csrcFactor = parseBlendFactor(csrc);
        const WGPUBlendFactor cdstFactor = parseBlendFactor(cdst);
        const WGPUBlendFactor asrcFactor = parseBlendFactor(asrc);
        const WGPUBlendFactor adstFactor = parseBlendFactor(adst);
        const bool dual = isDualSourceBlendFactor(csrcFactor) || isDualSourceBlendFactor(cdstFactor) ||
            isDualSourceBlendFactor(asrcFactor) || isDualSourceBlendFactor(adstFactor);
        if (dual && wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DualSourceBlending) == WGPU_FALSE) {
            t.skip("dual-source-blending feature is not available on this device");
        }
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = csrcFactor;
        blend.color.dstFactor = cdstFactor;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = asrcFactor;
        blend.alpha.dstFactor = adstFactor;
        blend.alpha.operation = WGPUBlendOperation_Add;
        auto h = makePipeline(t, {colorTarget(format, WGPUColorWriteMask_All, &blend)},
                              fragmentShaderWithOutput("f32", componentCount, dual));
        const ColorRenderInfo* info = colorRenderInfo(format);
        const bool readsSrcAlpha = blendFactorNameReadsSrcAlpha(csrc) || blendFactorNameReadsSrcAlpha(cdst);
        const bool success = info != nullptr && std::string_view(info->sampleType) == "float" &&
            componentCount >= info->componentCount && (!readsSrcAlpha || componentCount == 4);
        doCreateRenderPipelineTest(t, success, h);
    });

std::vector<Value> dualSourceFactorValues() {
    return {std::string("src1"), std::string("one-minus-src1"), std::string("src1-alpha"),
            std::string("one-minus-src1-alpha")};
}

CTS_TEST(g, "dual_source_blending,color_target_count")
    .desc("Test that when the blend factor of color attachment 0 uses src1 there must be exactly one color target.")
    .params([](ParamsBuilder u) {
        return u.combine("blendFactor", dualSourceFactorValues())
            .combine("colorTargetsCount", {1, 2})
            .combine("maskOutNonZeroIndexColorTargets", {true, false})
            .beginSubcases()
            .combine("component", {std::string("color"), std::string("alpha")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DualSourceBlending) == WGPU_FALSE) {
            t.skip("dual-source-blending feature is not available on this device");
        }
        const uint32_t count = static_cast<uint32_t>(t.param<int>("colorTargetsCount"));
        const bool mask = t.param<bool>("maskOutNonZeroIndexColorTargets");
        const std::string component = t.param<std::string>("component");
        const WGPUBlendFactor factor = parseBlendFactor(t.param<std::string>("blendFactor"));
        WGPUBlendState defaultBlend = WGPU_BLEND_STATE_INIT;
        defaultBlend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        defaultBlend.color.dstFactor = WGPUBlendFactor_DstAlpha;
        defaultBlend.color.operation = WGPUBlendOperation_Add;
        defaultBlend.alpha = defaultBlend.color;
        WGPUBlendState testBlend = defaultBlend;
        WGPUBlendComponent* selected = component == "color" ? &testBlend.color : &testBlend.alpha;
        selected->srcFactor = factor;
        selected->dstFactor = factor;
        std::vector<WGPUBlendState> blends(count, defaultBlend);
        blends[0] = testBlend;
        std::vector<WGPUColorTargetState> targets;
        for (uint32_t i = 0; i < count; ++i) {
            targets.push_back(colorTarget(WGPUTextureFormat_RGBA8Unorm,
                                          i != 0 && mask ? WGPUColorWriteMask_None : WGPUColorWriteMask_All,
                                          &blends[i]));
        }
        auto h = makePipeline(t, std::move(targets), fragmentShaderWithOutput("f32", 4, true));
        doCreateRenderPipelineTest(t, count == 1, h);
    });

CTS_TEST(g, "dual_source_blending,use_blend_src")
    .desc("Test that src1 blend factors require dual source blending to be used in the fragment shader.")
    .params([](ParamsBuilder u) {
        return u.combine("blendFactor", blendFactorValues())
            .combine("useBlendSrc1", {true, false})
            .combine("writeMask", {uint64_t(0), uint64_t(WGPUColorWriteMask_All)})
            .beginSubcases()
            .combine("component", {std::string("color"), std::string("alpha")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DualSourceBlending) == WGPU_FALSE) {
            t.skip("dual-source-blending feature is not available on this device");
        }
        const std::string component = t.param<std::string>("component");
        const WGPUBlendFactor factor = parseBlendFactor(t.param<std::string>("blendFactor"));
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_DstAlpha;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha = blend.color;
        WGPUBlendComponent* selected = component == "color" ? &blend.color : &blend.alpha;
        selected->srcFactor = factor;
        selected->dstFactor = factor;
        const bool useBlendSrc1 = t.param<bool>("useBlendSrc1");
        auto h = makePipeline(t, {colorTarget(WGPUTextureFormat_RGBA8Unorm,
                                              static_cast<WGPUColorWriteMask>(t.param<uint64_t>("writeMask")), &blend)},
                              fragmentShaderWithOutput("f32", 4, useBlendSrc1));
        doCreateRenderPipelineTest(t, !isDualSourceBlendFactor(factor) || useBlendSrc1, h);
    });

} // namespace
