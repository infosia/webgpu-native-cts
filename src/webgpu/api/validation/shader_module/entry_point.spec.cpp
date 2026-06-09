// Ported from gpuweb/cts src/webgpu/api/validation/shader_module/entry_point.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: isAsync=true sub-cases are exercised via the same synchronous pipeline-creation path;
// the harness has no async pipeline-creation wrapper. Validation behaviour is identical.

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,shader_module,entry_point",
    "This tests entry point validation of compute/render pipelines and their shader modules.");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from a std::string
// ---------------------------------------------------------------------------
WGPUStringView sv(const std::string& s) {
    return WGPUStringView{s.c_str(), s.size()};
}

// ---------------------------------------------------------------------------
// Mirrors getShaderWithEntryPoint() from webgpu/util/shader.ts
// ---------------------------------------------------------------------------
std::string getShaderWithEntryPoint(const std::string& stage, const std::string& entryPoint) {
    if (stage == "compute") {
        return "@compute @workgroup_size(1) fn " + entryPoint + "() {}";
    }
    if (stage == "vertex") {
        return std::string("\n      @vertex fn ") + entryPoint +
               "() -> @builtin(position) vec4<f32> {\n"
               "        return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
               "      }";
    }
    if (stage == "fragment") {
        return std::string("\n      @fragment fn ") + entryPoint +
               "() -> @location(0) vec4<f32> {\n"
               "        return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
               "      }";
    }
    return "";
}

// ---------------------------------------------------------------------------
// kDefaultVertexShaderCode — mirrors webgpu/util/shader.ts
// ---------------------------------------------------------------------------
static constexpr const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------
// kEntryPointTestCases — mirrors the upstream kEntryPointTestCases array.
//
// Each entry has:
//   shaderModuleEntryPoint — the identifier used in the WGSL source
//   stageEntryPoint        — the identifier passed as entryPoint in the descriptor
//
// Note: some stageEntryPoint strings contain embedded null bytes ('\0'). These
// are represented here as std::string literals with explicit length using the
// string_literal helper so the null bytes are preserved.
// ---------------------------------------------------------------------------
struct EntryPointTestCase {
    std::string shaderModuleEntryPoint;
    std::string stageEntryPoint;
};

// Helper to build a std::string that may contain embedded NUL bytes.
// We use initializer_list<char> to ensure the compiler never truncates at '\0'.
static std::string makeStr(std::initializer_list<char> chars) {
    return std::string(chars.begin(), chars.end());
}

static const std::vector<EntryPointTestCase> kEntryPointTestCases = {
    // { shaderModuleEntryPoint, stageEntryPoint }
    {"main",       "main"},
    {"main",       ""},
    {"main",       makeStr({'m','a','i','n','\0'})},          // "main\0"
    {"main",       makeStr({'m','a','i','n','\0','a'})},      // "main\0a"
    {"main",       "mian"},
    {"main",       "main "},
    {"main",       "ma in"},
    {"main",       makeStr({'m','a','i','n','\n'})},           // "main\n"
    {"mian",       "mian"},
    {"mian",       "main"},
    {"mainmain",   "mainmain"},
    {"mainmain",   "foo"},
    {"main_t12V3", "main_t12V3"},
    {"main_t12V3", "main_t12V5"},
    {"main_t12V3", "_main_t12V3"},
    // Unicode: "séquençage" (NFC) vs "séquençage" (NFD-like decomposed)
    // Both entries from the upstream kEntryPointTestCases are reproduced here.
    // Line 41 upstream: shaderModule=séquençage, stage=séquençage (same, NFC)
    // Line 42 upstream: shaderModule=séquençage, stage=séquençage (same string repeated — identical)
    {"séquençage", "séquençage"},
    {"séquençage", "séquençage"},
};

// ---------------------------------------------------------------------------
// Params helpers: combineWithParams-equivalent for kEntryPointTestCases
// ---------------------------------------------------------------------------
std::vector<Value> entryPointTestCaseIndexValues() {
    std::vector<Value> values;
    values.reserve(kEntryPointTestCases.size());
    for (size_t i = 0; i < kEntryPointTestCases.size(); ++i) {
        values.emplace_back(static_cast<int64_t>(i));
    }
    return values;
}

