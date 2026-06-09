// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/sample_mask.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports the fragment_output_mask test: flat fragment entry, sampleCount=4, color + depth + stencil per-sample checks.
// Deferred: sampleCount=1, interp entry, full matrix, alpha_to_coverage_mask.

#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,sample_mask",
    "MSAA sample mask tests: rasterization mask x pipeline mask x fragment output mask.");

// -----------------------------------------------------------------------------
// Constants (match upstream)
// -----------------------------------------------------------------------------

constexpr uint32_t kSampleCount       = 4;
constexpr uint32_t kRenderTargetSize  = 1;
constexpr WGPUTextureFormat kFormat   = WGPUTextureFormat_RGBA8Unorm;
constexpr WGPUTextureFormat kDSFormat = WGPUTextureFormat_Depth24PlusStencil8;

// Depth/stencil constants (match upstream kDepth*/kStencil* consts).
constexpr float    kDepthClearValue       = 1.0f;
constexpr float    kDepthWriteValue       = 0.0f;
constexpr uint32_t kStencilClearValue     = 0u;
constexpr uint32_t kStencilReferenceValue = 0xffu;

// kColors[i] = rgba8 bytes for sample i: Red, Green, Blue, Yellow
// These match upstream kColors[0..3].
constexpr std::array<std::array<uint8_t, 4>, 4> kColors = {{
    {{0xff, 0x00, 0x00, 0xff}},  // 0: Red
    {{0x00, 0xff, 0x00, 0xff}},  // 1: Green
    {{0x00, 0x00, 0xff, 0xff}},  // 2: Blue
    {{0xff, 0xff, 0x00, 0xff}},  // 3: Yellow
}};

// Storage readback: 4 samples x 4 components x 4 bytes = 64 bytes.
constexpr uint32_t kReadbackComponentCount = 4u;
constexpr uint32_t kReadbackSampleCount    = kSampleCount;
constexpr uint64_t kReadbackBufferSize     =
    kReadbackSampleCount * kReadbackComponentCount * sizeof(float);

// -----------------------------------------------------------------------------
// Shaders (verbatim from upstream kSampleMaskTestShader, flat path only)
// -----------------------------------------------------------------------------

// The vertex + fragment shader module.  Contains:
//   - Varyings struct
//   - vmain: instanced vertex shader placing tiny quads at the 4 standard
//     sample centers of a 1×1 NDC render target.
//   - fmain__fragment_output_mask__flat: flat fragment writing @builtin(sample_mask).
// The uvsInterpolated array and interp/alpha-to-coverage entry points are
// intentionally omitted (not referenced by the flat path).
constexpr std::string_view kSampleMaskShader = R"(
struct Varyings {
  @builtin(position) Position : vec4<f32>,
  @location(0) @interpolate(flat, either) uvFlat : vec2<f32>,
}

@vertex
fn vmain(@builtin(vertex_index) VertexIndex : u32,
    @builtin(instance_index) InstanceIndex : u32) -> Varyings {
  // Standard sample locations within a pixel, where the pixel ranges from (-1,-1) to (1,1),
  // and is centered at (0,0) (NDC - the test uses a 1x1 render target).
  // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_standard_multisample_quality_levels
  var sampleCenters = array(
      // sampleCount = 1
      vec2f(0, 0),
      // sampleCount = 4
      vec2f(-2,  6) / 8,
      vec2f( 6,  2) / 8,
      vec2f(-6, -2) / 8,
      vec2f( 2, -6) / 8,
    );
  // A tiny quad to draw around the sample center to ensure we hit only the expected point.
  let kTinyQuadRadius = 1.0 / 32;
  var tinyQuad = array(
    vec2f( kTinyQuadRadius,  kTinyQuadRadius),
    vec2f( kTinyQuadRadius, -kTinyQuadRadius),
    vec2f(-kTinyQuadRadius, -kTinyQuadRadius),
    vec2f( kTinyQuadRadius,  kTinyQuadRadius),
    vec2f(-kTinyQuadRadius, -kTinyQuadRadius),
    vec2f(-kTinyQuadRadius,  kTinyQuadRadius),
    );

  var uvsFlat = array(
      // sampleCount = 1
      // Note: avoids hitting the point between the 4 texels.
      vec2f(0.51, 0.51),
      // sampleCount = 4
      vec2f(0.25, 0.25),
      vec2f(0.75, 0.25),
      vec2f(0.25, 0.75),
      vec2f(0.75, 0.75),
    );

  var output : Varyings;
  let pos = sampleCenters[InstanceIndex] + tinyQuad[VertexIndex];
  output.Position = vec4(pos, 0.0, 1.0);
  output.uvFlat = uvsFlat[InstanceIndex];
  return output;
}

