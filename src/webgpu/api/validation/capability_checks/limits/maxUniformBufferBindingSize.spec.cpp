// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxUniformBufferBindingSize.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <string>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxUniformBufferBindingSizeTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxUniformBufferBindingSize"; }
};

TestGroup<MaxUniformBufferBindingSizeTest> testGroup = MakeTestGroup<MaxUniformBufferBindingSizeTest>(
    "api,validation,capability_checks,limits,maxUniformBufferBindingSize",
    "API Validation Tests for maxUniformBufferBindingSize.");

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
    .desc("Test using at and over maxUniformBufferBindingSize limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("bufferPart", bufferParts()); })
    .fn([](MaxUniformBufferBindingSizeTest& t) {
        const std::string bufferPart = t.param<std::string>("bufferPart");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                layoutEntry.binding = 0;
                layoutEntry.visibility = WGPUShaderStage_Vertex;
                layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
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
                bufferDesc.usage = WGPUBufferUsage_Uniform;
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

CTS_TEST(testGroup, "validate,maxBufferSize")
    .desc("Test that maxUniformBufferBindingSize <= maxBufferSize")
    .fn([](MaxUniformBufferBindingSizeTest& t) {
        t.expect(t.defaultLimit <= t.getDefaultLimit("maxBufferSize"));
        t.expect(t.adapterLimit <= t.getAdapterLimit("maxBufferSize"));
    });

} // namespace