// ---------------------------------------------------------------------------
// Pipeline creation helpers that mirror vtu.doCreateComputePipelineTest /
// vtu.doCreateRenderPipelineTest (synchronous path only — isAsync is ignored
// since the C harness has no async pipeline-creation wrapper).
// ---------------------------------------------------------------------------

void doCreateComputePipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool /* isAsync */,
    bool success,
    WGPUComputePipelineDescriptor& desc)
{
    t.expectValidationError([&] {
        t.createComputePipelineTracked(desc);
    }, !success);
}

void doCreateRenderPipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool /* isAsync */,
    bool success,
    WGPURenderPipelineDescriptor& desc,
    WGPUFragmentState* fragment = nullptr)
{
    // desc.fragment must already be set by the caller (via pointer to a local FragmentState).
    (void)fragment; // suppress unused-param warning
    t.expectValidationError([&] {
        t.createRenderPipelineTracked(desc);
    }, !success);
}

// ---------------------------------------------------------------------------
// test: compute
// ---------------------------------------------------------------------------
CTS_TEST(g, "compute")
    .desc(R"(
Tests calling createComputePipeline(Async) with valid compute stage shader and different entryPoints,
and check that the APIs only accept matching entryPoint.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("shaderModuleStage", {Value(std::string("compute")), Value(std::string("vertex")), Value(std::string("fragment"))})
            .beginSubcases()
            .combine("provideEntryPoint", {Value(false), Value(true)})
            .combine("extraEntryPoint", {Value(false), Value(true)})
            .combine("caseIndex", entryPointTestCaseIndexValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync              = t.param<bool>("isAsync");
        const bool provideEntryPoint    = t.param<bool>("provideEntryPoint");
        const bool extraEntryPoint      = t.param<bool>("extraEntryPoint");
        const std::string shaderModuleStage = t.param<std::string>("shaderModuleStage");
        const int caseIndex             = t.param<int>("caseIndex");
        const EntryPointTestCase& tc    = kEntryPointTestCases[static_cast<size_t>(caseIndex)];
        const std::string& shaderModuleEntryPoint = tc.shaderModuleEntryPoint;
        const std::string& stageEntryPoint        = tc.stageEntryPoint;

        std::string code = getShaderWithEntryPoint(shaderModuleStage, shaderModuleEntryPoint);
        if (extraEntryPoint) {
            code += " " + getShaderWithEntryPoint(shaderModuleStage, "extra");
        }

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = nullptr; // 'auto'
        desc.compute.module = shaderModule;
        if (provideEntryPoint) {
            desc.compute.entryPoint = sv(stageEntryPoint);
        }
        // If !provideEntryPoint, leave entryPoint as {nullptr,0} — auto-detect

        bool success = true;
        if (shaderModuleStage != "compute") {
            success = false;
        }
        if (!provideEntryPoint && extraEntryPoint) {
            success = false;
        }
        if (shaderModuleEntryPoint != stageEntryPoint && provideEntryPoint) {
            success = false;
        }

        doCreateComputePipelineTest(t, isAsync, success, desc);
    });

