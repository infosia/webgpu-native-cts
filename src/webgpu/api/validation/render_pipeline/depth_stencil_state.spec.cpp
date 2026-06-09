// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/depth_stencil_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,depth_stencil_state",
    "This test dedicatedly tests validation of GPUDepthStencilState of createRenderPipeline.");

// Returns a WGPUStringView from a null-terminated C string.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// Default vertex shader (kDefaultVertexShaderCode from upstream).
static const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Default fragment shader for rgba8unorm target.
// Mirrors getFragmentShaderCodeWithOutput([{values:[0,1,0,1], plainType:'f32', componentCount:4}]).
static const char* kDefaultFragmentShaderCode =
    "\n"
    "    \n"
    "\n"
    "    struct Outputs {\n"
    "      @location(0) o0 : vec4<f32>,\n"
    "    }\n"
    "\n"
    "    @fragment fn main() -> Outputs {\n"
    "        return Outputs(vec4<f32>(0.0000, 1.0000, 0.0000, 1.0000));\n"
    "    }\n";

// Fragment shader with frag_depth output.
// Mirrors getFragmentShaderCodeWithOutput(
//   [{values:[1,1,1,1], plainType:'f32', componentCount:4}],
//   {value:0.5}
// )
static const char* kFragDepthShaderCode =
    "\n"
    "    \n"
    "\n"
    "    struct Outputs {\n"
    "      @builtin(frag_depth) depth_out: f32,\n"
    "@location(0) o0 : vec4<f32>,\n"
    "    }\n"
    "\n"
    "    @fragment fn main() -> Outputs {\n"
    "        return Outputs(0.5000,vec4<f32>(1.0000, 1.0000, 1.0000, 1.0000));\n"
    "    }\n";

// -----------------------------------------------------------------------
// Helpers for compare function string <-> WGPU enum mapping.
// kCompareFunctions = ['never','less','equal','less-equal','greater','not-equal','greater-equal','always']
// -----------------------------------------------------------------------
static const char* kCompareFunctionStrings[] = {
    "never",
    "less",
    "equal",
    "less-equal",
    "greater",
    "not-equal",
    "greater-equal",
    "always",
};
static const WGPUCompareFunction kCompareFunctionEnums[] = {
    WGPUCompareFunction_Never,
    WGPUCompareFunction_Less,
    WGPUCompareFunction_Equal,
    WGPUCompareFunction_LessEqual,
    WGPUCompareFunction_Greater,
    WGPUCompareFunction_NotEqual,
    WGPUCompareFunction_GreaterEqual,
    WGPUCompareFunction_Always,
};
static constexpr int kCompareFunctionCount = 8;

WGPUCompareFunction parseCF(const std::string& s) {
    for (int i = 0; i < kCompareFunctionCount; ++i) {
        if (s == kCompareFunctionStrings[i]) return kCompareFunctionEnums[i];
    }
    return WGPUCompareFunction_Undefined;
}

std::vector<Value> compareFunctionValues() {
    std::vector<Value> v;
    v.reserve(kCompareFunctionCount);
    for (int i = 0; i < kCompareFunctionCount; ++i) {
        v.emplace_back(std::string(kCompareFunctionStrings[i]));
    }
    return v;
}

// compare functions including "undefined" sentinel at front (for stencil_test test)
std::vector<Value> compareFunctionValuesWithUndefined() {
    std::vector<Value> v;
    v.reserve(kCompareFunctionCount + 1);
    v.push_back(Value::undef());
    for (int i = 0; i < kCompareFunctionCount; ++i) {
        v.emplace_back(std::string(kCompareFunctionStrings[i]));
    }
    return v;
}

// -----------------------------------------------------------------------
// Helpers for stencil operation string <-> WGPU enum mapping.
// kStencilOperations = ['keep','zero','replace','invert','increment-clamp',
//                       'decrement-clamp','increment-wrap','decrement-wrap']
// -----------------------------------------------------------------------
static const char* kStencilOperationStrings[] = {
    "keep",
    "zero",
    "replace",
    "invert",
    "increment-clamp",
    "decrement-clamp",
    "increment-wrap",
    "decrement-wrap",
};
static const WGPUStencilOperation kStencilOperationEnums[] = {
    WGPUStencilOperation_Keep,
    WGPUStencilOperation_Zero,
    WGPUStencilOperation_Replace,
    WGPUStencilOperation_Invert,
    WGPUStencilOperation_IncrementClamp,
    WGPUStencilOperation_DecrementClamp,
    WGPUStencilOperation_IncrementWrap,
    WGPUStencilOperation_DecrementWrap,
};
static constexpr int kStencilOperationCount = 8;