//
// Fragment shader (flat path for fragment_output_mask test)
//

@group(0) @binding(0) var mySampler: sampler;
@group(0) @binding(1) var myTexture: texture_2d<f32>;
@group(0) @binding(2) var<uniform> fragMask: u32;

struct FragmentOutput1 {
  @builtin(sample_mask) mask : u32,
  @location(0) color : vec4<f32>,
}

@fragment fn fmain__fragment_output_mask__flat(varyings: Varyings) -> FragmentOutput1 {
  return FragmentOutput1(fragMask, textureSample(myTexture, mySampler, varyings.uvFlat));
}
)";

// Per-sample readback compute shader (color):
// mirrors upstream copy2DTextureToBufferUsingComputePass (type f32, componentCount 4, sampleCount 4, 1x1).
constexpr std::string_view kReadbackComputeShader = R"(
@group(0) @binding(0) var src: texture_multisampled_2d<f32>;
@group(0) @binding(1) var<storage, read_write> dst: array<f32>;

@compute @workgroup_size(1) fn main() {
  for (var s = 0u; s < 4u; s = s + 1u) {
    let v = textureLoad(src, vec2<i32>(0, 0), s);
    for (var c = 0u; c < 4u; c = c + 1u) { dst[s * 4u + c] = v[c]; }
  }
}
)";

// Per-sample depth readback compute shader:
// depth aspect bound as texture_multisampled_2d<f32>; reads .r of each sample into a 4-f32 buffer.
constexpr std::string_view kDepthReadbackComputeShader = R"(
@group(0) @binding(0) var src: texture_multisampled_2d<f32>;
@group(0) @binding(1) var<storage, read_write> dst: array<f32>;

@compute @workgroup_size(1) fn main() {
  for (var s = 0u; s < 4u; s = s + 1u) {
    dst[s] = textureLoad(src, vec2<i32>(0, 0), s).r;
  }
}
)";

// Per-sample stencil readback compute shader:
// stencil aspect bound as texture_multisampled_2d<u32>; reads .r of each sample into a 4-u32 buffer.
constexpr std::string_view kStencilReadbackComputeShader = R"(
@group(0) @binding(0) var src: texture_multisampled_2d<u32>;
@group(0) @binding(1) var<storage, read_write> dst: array<u32>;

