// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/clip_distances.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/api/validation/capability_checks/limits/limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class ClipDistancesTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxInterStageShaderVariables"; }
};

TestGroup<ClipDistancesTest> testGroup = MakeTestGroup<ClipDistancesTest>(
    "api,validation,capability_checks,features,clip_distances",
    "API Validation Tests for clip-distances.");

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::vector<Value> clipDistanceValues() {
    return {uint64_t(1), uint64_t(2), uint64_t(3), uint64_t(4),
            uint64_t(5), uint64_t(6), uint64_t(7), uint64_t(8)};
}

void skipIfClipDistancesUnsupported(ClipDistancesTest& t) {
    if (!wgpuAdapterHasFeature(t.adapter(), WGPUFeatureName_ClipDistances)) {
        t.skip("clip-distances feature is not supported on this adapter");
    }
}

WGPURenderPipelineDescriptor getPipelineDescriptorWithClipDistances(
    ClipDistancesTest& t,
    uint64_t interStageShaderVariables,
    bool pointList,
    uint64_t clipDistances,
    uint64_t startLocation,
    WGPUFragmentState* fragment,
    WGPUColorTargetState* target) {
    const uint64_t clipDistanceSlots = alignTo(clipDistances, 4) / 4;
    const int64_t vertexOutputVariables = static_cast<int64_t>(interStageShaderVariables)
        - (pointList ? 1 : 0) - static_cast<int64_t>(clipDistanceSlots);
    std::ostringstream varyings;
    for (int64_t i = 0; i < vertexOutputVariables; ++i) {
        const uint64_t location = startLocation + static_cast<uint64_t>(i);
        varyings << "  @location(" << location << ") v4_" << location << ": vec4f,\n";
    }

    std::ostringstream code;
    code << "enable clip_distances;\n"
         << "struct VSOut {\n"
         << "  @builtin(position) p: vec4f,\n"
         << varyings.str()
         << "  @builtin(clip_distances) clipDistances: array<f32, " << clipDistances << ">,\n"
         << "}\n"
         << "struct FSIn {\n"
         << varyings.str()
         << "}\n"
         << "struct FSOut {\n"
         << "  @location(0) color: vec4f,\n"
         << "}\n"
         << "@vertex fn vs() -> VSOut {\n"
         << "  var o: VSOut;\n"
         << "  o.p = vec4f(0);\n"
         << "  return o;\n"
         << "}\n"
         << "@fragment fn fs(i: FSIn) -> FSOut {\n"
         << "  var o: FSOut;\n"
         << "  o.color = vec4f(0);\n"
         << "  return o;\n"
         << "}\n";

    WGPUShaderModule module = t.createShaderModuleTracked(code.str());
    target->format = WGPUTextureFormat_RGBA8Unorm;
    target->writeMask = WGPUColorWriteMask_All;
    *fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment->module = module;
    fragment->entryPoint = sv("fs");
    fragment->targetCount = 1;
    fragment->targets = target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.primitive.topology = pointList ? WGPUPrimitiveTopology_PointList : WGPUPrimitiveTopology_TriangleList;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs");
    desc.fragment = fragment;
    return desc;
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .desc("Test using at and over maxInterStageShaderVariables limit with clip_distances in createRenderPipeline(Async)")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u)
            .combine("async", {false, true})
            .combine("pointList", {false, true})
            .combine("clipDistances", clipDistanceValues());
    })
    .fn([](ClipDistancesTest& t) {
        skipIfClipDistancesUnsupported(t);
        const bool async = t.param<bool>("async");
        const bool pointList = t.param<bool>("pointList");
        const uint64_t clipDistances = t.param<uint64_t>("clipDistances");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
                WGPURenderPipelineDescriptor desc = getPipelineDescriptorWithClipDistances(
                    t, inputs.testValue, pointList, clipDistances, 0, &fragment, &target);
                t.testCreateRenderPipeline(desc, async, inputs.shouldError);
            }, {}, {WGPUFeatureName_ClipDistances});
    });

CTS_TEST(testGroup, "createRenderPipeline,max_vertex_output_location")
    .desc("Test using clip_distances will limit the maximum value of vertex output location")
    .params([](ParamsBuilder u) {
        return u.combine("pointList", {false, true})
            .combine("clipDistances", clipDistanceValues())
            .combine("startLocation", {uint64_t(0), uint64_t(1), uint64_t(2)});
    })
    .fn([](ClipDistancesTest& t) {
        skipIfClipDistancesUnsupported(t);
        const bool pointList = t.param<bool>("pointList");
        const uint64_t clipDistances = t.param<uint64_t>("clipDistances");
        const uint64_t startLocation = t.param<uint64_t>("startLocation");
        std::optional<DeviceAndLimits> deviceAndLimits =
            t.getDeviceWithSpecificLimit(t.adapterLimit, {}, {WGPUFeatureName_ClipDistances});
        if (!deviceAndLimits.has_value()) {
            return;
        }
        t.testThenDestroyDevice(*deviceAndLimits, t.adapterLimit, [&](const SpecificLimitTestInputs& inputs) {
            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            WGPURenderPipelineDescriptor desc = getPipelineDescriptorWithClipDistances(
                t, inputs.actualLimit, pointList, clipDistances, startLocation, &fragment, &target);
            const uint64_t clipDistanceSlots = alignTo(clipDistances, 4) / 4;
            const uint64_t vertexOutputVariables =
                inputs.actualLimit - (pointList ? 1u : 0u) - clipDistanceSlots;
            const uint64_t maxLocationInTest = startLocation + vertexOutputVariables - 1;
            const uint64_t maxAllowedLocation = inputs.actualLimit - 1 - clipDistanceSlots;
            const bool shouldError = maxLocationInTest > maxAllowedLocation;
            t.expectValidationErrorOnLimitDevice([&] { t.createRenderPipelineTracked(desc); }, shouldError);
        });
    });

} // namespace