WGPUStencilOperation parseSO(const std::string& s) {
    for (int i = 0; i < kStencilOperationCount; ++i) {
        if (s == kStencilOperationStrings[i]) return kStencilOperationEnums[i];
    }
    return WGPUStencilOperation_Undefined;
}

// stencil operations including undefined sentinel at front (for stencil_write test)
std::vector<Value> stencilOperationValuesWithUndefined() {
    std::vector<Value> v;
    v.reserve(kStencilOperationCount + 1);
    v.push_back(Value::undef());
    for (int i = 0; i < kStencilOperationCount; ++i) {
        v.emplace_back(std::string(kStencilOperationStrings[i]));
    }
    return v;
}

// -----------------------------------------------------------------------
// kPrimitiveTopology = ['point-list','line-list','line-strip','triangle-list','triangle-strip']
// -----------------------------------------------------------------------
static const char* kPrimitiveTopologyStrings[] = {
    "point-list",
    "line-list",
    "line-strip",
    "triangle-list",
    "triangle-strip",
};
static const WGPUPrimitiveTopology kPrimitiveTopologyEnums[] = {
    WGPUPrimitiveTopology_PointList,
    WGPUPrimitiveTopology_LineList,
    WGPUPrimitiveTopology_LineStrip,
    WGPUPrimitiveTopology_TriangleList,
    WGPUPrimitiveTopology_TriangleStrip,
};
static constexpr int kPrimitiveTopologyCount = 5;

WGPUPrimitiveTopology parsePT(const std::string& s) {
    for (int i = 0; i < kPrimitiveTopologyCount; ++i) {
        if (s == kPrimitiveTopologyStrings[i]) return kPrimitiveTopologyEnums[i];
    }
    return WGPUPrimitiveTopology_Undefined;
}

std::vector<Value> primitiveTopologyValues() {
    std::vector<Value> v;
    v.reserve(kPrimitiveTopologyCount);
    for (int i = 0; i < kPrimitiveTopologyCount; ++i) {
        v.emplace_back(std::string(kPrimitiveTopologyStrings[i]));
    }
    return v;
}

// -----------------------------------------------------------------------
// Descriptor holder — keeps all sub-objects alive for pipeline creation.
// -----------------------------------------------------------------------
struct DescHolder {
    WGPUShaderModule        vertexModule     = nullptr;
    WGPUShaderModule        fragmentModule   = nullptr;
    WGPUColorTargetState    colorTarget      = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState       fragment         = WGPU_FRAGMENT_STATE_INIT;
    WGPUPipelineLayout      pipelineLayout   = nullptr;
    WGPUDepthStencilState   depthStencil     = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPURenderPipelineDescriptor desc        = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;

    // Whether desc.depthStencil points into depthStencil above.
    bool hasDepthStencil = false;
};

// Build the base descriptor (vertex + fragment + pipeline layout + one color target).
// Mirrors CreateRenderPipelineValidationTest::getDescriptor from common.ts.
//
// colorTargetFormat:  format for the color target (WGPUTextureFormat_Undefined = no color target)
// fragmentCode:       WGSL for the fragment shader
DescHolder buildBaseDescriptor(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat colorTargetFormat,
    const char* fragmentCode)
{
    DescHolder h;

    h.vertexModule   = t.createShaderModuleTracked(kDefaultVertexShaderCode);
    h.fragmentModule = t.createShaderModuleTracked(fragmentCode);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts     = nullptr;
    h.pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    h.colorTarget        = WGPU_COLOR_TARGET_STATE_INIT;
    h.colorTarget.format = colorTargetFormat;

    h.fragment             = WGPU_FRAGMENT_STATE_INIT;
    h.fragment.module      = h.fragmentModule;
    h.fragment.entryPoint  = sv("main");
    h.fragment.targetCount = (colorTargetFormat != WGPUTextureFormat_Undefined) ? 1 : 0;
    h.fragment.targets     = (colorTargetFormat != WGPUTextureFormat_Undefined) ? &h.colorTarget : nullptr;

    h.desc                   = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout            = h.pipelineLayout;
    h.desc.vertex.module     = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.fragment          = &h.fragment;
    h.desc.depthStencil      = nullptr;

    return h;
}