@compute @workgroup_size(1) fn main() {
  for (var s = 0u; s < 4u; s = s + 1u) {
    dst[s] = textureLoad(src, vec2<i32>(0, 0), s).r;
  }
}
)";

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Run the sample-mask test for one (rasterizationMask, sampleMask, fragmentShaderOutputMask).
void runSampleMaskTest(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t rasterizationMask,
    uint32_t sampleMask,
    uint32_t fragmentShaderOutputMask)
{
    // ------------------------------------------------------------------
    // 1. fragMask uniform buffer (4 bytes)
    // ------------------------------------------------------------------
    WGPUBufferDescriptor fragMaskBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    fragMaskBufDesc.size  = 4;
    fragMaskBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer fragMaskBuffer = t.createBufferTracked(fragMaskBufDesc);
    wgpuQueueWriteBuffer(t.queue(), fragMaskBuffer, 0, &fragmentShaderOutputMask, sizeof(uint32_t));

    // ------------------------------------------------------------------
    // 2. 2x2 sample texture (rgba8unorm, texel[x+y*2] = kColors[x+y*2])
    //    usage: TEXTURE_BINDING | COPY_DST
    // ------------------------------------------------------------------
    WGPUTextureDescriptor sampleTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    sampleTexDesc.size          = WGPUExtent3D{2, 2, 1};
    sampleTexDesc.mipLevelCount = 1;
    sampleTexDesc.sampleCount   = 1;
    sampleTexDesc.dimension     = WGPUTextureDimension_2D;
    sampleTexDesc.format        = kFormat;
    sampleTexDesc.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture sampleTexture   = t.createTextureTracked(sampleTexDesc);

    // 4 texels x 4 bytes = 16 bytes; bytesPerRow = 8 (2 texels * 4 bytes).
    const std::array<uint8_t, 16> texelData = {{
        kColors[0][0], kColors[0][1], kColors[0][2], kColors[0][3],  // (0,0) Red
        kColors[1][0], kColors[1][1], kColors[1][2], kColors[1][3],  // (1,0) Green
        kColors[2][0], kColors[2][1], kColors[2][2], kColors[2][3],  // (0,1) Blue
        kColors[3][0], kColors[3][1], kColors[3][2], kColors[3][3],  // (1,1) Yellow
    }};
    WGPUTexelCopyBufferLayout texLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    texLayout.offset       = 0;
    texLayout.bytesPerRow  = 8;   // 2 texels * 4 bytes
    texLayout.rowsPerImage = 2;
    t.queueWriteTexture(sampleTexture, WGPUExtent3D{2, 2, 1}, texLayout,
                        texelData.data(), texelData.size());

    // Sampler: nearest/nearest
    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

    // ------------------------------------------------------------------
    // 3. Multisampled 1x1 render target (sampleCount=4, RENDER_ATTACHMENT | TEXTURE_BINDING)
    // ------------------------------------------------------------------
    WGPUTextureDescriptor rtDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    rtDesc.size          = WGPUExtent3D{kRenderTargetSize, kRenderTargetSize, 1};
    rtDesc.mipLevelCount = 1;
    rtDesc.sampleCount   = kSampleCount;
    rtDesc.dimension     = WGPUTextureDimension_2D;
    rtDesc.format        = kFormat;
    rtDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture renderTarget = t.createTextureTracked(rtDesc);

    WGPUTextureViewDescriptor rtViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView rtView = t.createViewTracked(renderTarget, rtViewDesc);

    // ------------------------------------------------------------------
    // 3b. Multisampled 1x1 depth-stencil texture (sampleCount=4, depth24plus-stencil8,
    //     RENDER_ATTACHMENT | TEXTURE_BINDING)
    // ------------------------------------------------------------------
    WGPUTextureDescriptor dsTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    dsTexDesc.size          = WGPUExtent3D{kRenderTargetSize, kRenderTargetSize, 1};
    dsTexDesc.mipLevelCount = 1;
    dsTexDesc.sampleCount   = kSampleCount;
    dsTexDesc.dimension     = WGPUTextureDimension_2D;
    dsTexDesc.format        = kDSFormat;
    dsTexDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture dsTexture   = t.createTextureTracked(dsTexDesc);

    // Full combined view (used for render pass depth-stencil attachment)
    WGPUTextureViewDescriptor dsCombinedViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView dsView = t.createViewTracked(dsTexture, dsCombinedViewDesc);

    // Depth-only aspect view (bound as texture_multisampled_2d<f32> in compute readback)
    WGPUTextureViewDescriptor dsDepthViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    dsDepthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTextureView dsDepthView = t.createViewTracked(dsTexture, dsDepthViewDesc);

    // Stencil-only aspect view (bound as texture_multisampled_2d<u32> in compute readback)
    WGPUTextureViewDescriptor dsStencilViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    dsStencilViewDesc.aspect = WGPUTextureAspect_StencilOnly;
    WGPUTextureView dsStencilView = t.createViewTracked(dsTexture, dsStencilViewDesc);

    // ------------------------------------------------------------------
    // 4. Render pipeline: layout auto, vmain, fmain__fragment_output_mask__flat,
    //    triangle-list, multisample{count=4, mask=sampleMask, alphaToCoverage=false}.
    //    depthStencil: depth24plus-stencil8, depthWriteEnabled, depthCompare always,
    //    stencilFront/Back {compare always, passOp replace}.
    // ------------------------------------------------------------------
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(kSampleMaskShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = kFormat;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = shaderModule;
    fragment.entryPoint  = stringView("fmain__fragment_output_mask__flat");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPUDepthStencilState dsState = WGPU_DEPTH_STENCIL_STATE_INIT;
    dsState.format               = kDSFormat;
    dsState.depthWriteEnabled    = WGPUOptionalBool_True;
    dsState.depthCompare         = WGPUCompareFunction_Always;
    dsState.stencilFront.compare  = WGPUCompareFunction_Always;
    dsState.stencilFront.failOp   = WGPUStencilOperation_Keep;
    dsState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    dsState.stencilFront.passOp   = WGPUStencilOperation_Replace;
    dsState.stencilBack.compare   = WGPUCompareFunction_Always;
    dsState.stencilBack.failOp    = WGPUStencilOperation_Keep;
    dsState.stencilBack.depthFailOp = WGPUStencilOperation_Keep;
    dsState.stencilBack.passOp    = WGPUStencilOperation_Replace;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout = null → auto layout
    pipeDesc.layout                             = nullptr;
    pipeDesc.vertex.module                      = shaderModule;
    pipeDesc.vertex.entryPoint                  = stringView("vmain");
    pipeDesc.primitive.topology                 = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count                  = kSampleCount;
    pipeDesc.multisample.mask                   = sampleMask;
    pipeDesc.multisample.alphaToCoverageEnabled = false;
    pipeDesc.fragment                           = &fragment;
    pipeDesc.depthStencil                       = &dsState;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // ------------------------------------------------------------------
    // 5. Bind group for the render pass (auto layout from pipeline):
    //    {0: sampler, 1: sampleTexture view, 2: fragMaskBuffer}
    // ------------------------------------------------------------------
    WGPUTextureViewDescriptor sampleTexViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView sampleTexView = t.createViewTracked(sampleTexture, sampleTexViewDesc);

    WGPUBindGroupLayout renderBGL = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);

    std::array<WGPUBindGroupEntry, 3> renderEntries;
    renderEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    renderEntries[0].binding = 0;
    renderEntries[0].sampler = sampler;

    renderEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    renderEntries[1].binding     = 1;
    renderEntries[1].textureView = sampleTexView;

    renderEntries[2] = WGPU_BIND_GROUP_ENTRY_INIT;
    renderEntries[2].binding = 2;
    renderEntries[2].buffer  = fragMaskBuffer;
    renderEntries[2].offset  = 0;
    renderEntries[2].size    = 4;

    WGPUBindGroupDescriptor renderBGDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    renderBGDesc.layout     = renderBGL;
    renderBGDesc.entryCount = renderEntries.size();
    renderBGDesc.entries    = renderEntries.data();
    WGPUBindGroup renderBG = t.createBindGroupTracked(renderBGDesc);
    wgpuBindGroupLayoutRelease(renderBGL);

    // ------------------------------------------------------------------
    // 6. Render pass: clear color {0,0,0,0}, clear depth=1.0, clear stencil=0,
    //    draw instanced quads per rasterizationMask bit.
    //    Upstream getTargetTexture sampleCount==4 branch:
    //      bit0 → draw(6,1,0,1)  top-left quad     → sample 0 → kColors[0] Red
    //      bit1 → draw(6,1,0,2)  top-right quad    → sample 1 → kColors[1] Green
    //      bit2 → draw(6,1,0,3)  bottom-left quad  → sample 2 → kColors[2] Blue
    //      bit3 → draw(6,1,0,4)  bottom-right quad → sample 3 → kColors[3] Yellow
    // ------------------------------------------------------------------
    {
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view        = rtView;
        colorAttachment.loadOp      = WGPULoadOp_Clear;
        colorAttachment.storeOp     = WGPUStoreOp_Store;
        colorAttachment.clearValue  = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDepthStencilAttachment dsAttachment =
            WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        dsAttachment.view              = dsView;
        dsAttachment.depthLoadOp       = WGPULoadOp_Clear;
        dsAttachment.depthStoreOp      = WGPUStoreOp_Store;
        dsAttachment.depthClearValue   = kDepthClearValue;
        dsAttachment.stencilLoadOp     = WGPULoadOp_Clear;
        dsAttachment.stencilStoreOp    = WGPUStoreOp_Store;
        dsAttachment.stencilClearValue = kStencilClearValue;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount    = 1;
        passDesc.colorAttachments        = &colorAttachment;
        passDesc.depthStencilAttachment  = &dsAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, renderBG, 0, nullptr);
        // Set stencil reference value (0xff) before draws (upstream setStencilReference).
        wgpuRenderPassEncoderSetStencilReference(pass, kStencilReferenceValue);
        // Instanced draws: instance index selects sample center + uvsFlat entry.
        if ((rasterizationMask & 1u) != 0u) { wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 1); }
        if ((rasterizationMask & 2u) != 0u) { wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 2); }
        if ((rasterizationMask & 4u) != 0u) { wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 3); }
        if ((rasterizationMask & 8u) != 0u) { wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 4); }
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // ------------------------------------------------------------------
    // 7. Readback via compute pass:
    //    texture_multisampled_2d<f32> + textureLoad → storage buffer (64 bytes).
    // ------------------------------------------------------------------
    WGPUBufferDescriptor readBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readBufDesc.size  = kReadbackBufferSize;
    readBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer readbackBuffer = t.createBufferTracked(readBufDesc);

    WGPUShaderModule computeModule = t.createShaderModuleTracked(kReadbackComputeShader);

    WGPUComputePipelineDescriptor compPipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    // layout = null → auto
    compPipeDesc.layout              = nullptr;
    compPipeDesc.compute.module      = computeModule;
    compPipeDesc.compute.entryPoint  = stringView("main");
    WGPUComputePipeline computePipeline = t.createComputePipelineTracked(compPipeDesc);

    WGPUBindGroupLayout computeBGL = wgpuComputePipelineGetBindGroupLayout(computePipeline, 0);

    std::array<WGPUBindGroupEntry, 2> computeEntries;
    computeEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    computeEntries[0].binding     = 0;
    computeEntries[0].textureView = rtView;  // plain view of the 4-sample texture

    computeEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    computeEntries[1].binding = 1;
    computeEntries[1].buffer  = readbackBuffer;
    computeEntries[1].offset  = 0;
    computeEntries[1].size    = kReadbackBufferSize;

    WGPUBindGroupDescriptor computeBGDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    computeBGDesc.layout     = computeBGL;
    computeBGDesc.entryCount = computeEntries.size();
    computeBGDesc.entries    = computeEntries.data();
    WGPUBindGroup computeBG = t.createBindGroupTracked(computeBGDesc);
    wgpuBindGroupLayoutRelease(computeBGL);

    {
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
        wgpuComputePassEncoderSetPipeline(cp, computePipeline);
        wgpuComputePassEncoderSetBindGroup(cp, 0, computeBG, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
        wgpuComputePassEncoderEnd(cp);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // ------------------------------------------------------------------
    // 8. Build expected f32 array (16 floats: 4 samples x 4 components).
    //    sample i covered iff (rasterizationMask & sampleMask & fragmentShaderOutputMask & (1<<i)) != 0.
    //    If covered → kColors[i][0..3] / 255.0; else 0,0,0,0.
    // ------------------------------------------------------------------
    std::array<float, 16> expected = {};
    for (uint32_t i = 0; i < kSampleCount; ++i) {
        const bool covered =
            (rasterizationMask & sampleMask & fragmentShaderOutputMask & (1u << i)) != 0u;
        if (covered) {
            expected[i * 4 + 0] = static_cast<float>(kColors[i][0]) / 255.0f;
            expected[i * 4 + 1] = static_cast<float>(kColors[i][1]) / 255.0f;
            expected[i * 4 + 2] = static_cast<float>(kColors[i][2]) / 255.0f;
            expected[i * 4 + 3] = static_cast<float>(kColors[i][3]) / 255.0f;
        }
        // else 0.0f (already zero-initialized)
    }

    t.expectGPUBufferValuesEqual(
        readbackBuffer,
        expected.data(),
        expected.size() * sizeof(float));

    // ------------------------------------------------------------------
    // 9. Depth readback via compute pass:
    //    depth aspect view (WGPUTextureAspect_DepthOnly) as texture_multisampled_2d<f32>
    //    → 4-f32 storage buffer (16 bytes).
    // ------------------------------------------------------------------
    constexpr uint64_t kDepthReadbackSize   = kSampleCount * sizeof(float);
    constexpr uint64_t kStencilReadbackSize = kSampleCount * sizeof(uint32_t);

    WGPUBufferDescriptor depthBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    depthBufDesc.size  = kDepthReadbackSize;
    depthBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer depthReadbackBuffer = t.createBufferTracked(depthBufDesc);

    {
        WGPUShaderModule depthCompModule =
            t.createShaderModuleTracked(kDepthReadbackComputeShader);

        WGPUComputePipelineDescriptor depthCompPipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        depthCompPipeDesc.layout             = nullptr;
        depthCompPipeDesc.compute.module     = depthCompModule;
        depthCompPipeDesc.compute.entryPoint = stringView("main");
        WGPUComputePipeline depthCompPipeline =
            t.createComputePipelineTracked(depthCompPipeDesc);

        WGPUBindGroupLayout depthCompBGL =
            wgpuComputePipelineGetBindGroupLayout(depthCompPipeline, 0);

        std::array<WGPUBindGroupEntry, 2> depthEntries;
        depthEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
        depthEntries[0].binding     = 0;
        depthEntries[0].textureView = dsDepthView;

        depthEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
        depthEntries[1].binding = 1;
        depthEntries[1].buffer  = depthReadbackBuffer;
        depthEntries[1].offset  = 0;
        depthEntries[1].size    = kDepthReadbackSize;

        WGPUBindGroupDescriptor depthBGDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        depthBGDesc.layout     = depthCompBGL;
        depthBGDesc.entryCount = depthEntries.size();
        depthBGDesc.entries    = depthEntries.data();
        WGPUBindGroup depthBG = t.createBindGroupTracked(depthBGDesc);
        wgpuBindGroupLayoutRelease(depthCompBGL);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
        wgpuComputePassEncoderSetPipeline(cp, depthCompPipeline);
        wgpuComputePassEncoderSetBindGroup(cp, 0, depthBG, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
        wgpuComputePassEncoderEnd(cp);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // ------------------------------------------------------------------
    // 10. Stencil readback via compute pass:
    //     stencil aspect view (WGPUTextureAspect_StencilOnly) as texture_multisampled_2d<u32>
    //     → 4-u32 storage buffer (16 bytes).
    // ------------------------------------------------------------------
    WGPUBufferDescriptor stencilBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    stencilBufDesc.size  = kStencilReadbackSize;
    stencilBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer stencilReadbackBuffer = t.createBufferTracked(stencilBufDesc);

    {
        WGPUShaderModule stencilCompModule =
            t.createShaderModuleTracked(kStencilReadbackComputeShader);

        WGPUComputePipelineDescriptor stencilCompPipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        stencilCompPipeDesc.layout             = nullptr;
        stencilCompPipeDesc.compute.module     = stencilCompModule;
        stencilCompPipeDesc.compute.entryPoint = stringView("main");
        WGPUComputePipeline stencilCompPipeline =
            t.createComputePipelineTracked(stencilCompPipeDesc);

        WGPUBindGroupLayout stencilCompBGL =
            wgpuComputePipelineGetBindGroupLayout(stencilCompPipeline, 0);

        std::array<WGPUBindGroupEntry, 2> stencilEntries;
        stencilEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
        stencilEntries[0].binding     = 0;
        stencilEntries[0].textureView = dsStencilView;

        stencilEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
        stencilEntries[1].binding = 1;
        stencilEntries[1].buffer  = stencilReadbackBuffer;
        stencilEntries[1].offset  = 0;
        stencilEntries[1].size    = kStencilReadbackSize;

        WGPUBindGroupDescriptor stencilBGDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        stencilBGDesc.layout     = stencilCompBGL;
        stencilBGDesc.entryCount = stencilEntries.size();
        stencilBGDesc.entries    = stencilEntries.data();
        WGPUBindGroup stencilBG = t.createBindGroupTracked(stencilBGDesc);
        wgpuBindGroupLayoutRelease(stencilCompBGL);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
        wgpuComputePassEncoderSetPipeline(cp, stencilCompPipeline);
        wgpuComputePassEncoderSetBindGroup(cp, 0, stencilBG, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
        wgpuComputePassEncoderEnd(cp);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // ------------------------------------------------------------------
    // 11. Verify depth (4 f32): covered → kDepthWriteValue (0.0), else kDepthClearValue (1.0).
    //     Verify stencil (4 u32): covered → kStencilReferenceValue (255), else kStencilClearValue (0).
    //     covered = (rasterizationMask & sampleMask & fragmentShaderOutputMask & (1u<<i)) != 0.
    // ------------------------------------------------------------------
    std::array<float, 4> expectedDepth = {};
    std::array<uint32_t, 4> expectedStencil = {};
    for (uint32_t i = 0; i < kSampleCount; ++i) {
        const bool covered =
            (rasterizationMask & sampleMask & fragmentShaderOutputMask & (1u << i)) != 0u;
        expectedDepth[i]   = covered ? kDepthWriteValue : kDepthClearValue;
        expectedStencil[i] = covered ? kStencilReferenceValue : kStencilClearValue;
    }

    // Use expectGPUBufferValuesPassCheck for depth (small tolerance for f32 precision).
    t.expectGPUBufferValuesPassCheck(
        depthReadbackBuffer,
        [expectedDepth](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < kSampleCount * sizeof(float)) {
                return std::string("depth readback buffer too small");
            }
            for (uint32_t i = 0; i < kSampleCount; ++i) {
                float got = 0.0f;
                std::memcpy(&got, actual + i * sizeof(float), sizeof(float));
                float exp = expectedDepth[i];
                float diff = got - exp;
                if (diff < 0.0f) diff = -diff;
                if (diff > 1e-5f) {
                    std::ostringstream msg;
                    msg << "depth sample " << i << ": got " << got << ", expected " << exp;
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        kDepthReadbackSize);

    // Stencil: exact u32 compare.
    t.expectGPUBufferValuesEqual(
        stencilReadbackBuffer,
        expectedStencil.data(),
        expectedStencil.size() * sizeof(uint32_t));
}

// -----------------------------------------------------------------------------
// 6 literal test cases (spec §Cases table)
// -----------------------------------------------------------------------------

// all_full: all masks = 0b1111 → all 4 samples covered (R, G, B, Y)
CTS_TEST(g, "all_full")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runSampleMaskTest(t, 0b1111u, 0b1111u, 0b1111u);
    });

// raster_subset: rasterizationMask=0b0011 → only samples 0,1 drawn (R, G, _, _)
CTS_TEST(g, "raster_subset")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runSampleMaskTest(t, 0b0011u, 0b1111u, 0b1111u);
    });

// sample_mask_subset: sampleMask=0b0101 → pipeline passes samples 0,2 (R, _, B, _)
CTS_TEST(g, "sample_mask_subset")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runSampleMaskTest(t, 0b1111u, 0b0101u, 0b1111u);
    });

// frag_mask_subset: fragmentShaderOutputMask=0b1100 → fragment passes samples 2,3 (_, _, B, Y)
CTS_TEST(g, "frag_mask_subset")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runSampleMaskTest(t, 0b1111u, 0b1111u, 0b1100u);
    });

// and_of_all: AND of all three masks → only sample 2 survives (_, _, B, _)
CTS_TEST(g, "and_of_all")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runSampleMaskTest(t, 0b1110u, 0b0110u, 0b1100u);
    });

// none: sampleMask=0 → nothing passes (_, _, _, _)
CTS_TEST(g, "none")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runSampleMaskTest(t, 0b1111u, 0b0000u, 0b1111u);
    });

} // namespace
