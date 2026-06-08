// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/render/state_tracking.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Full port: all 5 tests (V26d + V26d-rest / T70).

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,render,state_tracking",
    "Ensure state is set correctly. Tries to stress state caching (setting different states multiple "
    "times in different orders) for setIndexBuffer and setVertexBuffer.");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// getRenderPipelineForTest
// Vertex buffer layout: arrayStride=8, float32 @location(0) + unorm8x4 @location(1).
// Vertex shader: position = vec4f(vertexPosition, 0.5, 0.0, 1.0); color = vertexColor.
// Fragment: returns input.color.
// Primitive: point-list. Target: rgba8unorm.
// ---------------------------------------------------------------------------
static WGPURenderPipeline getRenderPipelineForTest(AllFeaturesMaxLimitsGpuTest& t,
                                                   uint64_t arrayStride)
{
    constexpr std::string_view kVertexShader = R"(
        struct Inputs {
          @location(0) vertexPosition : f32,
          @location(1) vertexColor : vec4<f32>,
        };
        struct Outputs {
          @builtin(position) position : vec4<f32>,
          @location(0) color : vec4<f32>,
        };
        @vertex
        fn main(input : Inputs)-> Outputs {
          var outputs : Outputs;
          outputs.position =
            vec4<f32>(input.vertexPosition, 0.5, 0.0, 1.0);
          outputs.color = input.vertexColor;
          return outputs;
        })";

    constexpr std::string_view kFragmentShader = R"(
        struct Input {
          @location(0) color : vec4<f32>
        };
        @fragment
        fn main(input : Input) -> @location(0) vec4<f32> {
          return input.color;
        })";

    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPUVertexAttribute attrs[2] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
    attrs[0].format         = WGPUVertexFormat_Float32;
    attrs[0].offset         = 0;
    attrs[0].shaderLocation = 0;

    attrs[1].format         = WGPUVertexFormat_Unorm8x4;
    attrs[1].offset         = 4;
    attrs[1].shaderLocation = 1;

    WGPUVertexBufferLayout vbLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbLayout.arrayStride    = arrayStride;
    vbLayout.stepMode       = WGPUVertexStepMode_Vertex;
    vbLayout.attributeCount = 2;
    vbLayout.attributes     = attrs;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.vertex.module      = vertModule;
    pipeDesc.vertex.entryPoint  = sv("main");
    pipeDesc.vertex.bufferCount = 1;
    pipeDesc.vertex.buffers     = &vbLayout;
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipeDesc.multisample.count  = 1;
    pipeDesc.fragment           = &fragment;

    return t.createRenderPipelineTracked(pipeDesc);
}

