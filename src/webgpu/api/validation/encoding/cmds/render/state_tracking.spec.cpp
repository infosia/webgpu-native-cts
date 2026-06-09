// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render/state_tracking.spec.ts @ 492fefb37fe

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// WGPUStringView helper
// ---------------------------------------------------------------------------
static WGPUStringView sv(const char* s) {
    WGPUStringView view = WGPU_STRING_VIEW_INIT;
    view.data   = s;
    view.length = WGPU_STRLEN;
    return view;
}

// ---------------------------------------------------------------------------
// TestGroup
// ---------------------------------------------------------------------------
TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render,state_tracking",
    "Validation tests for setVertexBuffer/setIndexBuffer state (not validation). See also operation tests.");

// ---------------------------------------------------------------------------
// Helpers — mirrors the F fixture class methods from the upstream TypeScript.
// ---------------------------------------------------------------------------

// getVertexBuffer: creates a 256-byte VERTEX buffer.
static WGPUBuffer getVertexBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size  = 256;
    desc.usage = WGPUBufferUsage_Vertex;
    return t.createBufferTracked(desc);
}

// createRenderPipeline: creates a render pipeline that expects `bufferCount`
// vertex buffers, each supplying one vec3<f32> attribute at location i.
// Mirrors F.createRenderPipeline(bufferCount) from the upstream fixture.
static WGPURenderPipeline createRenderPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t bufferCount)
{
    // Build WGSL vertex shader with bufferCount @location attributes.
    // e.g. bufferCount=1:
    //   struct Inputs { @location(0) a_position0 : vec3<f32>, };
    std::string inputFields;
    for (uint32_t i = 0; i < bufferCount; ++i) {
        inputFields += "@location(" + std::to_string(i) + ") a_position"
                     + std::to_string(i) + " : vec3<f32>,\n";
    }
    std::string vertWGSL =
        "struct Inputs {\n" + inputFields + "};\n"
        "@vertex fn main(input : Inputs) -> @builtin(position) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
        "}\n";

    const std::string_view fragWGSL =
        "@fragment fn main() -> @location(0) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
        "}\n";

    WGPUShaderModule vertModule = t.createShaderModuleTracked(
        std::string_view(vertWGSL.data(), vertWGSL.size()));
    WGPUShaderModule fragModule = t.createShaderModuleTracked(fragWGSL);

    // Vertex buffer layout: all attributes share one buffer layout entry
    // (arrayStride=12, one attribute per location).
    std::vector<WGPUVertexAttribute> attributes(bufferCount);
    for (uint32_t i = 0; i < bufferCount; ++i) {
        attributes[i].format         = WGPUVertexFormat_Float32x3;
        attributes[i].offset         = 0;
        attributes[i].shaderLocation = i;
    }

    WGPUVertexBufferLayout vbLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbLayout.arrayStride    = 3 * 4; // 12 bytes
    vbLayout.stepMode       = WGPUVertexStepMode_Vertex;
    vbLayout.attributeCount = bufferCount;
    vbLayout.attributes     = attributes.data();

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout                = nullptr; // auto
    desc.vertex.module         = vertModule;
    desc.vertex.entryPoint     = sv("main");
    // Upstream uses ONE buffer slot (buffers array has 1 element) with
    // bufferCount attributes inside it — not bufferCount separate slots.
    desc.vertex.bufferCount    = (bufferCount > 0) ? 1u : 0u;
    desc.vertex.buffers        = (bufferCount > 0) ? &vbLayout : nullptr;
    desc.primitive.topology    = WGPUPrimitiveTopology_TriangleList;
    desc.fragment              = &fragment;

    return t.createRenderPipelineTracked(desc);
}

