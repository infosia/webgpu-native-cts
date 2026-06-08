// Ported from gpuweb/cts src/webgpu/api/operation/texture_view/write.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports the format test: rgba8unorm, sampleCount=1, viewUsageMethod=inherit, 3 non-MSAA write methods.
// Deferred: render-pass-resolve, sampleCount=4, full kRegularTextureFormats matrix, viewUsageMethod=minimal,
//           dimension and aspect tests (unimplemented upstream).

#include <array>
#include <cmath>
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
    "api,operation,texture_view,write",
    "Writing textures through texture views via storage-write-compute, storage-write-fragment, "
    "render-pass-store; readback via copyTextureToBuffer and per-texel ±1 check.");

// -----------------------------------------------------------------------------
// Constants (match upstream)
// -----------------------------------------------------------------------------

constexpr uint32_t kTextureSize    = 16;
constexpr uint32_t kBytesPerRow    = 256;   // align(16*4=64, 256) = 256
constexpr uint32_t kRowsPerImage   = 16;
constexpr uint64_t kReadbackSize   = static_cast<uint64_t>(kBytesPerRow) * kRowsPerImage; // 4096

// kColorsFloat — copied verbatim from upstream write.spec.ts kColorsFloat (rgba8unorm → float type).
// Each entry is {R, G, B, A}.
constexpr std::array<std::array<float, 4>, 10> kColorsFloat = {{
    {{ 1.0f, 0.0f, 0.0f, 0.8f }},
    {{ 0.0f, 1.0f, 0.0f, 0.7f }},
    {{ 0.0f, 0.0f, 0.0f, 0.6f }},
    {{ 0.0f, 0.0f, 0.0f, 0.5f }},
    {{ 1.0f, 1.0f, 1.0f, 0.4f }},
    {{ 0.7f, 0.0f, 0.0f, 0.3f }},
    {{ 0.0f, 0.8f, 0.0f, 0.2f }},
    {{ 0.0f, 0.0f, 0.9f, 0.1f }},
    {{ 0.1f, 0.2f, 0.0f, 0.3f }},
    {{ 0.4f, 0.3f, 0.6f, 0.8f }},
}};

// -----------------------------------------------------------------------------
// Shaders
// -----------------------------------------------------------------------------

// Fullscreen-quad vertex shader (kFullscreenQuadVertexShaderCode from upstream util/shader.ts:15).
constexpr std::string_view kFullscreenQuadVertexShaderCode = R"(
struct VertexOutput {
  @builtin(position) Position : vec4<f32>
};

@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> VertexOutput {
  var pos = array<vec2<f32>, 6>(
      vec2<f32>( 1.0,  1.0),
      vec2<f32>( 1.0, -1.0),
      vec2<f32>(-1.0, -1.0),
      vec2<f32>( 1.0,  1.0),
      vec2<f32>(-1.0, -1.0),
      vec2<f32>(-1.0,  1.0));

  var output : VertexOutput;
  output.Position = vec4<f32>(pos[VertexIndex], 0.0, 1.0);
  return output;
}
)";

// storage-write-compute shader
// @group(0) @binding(0) = texture_storage_2d<rgba8unorm, write> dst
// dispatch 16x16 x 1
constexpr std::string_view kComputeWriteShader = R"(
@group(0) @binding(0) var dst: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(1, 1) fn main(
  @builtin(global_invocation_id) global_id: vec3<u32>,
) {
  const src = array<vec4f, 10>(
    vec4f(1.0, 0.0, 0.0, 0.8),
    vec4f(0.0, 1.0, 0.0, 0.7),
    vec4f(0.0, 0.0, 0.0, 0.6),
    vec4f(0.0, 0.0, 0.0, 0.5),
    vec4f(1.0, 1.0, 1.0, 0.4),
    vec4f(0.7, 0.0, 0.0, 0.3),
    vec4f(0.0, 0.8, 0.0, 0.2),
    vec4f(0.0, 0.0, 0.9, 0.1),
    vec4f(0.1, 0.2, 0.0, 0.3),
    vec4f(0.4, 0.3, 0.6, 0.8)
  );
  let coord = vec2u(global_id.xy);
  let idx = coord.x + coord.y * 16u;
  textureStore(dst, coord, src[idx % 10u]);
}
)";

// storage-write-fragment vertex shader (reuse kFullscreenQuadVertexShaderCode above)

// storage-write-fragment shader
// Fragment shader has NO @location return — only textureStore.
// @group(0) @binding(0) = texture_storage_2d<rgba8unorm, write> dst
constexpr std::string_view kFragmentWriteShader = R"(
@group(0) @binding(0) var dst: texture_storage_2d<rgba8unorm, write>;

