// Ported from gpuweb/cts src/webgpu/shader/execution/shader_io/user_io.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for reviewer):
// - Upstream creates a single WGPUBuffer and sets three vertex-buffer slots
//   from different offsets into it (offsets 0, 16, 48).  The C API call
//   wgpuRenderPassEncoderSetVertexBuffer accepts (slot, buffer, offset, size),
//   so this is a direct 1:1 translation.
// - Upstream uses t.makeBufferWithContents (Uint32Array) for the combined
//   vertex buffer; this port uses t.makeBufferWithContents with a flat
//   std::vector<uint32_t>.
// - The upstream output buffer size formula (product of component counts ×
//   numTextures × bytesPerComponent × copyWidth = 384 bytes) is preserved
//   verbatim; actual written data only occupies 112 bytes.
// - The three output textures each have width=64 (= 256 / bytesPerComponent)
//   to satisfy the minimum 256-bytes-per-row rule for texture-to-buffer copies.
// - Texture-to-buffer copies use wgpuCommandEncoderCopyTextureToBuffer with
//   per-texture bytesPerRow = bytesPerComponent × components[i] × width.
// - f16 subcase: runtime-skipped with reason when the device lacks
//   WGPUFeatureName_ShaderF16 (mirrors upstream skipIfDeviceDoesNotHaveFeature).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,shader_io,user_io",
    R"(
Test for user-defined shader I/O.

passthrough:
  * Data passed into vertex shader as uints and converted to test type
  * Passed from vertex to fragment as test type
  * Output from fragment shader as uint
)");

static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// generateInterstagePassthroughCode: direct port of the upstream helper.
// Produces a WGSL module with vsMain + fsMain.
//   type       — one of "f32", "f16", "i32", "u32"
static std::string generateInterstagePassthroughCode(const std::string& type) {
    std::string src;
    if (type == "f16") {
        src += "enable f16;\n";
    }
    src +=
        "struct IOData {\n"
        "  @builtin(position) pos : vec4f,\n"
        "  @location(0) @interpolate(flat, either) user0 : " + type + ",\n"
        "  @location(1) @interpolate(flat, either) user1 : vec2<" + type + ">,\n"
        "  @location(2) @interpolate(flat, either) user2 : vec4<" + type + ">,\n"
        "}\n"
        "\n"
        "struct VertexInput {\n"
        "  @builtin(vertex_index) idx : u32,\n"
        "  @location(0) in0 : u32,\n"
        "  @location(1) in1 : vec2u,\n"
        "  @location(2) in2 : vec4u,\n"
        "}\n"
        "\n"
        "@vertex\n"
        "fn vsMain(input : VertexInput) -> IOData {\n"
        "  const vertices = array(\n"
        "    vec4f(-1, -1, 0, 1),\n"
        "    vec4f(-1,  1, 0, 1),\n"
        "    vec4f( 1, -1, 0, 1),\n"
        "  );\n"
        "  var data : IOData;\n"
        "  data.pos = vertices[input.idx];\n"
        "  data.user0 = " + type + "(input.in0);\n"
        "  data.user1 = vec2<" + type + ">(input.in1);\n"
        "  data.user2 = vec4<" + type + ">(input.in2);\n"
        "  return data;\n"
        "}\n"
        "\n"
        "struct FragOutput {\n"
        "  @location(0) out0 : u32,\n"
        "  @location(1) out1 : vec2u,\n"
        "  @location(2) out2 : vec4u,\n"
        "}\n"
        "\n"
        "@fragment\n"
        "fn fsMain(input : IOData) -> FragOutput {\n"
        "  var out : FragOutput;\n"
        "  out.out0 = u32(input.user0);\n"
        "  out.out1 = vec2u(input.user1);\n"
        "  out.out2 = vec4u(input.user2);\n"
        "  return out;\n"
        "}\n";
    return src;
}