// ---------------------------------------------------------------------------
// Test case: set_index_buffer_without_changing_buffer
// ---------------------------------------------------------------------------
CTS_TEST(g, "set_index_buffer_without_changing_buffer")
    .desc("Test that setting index buffer states (index format, offset, size) multiple times in "
          "different orders still keeps the correctness of each draw call.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Index buffer: uint16 {0, 1, 2, 3, 4, 0} = 12 bytes (padded to multiple of 4;
        // makeBufferWithContents requires mappedAtCreation size to be a multiple of 4).
        // Byte layout: 00 00 | 01 00 | 02 00 | 03 00 | 04 00 | 00 00
        // The trailing padding index (bytes 10-11) is never read by any draw call.
        const uint16_t indexData[6] = {0, 1, 2, 3, 4, 0};
        WGPUBuffer indexBuffer = t.makeBufferWithContents(
            indexData,
            sizeof(indexData),
            WGPUBufferUsage_Index);

        // Vertex buffer: 8 * (0x10000 + 1) = 524296 bytes, usage VERTEX.
        // Each vertex slot is 8 bytes: 4 bytes f32 position + 4 bytes rgba8 color.
        constexpr uint32_t kVertexAttributeSize   = 8;
        constexpr uint32_t kVertexAttributesCount = 0x10000 + 1; // 65537
        constexpr uint32_t kVertexBufferSize      = kVertexAttributeSize * kVertexAttributesCount; // 524296

        // kPositions[0..4] for vertices 0..4; kPositions[5] for vertex 0x10000.
        const float kPositions[6] = {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f, -0.4f};

        // kColors[i] = rgba8 (4 bytes). kColors[5] is for vertex 0x10000.
        const uint8_t kColors[6][4] = {
            {255,   0,   0, 255}, // index 0: red
            {255, 255, 255, 255}, // index 1: white (unused — overridden by draw 1 → vertex 0x10000)
            {  0,   0, 255, 255}, // index 2: blue
            {255,   0, 255, 255}, // index 3: magenta
            {  0, 255, 255, 255}, // index 4: cyan
            {  0, 255,   0, 255}, // index 0x10000: green
        };

        // Build the zeroed vertex buffer then fill in the required slots.
        std::vector<uint8_t> vertexData(kVertexBufferSize, 0u);

        // Vertices 0..4 (i = 0..4).
        for (uint32_t i = 0; i < 5; ++i) {
            uint32_t base = kVertexAttributeSize * i;
            // Write f32 position at bytes [base .. base+3].
            std::memcpy(vertexData.data() + base, &kPositions[i], sizeof(float));
            // Write rgba8 color at bytes [base+4 .. base+7].
            std::memcpy(vertexData.data() + base + 4, kColors[i], 4);
        }

        // Vertex at index 0x10000.
        {
            uint32_t lastBase = kVertexAttributeSize * (kVertexAttributesCount - 1);
            std::memcpy(vertexData.data() + lastBase, &kPositions[5], sizeof(float));
            std::memcpy(vertexData.data() + lastBase + 4, kColors[5], 4);
        }

        WGPUBuffer vertexBuffer = t.makeBufferWithContents(
            vertexData.data(),
            kVertexBufferSize,
            WGPUBufferUsage_Vertex);

        // Render pipeline.
        WGPURenderPipeline pipeline = getRenderPipelineForTest(t, kVertexAttributeSize);

        // Output texture: 5×1 rgba8unorm, COPY_SRC | RENDER_ATTACHMENT.
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{5, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        // Render pass: one color attachment, clearValue [0,0,0,1], loadOp clear, storeOp store.
        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = outputView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);

        // Draw 1: indexFormat=Uint32, offset=0, size=4.
        // Bytes [0..3] of index buffer = {0x00, 0x00, 0x01, 0x00} → uint32 LE = 0x00010000 = 65536.
        // → vertex 0x10000 → position kPositions[5]=-0.4 → pixel x=1; color kColors[5]=green.
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, 4);
        wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);

        // Draw 2: indexFormat=Uint16, offset=0, size=4.
        // → index 0 → vertex 0 → position -0.8 → pixel x=0; color kColors[0]=red.
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, 4);
        wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);

        // Draw 3: two setIndexBuffer calls; second (offset=4, size=2) wins.
        // → index at byte offset 4 = uint16 value 2 → vertex 2 → position 0.0 → pixel x=2; color kColors[2]=blue.
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, 2);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 4, 2);
        wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);

        // Draw 4: two setIndexBuffer calls; second (offset=6, size=4) wins.
        // → indices at byte offset 6: uint16 values {3, 4} → vertices 3,4 → pixels x=3,4;
        // colors kColors[3]=magenta, kColors[4]=cyan.
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 6, 2);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 6, 4);
        wgpuRenderPassEncoderDrawIndexed(pass, 2, 1, 0, 0, 0);

        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Readback: copyTextureToBuffer (bytesPerRow=256, rowsPerImage=1, size=5×1×1).
        constexpr uint64_t kReadbackSize = 256;

        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = kReadbackSize;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(rbDesc);

        WGPUTexelCopyTextureInfo copySrc = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copySrc.texture  = outputTexture;
        copySrc.mipLevel = 0;
        copySrc.origin   = WGPUOrigin3D{0, 0, 0};
        copySrc.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo copyDst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        copyDst.buffer              = readback;
        copyDst.layout.offset       = 0;
        copyDst.layout.bytesPerRow  = 256;
        copyDst.layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{5, 1, 1};

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToBuffer(copyEncoder, &copySrc, &copyDst, &copySize);
        WGPUCommandBuffer copyCmdBuf = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyCmdBuf);

        // Expected: pixel x in 0..4 at byte offset x*4.
        //   x==0 → kColors[0] = red   {255,   0,   0, 255}
        //   x==1 → kColors[5] = green {  0, 255,   0, 255}  (vertex 0x10000)
        //   x==2 → kColors[2] = blue  {  0,   0, 255, 255}
        //   x==3 → kColors[3] = magenta {255, 0, 255, 255}
        //   x==4 → kColors[4] = cyan  {  0, 255, 255, 255}
        t.expectGPUBufferValuesPassCheck(
            readback,
            [&kColors](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 5 * 4) {
                    return std::string("readback buffer too small (need >= 20 bytes)");
                }
                for (int x = 0; x < 5; ++x) {
                    // x==1 maps to kColors[5] (the 0x10000 vertex), all others to kColors[x].
                    const uint8_t* expected = (x == 1) ? kColors[5] : kColors[x];
                    const uint8_t* got      = actual + x * 4;
                    if (got[0] != expected[0] || got[1] != expected[1] ||
                        got[2] != expected[2] || got[3] != expected[3]) {
                        std::ostringstream msg;
                        msg << "pixel x=" << x
                            << " expected rgba={"
                            << static_cast<int>(expected[0]) << ","
                            << static_cast<int>(expected[1]) << ","
                            << static_cast<int>(expected[2]) << ","
                            << static_cast<int>(expected[3]) << "}, got {"
                            << static_cast<int>(got[0]) << ","
                            << static_cast<int>(got[1]) << ","
                            << static_cast<int>(got[2]) << ","
                            << static_cast<int>(got[3]) << "}";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

// ---------------------------------------------------------------------------
// Test case: set_vertex_buffer_without_changing_buffer
// ---------------------------------------------------------------------------
CTS_TEST(g, "set_vertex_buffer_without_changing_buffer")
    .desc("Test that setting vertex buffer states (offset, size) multiple times in different orders "
          "still keeps the correctness of each draw call.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kVertexAttributeSize = 8;

        const float kPositions[8] = {
            -0.875f, -0.625f, -0.375f, -0.125f, 0.125f, 0.375f, 0.625f, 0.875f
        };
        const uint8_t kColors[8][4] = {
            {255,   0,   0, 255},
            {  0, 255,   0, 255},
            {  0,   0, 255, 255},
            { 51,   0,   0, 255},
            {  0,  51,   0, 255},
            {  0,   0,  51, 255},
            {255,   0, 255, 255},
            {255, 255,   0, 255},
        };

        // Build vertex buffer: 8 vertices × 8 bytes.
        constexpr uint32_t kNumVerts   = 8;
        constexpr uint32_t kVBSize     = kVertexAttributeSize * kNumVerts;
        std::vector<uint8_t> vertexData(kVBSize, 0u);
        for (uint32_t i = 0; i < kNumVerts; ++i) {
            uint32_t base = kVertexAttributeSize * i;
            std::memcpy(vertexData.data() + base,     &kPositions[i], sizeof(float));
            std::memcpy(vertexData.data() + base + 4, kColors[i],     4);
        }
        WGPUBuffer vb = t.makeBufferWithContents(vertexData.data(), kVBSize, WGPUBufferUsage_Vertex);

        WGPURenderPipeline pipeline = getRenderPipelineForTest(t, kVertexAttributeSize);

        // Output texture: 8×1 rgba8unorm.
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{8, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = outputView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        wgpuRenderPassEncoderSetPipeline(pass, pipeline);

        // Change 'size' in setVertexBuffer() — last call wins; draw verts 0,1.
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0,
            static_cast<uint64_t>(kVertexAttributeSize));
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0,
            static_cast<uint64_t>(kVertexAttributeSize) * 2);
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        // Change 'offset' in setVertexBuffer() — draw verts 2,3.
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb,
            static_cast<uint64_t>(kVertexAttributeSize) * 2,
            static_cast<uint64_t>(kVertexAttributeSize) * 2);
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        // Change 'size' again — last call wins; draw verts 4,5,6,7.
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb,
            static_cast<uint64_t>(kVertexAttributeSize) * 4,
            static_cast<uint64_t>(kVertexAttributeSize) * 2);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb,
            static_cast<uint64_t>(kVertexAttributeSize) * 4,
            static_cast<uint64_t>(kVertexAttributeSize) * 4);
        wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);

        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Readback: copyTextureToBuffer (bytesPerRow=256, rowsPerImage=1, size=8×1×1).
        constexpr uint64_t kReadbackSize = 256;

        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = kReadbackSize;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(rbDesc);

        WGPUTexelCopyTextureInfo copySrc = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copySrc.texture  = outputTexture;
        copySrc.mipLevel = 0;
        copySrc.origin   = WGPUOrigin3D{0, 0, 0};
        copySrc.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo copyDst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        copyDst.buffer              = readback;
        copyDst.layout.offset       = 0;
        copyDst.layout.bytesPerRow  = 256;
        copyDst.layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{8, 1, 1};

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToBuffer(copyEncoder, &copySrc, &copyDst, &copySize);
        WGPUCommandBuffer copyCmdBuf = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyCmdBuf);

        // Expected: pixel x = kColors[x] for x 0..7.
        t.expectGPUBufferValuesPassCheck(
            readback,
            [&kColors](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 8 * 4) {
                    return std::string("readback buffer too small (need >= 32 bytes)");
                }
                for (int x = 0; x < 8; ++x) {
                    const uint8_t* expected = kColors[x];
                    const uint8_t* got      = actual + x * 4;
                    if (got[0] != expected[0] || got[1] != expected[1] ||
                        got[2] != expected[2] || got[3] != expected[3]) {
                        std::ostringstream msg;
                        msg << "pixel x=" << x
                            << " expected rgba={"
                            << static_cast<int>(expected[0]) << ","
                            << static_cast<int>(expected[1]) << ","
                            << static_cast<int>(expected[2]) << ","
                            << static_cast<int>(expected[3]) << "}, got {"
                            << static_cast<int>(got[0]) << ","
                            << static_cast<int>(got[1]) << ","
                            << static_cast<int>(got[2]) << ","
                            << static_cast<int>(got[3]) << "}";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

// ---------------------------------------------------------------------------
// Test case: change_pipeline_before_and_after_vertex_buffer
// ---------------------------------------------------------------------------
CTS_TEST(g, "change_pipeline_before_and_after_vertex_buffer")
    .desc("Test that changing the pipeline {before,after} the vertex buffers still keeps the "
          "correctness of each draw call (In D3D12, the vertex buffer stride is part of "
          "SetVertexBuffer instead of the pipeline.)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kVertexAttributeSize = 8;

        const float kPositions[6] = {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f, 0.9f};
        const uint8_t kColors[6][4] = {
            {255,   0,   0, 255},
            {255, 255, 255, 255},
            {  0, 255,   0, 255},
            {  0,   0, 255, 255},
            {255,   0, 255, 255},
            {  0, 255, 255, 255},
        };

        // Build vertex buffer: 6 × 8 bytes.
        constexpr uint32_t kNumVerts = 6;
        constexpr uint32_t kVBSize   = kVertexAttributeSize * kNumVerts;
        std::vector<uint8_t> vertexData(kVBSize, 0u);
        for (uint32_t i = 0; i < kNumVerts; ++i) {
            uint32_t base = kVertexAttributeSize * i;
            std::memcpy(vertexData.data() + base,     &kPositions[i], sizeof(float));
            std::memcpy(vertexData.data() + base + 4, kColors[i],     4);
        }
        WGPUBuffer vb = t.makeBufferWithContents(vertexData.data(), kVBSize, WGPUBufferUsage_Vertex);

        // Two pipelines with different strides.
        WGPURenderPipeline p1 = getRenderPipelineForTest(t, kVertexAttributeSize);
        WGPURenderPipeline p2 = getRenderPipelineForTest(t, static_cast<uint64_t>(kVertexAttributeSize) * 2);

        // Output texture: 5×1 rgba8unorm (kPointsCount = 6-1 = 5).
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{5, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = outputView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        // Update pipeline before setVertexBuffer → stride = 2*kVertexAttributeSize.
        // Verts at byte 0 and 16 = kPositions[0], kPositions[2] → pixels 0, 2.
        wgpuRenderPassEncoderSetPipeline(pass, p1);
        wgpuRenderPassEncoderSetPipeline(pass, p2);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        // Update pipeline after setVertexBuffer → stride = kVertexAttributeSize.
        // Offset 3*8=24, verts at byte 24 and 32 = kPositions[3], kPositions[4] → pixels 3, 4.
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb,
            static_cast<uint64_t>(kVertexAttributeSize) * 3, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetPipeline(pass, p1);
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Readback: copyTextureToBuffer (bytesPerRow=256, rowsPerImage=1, size=5×1×1).
        constexpr uint64_t kReadbackSize = 256;

        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = kReadbackSize;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(rbDesc);

        WGPUTexelCopyTextureInfo copySrc = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copySrc.texture  = outputTexture;
        copySrc.mipLevel = 0;
        copySrc.origin   = WGPUOrigin3D{0, 0, 0};
        copySrc.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo copyDst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        copyDst.buffer              = readback;
        copyDst.layout.offset       = 0;
        copyDst.layout.bytesPerRow  = 256;
        copyDst.layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{5, 1, 1};

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToBuffer(copyEncoder, &copySrc, &copyDst, &copySize);
        WGPUCommandBuffer copyCmdBuf = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyCmdBuf);

        // Expected: pixel 1 is never drawn → black {0,0,0,255}; all others kColors[x].
        const uint8_t kBlack[4] = {0, 0, 0, 255};
        t.expectGPUBufferValuesPassCheck(
            readback,
            [&kColors, &kBlack](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 5 * 4) {
                    return std::string("readback buffer too small (need >= 20 bytes)");
                }
                for (int x = 0; x < 5; ++x) {
                    const uint8_t* expected = (x == 1) ? kBlack : kColors[x];
                    const uint8_t* got      = actual + x * 4;
                    if (got[0] != expected[0] || got[1] != expected[1] ||
                        got[2] != expected[2] || got[3] != expected[3]) {
                        std::ostringstream msg;
                        msg << "pixel x=" << x
                            << " expected rgba={"
                            << static_cast<int>(expected[0]) << ","
                            << static_cast<int>(expected[1]) << ","
                            << static_cast<int>(expected[2]) << ","
                            << static_cast<int>(expected[3]) << "}, got {"
                            << static_cast<int>(got[0]) << ","
                            << static_cast<int>(got[1]) << ","
                            << static_cast<int>(got[2]) << ","
                            << static_cast<int>(got[3]) << "}";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

// ---------------------------------------------------------------------------
// Test case: set_vertex_buffer_but_not_used_in_draw
// ---------------------------------------------------------------------------
CTS_TEST(g, "set_vertex_buffer_but_not_used_in_draw")
    .desc("Test that drawing after having set vertex buffer slots not used by the pipeline works "
          "correctly.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // kPositions and kColors as per upstream.
        const float kPositions[2]   = {-0.75f, -0.25f};
        const uint8_t kColors[8]    = {255, 0, 0, 255,  0, 255, 0, 255}; // 2 rgba8 pixels
        constexpr uint32_t kAttributeStride = 4;

        // positionBuffer: 2 × 4 bytes of float32.
        WGPUBuffer positionBuffer = t.makeBufferWithContents(
            kPositions, sizeof(kPositions), WGPUBufferUsage_Vertex);

        // colorBuffer: 2 × 4 bytes of unorm8x4.
        WGPUBuffer colorBuffer = t.makeBufferWithContents(
            kColors, sizeof(kColors), WGPUBufferUsage_Vertex);

        // ---------------------------------------------------------------------------
        // Fragment shader (shared by p1 and p2).
        // ---------------------------------------------------------------------------
        constexpr std::string_view kFragmentShader = R"(
        struct Input {
          @location(0) color : vec4<f32>
        };
        @fragment
        fn main(input : Input) -> @location(0) vec4<f32> {
          return input.color;
        })";

        // ---------------------------------------------------------------------------
        // p1: 2 vertex buffers.
        //   buffer0: unorm8x4 @location(0) (vertexColor), stride 4.
        //   buffer1: float32  @location(1) (vertexPosition), stride 4.
        // ---------------------------------------------------------------------------
        constexpr std::string_view kVertexShaderP1 = R"(
        struct Inputs {
          @location(0) vertexColor : vec4<f32>,
          @location(1) vertexPosition : f32,
        };
        struct Outputs {
          @builtin(position) position : vec4<f32>,
          @location(0) color : vec4<f32>,
        };
        @vertex
        fn main(input : Inputs)-> Outputs {
          var outputs : Outputs;
          outputs.position =
            vec4<f32>(input.vertexPosition, 0.5, 0.0, 1.0);
          outputs.color = input.vertexColor;
          return outputs;
        })";

        WGPUShaderModule vertModP1 = t.createShaderModuleTracked(kVertexShaderP1);
        WGPUShaderModule fragMod   = t.createShaderModuleTracked(kFragmentShader);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = fragMod;
        fragment.entryPoint  = sv("main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        // p1 attributes and layouts.
        WGPUVertexAttribute attrsP1[2] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
        attrsP1[0].format         = WGPUVertexFormat_Unorm8x4;
        attrsP1[0].offset         = 0;
        attrsP1[0].shaderLocation = 0;

        attrsP1[1].format         = WGPUVertexFormat_Float32;
        attrsP1[1].offset         = 0;
        attrsP1[1].shaderLocation = 1;

        WGPUVertexBufferLayout vbLayoutsP1[2] = {WGPU_VERTEX_BUFFER_LAYOUT_INIT,
                                                  WGPU_VERTEX_BUFFER_LAYOUT_INIT};
        vbLayoutsP1[0].arrayStride    = kAttributeStride;
        vbLayoutsP1[0].stepMode       = WGPUVertexStepMode_Vertex;
        vbLayoutsP1[0].attributeCount = 1;
        vbLayoutsP1[0].attributes     = &attrsP1[0];

        vbLayoutsP1[1].arrayStride    = kAttributeStride;
        vbLayoutsP1[1].stepMode       = WGPUVertexStepMode_Vertex;
        vbLayoutsP1[1].attributeCount = 1;
        vbLayoutsP1[1].attributes     = &attrsP1[1];

        WGPURenderPipelineDescriptor pipeDescP1 = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDescP1.layout             = nullptr;
        pipeDescP1.vertex.module      = vertModP1;
        pipeDescP1.vertex.entryPoint  = sv("main");
        pipeDescP1.vertex.bufferCount = 2;
        pipeDescP1.vertex.buffers     = vbLayoutsP1;
        pipeDescP1.primitive.topology = WGPUPrimitiveTopology_PointList;
        pipeDescP1.multisample.count  = 1;
        pipeDescP1.fragment           = &fragment;

        WGPURenderPipeline p1 = t.createRenderPipelineTracked(pipeDescP1);

        // ---------------------------------------------------------------------------
        // p2: 1 vertex buffer; position comes from inline WGSL array.
        //   buffer0: unorm8x4 @location(0) (vertexColor), stride 4.
        // ---------------------------------------------------------------------------
        constexpr std::string_view kVertexShaderP2 = R"(
        struct Inputs {
          @builtin(vertex_index) vertexIndex : u32,
          @location(0) vertexColor : vec4<f32>,
        };
        struct Outputs {
          @builtin(position) position : vec4<f32>,
          @location(0) color : vec4<f32>,
        };
        @vertex
        fn main(input : Inputs)-> Outputs {
          var kPositions = array<f32, 2> (0.25, 0.75);
          var outputs : Outputs;
          outputs.position =
              vec4(kPositions[input.vertexIndex], 0.5, 0.0, 1.0);
          outputs.color = input.vertexColor;
          return outputs;
        })";

        WGPUShaderModule vertModP2 = t.createShaderModuleTracked(kVertexShaderP2);

        WGPUVertexAttribute attrP2 = WGPU_VERTEX_ATTRIBUTE_INIT;
        attrP2.format         = WGPUVertexFormat_Unorm8x4;
        attrP2.offset         = 0;
        attrP2.shaderLocation = 0;

        WGPUVertexBufferLayout vbLayoutP2 = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vbLayoutP2.arrayStride    = kAttributeStride;
        vbLayoutP2.stepMode       = WGPUVertexStepMode_Vertex;
        vbLayoutP2.attributeCount = 1;
        vbLayoutP2.attributes     = &attrP2;

        // p2 needs its own fragment since it shares the same WGSL but we reuse fragMod.
        WGPURenderPipelineDescriptor pipeDescP2 = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDescP2.layout             = nullptr;
        pipeDescP2.vertex.module      = vertModP2;
        pipeDescP2.vertex.entryPoint  = sv("main");
        pipeDescP2.vertex.bufferCount = 1;
        pipeDescP2.vertex.buffers     = &vbLayoutP2;
        pipeDescP2.primitive.topology = WGPUPrimitiveTopology_PointList;
        pipeDescP2.multisample.count  = 1;
        pipeDescP2.fragment           = &fragment;

        WGPURenderPipeline p2 = t.createRenderPipelineTracked(pipeDescP2);

        // Output texture: 4×1 rgba8unorm.
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{4, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = outputView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        // Draw 1: p1 uses both colorBuffer (slot 0) and positionBuffer (slot 1).
        // positions -0.75,-0.25 → pixels 0,1; colors kColors[0..3], kColors[4..7].
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, colorBuffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, positionBuffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetPipeline(pass, p1);
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        // Draw 2: p2 only uses colorBuffer (slot 0); slot 1 stays bound but unused.
        // positions 0.25,0.75 (from WGSL array) → pixels 2,3; colors kColors[0..3], kColors[4..7].
        wgpuRenderPassEncoderSetPipeline(pass, p2);
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Readback: copyTextureToBuffer (bytesPerRow=256, rowsPerImage=1, size=4×1×1).
        constexpr uint64_t kReadbackSize = 256;

        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = kReadbackSize;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(rbDesc);

        WGPUTexelCopyTextureInfo copySrc = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copySrc.texture  = outputTexture;
        copySrc.mipLevel = 0;
        copySrc.origin   = WGPUOrigin3D{0, 0, 0};
        copySrc.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo copyDst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        copyDst.buffer              = readback;
        copyDst.layout.offset       = 0;
        copyDst.layout.bytesPerRow  = 256;
        copyDst.layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{4, 1, 1};

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToBuffer(copyEncoder, &copySrc, &copyDst, &copySize);
        WGPUCommandBuffer copyCmdBuf = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyCmdBuf);

        // Expected: {kColors[0..3], kColors[4..7], kColors[0..3], kColors[4..7]}
        //           = {red, green, red, green}.
        // kColors flat: bytes 0..3 = red, bytes 4..7 = green.
        t.expectGPUBufferValuesPassCheck(
            readback,
            [&kColors](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 4 * 4) {
                    return std::string("readback buffer too small (need >= 16 bytes)");
                }
                // Pattern: pixel 0→kColors[0..3], pixel 1→kColors[4..7],
                //          pixel 2→kColors[0..3], pixel 3→kColors[4..7].
                for (int x = 0; x < 4; ++x) {
                    const uint8_t* expected = &kColors[(x % 2) * 4];
                    const uint8_t* got      = actual + x * 4;
                    if (got[0] != expected[0] || got[1] != expected[1] ||
                        got[2] != expected[2] || got[3] != expected[3]) {
                        std::ostringstream msg;
                        msg << "pixel x=" << x
                            << " expected rgba={"
                            << static_cast<int>(expected[0]) << ","
                            << static_cast<int>(expected[1]) << ","
                            << static_cast<int>(expected[2]) << ","
                            << static_cast<int>(expected[3]) << "}, got {"
                            << static_cast<int>(got[0]) << ","
                            << static_cast<int>(got[1]) << ","
                            << static_cast<int>(got[2]) << ","
                            << static_cast<int>(got[3]) << "}";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

// ---------------------------------------------------------------------------
// Test case: set_index_buffer_before_non_indexed_draw
// ---------------------------------------------------------------------------
CTS_TEST(g, "set_index_buffer_before_non_indexed_draw")
    .desc("Test that setting / not setting the index buffer does not impact a non-indexed draw.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kVertexAttributeSize = 8;

        const float kPositions[4] = {-0.75f, -0.25f, 0.25f, 0.75f};
        const uint8_t kColors[4][4] = {
            {255,   0,   0, 255},
            {  0, 255,   0, 255},
            {  0,   0, 255, 255},
            {255,   0, 255, 255},
        };

        // Build vertex buffer: 4 × 8 bytes.
        constexpr uint32_t kNumVerts = 4;
        constexpr uint32_t kVBSize   = kVertexAttributeSize * kNumVerts;
        std::vector<uint8_t> vertexData(kVBSize, 0u);
        for (uint32_t i = 0; i < kNumVerts; ++i) {
            uint32_t base = kVertexAttributeSize * i;
            std::memcpy(vertexData.data() + base,     &kPositions[i], sizeof(float));
            std::memcpy(vertexData.data() + base + 4, kColors[i],     4);
        }
        WGPUBuffer vb = t.makeBufferWithContents(vertexData.data(), kVBSize, WGPUBufferUsage_Vertex);

        // Index buffer: uint16 {2, 3} — 4 bytes (already 4-aligned).
        const uint16_t indexData[2] = {2, 3};
        WGPUBuffer ib = t.makeBufferWithContents(indexData, sizeof(indexData), WGPUBufferUsage_Index);

        WGPURenderPipeline pipeline = getRenderPipelineForTest(t, kVertexAttributeSize);

        // Output texture: 4×1 rgba8unorm.
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{4, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = outputView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        // Draw 1: indexed — indices {2,3} → vertices 2,3 → pixels 2,3.
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDrawIndexed(pass, 2, 1, 0, 0, 0);

        // Draw 2: non-indexed — vertices 0,1 → pixels 0,1.
        wgpuRenderPassEncoderDraw(pass, 2, 1, 0, 0);

        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Readback: copyTextureToBuffer (bytesPerRow=256, rowsPerImage=1, size=4×1×1).
        constexpr uint64_t kReadbackSize = 256;

        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = kReadbackSize;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(rbDesc);

        WGPUTexelCopyTextureInfo copySrc = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        copySrc.texture  = outputTexture;
        copySrc.mipLevel = 0;
        copySrc.origin   = WGPUOrigin3D{0, 0, 0};
        copySrc.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo copyDst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        copyDst.buffer              = readback;
        copyDst.layout.offset       = 0;
        copyDst.layout.bytesPerRow  = 256;
        copyDst.layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{4, 1, 1};

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToBuffer(copyEncoder, &copySrc, &copyDst, &copySize);
        WGPUCommandBuffer copyCmdBuf = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyCmdBuf);

        // Expected: pixel x = kColors[x] for x 0..3.
        t.expectGPUBufferValuesPassCheck(
            readback,
            [&kColors](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 4 * 4) {
                    return std::string("readback buffer too small (need >= 16 bytes)");
                }
                for (int x = 0; x < 4; ++x) {
                    const uint8_t* expected = kColors[x];
                    const uint8_t* got      = actual + x * 4;
                    if (got[0] != expected[0] || got[1] != expected[1] ||
                        got[2] != expected[2] || got[3] != expected[3]) {
                        std::ostringstream msg;
                        msg << "pixel x=" << x
                            << " expected rgba={"
                            << static_cast<int>(expected[0]) << ","
                            << static_cast<int>(expected[1]) << ","
                            << static_cast<int>(expected[2]) << ","
                            << static_cast<int>(expected[3]) << "}, got {"
                            << static_cast<int>(got[0]) << ","
                            << static_cast<int>(got[1]) << ","
                            << static_cast<int>(got[2]) << ","
                            << static_cast<int>(got[3]) << "}";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

} // namespace
