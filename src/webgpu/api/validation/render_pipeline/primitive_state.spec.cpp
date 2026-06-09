// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/primitive_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,primitive_state",
    "Validation of GPUPrimitiveState of createRenderPipeline.");

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
    "struct Outputs {\n"
    "  @location(0) o0 : vec4<f32>,\n"
    "}\n"
    "\n"
    "@fragment fn main() -> Outputs {\n"
    "    return Outputs(vec4<f32>(0.0000, 1.0000, 0.0000, 1.0000));\n"
    "}\n";

// Build a render pipeline descriptor with the given primitive state,
// mirroring CreateRenderPipelineValidationTest::getDescriptor from common.ts.
//
// The descriptor holder keeps all sub-objects alive for the call to
// createRenderPipelineTracked / expectValidationError.
struct RenderPipelineDescriptorHolder {
    WGPUShaderModule vertexModule   = nullptr;
    WGPUShaderModule fragmentModule = nullptr;
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState fragment       = WGPU_FRAGMENT_STATE_INIT;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

RenderPipelineDescriptorHolder buildDescriptorWithPrimitive(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUPrimitiveTopology topology,
    WGPUIndexFormat stripIndexFormat,
    bool unclippedDepth)
{
    RenderPipelineDescriptorHolder h;

    h.vertexModule   = t.createShaderModuleTracked(kDefaultVertexShaderCode);
    h.fragmentModule = t.createShaderModuleTracked(kDefaultFragmentShaderCode);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts     = nullptr;
    h.pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    h.colorTarget        = WGPU_COLOR_TARGET_STATE_INIT;
    h.colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    h.fragment            = WGPU_FRAGMENT_STATE_INIT;
    h.fragment.module     = h.fragmentModule;
    h.fragment.entryPoint = sv("main");
    h.fragment.targetCount = 1;
    h.fragment.targets     = &h.colorTarget;

    h.desc                   = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout            = h.pipelineLayout;
    h.desc.vertex.module     = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.fragment          = &h.fragment;

    // Apply primitive state.
    h.desc.primitive.topology         = topology;
    h.desc.primitive.stripIndexFormat = stripIndexFormat;
    h.desc.primitive.unclippedDepth   = unclippedDepth ? WGPU_TRUE : WGPU_FALSE;

    return h;
}

// Mirrors vtu.doCreateRenderPipelineTest for the synchronous path.
// For isAsync=true the C++ harness has no async wrapper; both paths use expectValidationError.
void doCreateRenderPipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool shouldError,
    RenderPipelineDescriptorHolder& h)
{
    t.expectValidationError([&] {
        t.createRenderPipelineTracked(h.desc);
    }, shouldError);
}

// ---------------------------------------------------------------------------
// Topology values: undefined + kPrimitiveTopology
// kPrimitiveTopology = ['point-list','line-list','line-strip','triangle-list','triangle-strip']
// Stored as strings matching the upstream identifier; "undefined" means WGPUPrimitiveTopology_Undefined.
// ---------------------------------------------------------------------------
static const std::vector<Value> kTopologyValues = {
    Value(std::string("undefined")),
    Value(std::string("point-list")),
    Value(std::string("line-list")),
    Value(std::string("line-strip")),
    Value(std::string("triangle-list")),
    Value(std::string("triangle-strip")),
};

// Maps a topology string to WGPUPrimitiveTopology.
WGPUPrimitiveTopology topologyFromString(const std::string& s) {
    if (s == "undefined")       return WGPUPrimitiveTopology_Undefined;
    if (s == "point-list")      return WGPUPrimitiveTopology_PointList;
    if (s == "line-list")       return WGPUPrimitiveTopology_LineList;
    if (s == "line-strip")      return WGPUPrimitiveTopology_LineStrip;
    if (s == "triangle-list")   return WGPUPrimitiveTopology_TriangleList;
    if (s == "triangle-strip")  return WGPUPrimitiveTopology_TriangleStrip;
    return WGPUPrimitiveTopology_Undefined;
}

// ---------------------------------------------------------------------------
// Strip index format values: undefined + kIndexFormat = ['uint16','uint32']
// "undefined" means WGPUIndexFormat_Undefined (no strip index format set).
// ---------------------------------------------------------------------------
static const std::vector<Value> kStripIndexFormatValues = {
    Value(std::string("undefined")),
    Value(std::string("uint16")),
    Value(std::string("uint32")),
};

// Maps a strip index format string to WGPUIndexFormat.
WGPUIndexFormat stripIndexFormatFromString(const std::string& s) {
    if (s == "uint16") return WGPUIndexFormat_Uint16;
    if (s == "uint32") return WGPUIndexFormat_Uint32;
    return WGPUIndexFormat_Undefined;
}

// ---------------------------------------------------------------------------
// test: strip_index_format
// If primitive.topology is not "line-strip" or "triangle-strip",
// primitive.stripIndexFormat must be undefined.
// ---------------------------------------------------------------------------
CTS_TEST(g, "strip_index_format")
    .desc(
        "If primitive.topology is not \"line-strip\" or \"triangle-strip\", "
        "primitive.stripIndexFormat must be undefined.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",          {Value(false), Value(true)})
            .combine("topology",         kTopologyValues)
            .combine("stripIndexFormat", kStripIndexFormatValues);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const std::string topologyStr         = t.param<std::string>("topology");
        const std::string stripIndexFormatStr = t.param<std::string>("stripIndexFormat");

        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)isAsync;

        const WGPUPrimitiveTopology topology         = topologyFromString(topologyStr);
        const WGPUIndexFormat       stripIndexFormat = stripIndexFormatFromString(stripIndexFormatStr);

        // Upstream success condition:
        //   topology === 'line-strip' || topology === 'triangle-strip' || stripIndexFormat === undefined
        const bool success =
            topology == WGPUPrimitiveTopology_LineStrip ||
            topology == WGPUPrimitiveTopology_TriangleStrip ||
            stripIndexFormat == WGPUIndexFormat_Undefined;

        auto h = buildDescriptorWithPrimitive(t, topology, stripIndexFormat, false);
        doCreateRenderPipelineTest(t, !success, h);
    });

// ---------------------------------------------------------------------------
// test: unclipped_depth
// If primitive.unclippedDepth is true, features must contain "depth-clip-control".
// ---------------------------------------------------------------------------
CTS_TEST(g, "unclipped_depth")
    .desc(
        "If primitive.unclippedDepth is true, features must contain \"depth-clip-control\".")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync",        {Value(false), Value(true)})
            .combine("unclippedDepth", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync        = t.param<bool>("isAsync");
        const bool unclippedDepth = t.param<bool>("unclippedDepth");

        // async=true uses same synchronous validation path (harness has no async pipeline wrapper).
        (void)isAsync;

        // Upstream: hasFeature(t.device.features, 'depth-clip-control')
        const bool hasDepthClipControl =
            wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DepthClipControl) != 0;

        // Upstream success condition: !unclippedDepth || hasFeature(t.device.features, 'depth-clip-control')
        const bool success = !unclippedDepth || hasDepthClipControl;

        auto h = buildDescriptorWithPrimitive(
            t, WGPUPrimitiveTopology_Undefined, WGPUIndexFormat_Undefined, unclippedDepth);
        doCreateRenderPipelineTest(t, !success, h);
    });

} // namespace
