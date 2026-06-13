// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxBindingsPerBindGroup.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <string>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxBindingsPerBindGroupTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxBindingsPerBindGroup"; }
};

TestGroup<MaxBindingsPerBindGroupTest> testGroup = MakeTestGroup<MaxBindingsPerBindGroupTest>(
    "api,validation,capability_checks,limits,maxBindingsPerBindGroup",
    "API Validation Tests for maxBindingsPerBindGroup.");

CTS_TEST(testGroup, "createBindGroupLayout,at_over")
    .desc("Test using createBindGroupLayout at and over maxBindingsPerBindGroup limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxBindingsPerBindGroupTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.expectValidationErrorOnLimitDevice([&] {
                    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                    entry.binding = static_cast<uint32_t>(inputs.testValue - 1);
                    entry.visibility = WGPUShaderStage_Vertex;
                    entry.buffer.type = WGPUBufferBindingType_Uniform;
                    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
                    desc.entryCount = 1;
                    desc.entries = &entry;
                    t.createBindGroupLayoutTracked(desc);
                }, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "createPipeline,at_over")
    .desc("Test using createRenderPipeline(Async) and createComputePipeline(Async) at and over maxBindingsPerBindGroup limit")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u)
            .combine("createPipelineType", kCreatePipelineTypeValues())
            .combine("async", {false, true});
    })
    .fn([](MaxBindingsPerBindGroupTest& t) {
        const std::string createPipelineType = t.param<std::string>("createPipelineType");
        const bool async = t.param<bool>("async");
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                const uint64_t lastIndex = inputs.testValue - 1;
                const std::string code = t.getBindingIndexWGSLForPipelineType(createPipelineType, lastIndex);
                WGPUShaderModule module = t.createShaderModuleTracked(code);
                t.testCreatePipeline(createPipelineType, async, module, inputs.shouldError, code);
            });
    });

CTS_TEST(testGroup, "validate")
    .desc("Test maxBindingsPerBindGroup matches the spec limits")
    .fn([](MaxBindingsPerBindGroupTest& t) {
        const uint64_t maxBindingsPerShaderStage =
            t.getAdapterLimit("maxSampledTexturesPerShaderStage") +
            t.getAdapterLimit("maxSamplersPerShaderStage") +
            t.getAdapterLimit("maxStorageBuffersPerShaderStage") +
            t.getAdapterLimit("maxStorageTexturesPerShaderStage") +
            t.getAdapterLimit("maxUniformBuffersPerShaderStage");
        const uint64_t maxShaderStagesPerPipeline = 2;
        const uint64_t minimum = maxBindingsPerShaderStage * maxShaderStagesPerPipeline;
        t.expect(t.adapterLimit >= minimum);
    });

} // namespace
