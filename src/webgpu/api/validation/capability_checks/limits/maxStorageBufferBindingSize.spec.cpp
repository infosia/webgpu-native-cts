// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxStorageBufferBindingSize.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>
#include <string>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxStorageBufferBindingSizeTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxStorageBufferBindingSize"; }
};

TestGroup<MaxStorageBufferBindingSizeTest> testGroup = MakeTestGroup<MaxStorageBufferBindingSizeTest>(
    "api,validation,capability_checks,limits,maxStorageBufferBindingSize",
    "API Validation Tests for maxStorageBufferBindingSize.");

constexpr uint64_t kStorageBufferRequiredSizeAlignment = 4;

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

uint64_t roundDown(uint64_t value, uint64_t alignment) {
    return value / alignment * alignment;
}

uint64_t getDeviceLimitToRequest(std::string_view limitTest, uint64_t defaultLimit, uint64_t maximumLimit) {
    if (limitTest == "atDefault") return defaultLimit;
    if (limitTest == "underDefault") return defaultLimit - kStorageBufferRequiredSizeAlignment;
    if (limitTest == "betweenDefaultAndMaximum") return (defaultLimit + maximumLimit) / 2;
    if (limitTest == "atMaximum") return maximumLimit;
    if (limitTest == "overMaximum") return maximumLimit + kStorageBufferRequiredSizeAlignment;
    std::abort();
}

uint64_t getTestValue(std::string_view testValueName, uint64_t requestedLimit) {
    if (testValueName == "atLimit") return roundDown(requestedLimit, kStorageBufferRequiredSizeAlignment);
    if (testValueName == "overLimit") {
        return alignTo(requestedLimit + kStorageBufferRequiredSizeAlignment, kStorageBufferRequiredSizeAlignment);
    }
    std::abort();
}

struct SizeAndOffset {
    uint64_t size = 0;
    uint64_t offset = 0;
};

SizeAndOffset getSizeAndOffsetForBufferPart(WGPUDevice device, std::string_view bufferPart, uint64_t size) {
    WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
    const WGPULimits limits = queryLimits(device, &compat);
    if (bufferPart == "wholeBuffer") return SizeAndOffset{size, 0};
    if (bufferPart == "biggerBufferWithOffset") {
        return SizeAndOffset{size + limits.minUniformBufferOffsetAlignment, limits.minUniformBufferOffsetAlignment};
    }
    std::abort();
}

std::vector<Value> bufferParts() {
    return {std::string("wholeBuffer"), std::string("biggerBufferWithOffset")};
}

CTS_TEST(testGroup, "createBindGroup,at_over")
    .desc("Test using createBindGroup at and over maxStorageBufferBindingSize limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("bufferPart", bufferParts()); })
    .fn([](MaxStorageBufferBindingSizeTest& t) {
        const std::string limitTest = t.param<std::string>("limitTest");
        const std::string testValueName = t.param<std::string>("testValueName");
        const std::string bufferPart = t.param<std::string>("bufferPart");
        const uint64_t requestedLimit = getDeviceLimitToRequest(limitTest, t.defaultLimit, t.adapterLimit);
        const uint64_t testValue = getTestValue(testValueName, requestedLimit);
        t.testDeviceWithSpecificLimits(requestedLimit, testValue, [&](const SpecificLimitTestInputs& inputs) {
            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = 0;
            layoutEntry.visibility = WGPUShaderStage_Compute;
            layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
            WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            layoutDesc.entryCount = 1;
            layoutDesc.entries = &layoutEntry;
            WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(layoutDesc);

            const SizeAndOffset part = getSizeAndOffsetForBufferPart(inputs.device, bufferPart, inputs.testValue);
            WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
            const WGPULimits limits = queryLimits(inputs.device, &compat);
            if (part.size > limits.maxBufferSize) return;

            wgpuDevicePushErrorScope(t.device(), WGPUErrorFilter_OutOfMemory);
            WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            bufferDesc.size = part.size;
            bufferDesc.usage = WGPUBufferUsage_Storage;
            WGPUBuffer buffer = t.createBufferTracked(bufferDesc);
            ScopeResult oom = t.popErrorScopeOnLimitDevice();
            if (oom.status == WGPUPopErrorScopeStatus_Success && oom.type == WGPUErrorType_NoError) {
                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = 0;
                entry.buffer = buffer;
                entry.offset = part.offset;
                entry.size = inputs.testValue;
                WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                desc.layout = layout;
                desc.entryCount = 1;
                desc.entries = &entry;
                t.expectValidationErrorOnLimitDevice([&] { t.createBindGroupTracked(desc); }, inputs.shouldError);
            }
        }, {adapterLimitRequest("maxBufferSize")});
    });

CTS_TEST(testGroup, "validate")
    .desc("Test that maxStorageBufferBindingSize is a multiple of 4 bytes")
    .fn([](MaxStorageBufferBindingSizeTest& t) {
        t.expect(t.defaultLimit % 4 == 0);
        t.expect(t.adapterLimit % 4 == 0);
    });

CTS_TEST(testGroup, "validate,maxBufferSize")
    .desc("Test that maxStorageBufferBindingSize <= maxBufferSize")
    .fn([](MaxStorageBufferBindingSizeTest& t) {
        t.expect(t.defaultLimit <= t.getDefaultLimit("maxBufferSize"));
        t.expect(t.adapterLimit <= t.getAdapterLimit("maxBufferSize"));
    });

} // namespace
