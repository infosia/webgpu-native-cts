// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxVertexBuffers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxVertexBuffersTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxVertexBuffers"; }
};

TestGroup<MaxVertexBuffersTest> testGroup = MakeTestGroup<MaxVertexBuffersTest>(
    "api,validation,capability_checks,limits,maxVertexBuffers",
    "API Validation Tests for maxVertexBuffers.");

std::vector<Value> renderEncoderTypes() {
    return {std::string("render"), std::string("renderBundle")};
}

WGPURenderPipelineDescriptor pipelineDesc(
    MaxVertexBuffersTest& t,
    uint64_t testValue,
    std::vector<WGPUVertexBufferLayout>* buffers,
    WGPUVertexAttribute* attribute,
    WGPUDepthStencilState* depth) {
    WGPUShaderModule module = t.createShaderModuleTracked(
        "@vertex fn vs(@location(0) p: vec4f) -> @builtin(position) vec4f { return p; }\n");
    buffers->assign(static_cast<size_t>(testValue), WGPU_VERTEX_BUFFER_LAYOUT_INIT);
    attribute->format = WGPUVertexFormat_Float32;
    attribute->offset = 0;
    attribute->shaderLocation = 0;
    (*buffers)[static_cast<size_t>(testValue - 1)].arrayStride = 16;
    (*buffers)[static_cast<size_t>(testValue - 1)].attributeCount = 1;
    (*buffers)[static_cast<size_t>(testValue - 1)].attributes = attribute;
    depth->format = WGPUTextureFormat_Depth32Float;
    depth->depthWriteEnabled = WGPUOptionalBool_True;
    depth->depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs");
    desc.vertex.bufferCount = buffers->size();
    desc.vertex.buffers = buffers->data();
    desc.depthStencil = depth;
    return desc;
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}); })
    .fn([](MaxVertexBuffersTest& t) {
        const bool async = t.param<bool>("async");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUVertexBufferLayout> buffers;
                WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
                WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
                WGPURenderPipelineDescriptor desc = pipelineDesc(t, inputs.testValue, &buffers, &attribute, &depth);
                t.testCreateRenderPipeline(desc, async, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "setVertexBuffer,at_over")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("encoderType", renderEncoderTypes()); })
    .fn([](MaxVertexBuffersTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
                desc.size = 16;
                desc.usage = WGPUBufferUsage_Vertex;
                WGPUBuffer buffer = t.createBufferTracked(desc);
                t.testGPURenderAndBindingCommandsMixin(encoderType, [&](const LimitTest::BindingCommandContext& ctx) {
                    ctx.setVertexBuffer(static_cast<uint32_t>(inputs.testValue - 1), buffer);
                }, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "validate,maxBindGroupsPlusVertexBuffers")
    .fn([](MaxVertexBuffersTest& t) {
        t.expect(t.defaultLimit <= t.getDefaultLimit("maxBindGroupsPlusVertexBuffers"));
        t.expect(t.adapterLimit <= t.getAdapterLimit("maxBindGroupsPlusVertexBuffers"));
    });

} // namespace
