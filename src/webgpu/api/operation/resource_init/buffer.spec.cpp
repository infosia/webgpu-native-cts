// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/buffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal port: 6 first-use zero-init paths (partial_write_buffer, copy_buffer_to_buffer_copy_source,
// uniform_buffer, storage_buffer, vertex_buffer, index_buffer) at offset/bufferOffset=0, single case each.
// Deferred: map_whole_buffer, map_partial_buffer, mapped_at_creation_*, copy_buffer_to_texture,
//   resolve_query_set_to_partial_buffer, copy_texture_to_partial_buffer, readonly_storage_buffer,
//   indirect_buffer_for_draw_indirect, indirect_buffer_for_dispatch_indirect,
//   and the bufferOffset!=0 / bufferUsage / mapMode subcase matrices (V29d).

#include <cstdint>
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
    "api,operation,resource_init,buffer",
    "Verify uninitialized GPUBuffers are zero-initialized on first use across structurally-distinct "
    "read paths (copy-source, uniform bind, storage bind, vertex fetch, index fetch) plus a "
    "partial-writeBuffer canary.");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// expectBufferZero: verify the entire buffer reads back as all zeros.
// ---------------------------------------------------------------------------
static void expectBufferZero(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, size_t size) {
    std::vector<uint8_t> zeros(size, 0u);
    t.expectGPUBufferValuesEqual(buffer, zeros.data(), zeros.size());
}

// ---------------------------------------------------------------------------
// expectOutputTextureGreen: copy the 1x1 rgba8unorm output texture to a
// 256-byte readback buffer and verify bytes [0..3] == {0, 255, 0, 255}.
// ---------------------------------------------------------------------------
static void expectOutputTextureGreen(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    // Readback buffer: bytesPerRow must be >= 256 (WebGPU alignment).
    constexpr uint64_t kReadbackSize = 256;

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kReadbackSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture  = texture;
    src.mipLevel = 0;
    src.origin   = WGPUOrigin3D{0, 0, 0};
    src.aspect   = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer              = readback;
    dst.layout.offset       = 0;
    dst.layout.bytesPerRow  = 256;
    dst.layout.rowsPerImage = 1;

    WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    t.expectGPUBufferValuesPassCheck(
        readback,
        [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                return "readback buffer too small (need >= 4 bytes)";
            }
            const uint8_t r = actual[0];
            const uint8_t green = actual[1];
            const uint8_t b = actual[2];
            const uint8_t a = actual[3];
            if (r != 0 || green != 255 || b != 0 || a != 255) {
                std::ostringstream msg;
                msg << "output texture pixel expected rgba={0,255,0,255}, got {"
                    << static_cast<int>(r) << ","
                    << static_cast<int>(green) << ","
                    << static_cast<int>(b) << ","
                    << static_cast<int>(a) << "}";
                return msg.str();
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(kReadbackSize));
}

// ---------------------------------------------------------------------------
// Shaders (faithful to upstream buffer.spec.ts)
// ---------------------------------------------------------------------------

// Uniform-buffer compute shader:
// reads vec4<u32> from uniform, writes green to storage texture iff all zero.
constexpr std::string_view kUniformComputeShader = R"(
struct UBO {
  value : vec4<u32>
};
@group(0) @binding(0) var<uniform> ubo : UBO;
@group(0) @binding(1) var outImage : texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(1) fn main() {
    if (all(ubo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
        textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
    } else {
        textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
    }
}
)";

// Storage-buffer compute shader:
// reads vec4<u32> from read_write storage, writes green to storage texture iff all zero.
constexpr std::string_view kStorageComputeShader = R"(
struct SSBO {
  value : vec4<u32>
};
@group(0) @binding(0) var<storage, read_write> ssbo : SSBO;
@group(0) @binding(1) var outImage : texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(1) fn main() {
    if (all(ssbo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
        textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
    } else {
        textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
    }
}
)";

// Vertex shader for vertex_buffer test:
// reads @location(0) pos : vec4<f32>; color = green iff all zero; position = (0,0,0,1).
constexpr std::string_view kVertexBufferVertexShader = R"(
struct VertexOut {
  @location(0) color : vec4<f32>,
  @builtin(position) position : vec4<f32>,
};

@vertex fn main(@location(0) pos : vec4<f32>) -> VertexOut {
  var output : VertexOut;
  if (all(pos == vec4<f32>(0.0, 0.0, 0.0, 0.0))) {
    output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
  } else {
    output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
  }
  output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
  return output;
}
)";