// beginRenderPass: creates a 16x16 rgba8unorm RenderAttachment texture and
// starts a render pass on it. Mirrors F.beginRenderPass(commandEncoder).
static WGPURenderPassEncoder beginRenderPass(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUCommandEncoder cmdEnc)
{
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{16, 16, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment;
    WGPUTexture tex = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(tex, viewDesc);

    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view       = view;
    colorAttach.loadOp     = WGPULoadOp_Clear;
    colorAttach.storeOp    = WGPUStoreOp_Store;
    colorAttach.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;

    return wgpuCommandEncoderBeginRenderPass(cmdEnc, &passDesc);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// g.test('all_needed_vertex_buffer_should_be_bound') — upstream .unimplemented()
CTS_TEST(g, "all_needed_vertex_buffer_should_be_bound")
    .desc(
        "In this test we test that any missing vertex buffer for a used slot will cause\n"
        "validation errors when drawing.\n"
        "- All (non/indexed, in/direct) draw commands\n"
        "    - A needed vertex buffer is not bound\n"
        "        - Was bound in another render pass but not the current one")
    .unimplemented("upstream is .unimplemented() — test body not yet written in CTS");

// g.test('all_needed_index_buffer_should_be_bound') — upstream .unimplemented()
CTS_TEST(g, "all_needed_index_buffer_should_be_bound")
    .desc(
        "In this test we test that missing index buffer for a used slot will cause\n"
        "validation errors when drawing.\n"
        "- All indexed in/direct draw commands\n"
        "    - No index buffer is bound")
    .unimplemented("upstream is .unimplemented() — test body not yet written in CTS");

// g.test('vertex_buffers_inherit_from_previous_pipeline')
// Checks that:
//   - draw without any vertex buffer set → validation error on finish()
//   - draw after switching from pipeline2→pipeline1, with vb[0..1] already set
//     (pipeline1 needs only slot 0) → no error
CTS_TEST(g, "vertex_buffers_inherit_from_previous_pipeline")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPURenderPipeline pipeline1 = createRenderPipeline(t, 1);
        WGPURenderPipeline pipeline2 = createRenderPipeline(t, 2);

        WGPUBuffer vertexBuffer1 = getVertexBuffer(t);
        WGPUBuffer vertexBuffer2 = getVertexBuffer(t);

        // Check failure when vertex buffer is not set.
        {
            WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
            WGPURenderPassEncoder renderPass = beginRenderPass(t, cmdEnc);
            wgpuRenderPassEncoderSetPipeline(renderPass, pipeline1);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);

            t.expectValidationError([&] {
                t.finishTracked(cmdEnc);
            }, true);
        }

        // Check success when vertex buffer is inherited from previous pipeline.
        {
            WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
            WGPURenderPassEncoder renderPass = beginRenderPass(t, cmdEnc);
            wgpuRenderPassEncoderSetPipeline(renderPass, pipeline2);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer1, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 1, vertexBuffer2, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderSetPipeline(renderPass, pipeline1);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);

            WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
            t.expectValidationError([&] {
                wgpuQueueSubmit(t.queue(), 1, &cb);
            }, false);
        }
    });

// g.test('vertex_buffers_do_not_inherit_between_render_passes')
// Checks that:
//   - vertex buffers set in one pass are available in that pass → no error
//   - vertex buffers are NOT carried over to a subsequent render pass → validation error on finish()
CTS_TEST(g, "vertex_buffers_do_not_inherit_between_render_passes")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPURenderPipeline pipeline1 = createRenderPipeline(t, 1);
        WGPURenderPipeline pipeline2 = createRenderPipeline(t, 2);

        WGPUBuffer vertexBuffer1 = getVertexBuffer(t);
        WGPUBuffer vertexBuffer2 = getVertexBuffer(t);

        // Check success when vertex buffer is set for each render pass.
        {
            WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
            {
                WGPURenderPassEncoder renderPass = beginRenderPass(t, cmdEnc);
                wgpuRenderPassEncoderSetPipeline(renderPass, pipeline2);
                wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer1, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderSetVertexBuffer(renderPass, 1, vertexBuffer2, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(renderPass);
                wgpuRenderPassEncoderRelease(renderPass);
            }
            {
                WGPURenderPassEncoder renderPass = beginRenderPass(t, cmdEnc);
                wgpuRenderPassEncoderSetPipeline(renderPass, pipeline1);
                wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer1, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(renderPass);
                wgpuRenderPassEncoderRelease(renderPass);
            }
            WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
            t.expectValidationError([&] {
                wgpuQueueSubmit(t.queue(), 1, &cb);
            }, false);
        }

        // Check failure because vertex buffer is not inherited in second render pass.
        {
            WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
            {
                WGPURenderPassEncoder renderPass = beginRenderPass(t, cmdEnc);
                wgpuRenderPassEncoderSetPipeline(renderPass, pipeline2);
                wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer1, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderSetVertexBuffer(renderPass, 1, vertexBuffer2, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(renderPass);
                wgpuRenderPassEncoderRelease(renderPass);
            }
            {
                WGPURenderPassEncoder renderPass = beginRenderPass(t, cmdEnc);
                wgpuRenderPassEncoderSetPipeline(renderPass, pipeline1);
                // vertex buffer NOT set — should error
                wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(renderPass);
                wgpuRenderPassEncoderRelease(renderPass);
            }

            t.expectValidationError([&] {
                t.finishTracked(cmdEnc);
            }, true);
        }
    });

} // namespace
