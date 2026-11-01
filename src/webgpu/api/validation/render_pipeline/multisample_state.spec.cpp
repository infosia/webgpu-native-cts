// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/multisample_state.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.

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
    "api,validation,render_pipeline,multisample_state",
    "Validation of GPUMultisampleState of createRenderPipeline.");

// Returns a WGPUStringView from a null-terminated C string.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// Default vertex shader (kDefaultVertexShaderCode equivalent).
static const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Default fragment shader (kDefaultFragmentShaderCode equivalent).
static const char* kDefaultFragmentShaderCode =
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

// Fragment shader with @builtin(sample_mask) output (verbatim from upstream spec).
static const char* kSampleMaskFragmentShaderCode =
    "      struct Output {\n"
    "        @builtin(sample_mask) mask_out: u32,\n"
    "        @location(0) color : vec4<f32>,\n"
    "      }\n"
    "      @fragment fn main() -> Output {\n"
    "        var o: Output;\n"
    "        // We need to make sure this sample_mask isn't optimized out even its value equals \"no op\".\n"
    "        o.mask_out = 0xFFFFFFFFu;\n"
    "        o.color = vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
    "        return o;\n"
    "      }\n";

// Build a render pipeline descriptor with the given multisample state and optional fragment shader.
// Mirrors CreateRenderPipelineValidationTest::getDescriptor in the upstream.
void createRenderPipelineForValidation(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t multisampleCount,
    bool alphaToCoverageEnabled,
    const char* fragmentShaderCode) {

    WGPUShaderModule vertModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(fragmentShaderCode);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts = nullptr;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(plDesc);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = sv("main");
    desc.multisample.count = multisampleCount;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.multisample.alphaToCoverageEnabled = alphaToCoverageEnabled ? WGPU_TRUE : WGPU_FALSE;
    desc.fragment = &fragment;

    t.createRenderPipelineTracked(desc);
}

// getFragmentShaderCodeWithOutput() for a single output at `location`, whose type follows the
// sample type of `format` (the values do not matter to these validation tests).
std::string fragmentShaderCodeForFormat(WGPUTextureFormat format, uint32_t location = 0) {
    const std::string_view identifier = textureFormatIdentifier(format);
    const char* type = identifier.find("uint") != std::string_view::npos   ? "u32"
                       : identifier.find("sint") != std::string_view::npos ? "i32"
                                                                           : "f32";
    const std::string outputType = std::string("vec4<") + type + ">";
    return "@fragment fn main() -> @location(" + std::to_string(location) + ") " + outputType + " {\n"
           "  return " + outputType + "(0, 1, 0, 1);\n"
           "}\n";
}

// The parts of getDescriptor() that the alpha_to_coverage,* tests vary.
struct AlphaToCoverageDescriptor {
    bool alphaToCoverageEnabled = false;
    bool noFragment = false;
    // Index of the only non-null color target.
    uint32_t targetIndex = 0;
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Undefined;
};

void doAlphaToCoverageRenderPipelineTest(AllFeaturesMaxLimitsGpuTest& t,
                                         const AlphaToCoverageDescriptor& options,
                                         bool success) {
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = t.createPipelineLayoutTracked(WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT);
    desc.vertex.module = t.createShaderModuleTracked(kDefaultVertexShaderCode);
    desc.vertex.entryPoint = sv("main");
    desc.multisample.count = 4;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.multisample.alphaToCoverageEnabled = options.alphaToCoverageEnabled ? WGPU_TRUE : WGPU_FALSE;

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (options.depthFormat != WGPUTextureFormat_Undefined) {
        depthStencil.format = options.depthFormat;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        // depthCompare stays undefined as in upstream. The stencil members are the WebIDL defaults
        // of GPUStencilFaceState: the C INIT leaves the operations at Undefined, which Dawn does not
        // treat as 'keep' when it decides whether depthCompare is required.
        WGPUStencilFaceState stencilFace = WGPU_STENCIL_FACE_STATE_INIT;
        stencilFace.compare = WGPUCompareFunction_Always;
        stencilFace.failOp = WGPUStencilOperation_Keep;
        stencilFace.depthFailOp = WGPUStencilOperation_Keep;
        stencilFace.passOp = WGPUStencilOperation_Keep;
        depthStencil.stencilFront = stencilFace;
        depthStencil.stencilBack = stencilFace;
        desc.depthStencil = &depthStencil;
    }

    // The target before `targetIndex` stays a hole (`targets[targetIndex] = ...` in upstream).
    std::vector<WGPUColorTargetState> targets(options.targetIndex + 1, WGPU_COLOR_TARGET_STATE_INIT);
    targets[options.targetIndex].format = options.format;
    targets[options.targetIndex].writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    if (!options.noFragment) {
        fragment.module = t.createShaderModuleTracked(
            fragmentShaderCodeForFormat(options.format, options.targetIndex));
        fragment.entryPoint = sv("main");
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();
        desc.fragment = &fragment;
    }

    t.expectValidationError([&] {
        t.createRenderPipelineTracked(desc);
    }, !success);
}

// ---------------------------------------------------------------------------
// test: count
// Validates that multisample.count must be either 1 or 4.
// ---------------------------------------------------------------------------
CTS_TEST(g, "count")
    .desc("If multisample.count must either be 1 or 4.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("count", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(8), Value(16), Value(1024)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const uint32_t count = static_cast<uint32_t>(t.param<int>("count"));

        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)isAsync;

        const bool success = (count == 1 || count == 4);

        t.expectValidationError([&] {
            createRenderPipelineForValidation(t, count, false, kDefaultFragmentShaderCode);
        }, !success);
    });

