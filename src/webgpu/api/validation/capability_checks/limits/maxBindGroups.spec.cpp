// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxBindGroups.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxBindGroupsTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxBindGroups"; }
};

TestGroup<MaxBindGroupsTest> testGroup = MakeTestGroup<MaxBindGroupsTest>(
    "api,validation,capability_checks,limits,maxBindGroups",
    "API Validation Tests for maxBindGroups.");

std::vector<Value> encoderTypes() {
    return {std::string("compute"), std::string("render"), std::string("renderBundle")};
}

WGPUBindGroupLayout createOneEntryBGL(MaxBindGroupsTest& t, WGPUShaderStage visibility) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = visibility;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

CTS_TEST(testGroup, "createPipelineLayout,at_over")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxBindGroupsTest& t) {
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUBindGroupLayout> layouts;
                for (uint64_t i = 0; i < inputs.testValue; ++i) {
                    layouts.push_back(createOneEntryBGL(t, WGPUShaderStage_Vertex));
                }
                WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
                desc.bindGroupLayoutCount = layouts.size();
                desc.bindGroupLayouts = layouts.data();
                t.expectValidationErrorOnLimitDevice([&] { t.createPipelineLayoutTracked(desc); }, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "createPipeline,at_over")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u).combine("createPipelineType", kCreatePipelineTypeValues()).combine("async", {false, true});
    })
    .fn([](MaxBindGroupsTest& t) {
        const std::string pipelineType = t.param<std::string>("createPipelineType");
        const bool async = t.param<bool>("async");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                const std::string code = t.getGroupIndexWGSLForPipelineType(pipelineType, inputs.testValue - 1);
                WGPUShaderModule module = t.createShaderModuleTracked(code);
                t.testCreatePipeline(pipelineType, async, module, inputs.shouldError, code);
            });
    });

CTS_TEST(testGroup, "setBindGroup,at_over")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("encoderType", encoderTypes()); })
    .fn([](MaxBindGroupsTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.testGPUBindingCommandsMixin(encoderType, [&](const LimitTest::BindingCommandContext& ctx) {
                    ctx.setBindGroup(static_cast<uint32_t>(inputs.testValue - 1), ctx.bindGroup);
                }, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "validate,maxBindGroupsPlusVertexBuffers")
    .fn([](MaxBindGroupsTest& t) {
        t.expect(t.defaultLimit <= t.getDefaultLimit("maxBindGroupsPlusVertexBuffers"));
        t.expect(t.adapterLimit <= t.getAdapterLimit("maxBindGroupsPlusVertexBuffers"));
    });

} // namespace
