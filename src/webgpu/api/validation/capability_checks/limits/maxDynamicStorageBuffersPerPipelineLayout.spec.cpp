// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxDynamicStorageBuffersPerPipelineLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>
#include <array>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxDynamicStorageBuffersPerPipelineLayoutTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxDynamicStorageBuffersPerPipelineLayout"; }
};

TestGroup<MaxDynamicStorageBuffersPerPipelineLayoutTest> testGroup = MakeTestGroup<MaxDynamicStorageBuffersPerPipelineLayoutTest>(
    "api,validation,capability_checks,limits,maxDynamicStorageBuffersPerPipelineLayout",
    "API Validation Tests for maxDynamicStorageBuffersPerPipelineLayout.");

std::vector<Value> shaderStageCombinationValues() {
    return {uint64_t(WGPUShaderStage_Vertex), uint64_t(WGPUShaderStage_Fragment),
            uint64_t(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment), uint64_t(WGPUShaderStage_Compute),
            uint64_t(WGPUShaderStage_Vertex | WGPUShaderStage_Compute),
            uint64_t(WGPUShaderStage_Fragment | WGPUShaderStage_Compute),
            uint64_t(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute)};
}

std::vector<Value> storageTypes() {
    return {std::string("storage"), std::string("read-only-storage")};
}

std::vector<LimitRequestEntry> extraLimits() {
    return {adapterLimitRequest("maxBindingsPerBindGroup"), adapterLimitRequest("maxBindGroups"),
            adapterLimitRequest("maxStorageBuffersPerShaderStage"),
            adapterLimitRequest("maxStorageBuffersInFragmentStage"),
            adapterLimitRequest("maxStorageBuffersInVertexStage")};
}

WGPUBufferBindingType bindingType(const std::string& type) {
    return type == "storage" ? WGPUBufferBindingType_Storage : WGPUBufferBindingType_ReadOnlyStorage;
}

WGPUBindGroupLayout createBGL(MaxDynamicStorageBuffersPerPipelineLayoutTest& t, uint64_t count, WGPUShaderStage visibility, const std::string& type) {
    std::vector<WGPUBindGroupLayoutEntry> entries(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        entries[static_cast<size_t>(i)] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[static_cast<size_t>(i)].binding = static_cast<uint32_t>(i);
        entries[static_cast<size_t>(i)].visibility = visibility;
        entries[static_cast<size_t>(i)].buffer.type = bindingType(type);
        entries[static_cast<size_t>(i)].buffer.hasDynamicOffset = WGPU_TRUE;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

CTS_TEST(testGroup, "createBindGroupLayout,at_over")
    .desc("Test using createBindGroupLayout at and over maxDynamicStorageBuffersPerPipelineLayout limit")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u).combine("visibility", shaderStageCombinationValues()).combine("type", storageTypes())
            .filter([](const ParamRecord& p) {
                const auto visibility = static_cast<WGPUShaderStage>(valueAs<uint64_t>(*findParam(p, "visibility")));
                const std::string type = valueAs<std::string>(*findParam(p, "type"));
                return (visibility & WGPUShaderStage_Vertex) == 0 || type != "storage";
            });
    })
    .fn([](MaxDynamicStorageBuffersPerPipelineLayoutTest& t) {
        WGPUShaderStage visibility = static_cast<WGPUShaderStage>(t.param<uint64_t>("visibility"));
        std::string type = t.param<std::string>("type");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.skipIfNotEnoughStorageBuffersInStage(visibility, inputs.testValue);
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                WGPULimits limits = queryLimits(inputs.device, &compat);
                bool shouldError = inputs.shouldError || inputs.testValue > limits.maxStorageBuffersPerShaderStage;
                t.expectValidationErrorOnLimitDevice([&] { createBGL(t, inputs.testValue, visibility, type); }, shouldError);
            }, extraLimits());
    });

CTS_TEST(testGroup, "createPipelineLayout,at_over")
    .desc("Test using at and over maxDynamicStorageBuffersPerPipelineLayout limit in createPipelineLayout")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("type", storageTypes()); })
    .fn([](MaxDynamicStorageBuffersPerPipelineLayoutTest& t) {
        std::string type = t.param<std::string>("type");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                WGPULimits limits = queryLimits(inputs.device, &compat);
                const uint64_t maxCompute = std::min<uint64_t>(limits.maxStorageBuffersPerShaderStage, inputs.actualLimit);
                const uint64_t fragLimit = compat.maxStorageBuffersInFragmentStage == WGPU_LIMIT_U32_UNDEFINED ? maxCompute : compat.maxStorageBuffersInFragmentStage;
                const uint64_t vertLimit = compat.maxStorageBuffersInVertexStage == WGPU_LIMIT_U32_UNDEFINED ? maxCompute : compat.maxStorageBuffersInVertexStage;
                const uint64_t maxFragment = std::min<uint64_t>(fragLimit, inputs.actualLimit);
                const uint64_t maxVertex = type == "storage" ? 0 : std::min<uint64_t>(vertLimit, inputs.actualLimit);
                if (maxCompute + maxFragment + maxVertex < inputs.testValue) t.skip("not enough storage bindings across stages");
                std::array<uint64_t, 3> perStage = {maxVertex, maxFragment, maxCompute};
                std::vector<WGPUBindGroupLayout> layouts;
                uint64_t remaining = inputs.testValue;
                for (uint32_t stageBit = 0; stageBit < 3; ++stageBit) {
                    const uint64_t count = std::min(remaining, perStage[stageBit]);
                    remaining -= count;
                    layouts.push_back(createBGL(t, count, static_cast<WGPUShaderStage>(1u << stageBit), type));
                }
                WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
                desc.bindGroupLayoutCount = layouts.size();
                desc.bindGroupLayouts = layouts.data();
                t.expectValidationErrorOnLimitDevice([&] { t.createPipelineLayoutTracked(desc); }, inputs.shouldError);
            }, extraLimits());
    });

} // namespace
