// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxVertexAttributes.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <sstream>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxVertexAttributesTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxVertexAttributes"; }
};

TestGroup<MaxVertexAttributesTest> testGroup = MakeTestGroup<MaxVertexAttributesTest>(
    "api,validation,capability_checks,limits,maxVertexAttributes",
    "API Validation Tests for maxVertexAttributes.");

WGPURenderPipelineDescriptor getPipelineDescriptor(
    MaxVertexAttributesTest& t,
    uint64_t lastIndex,
    WGPUVertexAttribute* attribute,
    WGPUVertexBufferLayout* buffer,
    WGPUDepthStencilState* depth) {
    std::ostringstream code;
    code << "@vertex fn vs(@location(" << lastIndex << ") v: vec4f) -> @builtin(position) vec4f {\n"
         << "  return v;\n"
         << "}\n";
    WGPUShaderModule module = t.createShaderModuleTracked(code.str());
    attribute->format = WGPUVertexFormat_Float32x4;
    attribute->offset = 0;
    attribute->shaderLocation = static_cast<uint32_t>(lastIndex);
    buffer->arrayStride = 32;
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
    .desc("Test using createRenderPipeline(Async) at and over maxVertexAttributes limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}); })
    .fn([](MaxVertexAttributesTest& t) {
        const bool async = t.param<bool>("async");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
                WGPUVertexBufferLayout buffer = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
                WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
                WGPURenderPipelineDescriptor desc =
                    getPipelineDescriptor(t, inputs.testValue - 1, &attribute, &buffer, &depth);
                t.testCreateRenderPipeline(desc, async, inputs.shouldError);
            });
    });

} // namespace