// ---------------------------------------------------------------------------
// test: alpha_to_coverage,count
// Validates that when alphaToCoverageEnabled=true, count must be 4 (not 1).
// ---------------------------------------------------------------------------
CTS_TEST(g, "alpha_to_coverage,count")
    .desc(
        "If multisample.alphaToCoverageEnabled is true, multisample.count must be greater than 1, "
        "e.g. it can only be 4.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("alphaToCoverageEnabled", {Value(false), Value(true)})
            .beginSubcases()
            .combine("count", {Value(1), Value(4)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const bool alphaToCoverageEnabled = t.param<bool>("alphaToCoverageEnabled");
        const uint32_t count = static_cast<uint32_t>(t.param<int>("count"));

        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)isAsync;

        const bool success = alphaToCoverageEnabled
            ? (count == 4)
            : (count == 1 || count == 4);

        t.expectValidationError([&] {
            createRenderPipelineForValidation(t, count, alphaToCoverageEnabled, kDefaultFragmentShaderCode);
        }, !success);
    });

// ---------------------------------------------------------------------------
// test: alpha_to_coverage,fragment_stage_required
// ---------------------------------------------------------------------------
CTS_TEST(g, "alpha_to_coverage,fragment_stage_required")
    .desc("If multisample.alphaToCoverageEnabled is true, a fragment stage must be provided.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("alphaToCoverageEnabled", {Value(false), Value(true)})
            .beginSubcases()
            .combine("noFragment", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)t.param<bool>("isAsync");

        AlphaToCoverageDescriptor options;
        options.alphaToCoverageEnabled = t.param<bool>("alphaToCoverageEnabled");
        options.noFragment = t.param<bool>("noFragment");
        options.depthFormat = WGPUTextureFormat_Depth32Float;

        const bool success = !(options.alphaToCoverageEnabled && options.noFragment);
        doAlphaToCoverageRenderPipelineTest(t, options, success);
    });

// ---------------------------------------------------------------------------
// test: alpha_to_coverage,first_target_required
// ---------------------------------------------------------------------------
CTS_TEST(g, "alpha_to_coverage,first_target_required")
    .desc("If multisample.alphaToCoverageEnabled is true, a fragment.targets[0] must exist.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("alphaToCoverageEnabled", {Value(false), Value(true)})
            .beginSubcases()
            .combine("targetIndex", {Value(0), Value(1)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)t.param<bool>("isAsync");

        AlphaToCoverageDescriptor options;
        options.alphaToCoverageEnabled = t.param<bool>("alphaToCoverageEnabled");
        options.targetIndex = static_cast<uint32_t>(t.param<int>("targetIndex"));

        const bool success = !(options.alphaToCoverageEnabled && options.targetIndex == 1);
        doAlphaToCoverageRenderPipelineTest(t, options, success);
    });

// ---------------------------------------------------------------------------
// test: alpha_to_coverage,first_format_blendable_and_has_alpha
// ---------------------------------------------------------------------------
CTS_TEST(g, "alpha_to_coverage,first_format_blendable_and_has_alpha")
    .desc("If multisample.alphaToCoverageEnabled is true, a fragment.targets[0].format must be blendable.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("alphaToCoverageEnabled", {Value(false), Value(true)})
            .beginSubcases()
            .combine("format", formatIdentifierValues(kColorRenderableTextureFormats));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)t.param<bool>("isAsync");

        AlphaToCoverageDescriptor options;
        options.alphaToCoverageEnabled = t.param<bool>("alphaToCoverageEnabled");
        options.format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(options.format);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(options.format);

        const bool blendableWithAlpha =
            t.isTextureFormatBlendable(options.format) && isColorTextureFormatWithAlpha(options.format);
        const bool success = !options.alphaToCoverageEnabled || blendableWithAlpha;
        doAlphaToCoverageRenderPipelineTest(t, options, success);
    });

// ---------------------------------------------------------------------------
// test: alpha_to_coverage,sample_mask
// Validates that if sample_mask builtin is a pipeline output of fragment,
// multisample.alphaToCoverageEnabled must be false.
// ---------------------------------------------------------------------------
CTS_TEST(g, "alpha_to_coverage,sample_mask")
    .desc(
        "If sample_mask builtin is a pipeline output of fragment, "
        "multisample.alphaToCoverageEnabled should be false.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("alphaToCoverageEnabled", {Value(false), Value(true)})
            .beginSubcases()
            .combine("hasSampleMaskOutput", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const bool alphaToCoverageEnabled = t.param<bool>("alphaToCoverageEnabled");
        const bool hasSampleMaskOutput = t.param<bool>("hasSampleMaskOutput");

        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)isAsync;

        // The C++ harness never runs in compatibility mode, so the upstream
        // "t.isCompatibility && hasSampleMaskOutput -> skip" guard is omitted.

        const char* fragmentShaderCode = hasSampleMaskOutput
            ? kSampleMaskFragmentShaderCode
            : kDefaultFragmentShaderCode;

        // Validation rule: using @builtin(sample_mask) output together with
        // alphaToCoverageEnabled=true is invalid.
        const bool success = !hasSampleMaskOutput || !alphaToCoverageEnabled;

        t.expectValidationError([&] {
            createRenderPipelineForValidation(t, 4u, alphaToCoverageEnabled, fragmentShaderCode);
        }, !success);
    });

} // namespace