void doCreateRenderPipelineTest(AllFeaturesMaxLimitsGpuTest& t, bool shouldError, DescHolder& h) {
    t.expectValidationError([&] {
        t.createRenderPipelineTracked(h.desc);
    }, shouldError);
}

// -----------------------------------------------------------------------
// Format value lists
// -----------------------------------------------------------------------
std::vector<Value> allTextureFormatValues() {
    return formatIdentifierValues(kAllTextureFormats);
}

std::vector<Value> depthStencilFormatValues() {
    return formatIdentifierValues(kDepthStencilFormats);
}

// Depth stencil formats + undefined sentinel.
std::vector<Value> depthStencilFormatValuesWithUndefined() {
    std::vector<Value> v;
    v.reserve(kDepthStencilFormats.size() + 1);
    v.push_back(Value::undef());
    for (WGPUTextureFormat fmt : kDepthStencilFormats) {
        v.emplace_back(std::string(textureFormatIdentifier(fmt)));
    }
    return v;
}

// -----------------------------------------------------------------------
// test: format
// The texture format in depthStencilState must be a depth/stencil format.
// -----------------------------------------------------------------------
CTS_TEST(g, "format")
    .desc("The texture format in depthStencilState must be a depth/stencil format.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("format",  allTextureFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil        = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        // depthWriteEnabled: false  -> WGPUOptionalBool_False
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        // depthCompare: 'always'
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        const bool shouldError = !isDepthOrStencilTextureFormat(format);
        doCreateRenderPipelineTest(t, shouldError, h);
    });