// drawPassthrough: direct port of the upstream helper.
// Creates a render pipeline with 3 vertex-buffer slots and 3 color attachments
// (r32uint, rg32uint, rgba32uint), renders a triangle, copies 4 pixels from
// each attachment into a single output buffer, then checks the values.
//
// Expected output (per upstream):
//   out0 (r32uint,  1 component, value=1): [1, 1, 1, 1]
//   out1 (rg32uint, 2 components, value=2): [2,2, 2,2, 2,2, 2,2]
//   out2 (rgba32uint, 4 components, value=3): [3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3]
static void drawPassthrough(AllFeaturesMaxLimitsGpuTest& t, const std::string& code) {
    // Upstream: formats=['r32uint','rg32uint','rgba32uint'], components=[1,2,4].
    const WGPUTextureFormat formats[3] = {
        WGPUTextureFormat_R32Uint,
        WGPUTextureFormat_RG32Uint,
        WGPUTextureFormat_RGBA32Uint,
    };
    const uint32_t components[3] = {1, 2, 4};

    const uint32_t bytesPerComponent = 4;
    // width = 256 / bytesPerComponent = 64  (ensures bytesPerRow >= 256).
    const uint32_t width = 256 / bytesPerComponent;
    const uint32_t height = 2;
    const uint32_t copyWidth = 4;

    // -----------------------------------------------------------------
    // Shader module (shared by vertex + fragment).
    // -----------------------------------------------------------------
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

    // -----------------------------------------------------------------
    // Vertex attribute + buffer layouts: 3 slots.
    //   slot 0: scalar u32, arrayStride=4
    //   slot 1: vec2u,      arrayStride=8
    //   slot 2: vec4u,      arrayStride=16
    // -----------------------------------------------------------------
    WGPUVertexAttribute attr0 = WGPU_VERTEX_ATTRIBUTE_INIT;
    attr0.format         = WGPUVertexFormat_Uint32;
    attr0.offset         = 0;
    attr0.shaderLocation = 0;

    WGPUVertexBufferLayout vbl0 = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbl0.arrayStride    = 4;
    vbl0.stepMode       = WGPUVertexStepMode_Vertex;
    vbl0.attributeCount = 1;
    vbl0.attributes     = &attr0;

    WGPUVertexAttribute attr1 = WGPU_VERTEX_ATTRIBUTE_INIT;
    attr1.format         = WGPUVertexFormat_Uint32x2;
    attr1.offset         = 0;
    attr1.shaderLocation = 1;

    WGPUVertexBufferLayout vbl1 = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbl1.arrayStride    = 8;
    vbl1.stepMode       = WGPUVertexStepMode_Vertex;
    vbl1.attributeCount = 1;
    vbl1.attributes     = &attr1;

    WGPUVertexAttribute attr2 = WGPU_VERTEX_ATTRIBUTE_INIT;
    attr2.format         = WGPUVertexFormat_Uint32x4;
    attr2.offset         = 0;
    attr2.shaderLocation = 2;

    WGPUVertexBufferLayout vbl2 = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbl2.arrayStride    = 16;
    vbl2.stepMode       = WGPUVertexStepMode_Vertex;
    vbl2.attributeCount = 1;
    vbl2.attributes     = &attr2;

    const WGPUVertexBufferLayout vertexBufferLayouts[3] = {vbl0, vbl1, vbl2};

    // -----------------------------------------------------------------
    // Color target states: one per output texture format.
    // -----------------------------------------------------------------
    WGPUColorTargetState ct0 = WGPU_COLOR_TARGET_STATE_INIT;
    ct0.format = WGPUTextureFormat_R32Uint;

    WGPUColorTargetState ct1 = WGPU_COLOR_TARGET_STATE_INIT;
    ct1.format = WGPUTextureFormat_RG32Uint;

    WGPUColorTargetState ct2 = WGPU_COLOR_TARGET_STATE_INIT;
    ct2.format = WGPUTextureFormat_RGBA32Uint;

    const WGPUColorTargetState colorTargets[3] = {ct0, ct1, ct2};

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = shaderModule;
    fragment.entryPoint  = sv("fsMain");
    fragment.targetCount = 3;
    fragment.targets     = colorTargets;

    // -----------------------------------------------------------------
    // Render pipeline (auto layout — no bind groups needed).
    // -----------------------------------------------------------------
    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout                 = nullptr; // auto
    rpDesc.vertex.module          = shaderModule;
    rpDesc.vertex.entryPoint      = sv("vsMain");
    rpDesc.vertex.bufferCount     = 3;
    rpDesc.vertex.buffers         = vertexBufferLayouts;
    rpDesc.primitive.topology     = WGPUPrimitiveTopology_TriangleList;
    rpDesc.multisample.count      = 1;
    rpDesc.fragment               = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    // -----------------------------------------------------------------
    // Combined vertex buffer.
    // Upstream Uint32Array layout (64 words = 256 bytes):
    //   [0..3]  = scalar values: 1, 1, 1, 0    (3 verts × 1 u32, +1 pad)
    //   [4..15] = padding (unused between offset 16 and 48)
    //   [4..9]  (offset 16): vec2 values: (2,2),(2,2),(2,2) = 6 u32, +2 pad
    //   [12..27](offset 48): vec4 values: (3,3,3,3)×3 = 12 u32
    //
    // Upstream buffer data (Uint32Array, 25 words shown):
    //   scalar: 1,1,1,0
    //   (gap to 16-byte alignment: index 4..3 = skip 0 words; offset 16 = index 4)
    //   vec2:   2,2,2,2,2,2,0,0
    //   (gap to offset 48 = index 12)
    //   vec4:   3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3
    //
    // Upstream sets:
    //   slot 0 from offset  0, size 12 (3×4)
    //   slot 1 from offset 16, size 24 (3×8)
    //   slot 2 from offset 48, size 48 (3×16)
    //
    // Total buffer must cover at least offset 48 + 48 = 96 bytes = 24 u32s.
    // We allocate 28 u32s (112 bytes) to exactly match upstream's array
    // (4 scalar + 8 vec2 + 16 vec4 = 28 words).
    // -----------------------------------------------------------------
    const std::vector<uint32_t> vertexData = {
        // offset  0 (scalar, slot 0): 3 vertices × u32(1), padded to 16 bytes
        1u, 1u, 1u, 0u,
        // offset 16 (vec2u, slot 1): 3 vertices × (2,2) = 6 u32s, padded to 32 bytes
        2u, 2u, 2u, 2u, 2u, 2u, 0u, 0u,
        // offset 48 (vec4u, slot 2): 3 vertices × (3,3,3,3) = 12 u32s
        3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u,
    };
    // Sanity: upstream slot 2 starts at byte offset 48 = index 12.
    // vertexData[12] = the first "3u" in the vec4 block. Correct.
    WGPUBuffer vertexBuffer = t.makeBufferWithContents(
        vertexData.data(),
        vertexData.size() * sizeof(uint32_t),
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_Vertex);

    // -----------------------------------------------------------------
    // Output textures: 3, one per format.
    // -----------------------------------------------------------------
    WGPUTexture outputTextures[3];
    for (int i = 0; i < 3; ++i) {
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{width, height, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = formats[i];
        texDesc.usage         = WGPUTextureUsage_CopySrc |
                                WGPUTextureUsage_RenderAttachment |
                                WGPUTextureUsage_TextureBinding;
        outputTextures[i] = t.createTextureTracked(texDesc);
    }

    // -----------------------------------------------------------------
    // Output buffer.
    // Upstream formula: bufferSize = prod(components) * numTextures *
    //                               bytesPerComponent * copyWidth
    //                 = (1*2*4) * 3 * 4 * 4 = 384 bytes.
    // Actual data written occupies only 112 bytes; 384 is an over-allocation.
    // We use 384 verbatim to stay identical to upstream.
    // Zero-filled by WebGPU allocation — no pre-fill with expected values.
    // -----------------------------------------------------------------
    const uint32_t bufferSize = (1u * 2u * 4u) * 3u * bytesPerComponent * copyWidth;
    WGPUBufferDescriptor outBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outBufDesc.size  = bufferSize;
    outBufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer outputBuffer = t.createBufferTracked(outBufDesc);

    // -----------------------------------------------------------------
    // Render pass color attachments: 3 views.
    // -----------------------------------------------------------------
    WGPUTextureView outputViews[3];
    for (int i = 0; i < 3; ++i) {
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        outputViews[i] = t.createViewTracked(outputTextures[i], vd);
    }

    WGPURenderPassColorAttachment colorAttachments[3];
    for (int i = 0; i < 3; ++i) {
        colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachments[i].view      = outputViews[i];
        colorAttachments[i].loadOp    = WGPULoadOp_Clear;
        colorAttachments[i].storeOp   = WGPUStoreOp_Store;
        colorAttachments[i].clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 3;
    passDesc.colorAttachments     = colorAttachments;

    // -----------------------------------------------------------------
    // Encode: render pass + texture-to-buffer copies.
    // -----------------------------------------------------------------
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

    {
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        // slot 0: scalar u32, offset=0, size=12 (3 verts × 4 bytes)
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, 12);
        // slot 1: vec2u,      offset=16, size=24 (3 verts × 8 bytes)
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, vertexBuffer, 16, 24);
        // slot 2: vec4u,      offset=48, size=48 (3 verts × 16 bytes)
        wgpuRenderPassEncoderSetVertexBuffer(pass, 2, vertexBuffer, 48, 48);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
    }

    // Copy copyWidth=4 pixels (row 0) from each output texture into outputBuffer.
    // Each texture's bytesPerRow = bytesPerComponent × components[i] × width.
    uint64_t dstOffset = 0;
    for (int i = 0; i < 3; ++i) {
        const uint32_t bpr = bytesPerComponent * components[i] * width;

        WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture  = outputTextures[i];
        src.mipLevel = 0;
        src.origin   = WGPUOrigin3D{0, 0, 0};
        src.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer              = outputBuffer;
        dst.layout.offset       = dstOffset;
        dst.layout.bytesPerRow  = bpr;
        dst.layout.rowsPerImage = height;

        WGPUExtent3D copyExtent = WGPUExtent3D{copyWidth, 1, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copyExtent);

        dstOffset += static_cast<uint64_t>(components[i]) * bytesPerComponent * copyWidth;
    }

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    // -----------------------------------------------------------------
    // Expected output (Uint32Array, 28 u32 = 112 bytes):
    //   out0 (r32uint,    1 comp,  value=1): [1,1,1,1]
    //   out1 (rg32uint,   2 comps, value=2): [2,2, 2,2, 2,2, 2,2]
    //   out2 (rgba32uint, 4 comps, value=3): [3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3]
    // -----------------------------------------------------------------
    std::vector<uint32_t> expectVec;
    expectVec.reserve(28);
    for (int i = 0; i < 3; ++i) {
        const uint32_t value = static_cast<uint32_t>(i + 1); // 1, 2, 3
        for (uint32_t j = 0; j < components[i]; ++j) {
            // 4 pixels wide (copyWidth=4), 1 value per channel per pixel
            for (uint32_t pixel = 0; pixel < copyWidth; ++pixel) {
                expectVec.push_back(value);
            }
        }
    }
    t.expectGPUBufferValuesEqual(
        outputBuffer,
        expectVec.data(),
        expectVec.size() * sizeof(uint32_t));
}

// -----------------------------------------------------------------
// Test: passthrough
// -----------------------------------------------------------------
CTS_TEST(g, "passthrough")
    .desc("Tests passing user-defined data from vertex input through fragment output")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"f32", "f16", "i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type = t.param<std::string>("type");

        if (type == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available on this device");
            }
        }

        const std::string code = generateInterstagePassthroughCode(type);
        drawPassthrough(t, code);
    });

} // namespace
