// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxDynamicUniformBuffersPerPipelineLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxDynamicUniformBuffersPerPipelineLayoutTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxDynamicUniformBuffersPerPipelineLayout"; }
};

TestGroup<MaxDynamicUniformBuffersPerPipelineLayoutTest> testGroup = MakeTestGroup<MaxDynamicUniformBuffersPerPipelineLayoutTest>(
    "api,validation,capability_checks,limits,maxDynamicUniformBuffersPerPipelineLayout",
    "API Validation Tests for maxDynamicUniformBuffersPerPipelineLayout.");

std::vector<Value> shaderStageCombinationValues() {
    return {uint64_t(WGPUShaderStage_Vertex), uint64_t(WGPUShaderStage_Fragment),
            uint64_t(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment), uint64_t(WGPUShaderStage_Compute),
            uint64_t(WGPUShaderStage_Vertex | WGPUShaderStage_Compute),
            uint64_t(WGPUShaderStage_Fragment | WGPUShaderStage_Compute),
            uint64_t(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute)};
}

std::vector<LimitRequestEntry> extraLimits() {
    return {adapterLimitRequest("maxBindingsPerBindGroup"), adapterLimitRequest("maxBindGroups"),
            adapterLimitRequest("maxUniformBuffersPerShaderStage")};
}

WGPUBindGroupLayout createBGL(MaxDynamicUniformBuffersPerPipelineLayoutTest& t, uint64_t count, WGPUShaderStage visibility) {
    std::vector<WGPUBindGroupLayoutEntry> entries(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        entries[static_cast<size_t>(i)] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[static_cast<size_t>(i)].binding = static_cast<uint32_t>(i);
        entries[static_cast<size_t>(i)].visibility = visibility;
        entries[static_cast<size_t>(i)].buffer.type = WGPUBufferBindingType_Uniform;
        entries[static_cast<size_t>(i)].buffer.hasDynamicOffset = WGPU_TRUE;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

CTS_TEST(testGroup, "createBindGroupLayout,at_over")
    .desc("Test using createBindGroupLayout at and over maxDynamicUniformBuffersPerPipelineLayout limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("visibility", shaderStageCombinationValues()); })
    .fn([](MaxDynamicUniformBuffersPerPipelineLayoutTest& t) {
        WGPUShaderStage visibility = static_cast<WGPUShaderStage>(t.param<uint64_t>("visibility"));
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.expectValidationErrorOnLimitDevice([&] { createBGL(t, inputs.testValue, visibility); }, inputs.shouldError);
            }, extraLimits());
    });

CTS_TEST(testGroup, "createPipelineLayout,at_over")
    .desc("Test using at and over maxDynamicUniformBuffersPerPipelineLayout limit in createPipelineLayout")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxDynamicUniformBuffersPerPipelineLayoutTest& t) {
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                WGPULimits limits = queryLimits(inputs.device, &compat);
                const uint64_t maxUniformBindings = std::min<uint64_t>(limits.maxUniformBuffersPerShaderStage, inputs.actualLimit);
                if (maxUniformBindings * 3 < inputs.testValue) t.skip("not enough uniform bindings across stages");
                std::vector<WGPUBindGroupLayout> layouts;
                uint64_t remaining = inputs.testValue;
                for (uint32_t stageBit = 0; stageBit < 3; ++stageBit) {
                    const uint64_t count = std::min(remaining, maxUniformBindings);
                    remaining -= count;
                    layouts.push_back(createBGL(t, count, static_cast<WGPUShaderStage>(uint64_t{1} << stageBit)));
                }
                WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
                desc.bindGroupLayoutCount = layouts.size();
                desc.bindGroupLayouts = layouts.data();
                t.expectValidationErrorOnLimitDevice([&] { t.createPipelineLayoutTracked(desc); }, inputs.shouldError);
            }, extraLimits());
    });

} // namespace
