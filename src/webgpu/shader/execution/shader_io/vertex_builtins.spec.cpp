// Ported from gpuweb/cts src/webgpu/shader/execution/shader_io/vertex_builtins.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations):
// - t.skipIfDeviceDoesNotHaveFeature('clip-distances') maps to a runtime check
//   via wgpuDeviceHasFeature(..., WGPUFeatureName_ClipDistances) with t.skip().
// - Upstream JS builds expectedData with a redundant outer x-loop (the loop
//   variable x is unused in the body — it overwrites the same row pixels kSize
//   times). The net result is the same as a single pass over the row, which is
//   what this port does.
// - The readback buffer uses CopyDst|CopySrc (matching the upstream usage
//   of COPY_SRC|COPY_DST) so the harness can stage a readback.
// - WGSL shader is built as a std::string using integer-to-string substitution
//   for the clipDistances constant, matching upstream's template literal.

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> grp = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,shader_io,vertex_builtins",
    "Test vertex shader builtin variables\n\n* test builtin(clip_distances)\n");

// Helper: build a WGPUStringView from a std::string_view.
WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

CTS_TEST(grp, "outputs,clip_distances")
    .desc(
        "Test vertex shader builtin(clip_distances) values.\n\n"
        "In the tests, we draw a square with two triangles (top-right and bottom left), whose vertices\n"
        "have different clip distances values. (Top Left: -1, Bottom Right: 1 Top Right & Bottom Left: 0)\n"
        "1. The clip distances values of the pixels in the top-left region should be less than 0 so these\n"
        "   pixels will all be invisible\n"
        "2. The clip distances values of the pixels on the top-right-to-bottom-left diagonal line should\n"
        "   be equal to 0\n"
        "3. The clip distances values of the pixels in the bottom-right region should be greater than 0\n\n"
        "-1 - - - - - 0\n"
        " | \\      x x\n"
        " |   \\  x x x\n"
        " |    \\ x x x\n"
        " |   x x\\ x x\n"
        " | x x x x\\ x\n"
        " 0 x x x x x 1\n")
    .params([](ParamsBuilder u) {
        return u.combine("clipDistances", {1, 2, 3, 4, 5, 6, 7, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Runtime-skip when clip-distances feature is not available.
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ClipDistances)) {
            t.skip("clip-distances feature is not supported on this device");
        }

        const int clipDistances = t.param<int>("clipDistances");

        // Build the WGSL shader, substituting the clipDistances constant.
        // Mirrors upstream's template literal: `${clipDistances}`.
        const std::string cd = std::to_string(clipDistances);
        const std::string code =
            std::string("enable clip_distances;\n") +
            "const kClipDistancesSize = " + cd + ";\n" +
            "struct VertexOutputs {\n" +
            "    @builtin(position) position : vec4f,\n" +
            "    @builtin(clip_distances) clipDistances : array<f32, kClipDistancesSize>,\n" +
            "}\n" +
            "@vertex\n" +
            "fn vsMain(@builtin(vertex_index) vertexIndex : u32) -> VertexOutputs {\n" +
            "      var posAndClipDistances = array(\n" +
            "          vec3f(-1.0,  1.0, -1.0),\n" +
            "          vec3f( 1.0, -1.0,  1.0),\n" +
            "          vec3f( 1.0,  1.0,  0.0),\n" +
            "          vec3f(-1.0, -1.0,  0.0),\n" +
            "          vec3f( 1.0, -1.0,  1.0),\n" +
            "          vec3f(-1.0,  1.0, -1.0));\n" +
            "      var vertexOutput : VertexOutputs;\n" +
            "      vertexOutput.position = vec4f(posAndClipDistances[vertexIndex].xy, 0.0, 1.0);\n" +
            "      vertexOutput.clipDistances[kClipDistancesSize - 1] = posAndClipDistances[vertexIndex].z;\n" +
            "      return vertexOutput;\n" +
            "}\n" +
            "@fragment\n" +
            "fn fsMain() -> @location(0) vec4f {\n" +
            "    return vec4f(1.0, 0.0, 0.0, 1.0);\n" +
            "}\n";

        // Create the shader module and render pipeline (layout: auto).
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = shaderModule;
        fragment.entryPoint = sv("fsMain");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr; // auto layout
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.vertex.entryPoint = sv("vsMain");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.fragment = &fragment;
        WGPURenderPipeline renderPipeline = t.createRenderPipelineTracked(pipelineDesc);

        // Create a 7x7 output texture (RGBA8Unorm), cleared to Green.
        constexpr uint32_t kSize = 7;

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.size = WGPUExtent3D{kSize, kSize, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        // Record the render pass: clear to green, draw 6 vertices.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = outputView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.0, 1.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);

        // Copy texture to buffer for readback.
        // bytesPerRow must be a multiple of 256 (kBytesPerRowAlignment).
        constexpr uint32_t kBytesPerRow = 256;
        constexpr uint32_t kBytesPerPixel = 4;
        // Total size: last row uses kSize*4 bytes; earlier rows are each kBytesPerRow apart.
        constexpr uint64_t kOutputDataSize =
            static_cast<uint64_t>(kBytesPerRow) * (kSize - 1) + kSize * kBytesPerPixel;

        // Output/readback buffer: zero-initialized (never pre-filled with expected values).
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = kOutputDataSize;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer outputBuffer = t.createBufferTracked(bufDesc);

        // copyTextureToBuffer: harness helper takes (encoder, src texture, dst buffer,
        // bytesPerRow, copyExtent). This matches wgpuCommandEncoderCopyTextureToBuffer
        // with rowsPerImage == kSize.
        t.copyTextureToBuffer(encoder, outputTexture, outputBuffer, kBytesPerRow,
                              WGPUExtent3D{kSize, kSize, 1});

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Build expected data.
        // For each row y (0-based from top), the first (kSize - y - 1) pixels are Green
        // and the remaining pixels are Red. The diagonal runs from top-right to bottom-left:
        // pixels with clip_distance < 0 (top-left region) are clipped (Green background),
        // pixels with clip_distance >= 0 (bottom-right region) are rendered Red.
        //
        // Port note: upstream's expectedData loop has an outer `x` loop whose variable is
        // never used in the body — the inner i/j loops overwrite the same row offsets on
        // every x-iteration. The net result equals a single pass over each row, which is
        // what this port implements directly.
        std::vector<uint8_t> expectedData(static_cast<size_t>(kOutputDataSize), 0u);
        for (uint32_t y = 0; y < kSize; ++y) {
            const uint64_t baseOffset = static_cast<uint64_t>(kBytesPerRow) * y;
            const uint32_t lastRed = kSize - y - 1u;
            // Pixels 0 .. lastRed-1 : Green [0, 255, 0, 255]
            for (uint32_t i = 0; i < lastRed; ++i) {
                const size_t off = static_cast<size_t>(baseOffset + i * kBytesPerPixel);
                expectedData[off + 0] = 0;
                expectedData[off + 1] = 255;
                expectedData[off + 2] = 0;
                expectedData[off + 3] = 255;
            }
            // Pixels lastRed .. kSize-1 : Red [255, 0, 0, 255]
            for (uint32_t j = lastRed; j < kSize; ++j) {
                const size_t off = static_cast<size_t>(baseOffset + j * kBytesPerPixel);
                expectedData[off + 0] = 255;
                expectedData[off + 1] = 0;
                expectedData[off + 2] = 0;
                expectedData[off + 3] = 255;
            }
        }

        t.expectGPUBufferValuesEqual(outputBuffer, expectedData.data(), expectedData.size());
    });

} // namespace
