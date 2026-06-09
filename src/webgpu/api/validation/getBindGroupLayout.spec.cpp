// Ported from gpuweb/cts src/webgpu/api/validation/getBindGroupLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: unique_js_object tests are .unimplemented() — they test JavaScript object identity
// (expando properties, === comparison), which has no equivalent in the C API.

#include <cstdint>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,getBindGroupLayout",
    "getBindGroupLayout validation tests.");

// Returns a WGPUStringView from a null-terminated C string.
static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// Vertex shader shared by all tests.
static const char* kVertexShaderCode =
    "@vertex\n"
    "fn main()-> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Fragment shader with no bindings (used with explicit layout).
static const char* kFragmentShaderCode =
    "@fragment\n"
    "fn main() -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";

// Fragment shader with a uniform binding at group 0 binding 0 (used with auto layout).
static const char* kFragmentShaderWithBindingCode =
    "@group(0) @binding(0) var<uniform> binding: f32;\n"
    "@fragment\n"
    "fn main() -> @location(0) vec4<f32> {\n"
    "  _ = binding;\n"
    "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------
// index_range,explicit_layout
// Test that a validation error is generated if the index is greater than the
// maximum number of bind groups.
// ---------------------------------------------------------------------------
CTS_TEST(g, "index_range,explicit_layout")
    .desc(
        "Test that a validation error is generated if the index is greater than the maximum number of bind\n"
        "groups.")
    .params([](ParamsBuilder u) {
        return u.combine("index", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(5)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t index = static_cast<uint32_t>(t.param<int>("index"));

        // Create an explicit pipeline layout with one empty bind group layout.
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        WGPUBindGroupLayout pipelineBindGroupLayout = t.createBindGroupLayoutTracked(bglDesc);

        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts = &pipelineBindGroupLayout;
        WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

        // Build render pipeline with explicit layout.
        WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShaderCode);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShaderCode);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = sv("main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = pipelineLayout;
        desc.vertex.module = vertexModule;
        desc.vertex.entryPoint = sv("main");
        desc.fragment = &fragment;

        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(desc);

        const WGPULimits limits = t.getLimits();
        const bool shouldError = index >= limits.maxBindGroups;

        t.expectValidationError([&] {
            WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, index);
            if (bgl != nullptr) {
                wgpuBindGroupLayoutRelease(bgl);
            }
        }, shouldError);
    });

// ---------------------------------------------------------------------------
// index_range,auto_layout
// Test that a validation error is generated if the index is greater than the
// maximum number of bind groups.
// ---------------------------------------------------------------------------
CTS_TEST(g, "index_range,auto_layout")
    .desc(
        "Test that a validation error is generated if the index is greater than the maximum number of bind\n"
        "groups.")
    .params([](ParamsBuilder u) {
        return u.combine("index", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(5)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t index = static_cast<uint32_t>(t.param<int>("index"));

        // Build render pipeline with auto layout (pipelineLayout = nullptr).
        WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShaderCode);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShaderWithBindingCode);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = sv("main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = nullptr; // auto layout
        desc.vertex.module = vertexModule;
        desc.vertex.entryPoint = sv("main");
        desc.fragment = &fragment;

        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(desc);

        const WGPULimits limits = t.getLimits();
        const bool shouldError = index >= limits.maxBindGroups;

        t.expectValidationError([&] {
            WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, index);
            if (bgl != nullptr) {
                wgpuBindGroupLayoutRelease(bgl);
            }
        }, shouldError);
    });

// ---------------------------------------------------------------------------
// unique_js_object,auto_layout
// Tests that getBindGroupLayout returns a new JavaScript object for each call.
// This is a JavaScript/WebIDL object-identity test (=== and expando properties)
// that has no equivalent in the C API.
// ---------------------------------------------------------------------------
CTS_TEST(g, "unique_js_object,auto_layout")
    .desc("Test that getBindGroupLayout returns a new JavaScript object for each call.")
    .unimplemented("JavaScript object identity (expando properties, === comparison) has no C API equivalent");

// ---------------------------------------------------------------------------
// unique_js_object,explicit_layout
// Tests that getBindGroupLayout returns a new JavaScript object for each call.
// This is a JavaScript/WebIDL object-identity test (=== and expando properties)
// that has no equivalent in the C API.
// ---------------------------------------------------------------------------
CTS_TEST(g, "unique_js_object,explicit_layout")
    .desc("Test that getBindGroupLayout returns a new JavaScript object for each call.")
    .unimplemented("JavaScript object identity (expando properties, === comparison) has no C API equivalent");

} // namespace
