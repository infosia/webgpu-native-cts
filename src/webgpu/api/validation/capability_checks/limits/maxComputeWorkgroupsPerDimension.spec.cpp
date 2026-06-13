// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxComputeWorkgroupsPerDimension.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <array>
#include <string>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxComputeWorkgroupsPerDimensionTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxComputeWorkgroupsPerDimension"; }
};

TestGroup<MaxComputeWorkgroupsPerDimensionTest> testGroup = MakeTestGroup<MaxComputeWorkgroupsPerDimensionTest>(
    "api,validation,capability_checks,limits,maxComputeWorkgroupsPerDimension",
    "API Validation Tests for maxComputeWorkgroupsPerDimension.");

std::vector<Value> computePipelineTypeValues() {
    return {std::string("createComputePipeline"), std::string("createComputePipelineAsync")};
}

CTS_TEST(testGroup, "dispatchWorkgroups,at_over")
    .desc("Test using dispatchWorkgroups at and over maxComputeWorkgroupsPerDimension limit")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u)
            .combine("pipelineType", computePipelineTypeValues())
            .combine("axis", {int64_t(0), int64_t(1), int64_t(2)});
    })
    .fn([](MaxComputeWorkgroupsPerDimensionTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                const int64_t axis = t.param<int64_t>("axis");
                std::array<uint32_t, 3> counts = {1, 1, 1};
                counts[static_cast<size_t>(axis)] = static_cast<uint32_t>(inputs.testValue);

                WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                bufferDesc.size = 16;
                bufferDesc.usage = WGPUBufferUsage_Storage;
                WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

                WGPUShaderModule module = t.createShaderModuleTracked(
                    "@compute @workgroup_size(1) fn main() {\n"
                    "}\n");
                WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                pipelineDesc.layout = nullptr;
                pipelineDesc.compute.module = module;
                pipelineDesc.compute.entryPoint = sv("main");
                WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

                WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
                WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderDispatchWorkgroups(pass, counts[0], counts[1], counts[2]);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);

                t.expectValidationErrorOnLimitDevice([&] {
                    t.finishTracked(encoder);
                }, inputs.shouldError);
                wgpuBufferDestroy(buffer);
            });
    });

CTS_TEST(testGroup, "validate")
    .desc("Test that maxComputeWorkgroupsPerDimension <= maxComputeWorkgroupSizeX x maxComputeWorkgroupSizeY x maxComputeWorkgroupSizeZ")
    .fn([](MaxComputeWorkgroupsPerDimensionTest& t) {
        const uint64_t defaultProduct =
            t.getDefaultLimit("maxComputeWorkgroupSizeX") *
            t.getDefaultLimit("maxComputeWorkgroupSizeY") *
            t.getDefaultLimit("maxComputeWorkgroupSizeZ");
        const uint64_t adapterProduct =
            t.getAdapterLimit("maxComputeWorkgroupSizeX") *
            t.getAdapterLimit("maxComputeWorkgroupSizeY") *
            t.getAdapterLimit("maxComputeWorkgroupSizeZ");
        t.expect(t.defaultLimit <= defaultProduct);
        t.expect(t.adapterLimit <= adapterProduct);
    });

} // namespace