// ---------------------------------------------------------------------------
// test: vertex
// ---------------------------------------------------------------------------
CTS_TEST(g, "vertex")
    .desc(R"(
Tests calling createRenderPipeline(Async) with valid vertex stage shader and different entryPoints,
and check that the APIs only accept matching entryPoint.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("shaderModuleStage", {Value(std::string("compute")), Value(std::string("vertex")), Value(std::string("fragment"))})
            .beginSubcases()
            .combine("provideEntryPoint", {Value(false), Value(true)})
            .combine("extraEntryPoint", {Value(false), Value(true)})
            .combine("caseIndex", entryPointTestCaseIndexValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync              = t.param<bool>("isAsync");
        const bool provideEntryPoint    = t.param<bool>("provideEntryPoint");
        const bool extraEntryPoint      = t.param<bool>("extraEntryPoint");
        const std::string shaderModuleStage = t.param<std::string>("shaderModuleStage");
        const int caseIndex             = t.param<int>("caseIndex");
        const EntryPointTestCase& tc    = kEntryPointTestCases[static_cast<size_t>(caseIndex)];
        const std::string& shaderModuleEntryPoint = tc.shaderModuleEntryPoint;
        const std::string& stageEntryPoint        = tc.stageEntryPoint;

        std::string code = getShaderWithEntryPoint(shaderModuleStage, shaderModuleEntryPoint);
        if (extraEntryPoint) {
            code += " " + getShaderWithEntryPoint(shaderModuleStage, "extra");
        }

        WGPUShaderModule vertShaderModule = t.createShaderModuleTracked(code);

        // depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'always' }
        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format             = WGPUTextureFormat_Depth32Float;
        depthStencil.depthWriteEnabled  = WGPUOptionalBool_True;
        depthStencil.depthCompare       = WGPUCompareFunction_Always;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                 = nullptr; // 'auto'
        desc.vertex.module          = vertShaderModule;
        if (provideEntryPoint) {
            desc.vertex.entryPoint  = sv(stageEntryPoint);
        }
        desc.depthStencil           = &depthStencil;

        bool success = true;
        if (shaderModuleStage != "vertex") {
            success = false;
        }
        if (!provideEntryPoint && extraEntryPoint) {
            success = false;
        }
        if (shaderModuleEntryPoint != stageEntryPoint && provideEntryPoint) {
            success = false;
        }

        doCreateRenderPipelineTest(t, isAsync, success, desc);
    });

// ---------------------------------------------------------------------------
// test: fragment
// ---------------------------------------------------------------------------
CTS_TEST(g, "fragment")
    .desc(R"(
Tests calling createRenderPipeline(Async) with valid fragment stage shader and different entryPoints,
and check that the APIs only accept matching entryPoint.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("shaderModuleStage", {Value(std::string("compute")), Value(std::string("vertex")), Value(std::string("fragment"))})
            .beginSubcases()
            .combine("provideEntryPoint", {Value(false), Value(true)})
            .combine("extraEntryPoint", {Value(false), Value(true)})
            .combine("caseIndex", entryPointTestCaseIndexValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync              = t.param<bool>("isAsync");
        const bool provideEntryPoint    = t.param<bool>("provideEntryPoint");
        const bool extraEntryPoint      = t.param<bool>("extraEntryPoint");
        const std::string shaderModuleStage = t.param<std::string>("shaderModuleStage");
        const int caseIndex             = t.param<int>("caseIndex");
        const EntryPointTestCase& tc    = kEntryPointTestCases[static_cast<size_t>(caseIndex)];
        const std::string& shaderModuleEntryPoint = tc.shaderModuleEntryPoint;
        const std::string& stageEntryPoint        = tc.stageEntryPoint;

        std::string code = getShaderWithEntryPoint(shaderModuleStage, shaderModuleEntryPoint);
        if (extraEntryPoint) {
            code += " " + getShaderWithEntryPoint(shaderModuleStage, "extra");
        }

        // vertex stage: always use kDefaultVertexShaderCode
        WGPUShaderModule vertShaderModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);
        WGPUShaderModule fragShaderModule = t.createShaderModuleTracked(code);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format               = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment       = WGPU_FRAGMENT_STATE_INIT;
        fragment.module                  = fragShaderModule;
        if (provideEntryPoint) {
            fragment.entryPoint          = sv(stageEntryPoint);
        }
        fragment.targetCount             = 1;
        fragment.targets                 = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                       = nullptr; // 'auto'
        desc.vertex.module                = vertShaderModule;
        desc.fragment                     = &fragment;

        bool success = true;
        if (shaderModuleStage != "fragment") {
            success = false;
        }
        if (!provideEntryPoint && extraEntryPoint) {
            success = false;
        }
        if (shaderModuleEntryPoint != stageEntryPoint && provideEntryPoint) {
            success = false;
        }

        doCreateRenderPipelineTest(t, isAsync, success, desc);
    });

