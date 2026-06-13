// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/minStorageBufferOffsetAlignment.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <cstdint>
#include <string>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MinStorageBufferOffsetAlignmentTest : public LimitTest {
  public:
    const char* limitName() const override { return "minStorageBufferOffsetAlignment"; }
};

TestGroup<MinStorageBufferOffsetAlignmentTest> testGroup = MakeTestGroup<MinStorageBufferOffsetAlignmentTest>(
    "api,validation,capability_checks,limits,minStorageBufferOffsetAlignment",
    "API Validation Tests for minStorageBufferOffsetAlignment.");

uint32_t log2PowerOfTwo(uint64_t value) {
    uint32_t shift = 0;
    while ((uint64_t(1) << shift) < value) {
        ++shift;
    }
    return shift;
}

uint64_t powerOfTwo(uint32_t shift) {
    return uint64_t(1) << shift;
}

bool isPowerOfTwoValue(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

uint64_t getDeviceLimitToRequest(std::string_view limitTest, uint64_t defaultLimit, uint64_t minimumLimit) {
    const uint32_t defaultLog = log2PowerOfTwo(defaultLimit);
    const uint32_t minimumLog = log2PowerOfTwo(minimumLimit);
    if (limitTest == "atDefault") return defaultLimit;
    if (limitTest == "overDefault") return powerOfTwo(defaultLog + 1);
    if (limitTest == "betweenDefaultAndMinimum") return powerOfTwo((defaultLog + minimumLog) / 2);
    if (limitTest == "atMinimum") return minimumLimit;
    if (limitTest == "underMinimum") return powerOfTwo(minimumLog - 1);
    std::abort();
}

uint64_t getTestValue(std::string_view testValueName, uint64_t requestedLimit) {
    if (testValueName == "atLimit") return requestedLimit;
    if (testValueName == "underLimit") return powerOfTwo(log2PowerOfTwo(requestedLimit) - 1);
    std::abort();
}

void runCreateBindGroupTest(MinStorageBufferOffsetAlignmentTest& t) {
    const uint64_t requestedLimit =
        getDeviceLimitToRequest(t.param<std::string>("limitTest"), t.defaultLimit, t.adapterLimit);
    const uint64_t testValue = getTestValue(t.param<std::string>("testValueName"), requestedLimit);
    t.testDeviceWithSpecificLimits(requestedLimit, testValue, [&](const SpecificLimitTestInputs& inputs) {
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = inputs.testValue * 2;
        bufferDesc.usage = WGPUBufferUsage_Storage;
        WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &layoutEntry;
        WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(layoutDesc);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = buffer;
        entry.offset = inputs.testValue;
        WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout = layout;
        desc.entryCount = 1;
        desc.entries = &entry;
        t.expectValidationErrorOnLimitDevice([&] { t.createBindGroupTracked(desc); }, inputs.shouldError);
    });
}

void runSetBindGroupTest(MinStorageBufferOffsetAlignmentTest& t) {
    const uint64_t requestedLimit =
        getDeviceLimitToRequest(t.param<std::string>("limitTest"), t.defaultLimit, t.adapterLimit);
    const uint64_t testValue = getTestValue(t.param<std::string>("testValueName"), requestedLimit);
    t.testDeviceWithSpecificLimits(requestedLimit, testValue, [&](const SpecificLimitTestInputs& inputs) {
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = inputs.testValue * 2;
        bufferDesc.usage = WGPUBufferUsage_Storage;
        WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
        layoutEntry.buffer.hasDynamicOffset = WGPU_TRUE;
        WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &layoutEntry;
        WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(layoutDesc);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = inputs.testValue / 2;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        const uint32_t dynamicOffset = static_cast<uint32_t>(inputs.testValue);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        t.expectValidationErrorOnLimitDevice([&] { t.finishTracked(encoder); }, inputs.shouldError);
    });
}

CTS_TEST(testGroup, "createBindGroup,at_over")
    .desc("Test using createBindGroup at and over minStorageBufferOffsetAlignment limit")
    .params([](ParamsBuilder u) { return kMinimumLimitBaseParams(u); })
    .fn([](MinStorageBufferOffsetAlignmentTest& t) { runCreateBindGroupTest(t); });

CTS_TEST(testGroup, "setBindGroup,at_over")
    .desc("Test using setBindGroup at and over minStorageBufferOffsetAlignment limit")
    .params([](ParamsBuilder u) { return kMinimumLimitBaseParams(u); })
    .fn([](MinStorageBufferOffsetAlignmentTest& t) { runSetBindGroupTest(t); });

CTS_TEST(testGroup, "validate,powerOf2")
    .desc("Verify that minStorageBufferOffsetAlignment is power of 2")
    .fn([](MinStorageBufferOffsetAlignmentTest& t) {
        t.expect(isPowerOfTwoValue(t.defaultLimit));
        t.expect(isPowerOfTwoValue(t.adapterLimit));
    });

CTS_TEST(testGroup, "validate,greaterThanOrEqualTo32")
    .desc("Verify that minStorageBufferOffsetAlignment is >= 32")
    .fn([](MinStorageBufferOffsetAlignmentTest& t) {
        t.expect(t.defaultLimit >= 32);
        t.expect(t.adapterLimit >= 32);
    });

} // namespace