@fragment fn main(
  @builtin(position) fragCoord: vec4<f32>,
) {
  const src = array<vec4f, 10>(
    vec4f(1.0, 0.0, 0.0, 0.8),
    vec4f(0.0, 1.0, 0.0, 0.7),
    vec4f(0.0, 0.0, 0.0, 0.6),
    vec4f(0.0, 0.0, 0.0, 0.5),
    vec4f(1.0, 1.0, 1.0, 0.4),
    vec4f(0.7, 0.0, 0.0, 0.3),
    vec4f(0.0, 0.8, 0.0, 0.2),
    vec4f(0.0, 0.0, 0.9, 0.1),
    vec4f(0.1, 0.2, 0.0, 0.3),
    vec4f(0.4, 0.3, 0.6, 0.8)
  );
  let coord = vec2u(fragCoord.xy);
  let idx = coord.x + coord.y * 16u;
  textureStore(dst, coord, src[idx % 10u]);
}
)";

// render-pass-store fragment shader: returns @location(0) vec4f
constexpr std::string_view kRenderPassStoreFragmentShader = R"(
@fragment fn main(
  @builtin(position) fragCoord: vec4<f32>,
) -> @location(0) vec4f {
  const src = array<vec4f, 10>(
    vec4f(1.0, 0.0, 0.0, 0.8),
    vec4f(0.0, 1.0, 0.0, 0.7),
    vec4f(0.0, 0.0, 0.0, 0.6),
    vec4f(0.0, 0.0, 0.0, 0.5),
    vec4f(1.0, 1.0, 1.0, 0.4),
    vec4f(0.7, 0.0, 0.0, 0.3),
    vec4f(0.0, 0.8, 0.0, 0.2),
    vec4f(0.0, 0.0, 0.9, 0.1),
    vec4f(0.1, 0.2, 0.0, 0.3),
    vec4f(0.4, 0.3, 0.6, 0.8)
  );
  let coord = vec2u(fragCoord.xy);
  let idx = coord.x + coord.y * 16u;
  return src[idx % 10u];
}
)";

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// Quantize a float channel value to rgba8unorm byte: round(clamp(c, 0, 1) * 255).
static uint8_t quantize(float c) {
    float clamped = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

// Check the readback buffer against the expected kColorsFloat pattern.
// For each texel (x, y): byte offset = y*256 + x*4; expected = kColorsFloat[(y*16+x)%10] quantized.
// Returns a descriptive string on first mismatch, or nullopt on success.
std::optional<std::string> checkReadback(const uint8_t* actual, size_t len) {
    for (uint32_t y = 0; y < kTextureSize; ++y) {
        for (uint32_t x = 0; x < kTextureSize; ++x) {
            const uint64_t offset = static_cast<uint64_t>(y) * kBytesPerRow
                                  + static_cast<uint64_t>(x) * 4u;
            if (offset + 4u > len) {
                std::ostringstream msg;
                msg << "readback buffer too small at (" << x << ", " << y << ")"
                    << ": offset " << offset << " + 4 > len " << len;
                return msg.str();
            }
            const uint32_t idx = (y * kTextureSize + x) % 10u;
            const auto& color = kColorsFloat[idx];
            static const char* channels[] = {"R", "G", "B", "A"};
            for (uint32_t ch = 0; ch < 4u; ++ch) {
                const uint8_t expected = quantize(color[ch]);
                const uint8_t got      = actual[offset + ch];
                const int diff         = static_cast<int>(got) - static_cast<int>(expected);
                if (diff < -1 || diff > 1) {
                    std::ostringstream msg;
                    msg << "texel (" << x << ", " << y << ") channel " << channels[ch]
                        << ": expected " << static_cast<int>(expected)
                        << " ±1, got " << static_cast<int>(got);
                    return msg.str();
                }
            }
        }
    }
    return std::nullopt;
}

// Copy the 16x16 target texture to a readback buffer and verify texel values.
void verifyTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kReadbackSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture  = texture;
    source.mipLevel = 0;
    source.origin   = WGPUOrigin3D{0, 0, 0};
    source.aspect   = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer              = readback;
    destination.layout.offset       = 0;
    destination.layout.bytesPerRow  = kBytesPerRow;
    destination.layout.rowsPerImage = kRowsPerImage;

    WGPUExtent3D copySize = WGPUExtent3D{kTextureSize, kTextureSize, 1};

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    t.expectGPUBufferValuesPassCheck(
        readback,
        checkReadback,
        0,
        static_cast<size_t>(kReadbackSize));
}