// ---------------------------------------------------------------------------
// test: compute_undefined_entry_point_and_extra_stage
// ---------------------------------------------------------------------------
CTS_TEST(g, "compute_undefined_entry_point_and_extra_stage")
    .desc(R"(
Tests calling createComputePipeline(Async) with compute stage shader and
an undefined entryPoint is valid if there's an extra shader stage.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("extraShaderModuleStage", {Value(std::string("compute")), Value(std::string("vertex")), Value(std::string("fragment"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync                       = t.param<bool>("isAsync");
        const std::string extraShaderModuleStage = t.param<std::string>("extraShaderModuleStage");

        const std::string code =
            "        " + getShaderWithEntryPoint("compute", "main") + "\n"
            "        " + getShaderWithEntryPoint(extraShaderModuleStage, "extra") + "\n"
            "    ";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout          = nullptr; // 'auto'
        desc.compute.module  = shaderModule;
        // entryPoint = undefined (left as {nullptr,0})

        const bool success = (extraShaderModuleStage != "compute");
        doCreateComputePipelineTest(t, isAsync, success, desc);
    });

// ---------------------------------------------------------------------------
// test: vertex_undefined_entry_point_and_extra_stage
// ---------------------------------------------------------------------------
CTS_TEST(g, "vertex_undefined_entry_point_and_extra_stage")
    .desc(R"(
Tests calling createRenderPipeline(Async) with vertex stage shader and
an undefined entryPoint is valid if there's an extra shader stage.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("extraShaderModuleStage", {Value(std::string("compute")), Value(std::string("vertex")), Value(std::string("fragment"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync                       = t.param<bool>("isAsync");
        const std::string extraShaderModuleStage = t.param<std::string>("extraShaderModuleStage");

        const std::string code =
            "        " + getShaderWithEntryPoint("vertex", "main") + "\n"
            "        " + getShaderWithEntryPoint(extraShaderModuleStage, "extra") + "\n"
            "    ";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format            = WGPUTextureFormat_Depth32Float;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
        depthStencil.depthCompare      = WGPUCompareFunction_Always;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout           = nullptr; // 'auto'
        desc.vertex.module    = shaderModule;
        // vertex.entryPoint = undefined (left as {nullptr,0})
        desc.depthStencil     = &depthStencil;

        const bool success = (extraShaderModuleStage != "vertex");
        doCreateRenderPipelineTest(t, isAsync, success, desc);
    });

// ---------------------------------------------------------------------------
// test: fragment_undefined_entry_point_and_extra_stage
// ---------------------------------------------------------------------------
CTS_TEST(g, "fragment_undefined_entry_point_and_extra_stage")
    .desc(R"(
Tests calling createRenderPipeline(Async) with fragment stage shader and
an undefined entryPoint is valid if there's an extra shader stage.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("extraShaderModuleStage", {Value(std::string("compute")), Value(std::string("vertex")), Value(std::string("fragment"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync                       = t.param<bool>("isAsync");
        const std::string extraShaderModuleStage = t.param<std::string>("extraShaderModuleStage");

        const std::string code =
            "        " + getShaderWithEntryPoint("fragment", "main") + "\n"
            "        " + getShaderWithEntryPoint(extraShaderModuleStage, "extra") + "\n"
            "    ";

        WGPUShaderModule vertShaderModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);
        WGPUShaderModule fragShaderModule = t.createShaderModuleTracked(code);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format               = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment        = WGPU_FRAGMENT_STATE_INIT;
        fragment.module                   = fragShaderModule;
        // fragment.entryPoint = undefined (left as {nullptr,0})
        fragment.targetCount              = 1;
        fragment.targets                  = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                       = nullptr; // 'auto'
        desc.vertex.module                = vertShaderModule;
        desc.fragment                     = &fragment;

        const bool success = (extraShaderModuleStage != "fragment");
        doCreateRenderPipelineTest(t, isAsync, success, desc);
    });

} // namespace