// -----------------------------------------------------------------------
// test: depthCompare_optional
// The depthCompare in depthStencilState is optional for stencil-only formats
// but required for formats with a depth if depthCompare is used for anything.
// -----------------------------------------------------------------------
CTS_TEST(g, "depthCompare_optional")
    .desc(
        "The depthCompare in depthStencilState is optional for stencil-only formats but\n"
        "required for formats with a depth if depthCompare is not used for anything.")
    .params([](ParamsBuilder u) {
        // depthWriteEnabled: [false, true, undefined]  encoded as:
        //   "false"     -> false
        //   "true"      -> true
        //   "undefined" -> undef
        // depthCompare: ['always', undefined]  encoded similarly
        // stencilFront/BackDepthFailOp: ['keep', 'zero']
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("format",  depthStencilFormatValues())
            .beginSubcases()
            .combine("depthCompare", {Value(std::string("always")), Value::undef()})
            .combine("depthWriteEnabled", {Value(false), Value(true), Value::undef()})
            .combine("stencilFrontDepthFailOp", {Value(std::string("keep")), Value(std::string("zero"))})
            .combine("stencilBackDepthFailOp",  {Value(std::string("keep")), Value(std::string("zero"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        // depthCompare: 'always' or undefined
        const bool depthCompareIsUndefined = t.paramIsUndefined("depthCompare");
        const WGPUCompareFunction depthCompare =
            depthCompareIsUndefined ? WGPUCompareFunction_Undefined : WGPUCompareFunction_Always;

        // depthWriteEnabled: false, true, or undefined
        const bool depthWriteEnabledIsUndefined = t.paramIsUndefined("depthWriteEnabled");
        bool depthWriteEnabled = false;
        WGPUOptionalBool depthWriteEnabledOB = WGPUOptionalBool_Undefined;
        if (!depthWriteEnabledIsUndefined) {
            depthWriteEnabled = t.param<bool>("depthWriteEnabled");
            depthWriteEnabledOB = depthWriteEnabled ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        }

        const std::string sfStr = t.param<std::string>("stencilFrontDepthFailOp");
        const std::string sbStr = t.param<std::string>("stencilBackDepthFailOp");
        const WGPUStencilOperation stencilFrontDepthFailOp = parseSO(sfStr);
        const WGPUStencilOperation stencilBackDepthFailOp  = parseSO(sbStr);

        // Build descriptor
        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        h.depthStencil.depthWriteEnabled = depthWriteEnabledOB;
        h.depthStencil.depthCompare = depthCompare;
        h.depthStencil.stencilFront.depthFailOp = stencilFrontDepthFailOp;
        h.depthStencil.stencilBack.depthFailOp  = stencilBackDepthFailOp;
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        // Replicate upstream success logic:
        // const depthFailOpsAreKeep = stencilFrontDepthFailOp === 'keep' && stencilBackDepthFailOp === 'keep';
        // const stencilStateIsDefault = depthFailOpsAreKeep;
        // let success = true;
        // if (depthWriteEnabled || (depthCompare && depthCompare !== 'always')) {
        //   if (!isDepthTextureFormat(format)) success = false;
        // }
        // if (!stencilStateIsDefault) {
        //   if (!isStencilTextureFormat(format)) success = false;
        // }
        // if (isDepthTextureFormat(format)) {
        //   if (depthWriteEnabled === undefined) success = false;
        //   if (depthWriteEnabled || !depthFailOpsAreKeep) {
        //     if (depthCompare === undefined) success = false;
        //   }
        // }
        const bool depthFailOpsAreKeep =
            (stencilFrontDepthFailOp == WGPUStencilOperation_Keep) &&
            (stencilBackDepthFailOp  == WGPUStencilOperation_Keep);
        const bool stencilStateIsDefault = depthFailOpsAreKeep;
        bool success = true;

        // "depthCompare && depthCompare !== 'always'" — in upstream JS, depthCompare is either
        // 'always' or undefined; 'always' is truthy but equal to 'always' → false. undefined is
        // falsy → false. So this condition is always false for our two values.
        const bool depthCompareActive = (!depthCompareIsUndefined && depthCompare != WGPUCompareFunction_Always);
        if (depthWriteEnabled || depthCompareActive) {
            if (!isDepthTextureFormat(format)) success = false;
        }
        if (!stencilStateIsDefault) {
            if (!isStencilTextureFormat(format)) success = false;
        }
        if (isDepthTextureFormat(format)) {
            if (depthWriteEnabledIsUndefined) success = false;
            if (depthWriteEnabled || !depthFailOpsAreKeep) {
                if (depthCompareIsUndefined) success = false;
            }
        }

        doCreateRenderPipelineTest(t, !success, h);
    });

// -----------------------------------------------------------------------
// test: depthWriteEnabled_optional
// The depthWriteEnabled in depthStencilState is optional for stencil-only
// formats but required for formats with a depth.
// -----------------------------------------------------------------------
CTS_TEST(g, "depthWriteEnabled_optional")
    .desc(
        "The depthWriteEnabled in depthStencilState is optional for stencil-only formats "
        "but required for formats with a depth.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("format",  depthStencilFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;
        // depthWriteEnabled: undefined (WGPUOptionalBool_Undefined — default from INIT macro)
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_Undefined;
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        // success = !isDepthTextureFormat(format)
        const bool shouldError = isDepthTextureFormat(format);
        doCreateRenderPipelineTest(t, shouldError, h);
    });

// -----------------------------------------------------------------------
// test: depth_test
// Depth aspect must be contained in the format if depth test is enabled.
// -----------------------------------------------------------------------
CTS_TEST(g, "depth_test")
    .desc(
        "Depth aspect must be contained in the format if depth test is enabled "
        "in depthStencilState.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",       {Value(false), Value(true)})
            .combine("format",        depthStencilFormatValues())
            .combine("depthCompare",  compareFunctionValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        const WGPUCompareFunction depthCompare = parseCF(t.param<std::string>("depthCompare"));

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        h.depthStencil.depthCompare = depthCompare;
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        // depthTestEnabled = depthCompare !== undefined && depthCompare !== 'always'
        // (depthCompare is always defined here; check against 'always')
        const bool depthTestEnabled = (depthCompare != WGPUCompareFunction_Always);
        const bool success = !depthTestEnabled || isDepthTextureFormat(format);
        doCreateRenderPipelineTest(t, !success, h);
    });

// -----------------------------------------------------------------------
// test: depth_write
// Depth aspect must be contained in the format if depth write is enabled.
// -----------------------------------------------------------------------
CTS_TEST(g, "depth_write")
    .desc(
        "Depth aspect must be contained in the format if depth write is enabled "
        "in depthStencilState.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",           {Value(false), Value(true)})
            .combine("format",            depthStencilFormatValues())
            .combine("depthWriteEnabled", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        const bool depthWriteEnabled = t.param<bool>("depthWriteEnabled");

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        h.depthStencil.depthWriteEnabled =
            depthWriteEnabled ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        const bool success = !depthWriteEnabled || isDepthTextureFormat(format);
        doCreateRenderPipelineTest(t, !success, h);
    });

// -----------------------------------------------------------------------
// test: depth_write,frag_depth
// Depth aspect must be contained in the format if frag_depth is written.
// -----------------------------------------------------------------------
CTS_TEST(g, "depth_write,frag_depth")
    .desc(
        "Depth aspect must be contained in the format if frag_depth is written in "
        "fragment stage.")
    .params([](ParamsBuilder u) {
        // format: undefined + kDepthStencilFormats
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("format",  depthStencilFormatValuesWithUndefined());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const bool formatIsUndefined = t.paramIsUndefined("format");
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        if (!formatIsUndefined) {
            format = parseTextureFormat(t.param<std::string>("format"));
            t.skipIfTextureFormatNotSupported(format);
        }

        // Fragment shader writes @builtin(frag_depth).
        // The descriptor keeps one color target (rgba8unorm) to remain valid without depth stencil.
        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kFragDepthShaderCode);

        if (!formatIsUndefined) {
            h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
            h.depthStencil.format = format;
            h.depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
            h.depthStencil.depthCompare = WGPUCompareFunction_Always;
            h.desc.depthStencil = &h.depthStencil;
            h.hasDepthStencil = true;
        }

        // hasDepth = format ? isDepthTextureFormat(format) : false
        const bool hasDepth = !formatIsUndefined && isDepthTextureFormat(format);
        // success = hasDepth
        doCreateRenderPipelineTest(t, !hasDepth, h);
    });