// Vertex shader for index_buffer test:
// reads @builtin(vertex_index); color = green iff vertex_index == 0u.
constexpr std::string_view kIndexBufferVertexShader = R"(
struct VertexOut {
  @location(0) color : vec4<f32>,
  @builtin(position) position : vec4<f32>,
};

@vertex
fn main(@builtin(vertex_index) VertexIndex : u32) -> VertexOut {
  var output : VertexOut;
  if (VertexIndex == 0u) {
    output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
  } else {
    output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
  }
  output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
  return output;
}
)";

// Shared fragment shader: passes i_color through.
constexpr std::string_view kFragmentShader = R"(
@fragment fn main(@location(0) i_color : vec4<f32>) -> @location(0) vec4<f32> {
    return i_color;
}
)";

// ---------------------------------------------------------------------------
// testBufferZeroInitInBindGroup:
// Creates a compute pipeline from a shader, creates a 1x1 COPY_SRC|STORAGE_BINDING
// output texture, binds the test buffer at binding 0 and the texture view at
// binding 1, dispatches 1 workgroup, then verifies both outputs.
// ---------------------------------------------------------------------------
static void testBufferZeroInitInBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view shaderCode,
    WGPUBuffer testBuffer,
    size_t bufferSize)
{
    // Create output texture: COPY_SRC | STORAGE_BINDING
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_StorageBinding;
    WGPUTexture outputTexture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

    // Create compute pipeline (layout auto)
    WGPUShaderModule computeModule = t.createShaderModuleTracked(shaderCode);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.compute.module     = computeModule;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    // Bind group: {binding 0 → testBuffer, binding 1 → outputTexture view}
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer  = testBuffer;
    entries[0].offset  = 0;
    entries[0].size    = static_cast<uint64_t>(bufferSize);

    entries[1].binding     = 1;
    entries[1].textureView = outputView;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries    = entries;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Dispatch 1 workgroup
    {
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
        wgpuComputePassEncoderSetPipeline(cp, pipeline);
        wgpuComputePassEncoderSetBindGroup(cp, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
        wgpuComputePassEncoderEnd(cp);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // Verify output texture is green and test buffer is all zeros
    expectOutputTextureGreen(t, outputTexture);
    expectBufferZero(t, testBuffer, bufferSize);
}

// ---------------------------------------------------------------------------
// createRenderPipelineForTest:
// point-list topology, shared fragment shader.
// testVertexBuffer=true: vertex layout arrayStride 16, float32x4 @location(0).
// testVertexBuffer=false: no vertex buffer layout.
// ---------------------------------------------------------------------------
static WGPURenderPipeline createRenderPipelineForTest(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view vertexShaderCode,
    bool testVertexBuffer)
{
    WGPUShaderModule vertModule = t.createShaderModuleTracked(vertexShaderCode);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    // writeMask defaults to All via WGPU_COLOR_TARGET_STATE_INIT

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.vertex.module      = vertModule;
    pipeDesc.vertex.entryPoint  = sv("main");
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipeDesc.multisample.count  = 1;
    pipeDesc.fragment           = &fragment;

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;

    if (testVertexBuffer) {
        attribute.format        = WGPUVertexFormat_Float32x4;
        attribute.offset        = 0;
        attribute.shaderLocation = 0;

        vertexBufferLayout.arrayStride    = 16;
        vertexBufferLayout.stepMode       = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = 1;
        vertexBufferLayout.attributes     = &attribute;

        pipeDesc.vertex.bufferCount = 1;
        pipeDesc.vertex.buffers     = &vertexBufferLayout;
    }

    return t.createRenderPipelineTracked(pipeDesc);
}

// ---------------------------------------------------------------------------
// Test case: partial_write_buffer
// 32-byte COPY_SRC|COPY_DST buffer; writeBuffer(0, [1..12]);
// verify bytes [0..11] == {1..12}, rest == 0.
// ---------------------------------------------------------------------------
CTS_TEST(g, "partial_write_buffer")
    .desc("Verify when we upload data to a part of a buffer with writeBuffer() just after the "
          "creation of the buffer, the remaining part of that buffer will be initialized to 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kBufferSize = 32;
        constexpr uint32_t kCopySize   = 12;
        constexpr uint32_t kOffset     = 0;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = kBufferSize;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // Build write data (bytes 1..12) and expected buffer (all zeros except [0..11])
        uint8_t writeData[kCopySize];
        uint8_t expectedData[kBufferSize] = {};
        for (uint32_t i = 0; i < kCopySize; ++i) {
            writeData[i] = static_cast<uint8_t>(i + 1);
            expectedData[kOffset + i] = writeData[i];
        }

        wgpuQueueWriteBuffer(t.queue(), buffer, kOffset, writeData, kCopySize);

        t.expectGPUBufferValuesEqual(buffer, expectedData, kBufferSize);
    });

// ---------------------------------------------------------------------------
// Test case: copy_buffer_to_buffer_copy_source
// 32-byte COPY_SRC buffer, never written; verify all 32 bytes are zero.
// ---------------------------------------------------------------------------
CTS_TEST(g, "copy_buffer_to_buffer_copy_source")
    .desc("Verify when the first usage of a GPUBuffer is being used as the source buffer of "
          "CopyBufferToBuffer(), the contents of the GPUBuffer have already been initialized to 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kBufferSize = 32;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = kBufferSize;
        bufDesc.usage = WGPUBufferUsage_CopySrc;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // expectGPUBufferValuesEqual uses copy internally (the harness copies it to a MAP_READ buffer)
        expectBufferZero(t, buffer, kBufferSize);
    });

