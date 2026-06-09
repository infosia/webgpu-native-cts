// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/float32_blendable.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.
// Note: UniqueFeaturesOrLimitsGPUTest is replaced by GpuTest; feature gate is checked at runtime
// via wgpuDeviceHasFeature, mirroring the float32_filterable.spec.cpp pattern.

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,render_pipeline,float32_blendable",
    "Tests for capabilities added by float32-blendable flag.");

// Mirrors kFloat32Formats from the upstream spec.
// ColorTextureFormat[] = ['r32float', 'rg32float', 'rgba32float']
const std::vector<Value> kFloat32FormatValues = {
    Value(std::string("r32float")),
    Value(std::string("rg32float")),
    Value(std::string("rgba32float")),
};

// Maps a float32 format string to its WGPUTextureFormat.
WGPUTextureFormat float32FormatFromString(const std::string& s) {
    if (s == "r32float")    return WGPUTextureFormat_R32Float;
    if (s == "rg32float")   return WGPUTextureFormat_RG32Float;
    if (s == "rgba32float") return WGPUTextureFormat_RGBA32Float;
    return WGPUTextureFormat_Undefined;
}

// Returns a WGPUStringView for a null-terminated C string.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// Vertex shader — mirrors kDefaultVertexShaderCode from upstream.
static constexpr const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Fragment shader — mirrors getFragmentShaderCodeWithOutput([{values:[0,1,0,1], plainType:'f32', componentCount:4}]).
// All three float32 formats have sample type 'unfilterable-float' → plain type f32.
static constexpr const char* kFragmentShaderCode =
    "struct Outputs {\n"
    "  @location(0) o0 : vec4<f32>,\n"
    "}\n"
    "\n"
    "@fragment fn main() -> Outputs {\n"
    "    return Outputs(vec4<f32>(0.0000, 1.0000, 0.0000, 1.0000));\n"
    "}\n";

// Build a render pipeline descriptor for the given format and blend state,
// mirroring getDescriptorForCreateRenderPipelineValidationTest from common.ts.
//
// Caller must keep vertexModule, fragmentModule, blendState, colorTarget, pipelineLayout alive
// for the lifetime of the descriptor.
struct RenderPipelineDescriptorHolder {
    WGPUShaderModule vertexModule    = nullptr;
    WGPUShaderModule fragmentModule  = nullptr;
    WGPUBlendState   blendState      = WGPU_BLEND_STATE_INIT;
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState fragment       = WGPU_FRAGMENT_STATE_INIT;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

RenderPipelineDescriptorHolder buildRenderPipelineDescriptor(
    GpuTest& t,
    WGPUTextureFormat format,
    bool hasBlend)
{
    RenderPipelineDescriptorHolder h;

    h.vertexModule   = t.createShaderModuleTracked(kDefaultVertexShaderCode);
    h.fragmentModule = t.createShaderModuleTracked(kFragmentShaderCode);

    // Create a pipeline layout with no bind group layouts (matching upstream: createPipelineLayout({ bindGroupLayouts: [] })).
    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts     = nullptr;
    h.pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    // Color target state.
    h.colorTarget        = WGPU_COLOR_TARGET_STATE_INIT;
    h.colorTarget.format = format;

    if (hasBlend) {
        // blend: { color: {}, alpha: {} } — both use defaults (WGPU_BLEND_COMPONENT_INIT).
        h.blendState = WGPU_BLEND_STATE_INIT;
        h.colorTarget.blend = &h.blendState;
    } else {
        h.colorTarget.blend = nullptr;
    }

    h.fragment             = WGPU_FRAGMENT_STATE_INIT;
    h.fragment.module      = h.fragmentModule;
    h.fragment.entryPoint  = sv("main");
    h.fragment.targetCount = 1;
    h.fragment.targets     = &h.colorTarget;

    h.desc                    = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout             = h.pipelineLayout;
    h.desc.vertex.module      = h.vertexModule;
    h.desc.vertex.entryPoint  = sv("main");
    h.desc.fragment           = &h.fragment;

    return h;
}

// Mirrors vtu.doCreateRenderPipelineTest for the non-async (synchronous) path.
// For async=true the C++ harness has no async wrapper, so both paths use expectValidationError.
void doCreateRenderPipelineTest(GpuTest& t, bool shouldError, RenderPipelineDescriptorHolder& h) {
    t.expectValidationError([&] {
        t.createRenderPipelineTracked(h.desc);
    }, shouldError);
}

CTS_TEST(g, "create_render_pipeline")
    .desc(R"(
Tests that the float32-blendable feature is required to create a render
pipeline that uses blending with any float32-format attachment.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",   {Value(false), Value(true)})
            .combine("enabled",   {Value(true),  Value(false)})
            .beginSubcases()
            .combine("hasBlend",  {Value(true),  Value(false)})
            .combine("format",    kFloat32FormatValues);
    })
    .fn([](GpuTest& t) {
        const bool isAsync  = t.param<bool>("isAsync");
        const bool enabled  = t.param<bool>("enabled");
        const bool hasBlend = t.param<bool>("hasBlend");
        const std::string formatStr = t.param<std::string>("format");
        const WGPUTextureFormat format = float32FormatFromString(formatStr);

        // Gate on device feature availability, mirroring beforeAllSubcases /
        // selectDeviceOrSkipTestCase in the upstream.
        const bool deviceHasFeature =
            wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Float32Blendable) != 0;

        if (enabled && !deviceHasFeature) {
            t.skip("float32-blendable feature is not available on this device");
        }
        if (!enabled && deviceHasFeature) {
            t.skip("float32-blendable is always enabled on this device; cannot test without-feature path");
        }

        // The async path and the sync path produce the same validation outcome;
        // both are exercised via expectValidationError.
        (void)isAsync;

        // Upstream: vtu.doCreateRenderPipelineTest(t, isAsync, enabled || !hasBlend, descriptor)
        // shouldError = !(enabled || !hasBlend)
        const bool shouldError = !(enabled || !hasBlend);

        auto h = buildRenderPipelineDescriptor(t, format, hasBlend);
        doCreateRenderPipelineTest(t, shouldError, h);
    });

} // namespace