// -----------------------------------------------------------------------
// test: depth_bias
// Depth bias parameters are only valid with triangle topologies.
// -----------------------------------------------------------------------
CTS_TEST(g, "depth_bias")
    .desc("Depth bias parameters are only valid with triangle topologies.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",   {Value(false), Value(true)})
            .combine("topology",  primitiveTopologyValues())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{},
                ParamRecord{{"depthBias",           Value(-1)}},
                ParamRecord{{"depthBias",           Value(0)}},
                ParamRecord{{"depthBias",           Value(1)}},
                ParamRecord{{"depthBiasSlopeScale", Value(-1.0)}},
                ParamRecord{{"depthBiasSlopeScale", Value(0.0)}},
                ParamRecord{{"depthBiasSlopeScale", Value(1.0)}},
                ParamRecord{{"depthBiasClamp",      Value(-1.0)}},
                ParamRecord{{"depthBiasClamp",      Value(0.0)}},
                ParamRecord{{"depthBiasClamp",      Value(1.0)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUPrimitiveTopology topology = parsePT(t.param<std::string>("topology"));

        // Get optional bias params (missing means "not set" / default 0)
        const bool hasDepthBiasParam      = t.hasParam("depthBias");
        const bool hasDepthBiasSSParam     = t.hasParam("depthBiasSlopeScale");
        const bool hasDepthBiasClampParam  = t.hasParam("depthBiasClamp");

        int32_t depthBias = 0;
        float   depthBiasSlopeScale = 0.0f;
        float   depthBiasClamp = 0.0f;

        if (hasDepthBiasParam)     depthBias = static_cast<int32_t>(t.param<int>("depthBias"));
        if (hasDepthBiasSSParam)   depthBiasSlopeScale = static_cast<float>(t.param<double>("depthBiasSlopeScale"));
        if (hasDepthBiasClampParam) depthBiasClamp = static_cast<float>(t.param<double>("depthBiasClamp"));

        // Upstream: if (t.isCompatibility && !!depthBiasClamp) skip(...)
        // C++ harness is never in compatibility mode — omit skip.

        const bool isTriangleTopology =
            topology == WGPUPrimitiveTopology_TriangleList ||
            topology == WGPUPrimitiveTopology_TriangleStrip;

        // hasDepthBias = !!depthBias || !!depthBiasSlopeScale || !!depthBiasClamp
        const bool hasDepthBias = (depthBias != 0) || (depthBiasSlopeScale != 0.0f) || (depthBiasClamp != 0.0f);
        const bool shouldSucceed = !hasDepthBias || isTriangleTopology;

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);

        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = WGPUTextureFormat_Depth24Plus;
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
        h.depthStencil.depthCompare = WGPUCompareFunction_LessEqual;
        h.depthStencil.depthBias = depthBias;
        h.depthStencil.depthBiasSlopeScale = depthBiasSlopeScale;
        h.depthStencil.depthBiasClamp = depthBiasClamp;
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        h.desc.primitive.topology = topology;

        doCreateRenderPipelineTest(t, !shouldSucceed, h);
    });

