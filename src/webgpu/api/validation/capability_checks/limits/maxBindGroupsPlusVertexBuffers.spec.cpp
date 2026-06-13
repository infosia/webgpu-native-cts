// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxBindGroupsPlusVertexBuffers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>
#include <sstream>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxBindGroupsPlusVertexBuffersTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxBindGroupsPlusVertexBuffers"; }
};

TestGroup<MaxBindGroupsPlusVertexBuffersTest> testGroup = MakeTestGroup<MaxBindGroupsPlusVertexBuffersTest>(
    "api,validation,capability_checks,limits,maxBindGroupsPlusVertexBuffers",
    "API Validation Tests for maxBindGroupsPlusVertexBuffers.");

std::vector<Value> preferences() {
    return {std::string("vertexBuffers"), std::string("bindGroups")};
}

std::vector<Value> layoutTypes() {
    return {std::string("auto"), std::string("explicit")};
}

std::vector<Value> renderEncoderTypes() {
    return {std::string("render"), std::string("renderBundle")};
}

std::vector<Value> drawTypes() {
    return {std::string("draw"), std::string("drawIndexed"), std::string("drawIndirect"), std::string("drawIndexedIndirect")};
}

struct Counts {
    uint64_t vertexBuffers = 0;
    uint64_t bindGroups = 0;
};

Counts countsFor(WGPUDevice device, std::string_view preference, uint64_t testValue) {
    WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
    const WGPULimits limits = queryLimits(device, &compat);
    if (preference == "bindGroups") {
        const uint64_t bindGroups = std::min<uint64_t>(testValue, limits.maxBindGroups);
        return Counts{testValue - bindGroups, bindGroups};
    }
    const uint64_t vertexBuffers = std::min<uint64_t>(testValue, limits.maxVertexBuffers);
    return Counts{vertexBuffers, testValue - vertexBuffers};
}

WGPUPipelineLayout createExplicitLayout(MaxBindGroupsPlusVertexBuffersTest& t, uint64_t numBindGroups) {
    std::vector<WGPUBindGroupLayout> layouts;
    for (uint64_t i = 0; i < numBindGroups; ++i) {
        WGPUBindGroupLayoutDescriptor emptyDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layouts.push_back(t.createBindGroupLayoutTracked(emptyDesc));
    }
    if (numBindGroups > 0) {
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        layouts.back() = t.createBindGroupLayoutTracked(bglDesc);
    }
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    return t.createPipelineLayoutTracked(desc);
}

WGPURenderPipelineDescriptor pipelineDesc(
    MaxBindGroupsPlusVertexBuffersTest& t,
    const Counts& counts,
    std::string_view layoutType,
    std::vector<WGPUVertexBufferLayout>* buffers,
    WGPUVertexAttribute* attribute,
    WGPUFragmentState* fragment,
    WGPUColorTargetState* target) {
    std::ostringstream code;
    if (counts.bindGroups > 0) {
        code << "@group(" << (counts.bindGroups - 1) << ") @binding(0) var<uniform> u: f32;\n";
    }
    code << "@vertex fn vs(";
    if (counts.vertexBuffers > 0) {
        code << "@location(0) v: vec4f";
    }
    code << ") -> @builtin(position) vec4f {\n";
    if (counts.bindGroups > 0) code << "  _ = u;\n";
    if (counts.vertexBuffers > 0) code << "  _ = v;\n";
    code << "  return vec4f(0);\n"
         << "}\n"
         << "@fragment fn fs() -> @location(0) vec4f { return vec4f(0); }\n";
    WGPUShaderModule module = t.createShaderModuleTracked(code.str());
    buffers->assign(static_cast<size_t>(counts.vertexBuffers), WGPU_VERTEX_BUFFER_LAYOUT_INIT);
    if (counts.vertexBuffers > 0) {
        attribute->format = WGPUVertexFormat_Float32;
        attribute->offset = 0;
        attribute->shaderLocation = 0;
        (*buffers)[static_cast<size_t>(counts.vertexBuffers - 1)].arrayStride = 16;
        (*buffers)[static_cast<size_t>(counts.vertexBuffers - 1)].attributeCount = 1;
        (*buffers)[static_cast<size_t>(counts.vertexBuffers - 1)].attributes = attribute;
    }
    target->format = WGPUTextureFormat_RGBA8Unorm;
    target->writeMask = WGPUColorWriteMask_All;
    *fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment->module = module;
    fragment->entryPoint = sv("fs");
    fragment->targetCount = 1;
    fragment->targets = target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layoutType == "explicit" ? createExplicitLayout(t, counts.bindGroups) : nullptr;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs");
    desc.vertex.bufferCount = buffers->size();
    desc.vertex.buffers = buffers->empty() ? nullptr : buffers->data();
    desc.fragment = fragment;
    return desc;
}