// -----------------------------------------------------------------------------
// Test case: storage-write-compute
// Texture usage: STORAGE_BINDING | COPY_SRC
// View: createView() with WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT defaults (usage=0 = inherit)
// Compute pipeline, layout auto, dispatch 16x16
// -----------------------------------------------------------------------------
void runStorageWriteCompute(AllFeaturesMaxLimitsGpuTest& t) {
    // 1. Create texture: STORAGE_BINDING | COPY_SRC
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture   = t.createTextureTracked(texDesc);

    // 2. Create view (inherit usage: usage=0 via WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT defaults)
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    // 3. Compute pipeline (layout auto)
    WGPUShaderModule computeModule = t.createShaderModuleTracked(kComputeWriteShader);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.compute.module     = computeModule;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    // 4. Bind group: {binding 0 → storage view}
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding     = 0;
    entry.textureView = view;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // 5. Dispatch 16x16 compute workgroups
    {
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
        wgpuComputePassEncoderSetPipeline(cp, pipeline);
        wgpuComputePassEncoderSetBindGroup(cp, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(cp, kTextureSize, kTextureSize, 1);
        wgpuComputePassEncoderEnd(cp);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // 6. Readback and verify
    verifyTexture(t, texture);
}

// -----------------------------------------------------------------------------
// Test case: storage-write-fragment
// Texture usage: STORAGE_BINDING | COPY_SRC
// View: createView() defaults (usage=0 = inherit)
// Placeholder 16x16 rgba8unorm RENDER_ATTACHMENT (writeMask=None, storeOp=Discard)
// Fragment shader has NO @location return — only textureStore
// -----------------------------------------------------------------------------
void runStorageWriteFragment(AllFeaturesMaxLimitsGpuTest& t) {
    // 1. Create target texture: STORAGE_BINDING | COPY_SRC
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture   = t.createTextureTracked(texDesc);

    // 2. Create view (inherit usage: usage=0)
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    // 3. Create placeholder color attachment texture: RENDER_ATTACHMENT only
    WGPUTextureDescriptor phDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    phDesc.size          = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    phDesc.mipLevelCount = 1;
    phDesc.sampleCount   = 1;
    phDesc.dimension     = WGPUTextureDimension_2D;
    phDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    phDesc.usage         = WGPUTextureUsage_RenderAttachment;
    WGPUTexture placeholder = t.createTextureTracked(phDesc);

    WGPUTextureViewDescriptor phViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView placeholderView = t.createViewTracked(placeholder, phViewDesc);

    // 4. Render pipeline: layout auto, fullscreen-quad vertex, fragment with no return
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kFullscreenQuadVertexShaderCode);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentWriteShader);

    // Placeholder color target: writeMask=None (0)
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_None;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout              = nullptr; // auto layout
    pipeDesc.vertex.module       = vertModule;
    pipeDesc.vertex.entryPoint   = sv("main");
    pipeDesc.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count   = 1;
    pipeDesc.fragment            = &fragment;
    WGPURenderPipeline pipeline  = t.createRenderPipelineTracked(pipeDesc);

    // 5. Bind group: {binding 0 → storage view}
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding     = 0;
    entry.textureView = view;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // 6. Render pass: placeholder attachment, loadOp clear, storeOp discard; draw(6)
    {
        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = placeholderView;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Discard;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 1;
        passDesc.colorAttachments       = &colorAtt;
        passDesc.depthStencilAttachment = nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // 7. Readback and verify
    verifyTexture(t, texture);
}

// -----------------------------------------------------------------------------
// Test case: render-pass-store
// Texture usage: RENDER_ATTACHMENT | COPY_SRC
// View: createView() defaults (usage=0 = inherit) — used as color attachment
// Fullscreen-quad vertex, fragment returns @location(0) vec4f
// loadOp clear, storeOp store
// -----------------------------------------------------------------------------
void runRenderPassStore(AllFeaturesMaxLimitsGpuTest& t) {
    // 1. Create target texture: RENDER_ATTACHMENT | COPY_SRC
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture texture   = t.createTextureTracked(texDesc);

    // 2. Create view (inherit usage: usage=0)
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    // 3. Render pipeline: layout auto, fullscreen-quad vertex, fragment returns @location(0)
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kFullscreenQuadVertexShaderCode);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kRenderPassStoreFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    // writeMask defaults to All (0xF) via WGPU_COLOR_TARGET_STATE_INIT

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.vertex.module      = vertModule;
    pipeDesc.vertex.entryPoint  = sv("main");
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count  = 1;
    pipeDesc.fragment           = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // 4. Render pass: view as color attachment, loadOp clear, storeOp store; draw(6)
    {
        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view       = view;
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
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // 5. Readback and verify
    verifyTexture(t, texture);
}

// -----------------------------------------------------------------------------
// Test cases (3 — one per write method)
// -----------------------------------------------------------------------------

CTS_TEST(g, "storage-write-compute")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runStorageWriteCompute(t);
    });

CTS_TEST(g, "storage-write-fragment")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runStorageWriteFragment(t);
    });

CTS_TEST(g, "render-pass-store")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runRenderPassStore(t);
    });

} // namespace
