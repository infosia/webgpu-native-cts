// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/pipeline_output_targets.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports color,component_count for r8unorm/rg8unorm/rgba8unorm; format matrix + color,attachments + blend deferred.

#include <array>
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

} // namespace
