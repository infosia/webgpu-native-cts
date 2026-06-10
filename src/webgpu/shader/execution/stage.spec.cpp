// Ported from gpuweb/cts src/webgpu/shader/execution/stage.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 webgpu-native-cts contributors, BSD-3-Clause.
//
// checkElementsEqual (upstream util/check_contents.ts) is inlined: the C++ port
// uses t.expectGPUBufferValuesEqual() directly, which performs byte-level equality
// with no pre-filling of the output buffer.

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,stage",
    "Test trivial shaders for each shader stage kind");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// basic_compute
// Creates a compute pipeline that writes vec4u(1,2,3,42) to a storage buffer.
// Verifies the written values via readback.
// ---------------------------------------------------------------------------
CTS_TEST(g, "basic_compute")
    .desc("Test a trivial compute shader")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view kCode = R"(
@group(0) @binding(0)
var<storage, read_write> v : vec4u;

@compute @workgroup_size(1)
fn main() {
  v = vec4u(1,2,3,42);
}
)";

        // Create the storage buffer (zero-initialized, never pre-filled with expected values).
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4 * sizeof(uint32_t); // vec4u = 16 bytes
        bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // Create compute pipeline with auto layout.
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(kCode);

        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout             = nullptr; // auto layout
        pipeDesc.compute.module     = shaderModule;
        pipeDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        // Bind group: binding 0 → buffer.
        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer  = buffer;
        entry.offset  = 0;
        entry.size    = bufDesc.size;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        // Encode: begin compute pass, dispatch 1 workgroup.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Verify: readback must equal {1, 2, 3, 42}.
        // Inlined checkElementsEqual: byte-exact match using expectGPUBufferValuesEqual.
        const std::array<uint32_t, 4> expected = {1u, 2u, 3u, 42u};
        t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size() * sizeof(uint32_t));
    });

// ---------------------------------------------------------------------------
// basic_render
// Creates a vertex+fragment pipeline. The vertex shader covers the entire
// framebuffer with a right triangle; the fragment shader emits solid green.
// Reads back one pixel from the 8x8 rgba8unorm render target and expects
// {0x00, 0xff, 0x00, 0xff}.
// ---------------------------------------------------------------------------
CTS_TEST(g, "basic_render")
    .desc("Test trivial vertex and fragment shaders")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view kCode = R"(
@vertex
fn vert_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f {
  // A right triangle covering the whole framebuffer.
  const pos = array(
    vec2f(-1,-3),
    vec2f(3,1),
    vec2f(-1,1));
  return vec4f(pos[idx], 0, 1);
}

@fragment
fn frag_main() -> @location(0) vec4f {
  return vec4(0, 1, 0, 1); // green
}
)";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(kCode);

        // Render target: 8x8 rgba8unorm texture.
        constexpr uint32_t kWidth  = 8;
        constexpr uint32_t kHeight = 8;
        constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{kWidth, kHeight, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = kFormat;
        texDesc.usage         = WGPUTextureUsage_RenderAttachment |
                                WGPUTextureUsage_TextureBinding   |
                                WGPUTextureUsage_CopySrc;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView textureView = t.createViewTracked(texture, viewDesc);

        // Readback buffer: copy one pixel (4 bytes) but bytesPerRow must be >= 256 per WebGPU spec.
        // Zero-initialized — never pre-filled with expected bytes.
        constexpr uint64_t kReadbackSize = 256;
        WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        dstDesc.size  = kReadbackSize;
        dstDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer dst = t.createBufferTracked(dstDesc);

        // Build render pipeline.
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = kFormat;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = shaderModule;
        fragment.entryPoint  = sv("frag_main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout            = nullptr; // auto layout
        pipeDesc.vertex.module     = shaderModule;
        pipeDesc.vertex.entryPoint = sv("vert_main");
        pipeDesc.multisample.count = 1;
        pipeDesc.fragment          = &fragment;

        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

        // Render pass: clear to black, draw 3 vertices (the full-screen triangle).
        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = textureView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        // Copy pixel (0,0) from texture to readback buffer.
        WGPUTexelCopyTextureInfo srcInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        srcInfo.texture  = texture;
        srcInfo.mipLevel = 0;
        srcInfo.origin   = WGPUOrigin3D{0, 0, 0};
        srcInfo.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo dstInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dstInfo.buffer              = dst;
        dstInfo.layout.offset       = 0;
        dstInfo.layout.bytesPerRow  = 256;
        dstInfo.layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcInfo, &dstInfo, &copySize);

        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Expect one green pixel at offset 0: {0x00, 0xff, 0x00, 0xff}.
        // Inline-port of upstream's t.expectGPUBufferValuesEqual(dst, new Uint8Array([0x00, 0xff, 0x00, 0xff])).
        t.expectGPUBufferValuesPassCheck(
            dst,
            [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 4) {
                    return "readback buffer too small (need >= 4 bytes)";
                }
                const uint8_t red   = actual[0];
                const uint8_t green = actual[1];
                const uint8_t blue  = actual[2];
                const uint8_t alpha = actual[3];
                if (red != 0x00u || green != 0xffu || blue != 0x00u || alpha != 0xffu) {
                    std::ostringstream msg;
                    msg << "pixel[0,0] expected rgba={0,255,0,255}, got {"
                        << static_cast<int>(red) << ","
                        << static_cast<int>(green) << ","
                        << static_cast<int>(blue) << ","
                        << static_cast<int>(alpha) << "}";
                    return msg.str();
                }
                return std::nullopt;
            },
            /*srcByteOffset=*/ 0,
            /*byteLength=*/    static_cast<size_t>(kReadbackSize));
    });

} // namespace