std::vector<LimitRequestEntry> extraLimits() {
    return {adapterLimitRequest("maxBindGroups"), adapterLimitRequest("maxVertexBuffers")};
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u).combine("async", {false, true}).beginSubcases()
            .combine("preference", preferences()).combine("layoutType", layoutTypes());
    })
    .fn([](MaxBindGroupsPlusVertexBuffersTest& t) {
        const bool async = t.param<bool>("async");
        const std::string preference = t.param<std::string>("preference");
        const std::string layoutType = t.param<std::string>("layoutType");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                const WGPULimits limits = queryLimits(inputs.device, &compat);
                const uint64_t usable = limits.maxBindGroups + limits.maxVertexBuffers;
                if (usable < inputs.actualLimit || (usable == inputs.actualLimit && inputs.testValue > inputs.actualLimit)) {
                    t.skip("not enough usable bind groups plus vertex buffers");
                }
                Counts counts = countsFor(inputs.device, preference, inputs.testValue);
                std::vector<WGPUVertexBufferLayout> buffers;
                WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
                WGPURenderPipelineDescriptor desc =
                    pipelineDesc(t, counts, layoutType, &buffers, &attribute, &fragment, &target);
                t.testCreateRenderPipeline(desc, async, inputs.shouldError);
            }, extraLimits());
    });

CTS_TEST(testGroup, "draw,at_over")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u).combine("encoderType", renderEncoderTypes()).beginSubcases()
            .combine("preference", preferences()).combine("drawType", drawTypes());
    })
    .fn([](MaxBindGroupsPlusVertexBuffersTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string preference = t.param<std::string>("preference");
        const std::string drawType = t.param<std::string>("drawType");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                const WGPULimits limits = queryLimits(inputs.device, &compat);
                const uint64_t usable = limits.maxBindGroups + std::min(limits.maxVertexBuffers, limits.maxVertexAttributes);
                if (usable < inputs.actualLimit || (usable == inputs.actualLimit && inputs.testValue > inputs.actualLimit)) {
                    t.skip("not enough usable bind groups plus vertex buffers");
                }
                Counts counts = countsFor(inputs.device, preference, inputs.testValue);
                WGPUShaderModule module = t.createShaderModuleTracked(
                    "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0); }\n"
                    "@fragment fn fs() -> @location(0) vec4f { return vec4f(0); }\n");
                WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
                target.format = WGPUTextureFormat_RGBA8Unorm;
                target.writeMask = WGPUColorWriteMask_All;
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                fragment.module = module;
                fragment.entryPoint = sv("fs");
                fragment.targetCount = 1;
                fragment.targets = &target;
                WGPURenderPipelineDescriptor pipelineDescValue = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                pipelineDescValue.layout = nullptr;
                pipelineDescValue.vertex.module = module;
                pipelineDescValue.vertex.entryPoint = sv("vs");
                pipelineDescValue.fragment = &fragment;
                WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDescValue);
                WGPUBufferDescriptor vbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                vbDesc.size = 16;
                vbDesc.usage = WGPUBufferUsage_Vertex;
                WGPUBuffer vertexBuffer = t.createBufferTracked(vbDesc);
                WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                indirectDesc.size = 20;
                indirectDesc.usage = WGPUBufferUsage_Indirect;
                WGPUBuffer indirectBuffer = t.createBufferTracked(indirectDesc);
                WGPUBufferDescriptor indexDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                indexDesc.size = 4;
                indexDesc.usage = WGPUBufferUsage_Index;
                WGPUBuffer indexBuffer = t.createBufferTracked(indexDesc);
                t.testGPURenderAndBindingCommandsMixin(encoderType, [&](const LimitTest::BindingCommandContext& ctx) {
                    ctx.setVertexBuffer(limits.maxVertexBuffers - 1, vertexBuffer);
                    ctx.setVertexBuffer(limits.maxVertexBuffers - 1, nullptr);
                    ctx.setBindGroup(limits.maxBindGroups - 1, ctx.bindGroup);
                    ctx.setBindGroup(limits.maxBindGroups - 1, nullptr);
                    if (counts.vertexBuffers > 0) ctx.setVertexBuffer(static_cast<uint32_t>(counts.vertexBuffers - 1), vertexBuffer);
                    if (counts.bindGroups > 0) ctx.setBindGroup(static_cast<uint32_t>(counts.bindGroups - 1), ctx.bindGroup);
                    ctx.setPipeline(pipeline);
                    if (drawType == "drawIndexed" || drawType == "drawIndexedIndirect") ctx.setIndexBuffer(indexBuffer);
                    ctx.draw(drawType, indirectBuffer);
                }, inputs.shouldError);
            }, extraLimits());
    });

} // namespace
