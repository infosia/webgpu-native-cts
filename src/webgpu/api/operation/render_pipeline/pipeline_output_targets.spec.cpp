// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/pipeline_output_targets.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports color,component_count for r8unorm/rg8unorm/rgba8unorm; color,attachments (attachmentCount=2, emptyAttachmentId∈{0,1}).
// format matrix + blend deferred.

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,pipeline_output_targets",
    "Render pipeline output targets tests (fragment output component count vs target format).");

// Fullscreen triangle vertex shader (matches upstream kVertexShader).
constexpr std::string_view kVertexShader = R"(
@vertex fn main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 3>(vec2(-1.0, -3.0), vec2(3.0, 1.0), vec2(-1.0, 1.0));
  return vec4(pos[i], 0.0, 1.0);
}
)";

// Fragment shader source for a given componentCount N (outputs values [0, 1, 0, 1][0..N]).
//   N=1: f32       → 0.0
//   N=2: vec2<f32> → (0.0, 1.0)
//   N=3: vec3<f32> → (0.0, 1.0, 0.0)
//   N=4: vec4<f32> → (0.0, 1.0, 0.0, 1.0)
std::string makeFragmentShader(int componentCount) {
    switch (componentCount) {
        case 1:
            return "@fragment fn main() -> @location(0) f32 { return 0.0; }\n";
        case 2:
            return "@fragment fn main() -> @location(0) vec2<f32> { return vec2<f32>(0.0, 1.0); }\n";
        case 3:
            return "@fragment fn main() -> @location(0) vec3<f32> { return vec3<f32>(0.0, 1.0, 0.0); }\n";
        case 4:
            return "@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }\n";
        default:
            std::abort();
    }
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Per-format metadata: WGPUTextureFormat enum, component count K, and bytes per pixel Kbytes.
struct FormatInfo {
    WGPUTextureFormat format;
    uint32_t          componentCount; // K: number of components stored by the format
    uint32_t          bytesPerPixel;  // Kbytes: bytes per pixel
};

FormatInfo getFormatInfo(std::string_view name) {
    if (name == "r8unorm")    { return {WGPUTextureFormat_R8Unorm,    1, 1}; }
    if (name == "rg8unorm")   { return {WGPUTextureFormat_RG8Unorm,   2, 2}; }
    if (name == "rgba8unorm") { return {WGPUTextureFormat_RGBA8Unorm, 4, 4}; }
    std::abort();
}

// Expected unorm bytes for [0, 1, 0, 1][0..K] after rendering:
//   r8unorm    (K=1): {0}
//   rg8unorm   (K=2): {0, 255}
//   rgba8unorm (K=4): {0, 255, 0, 255}
constexpr std::array<uint8_t, 4> kExpectedValues = {0, 255, 0, 255};

// Minimum componentCount for each format (filter: only valid componentCount >= format's K).
int minComponentCount(std::string_view formatName) {
    if (formatName == "r8unorm")    { return 1; }
    if (formatName == "rg8unorm")   { return 2; }
    if (formatName == "rgba8unorm") { return 4; }
    return 1;
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void runColorComponentCountTest(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string formatName     = t.param<std::string>("format");
    const int         componentCount = t.param<int>("componentCount");

    const FormatInfo fmtInfo = getFormatInfo(formatName);

    // Build the fragment shader for this componentCount.
    const std::string fragSrc = makeFragmentShader(componentCount);

    // Create the 1×1 render target.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = fmtInfo.format;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture renderTarget = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(renderTarget, viewDesc);

    // Build the render pipeline (layout:auto → pipeDesc.layout left null by INIT).
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(fragSrc);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = fmtInfo.format;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = stringView("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto — pipeDesc.layout is null after INIT, which selects automatic layout.
    pipeDesc.vertex.module     = vertModule;
    pipeDesc.vertex.entryPoint = stringView("main");
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count  = 1;
    pipeDesc.fragment           = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // Render pass: clearValue {1, 0, 0, 1}, loadOp:Clear, storeOp:Store.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount   = 1;
    passDesc.colorAttachments       = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Readback: bytesPerRow = align(Kbytes, 256) = 256 for all three formats (1, 2, 4 < 256).
    constexpr uint32_t kBytesPerRow = 256;
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kBytesPerRow; // 1 row, 256 bytes
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    t.copyTextureToBuffer(encoder, renderTarget, readback, kBytesPerRow, WGPUExtent3D{1, 1, 1});
    submit(t, encoder);

    // Verify: check the first Kbytes bytes equal expected[0..K].
    const uint32_t kbytes = fmtInfo.bytesPerPixel;
    t.expectGPUBufferValuesPassCheck(
        readback,
        [kbytes](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < static_cast<size_t>(kbytes)) {
                std::ostringstream msg;
                msg << "readback buffer too small: " << len << " < " << kbytes;
                return msg.str();
            }
            for (uint32_t ch = 0; ch < kbytes; ++ch) {
                const uint8_t got      = actual[ch];
                const uint8_t expected = kExpectedValues[ch];
                if (got != expected) {
                    std::ostringstream msg;
                    msg << "component_count mismatch at byte " << ch
                        << ": expected " << static_cast<int>(expected)
                        << ", got "      << static_cast<int>(got);
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(kBytesPerRow));
}

// attachmentsFloatWriteValues: per-attachment RGBA f32 values (matches upstream).
//   index 0: {0.12, 0.34, 0.56, 0.0}  → rgba8unorm quantized ≈ {31, 87, 143, 0}
//   index 1: {0.78, 0.90, 0.19, 1.0}  → rgba8unorm quantized ≈ {199, 230, 48, 255}
struct AttachmentWriteValues {
    float r, g, b, a;
};
constexpr std::array<AttachmentWriteValues, 2> kAttachmentsFloatWriteValues = {{
    {0.12f, 0.34f, 0.56f, 0.0f},
    {0.78f, 0.90f, 0.19f, 1.0f},
}};

// Fragment shader for emptyAttachmentId=0: outputs only @location(1) with attachmentsFloatWriteValues[1].
constexpr std::string_view kFragShaderEmpty0 = R"(
struct FragOut {
  @location(1) color1 : vec4<f32>,
}
@fragment fn main() -> FragOut {
  var out : FragOut;
  out.color1 = vec4<f32>(0.78, 0.9, 0.19, 1.0);
  return out;
}
)";

// Fragment shader for emptyAttachmentId=1: outputs only @location(0) with attachmentsFloatWriteValues[0].
constexpr std::string_view kFragShaderEmpty1 = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.12, 0.34, 0.56, 0.0);
}
)";

void runColorAttachmentsTest(AllFeaturesMaxLimitsGpuTest& t) {
    const int emptyAttachmentId = t.param<int>("emptyAttachmentId");
    constexpr int kAttachmentCount = 2;
    const int realSlot = 1 - emptyAttachmentId; // the non-empty slot (0 or 1)

    // Fragment shader selected per emptyAttachmentId.
    const std::string_view fragSrc = (emptyAttachmentId == 0) ? kFragShaderEmpty0 : kFragShaderEmpty1;

    // Create the 1×1 rgba8unorm render target for the non-empty slot.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture renderTarget = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView realView = t.createViewTracked(renderTarget, viewDesc);

    // Build shaders.
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(fragSrc);

    // Pipeline targets: 2 entries. The empty slot has format=Undefined; the real slot has rgba8unorm.
    WGPUColorTargetState targets[kAttachmentCount];
    for (int i = 0; i < kAttachmentCount; ++i) {
        targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
        if (i == emptyAttachmentId) {
            targets[i].format = WGPUTextureFormat_Undefined;
        } else {
            targets[i].format = WGPUTextureFormat_RGBA8Unorm;
        }
    }

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = stringView("main");
    fragment.targetCount = static_cast<size_t>(kAttachmentCount);
    fragment.targets     = targets;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.vertex.module     = vertModule;
    pipeDesc.vertex.entryPoint = stringView("main");
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count  = 1;
    pipeDesc.fragment           = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // Render pass: 2 color attachments. The empty slot has view=nullptr; the real slot is the target.
    WGPURenderPassColorAttachment colorAttachments[kAttachmentCount];
    for (int i = 0; i < kAttachmentCount; ++i) {
        colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        if (i == emptyAttachmentId) {
            colorAttachments[i].view = nullptr;
        } else {
            colorAttachments[i].view       = realView;
            colorAttachments[i].loadOp     = WGPULoadOp_Clear;
            colorAttachments[i].storeOp    = WGPUStoreOp_Store;
            colorAttachments[i].clearValue = WGPUColor{0.5, 0.5, 0.5, 0.5};
        }
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount   = static_cast<size_t>(kAttachmentCount);
    passDesc.colorAttachments       = colorAttachments;
    passDesc.depthStencilAttachment = nullptr;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Readback: bytesPerRow = 256, size 1×1×1; check bytes [0..3].
    constexpr uint32_t kBytesPerRow = 256;
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kBytesPerRow;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    t.copyTextureToBuffer(encoder, renderTarget, readback, kBytesPerRow, WGPUExtent3D{1, 1, 1});
    submit(t, encoder);

    // Expected: attachmentsFloatWriteValues[realSlot] quantized round(clamp(c,0,1)*255), ±1 tolerance.
    const AttachmentWriteValues& wv = kAttachmentsFloatWriteValues[static_cast<size_t>(realSlot)];
    const std::array<uint8_t, 4> expected = {
        static_cast<uint8_t>(std::lround(std::min(std::max(wv.r, 0.0f), 1.0f) * 255.0f)),
        static_cast<uint8_t>(std::lround(std::min(std::max(wv.g, 0.0f), 1.0f) * 255.0f)),
        static_cast<uint8_t>(std::lround(std::min(std::max(wv.b, 0.0f), 1.0f) * 255.0f)),
        static_cast<uint8_t>(std::lround(std::min(std::max(wv.a, 0.0f), 1.0f) * 255.0f)),
    };

    t.expectGPUBufferValuesPassCheck(
        readback,
        [expected](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                std::ostringstream msg;
                msg << "readback buffer too small: " << len << " < 4";
                return msg.str();
            }
            for (uint32_t ch = 0; ch < 4; ++ch) {
                const int got = static_cast<int>(actual[ch]);
                const int exp = static_cast<int>(expected[ch]);
                if (std::abs(got - exp) > 1) {
                    std::ostringstream msg;
                    msg << "attachments mismatch at byte " << ch
                        << ": expected " << exp << " (±1)"
                        << ", got "      << got;
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(kBytesPerRow));
}

// color,attachments:
//   format=rgba8unorm (fixed), attachmentCount=2 (fixed), emptyAttachmentId ∈ {0, 1} = 2 cases.
CTS_TEST(g, "color,attachments")
    .params([](ParamsBuilder u) {
        return u.combine("emptyAttachmentId", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runColorAttachmentsTest(t);
    });

// color,component_count:
//   format is a case-level param (3 cases); componentCount is a subcase (filtered per format).
//   Only valid combinations (componentCount >= format's K) are kept:
//     r8unorm    (K=1): {1, 2, 3, 4} — 4 subcases
//     rg8unorm   (K=2): {2, 3, 4}   — 3 subcases
//     rgba8unorm (K=4): {4}          — 1 subcase
//   Total: 3 cases, 8 subcases.
CTS_TEST(g, "color,component_count")
    .params([](ParamsBuilder u) {
        return u.combine("format", {"r8unorm", "rg8unorm", "rgba8unorm"})
            .beginSubcases()
            .combine("componentCount", {1, 2, 3, 4})
            .filter([](const ParamRecord& params) {
                const std::string fmt    = valueAs<std::string>(*findParam(params, "format"));
                const int         compCt = static_cast<int>(valueAs<int64_t>(*findParam(params, "componentCount")));
                return compCt >= minComponentCount(fmt);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runColorComponentCountTest(t);
    });

// Fragment shader for color,component_count,blend: always outputs vec4f(0.0, 1.0, 0.0, 0.498).
// (Same for all 5 subcases — only blend factors differ.)
constexpr std::string_view kBlendFragShader = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.0, 1.0, 0.0, 0.498);
}
)";

// Blend subcase parameters for color,component_count,blend (rgba8unorm, output=[0,1,0,0.498]).
// Format = rgba8unorm (fixed).  Load value = {1.0, 0.0, 0.0, 1.0}.
// Upstream table (fragment output has alpha — the 5 length-4 entries):
//   0: colorSrc=one,              colorDst=one-minus-src-alpha, alphaSrc=one,  alphaDst=zero     → {0.502,1,0,0.498}
//   1: colorSrc=src-alpha,        colorDst=one-minus-src-alpha, alphaSrc=one,  alphaDst=zero     → {0.502,0.498,0,0.498}
//   2: colorSrc=dst-alpha,        colorDst=zero,               alphaSrc=one,  alphaDst=zero     → {0,1,0,0.498}
//   3: colorSrc=dst-alpha,        colorDst=zero,               alphaSrc=zero, alphaDst=src      → {0,1,0,0.498}
//   4: colorSrc=one-minus-dst-alpha, colorDst=dst-alpha,       alphaSrc=zero, alphaDst=dst-alpha→ {1,0,0,1}
// Quantized to rgba8unorm (round(c*255), ±1):
//   0→{128,255,0,127}, 1→{128,127,0,127}, 2→{0,255,0,127}, 3→{0,255,0,127}, 4→{255,0,0,255}
struct BlendSubcase {
    WGPUBlendFactor   colorSrcFactor;
    WGPUBlendFactor   colorDstFactor;
    WGPUBlendFactor   alphaSrcFactor;
    WGPUBlendFactor   alphaDstFactor;
    std::array<uint8_t, 4> expected; // rgba8unorm quantized result
};

// NOLINTNEXTLINE(cert-err58-cpp)
static const std::array<BlendSubcase, 5> kBlendSubcases = {{
    // 0: one / one-minus-src-alpha, one / zero → {0.502, 1, 0, 0.498} → {128, 255, 0, 127}
    { WGPUBlendFactor_One,             WGPUBlendFactor_OneMinusSrcAlpha, WGPUBlendFactor_One,  WGPUBlendFactor_Zero,     {128, 255, 0, 127} },
    // 1: src-alpha / one-minus-src-alpha, one / zero → {0.502, 0.498, 0, 0.498} → {128, 127, 0, 127}
    { WGPUBlendFactor_SrcAlpha,        WGPUBlendFactor_OneMinusSrcAlpha, WGPUBlendFactor_One,  WGPUBlendFactor_Zero,     {128, 127, 0, 127} },
    // 2: dst-alpha / zero, one / zero → {0, 1, 0, 0.498} → {0, 255, 0, 127}
    { WGPUBlendFactor_DstAlpha,        WGPUBlendFactor_Zero,             WGPUBlendFactor_One,  WGPUBlendFactor_Zero,     {  0, 255, 0, 127} },
    // 3: dst-alpha / zero, zero / src → {0, 1, 0, 0.498} → {0, 255, 0, 127}
    { WGPUBlendFactor_DstAlpha,        WGPUBlendFactor_Zero,             WGPUBlendFactor_Zero, WGPUBlendFactor_Src,      {  0, 255, 0, 127} },
    // 4: one-minus-dst-alpha / dst-alpha, zero / dst-alpha → {1, 0, 0, 1} → {255, 0, 0, 255}
    { WGPUBlendFactor_OneMinusDstAlpha, WGPUBlendFactor_DstAlpha,        WGPUBlendFactor_Zero, WGPUBlendFactor_DstAlpha, {255,   0, 0, 255} },
}};

void runColorComponentCountBlendTest(AllFeaturesMaxLimitsGpuTest& t) {
    const int subcaseIdx = t.param<int>("subcaseIdx");

    const BlendSubcase& sc = kBlendSubcases[static_cast<size_t>(subcaseIdx)];

    // Create the 1×1 rgba8unorm render target.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture renderTarget = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(renderTarget, viewDesc);

    // Build shaders: vertex = kVertexShader, fragment = kBlendFragShader (same for all subcases).
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kBlendFragShader);

    // Blend state — kept in a local variable so the pointer in WGPUColorTargetState stays valid
    // throughout pipeline creation (must not dangle).
    WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.color.srcFactor = sc.colorSrcFactor;
    blendState.color.dstFactor = sc.colorDstFactor;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = sc.alphaSrcFactor;
    blendState.alpha.dstFactor = sc.alphaDstFactor;

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.blend  = &blendState;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = stringView("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto — pipeDesc.layout is null after INIT.
    pipeDesc.vertex.module      = vertModule;
    pipeDesc.vertex.entryPoint  = stringView("main");
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count  = 1;
    pipeDesc.fragment           = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // Render pass: load value {1,0,0,1}, loadOp:Clear, storeOp:Store.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount   = 1;
    passDesc.colorAttachments       = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Readback: bytesPerRow = 256, size 1×1×1.
    constexpr uint32_t kBytesPerRow = 256;
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kBytesPerRow;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    t.copyTextureToBuffer(encoder, renderTarget, readback, kBytesPerRow, WGPUExtent3D{1, 1, 1});
    submit(t, encoder);

    // Verify: check 4 RGBA bytes with ±1 tolerance.
    const std::array<uint8_t, 4> expected = sc.expected;
    t.expectGPUBufferValuesPassCheck(
        readback,
        [expected, subcaseIdx](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                std::ostringstream msg;
                msg << "readback buffer too small: " << len << " < 4";
                return msg.str();
            }
            for (uint32_t ch = 0; ch < 4; ++ch) {
                const int got = static_cast<int>(actual[ch]);
                const int exp = static_cast<int>(expected[ch]);
                if (std::abs(got - exp) > 1) {
                    std::ostringstream msg;
                    msg << "blend mismatch (subcase " << subcaseIdx << ") at byte " << ch
                        << ": expected " << exp << " (±1)"
                        << ", got "      << got;
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(kBytesPerRow));
}

// color,component_count,blend:
//   format=rgba8unorm (fixed), fragment output=vec4f(0,1,0,0.498), load={1,0,0,1}.
//   5 subcases covering the has-alpha blend factor combinations (upstream length-4 output entries).
CTS_TEST(g, "color,component_count,blend")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("subcaseIdx", {0, 1, 2, 3, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runColorComponentCountBlendTest(t);
    });

} // namespace