// ---------------------------------------------------------------------------
// Test case: uniform_buffer
// 16-byte COPY_SRC|UNIFORM buffer; compute shader reads vec4<u32>, green-if-zero.
// Verify output texture green + buffer zeros.
// ---------------------------------------------------------------------------
CTS_TEST(g, "uniform_buffer")
    .desc("Verify when we use a GPUBuffer as a uniform buffer just after the creation of that "
          "GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kBoundBufferSize = 16;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = kBoundBufferSize;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Uniform;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        testBufferZeroInitInBindGroup(t, kUniformComputeShader, buffer, kBoundBufferSize);
    });

// ---------------------------------------------------------------------------
// Test case: storage_buffer
// 16-byte COPY_SRC|STORAGE buffer; compute shader reads var<storage, read_write>, green-if-zero.
// Verify output texture green + buffer zeros.
// ---------------------------------------------------------------------------
CTS_TEST(g, "storage_buffer")
    .desc("Verify when we use a GPUBuffer as a storage buffer just after the creation of that "
          "GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kBoundBufferSize = 16;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = kBoundBufferSize;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        testBufferZeroInitInBindGroup(t, kStorageComputeShader, buffer, kBoundBufferSize);
    });

// ---------------------------------------------------------------------------
// Test case: vertex_buffer
// 16-byte VERTEX|COPY_SRC buffer; render pipeline (point-list) reads @location(0) vec4f pos,
// green if all zero. Verify output texture green + buffer zeros.
// ---------------------------------------------------------------------------
CTS_TEST(g, "vertex_buffer")
    .desc("Verify when we use a GPUBuffer as a vertex buffer just after the creation of that "
          "GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kBufferSize = 16;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = kBufferSize;
        bufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopySrc;
        WGPUBuffer vertexBuffer = t.createBufferTracked(bufDesc);

        // Output texture: COPY_SRC | RENDER_ATTACHMENT (cleared to (0,0,0,0), green drawn)
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, kVertexBufferVertexShader, true);

        // Render pass: clear to (0,0,0,0), draw 1 point
        {
            WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAtt.view       = outputView;
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
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
        }

        // Verify output texture is green and vertex buffer is all zeros
        expectOutputTextureGreen(t, outputTexture);
        expectBufferZero(t, vertexBuffer, kBufferSize);
    });

// ---------------------------------------------------------------------------
// Test case: index_buffer
// 4-byte INDEX|COPY_SRC buffer; render pipeline (point-list), drawIndexed(1) with uint16 indices.
// Shader: green if VertexIndex == 0 (zero-initialized index). Verify output green + buffer zeros.
// ---------------------------------------------------------------------------
CTS_TEST(g, "index_buffer")
    .desc("Verify when we use a GPUBuffer as an index buffer just after the creation of that "
          "GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // The size of GPUBuffer must be at least 4 bytes.
        constexpr uint32_t kBufferSize = 4;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = kBufferSize;
        bufDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopySrc;
        WGPUBuffer indexBuffer = t.createBufferTracked(bufDesc);

        // Output texture: COPY_SRC | RENDER_ATTACHMENT (cleared to (0,0,0,0), green drawn)
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture outputTexture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView outputView = t.createViewTracked(outputTexture, viewDesc);

        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, kIndexBufferVertexShader, false);

        // Render pass: clear to (0,0,0,0), drawIndexed(1)
        {
            WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAtt.view       = outputView;
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
            wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, 4);
            wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
        }

        // Verify output texture is green and index buffer is all zeros
        expectOutputTextureGreen(t, outputTexture);
        expectBufferZero(t, indexBuffer, kBufferSize);
    });

} // namespace
