// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxVertexBufferArrayStride.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxVertexBufferArrayStrideTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxVertexBufferArrayStride"; }
};

TestGroup<MaxVertexBufferArrayStrideTest> testGroup = MakeTestGroup<MaxVertexBufferArrayStrideTest>(
    "api,validation,capability_checks,limits,maxVertexBufferArrayStride",
    "API Validation Tests for maxVertexBufferArrayStride.");

constexpr uint64_t kMinAttributeStride = 4;

uint64_t roundDown(uint64_t value, uint64_t alignment) {
    return value / alignment * alignment;
}

uint64_t getDeviceLimitToRequest(std::string_view limitTest, uint64_t defaultLimit, uint64_t maximumLimit) {
    if (limitTest == "atDefault") return defaultLimit;
    if (limitTest == "underDefault") return defaultLimit - kMinAttributeStride;
    if (limitTest == "betweenDefaultAndMaximum") {
        return std::min(defaultLimit, roundDown((defaultLimit + maximumLimit) / 2, kMinAttributeStride));
    }
    if (limitTest == "atMaximum") return maximumLimit;
    if (limitTest == "overMaximum") return maximumLimit + kMinAttributeStride;
    std::abort();
}

uint64_t getTestValue(std::string_view testValueName, uint64_t requestedLimit) {
    if (testValueName == "atLimit") return requestedLimit;
    if (testValueName == "overLimit") return requestedLimit + kMinAttributeStride;
    std::abort();
}

WGPURenderPipelineDescriptor getPipelineDescriptor(
    MaxVertexBufferArrayStrideTest& t,
    uint64_t testValue,
    WGPUVertexAttribute* attribute,
    WGPUVertexBufferLayout* buffer,
    WGPUDepthStencilState* depth) {
    WGPUShaderModule module = t.createShaderModuleTracked(
        "@vertex fn vs(@location(0) v: f32) -> @builtin(position) vec4f {\n"
        "  return vec4f(v);\n"
        "}\n");
    attribute->format = WGPUVertexFormat_Float32;
    attribute->offset = 0;
    attribute->shaderLocation = 0;
    buffer->arrayStride = testValue;
    buffer->attributeCount = 1;
    buffer->attributes = attribute;
    depth->format = WGPUTextureFormat_Depth32Float;
    depth->depthWriteEnabled = WGPUOptionalBool_True;
    depth->depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = buffer;
    desc.depthStencil = depth;
    return desc;
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .desc("Test using createRenderPipeline(Async) at and over maxVertexBufferArrayStride limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}); })
    .fn([](MaxVertexBufferArrayStrideTest& t) {
        const std::string limitTest = t.param<std::string>("limitTest");
        const std::string testValueName = t.param<std::string>("testValueName");
        const bool async = t.param<bool>("async");
        const uint64_t requestedLimit = getDeviceLimitToRequest(limitTest, t.defaultLimit, t.adapterLimit);
        const uint64_t testValue = getTestValue(testValueName, requestedLimit);
        t.testDeviceWithSpecificLimits(requestedLimit, testValue, [&](const SpecificLimitTestInputs& inputs) {
            WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
            WGPUVertexBufferLayout buffer = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
            WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
            WGPURenderPipelineDescriptor desc = getPipelineDescriptor(t, inputs.testValue, &attribute, &buffer, &depth);
            t.testCreateRenderPipeline(desc, async, inputs.shouldError);
        });
    });

CTS_TEST(testGroup, "validate")
    .desc("Test that maxVertexBufferArrayStride is a multiple of 4 bytes")
    .fn([](MaxVertexBufferArrayStrideTest& t) {
        t.expect(t.defaultLimit % 4 == 0);
        t.expect(t.adapterLimit % 4 == 0);
    });

} // namespace
