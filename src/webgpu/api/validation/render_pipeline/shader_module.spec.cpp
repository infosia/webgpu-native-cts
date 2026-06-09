// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/shader_module.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,shader_module",
    "This test dedicatedly tests createRenderPipeline validation issues related to the shader modules. "
    "Note: entry point matching tests are in ../shader_module/entry_point.spec.ts");

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

// Fragment shader equivalent to getFragmentShaderCodeWithOutput([{values:[0,1,0,1], plainType:'f32', componentCount:4}]).
// Used by device_mismatch test (values = [0, 1, 0, 1]).
static const char* kMismatchFragmentShaderCode =
    "\n"
    "    \n"
    "\n"
    "    struct Outputs {\n"
    "      @location(0) o0 : vec4<f32>,\n"
    "\n"
    "    }\n"
    "\n"
    "    @fragment fn main() -> Outputs {\n"
    "        return Outputs(vec4<f32>(0.0000, 1.0000, 0.0000, 1.0000));\n"
    "    }";

// Create a shader module on an arbitrary device (raw wgpu call, not tracked).
static WGPUShaderModule createShaderModuleOnDevice(WGPUDevice device, std::string_view wgsl) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = WGPUStringView{wgsl.data(), wgsl.size()};
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// Mirror of vtu.createInvalidShaderModule(t):
// Creates a shader module with invalid WGSL code, wrapped in expectValidationError so the
// device does not surface an uncaptured error. Returns the (error/invalid) handle.
static WGPUShaderModule createInvalidShaderModule(AllFeaturesMaxLimitsGpuTest& t) {
    // "deadbeaf" is not valid WGSL — same sentinel as upstream.
    constexpr std::string_view kInvalidCode = "deadbeaf";
    WGPUShaderModule shaderModule = nullptr;
    t.expectValidationError([&] {
        shaderModule = t.createShaderModuleTracked(kInvalidCode);
    }, true);
    return shaderModule;
}

// Mirror of doCreateRenderPipelineTest (sync path only — harness has no async wrapper).
// Calls createRenderPipelineTracked inside expectValidationError(!success).
static void doCreateRenderPipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    WGPURenderPipelineDescriptor& desc)
{
    t.expectValidationError([&] {
        t.createRenderPipelineTracked(desc);
    }, !success);
}

// ---------------------------------------------------------------------------
// test: device_mismatch
// Tests createRenderPipeline(Async) cannot be called with a shader module
// created from another device.
// ---------------------------------------------------------------------------

CTS_TEST(g, "device_mismatch")
    .desc(
        "Tests createRenderPipeline(Async) cannot be called with a shader module created from another device")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(true), Value(false)})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"vertex_mismatched", Value(false)}, {"fragment_mismatched", Value(false)}, {"_success", Value(true)}},
                ParamRecord{{"vertex_mismatched", Value(true)},  {"fragment_mismatched", Value(false)}, {"_success", Value(false)}},
                ParamRecord{{"vertex_mismatched", Value(false)}, {"fragment_mismatched", Value(true)},  {"_success", Value(false)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool vertex_mismatched   = t.param<bool>("vertex_mismatched");
        const bool fragment_mismatched = t.param<bool>("fragment_mismatched");
        const bool success             = t.param<bool>("_success");

        // async=true uses the same synchronous validation path (harness has no async pipeline wrapper).
        // (void)t.param<bool>("isAsync");

        // Vertex shader WGSL (same for both devices).
        constexpr std::string_view kVertWGSL =
            "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
            "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n";

        // Create vertex shader module on the appropriate device.
        WGPUShaderModule vertModule = nullptr;
        if (vertex_mismatched) {
            vertModule = createShaderModuleOnDevice(t.mismatchedDevice(), kVertWGSL);
        } else {
            vertModule = t.createShaderModuleTracked(kVertWGSL);
        }

        // Create fragment shader module on the appropriate device.
        WGPUShaderModule fragModule = nullptr;
        if (fragment_mismatched) {
            fragModule = createShaderModuleOnDevice(t.mismatchedDevice(), kMismatchFragmentShaderCode);
        } else {
            fragModule = t.createShaderModuleTracked(kMismatchFragmentShaderCode);
        }

        // Build the pipeline layout on the test device (t.getPipelineLayout() equivalent).
        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 0;
        plDesc.bindGroupLayouts = nullptr;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(plDesc);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = fragModule;
        fragment.entryPoint  = sv("main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout             = layout;
        desc.vertex.module      = vertModule;
        desc.vertex.entryPoint  = sv("main");
        desc.fragment           = &fragment;

        doCreateRenderPipelineTest(t, success, desc);

        // Release any mismatched-device resources (not tracked by harness).
        if (vertex_mismatched && vertModule != nullptr) {
            wgpuShaderModuleRelease(vertModule);
        }
        if (fragment_mismatched && fragModule != nullptr) {
            wgpuShaderModuleRelease(fragModule);
        }
    });

// ---------------------------------------------------------------------------
// test: invalid,vertex
// Tests that the vertex shader module must be valid.
// ---------------------------------------------------------------------------

CTS_TEST(g, "invalid,vertex")
    .desc("Tests shader module must be valid.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(true), Value(false)})
            .combine("isVertexShaderValid", {Value(true), Value(false)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isVertexShaderValid = t.param<bool>("isVertexShaderValid");
        // async=true uses the same synchronous validation path (harness has no async pipeline wrapper).

        WGPUShaderModule vertModule = isVertexShaderValid
            ? t.createShaderModuleTracked(kDefaultVertexShaderCode)
            : createInvalidShaderModule(t);

        // Specify a color attachment so we have at least one render target.
        WGPUShaderModule fragModule = t.createShaderModuleTracked(
            "@fragment fn main() -> @location(0) vec4f { return vec4f(0); }");

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = fragModule;
        fragment.entryPoint  = sv("main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout            = nullptr; // auto
        desc.vertex.module     = vertModule;
        desc.vertex.entryPoint = sv("main");
        desc.fragment          = &fragment;

        // success = isVertexShaderValid
        t.expectValidationError([&] {
            t.createRenderPipelineTracked(desc);
        }, !isVertexShaderValid);
    });

// ---------------------------------------------------------------------------
// test: invalid,fragment
// Tests that the fragment shader module must be valid.
// ---------------------------------------------------------------------------

CTS_TEST(g, "invalid,fragment")
    .desc("Tests shader module must be valid.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(true), Value(false)})
            .combine("isFragmentShaderValid", {Value(true), Value(false)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isFragmentShaderValid = t.param<bool>("isFragmentShaderValid");
        // async=true uses the same synchronous validation path (harness has no async pipeline wrapper).

        WGPUShaderModule vertModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);

        WGPUShaderModule fragModule = isFragmentShaderValid
            ? t.createShaderModuleTracked(kDefaultFragmentShaderCode)
            : createInvalidShaderModule(t);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = fragModule;
        fragment.entryPoint  = sv("main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout            = nullptr; // auto
        desc.vertex.module     = vertModule;
        desc.vertex.entryPoint = sv("main");
        desc.fragment          = &fragment;

        // success = isFragmentShaderValid
        t.expectValidationError([&] {
            t.createRenderPipelineTracked(desc);
        }, !isFragmentShaderValid);
    });

} // namespace