// -----------------------------------------------------------------------
// test: stencil_test
// Stencil aspect must be contained in the format if stencil test is enabled.
// -----------------------------------------------------------------------
CTS_TEST(g, "stencil_test")
    .desc(
        "Stencil aspect must be contained in the format if stencil test is enabled "
        "in depthStencilState.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",  {Value(false), Value(true)})
            .combine("format",   depthStencilFormatValues())
            .combine("face",     {Value(std::string("front")), Value(std::string("back"))})
            .combine("compare",  compareFunctionValuesWithUndefined());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        const std::string face = t.param<std::string>("face");
        const bool compareIsUndefined = t.paramIsUndefined("compare");
        WGPUCompareFunction compare = WGPUCompareFunction_Undefined;
        if (!compareIsUndefined) {
            compare = parseCF(t.param<std::string>("compare"));
        }

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;

        if (face == "front") {
            h.depthStencil.stencilFront.compare = compare;
        } else {
            h.depthStencil.stencilBack.compare = compare;
        }
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        // stencilTestEnabled = compare !== undefined && compare !== 'always'
        const bool stencilTestEnabled =
            !compareIsUndefined && (compare != WGPUCompareFunction_Always);
        const bool success = !stencilTestEnabled || isStencilTextureFormat(format);
        doCreateRenderPipelineTest(t, !success, h);
    });

// -----------------------------------------------------------------------
// test: stencil_write
// Stencil aspect must be contained in the format if stencil write is enabled.
// -----------------------------------------------------------------------
CTS_TEST(g, "stencil_write")
    .desc(
        "Stencil aspect must be contained in the format if stencil write is enabled "
        "in depthStencilState.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",      {Value(false), Value(true)})
            .combine("format",       depthStencilFormatValues())
            .combine("faceAndOpType", {
                Value(std::string("frontFailOp")),
                Value(std::string("frontDepthFailOp")),
                Value(std::string("frontPassOp")),
                Value(std::string("backFailOp")),
                Value(std::string("backDepthFailOp")),
                Value(std::string("backPassOp")),
            })
            .combine("op", stencilOperationValuesWithUndefined());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        (void)isAsync;

        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);

        const std::string faceAndOpType = t.param<std::string>("faceAndOpType");
        const bool opIsUndefined = t.paramIsUndefined("op");
        WGPUStencilOperation op = WGPUStencilOperation_Undefined;
        if (!opIsUndefined) {
            op = parseSO(t.param<std::string>("op"));
        }

        auto h = buildBaseDescriptor(t, WGPUTextureFormat_RGBA8Unorm, kDefaultFragmentShaderCode);
        h.depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format = format;
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        h.depthStencil.depthCompare = WGPUCompareFunction_Always;

        if (faceAndOpType == "frontFailOp") {
            h.depthStencil.stencilFront.failOp = op;
        } else if (faceAndOpType == "frontDepthFailOp") {
            h.depthStencil.stencilFront.depthFailOp = op;
        } else if (faceAndOpType == "frontPassOp") {
            h.depthStencil.stencilFront.passOp = op;
        } else if (faceAndOpType == "backFailOp") {
            h.depthStencil.stencilBack.failOp = op;
        } else if (faceAndOpType == "backDepthFailOp") {
            h.depthStencil.stencilBack.depthFailOp = op;
        } else { // backPassOp
            h.depthStencil.stencilBack.passOp = op;
        }
        h.desc.depthStencil = &h.depthStencil;
        h.hasDepthStencil = true;

        // stencilWriteEnabled = op !== undefined && op !== 'keep'
        const bool stencilWriteEnabled =
            !opIsUndefined && (op != WGPUStencilOperation_Keep);
        const bool success = !stencilWriteEnabled || isStencilTextureFormat(format);
        doCreateRenderPipelineTest(t, !success, h);
    });

} // namespace
