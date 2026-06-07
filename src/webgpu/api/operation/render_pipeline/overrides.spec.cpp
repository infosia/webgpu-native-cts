// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/overrides.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports basic for isAsync=false; async/precision/shared_shader_module/multi_entry_points deferred.

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,overrides",
    "Render pipeline pipeline-overridable constants tests.");

// Clear color for render attachment (RGBA).
constexpr std::array<double, 4> kClearValueResult = {0.2, 0.4, 0.6, 0.8};

// Default fragment output when no fragment override is applied (RGBA = white).
constexpr std::array<double, 4> kDefaultValueResult = {1.0, 1.0, 1.0, 1.0};

// Vertex shader with overridable xright and ytop controlling the fullscreen triangle extents.
// Defaults (3.0, 3.0) produce a triangle that covers the entire 1×1 viewport.
constexpr std::string_view kVertexShader = R"(
override xright: f32 = 3.0;
override ytop: f32 = 3.0;
@vertex fn main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, ytop),
    vec2<f32>(-1.0, -ytop),
    vec2<f32>(xright, 0.0));
  return vec4<f32>(pos[i], 0.0, 1.0);
}
)";

// Fragment shader with overridable R, G, B, A output color components.
// Defaults (1.0, 1.0, 1.0, 1.0) output white.
constexpr std::string_view kFragmentShader = R"(
override R: f32 = 1.0;
override G: f32 = 1.0;
override B: f32 = 1.0;
override A: f32 = 1.0;
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(R, G, B, A);
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Build a WGPUConstantEntry with the given WGSL identifier key and double value.
WGPUConstantEntry makeConstantEntry(std::string_view key, double value) {
    WGPUConstantEntry entry = WGPU_CONSTANT_ENTRY_INIT;
    entry.key   = stringView(key);
    entry.value = value;
    return entry;
}

struct SubcaseParams {
    // Index identifying which subcase to run (0-based, matches combineWithParams row order).
    int subcaseIndex;
};

// Per-subcase data: vertex constants, fragment constants, expected RGBA output.
struct SubcaseData {
    std::vector<std::pair<std::string_view, double>> vertexConstants;
    std::vector<std::pair<std::string_view, double>> fragmentConstants;
    std::array<double, 4> expectedRGBA; // {R, G, B, A}
};

SubcaseData getSubcaseData(int index) {
    switch (index) {
        // Subcase 0: no overrides → defaults → white output.
        case 0:
            return {{}, {}, kDefaultValueResult};
        // Subcase 1: xright=-3.0 collapses triangle → pixel keeps clear color.
        case 1:
            return {{{"xright", -3.0}}, {}, kClearValueResult};
        // Subcase 2: ytop=-3.0 collapses triangle → pixel keeps clear color.
        case 2:
            return {{{"ytop", -3.0}}, {}, kClearValueResult};
        // Subcase 3: xright=4.0, ytop=4.0 → triangle still covers viewport → white output.
        case 3:
            return {{{"xright", 4.0}, {"ytop", 4.0}}, {}, kDefaultValueResult};
        // Subcase 4: R=0.0, B=0.0 → green output {0, 1, 0, 1}.
        case 4:
            return {{}, {{"R", 0.0}, {"B", 0.0}}, {0.0, 1.0, 0.0, 1.0}};
        // Subcase 5: R=G=B=A=0.0 → black transparent output {0, 0, 0, 0}.
        case 5:
            return {{}, {{"R", 0.0}, {"G", 0.0}, {"B", 0.0}, {"A", 0.0}}, {0.0, 0.0, 0.0, 0.0}};
        default:
            std::abort();
    }
}

void runBasicTest(AllFeaturesMaxLimitsGpuTest& t) {
    const int subcaseIndex = t.param<int>("subcaseIndex");
    const SubcaseData sc = getSubcaseData(subcaseIndex);

    // Build WGPUConstantEntry arrays for vertex and fragment stages.
    // These must remain alive until createRenderPipeline returns.
    std::vector<WGPUConstantEntry> vertEntries;
    vertEntries.reserve(sc.vertexConstants.size());
    for (const auto& [key, val] : sc.vertexConstants) {
        vertEntries.push_back(makeConstantEntry(key, val));
    }

    std::vector<WGPUConstantEntry> fragEntries;
    fragEntries.reserve(sc.fragmentConstants.size());
    for (const auto& [key, val] : sc.fragmentConstants) {
        fragEntries.push_back(makeConstantEntry(key, val));
    }

    // Create a 1×1 bgra8unorm render target.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_BGRA8Unorm;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture renderTarget = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(renderTarget, viewDesc);

    // Create shader modules.
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

    // Color target: bgra8unorm.
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;

    // Fragment state with optional constants.
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module        = fragModule;
    fragment.entryPoint    = stringView("main");
    fragment.targetCount   = 1;
    fragment.targets       = &colorTarget;
    fragment.constantCount = fragEntries.size();
    fragment.constants     = fragEntries.empty() ? nullptr : fragEntries.data();

    // Render pipeline: layout:auto, triangle-list, CCW front face, cull back.
    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto — pipeDesc.layout is null after INIT, which selects automatic layout.
    pipeDesc.vertex.module        = vertModule;
    pipeDesc.vertex.entryPoint    = stringView("main");
    pipeDesc.vertex.constantCount = vertEntries.size();
    pipeDesc.vertex.constants     = vertEntries.empty() ? nullptr : vertEntries.data();
    pipeDesc.primitive.topology   = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.primitive.frontFace  = WGPUFrontFace_CCW;
    pipeDesc.primitive.cullMode   = WGPUCullMode_Back;
    pipeDesc.multisample.count    = 1;
    pipeDesc.fragment             = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // Render pass: clear {0.2, 0.4, 0.6, 0.8}, store.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{
        kClearValueResult[0],
        kClearValueResult[1],
        kClearValueResult[2],
        kClearValueResult[3]};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount   = 1;
    passDesc.colorAttachments       = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Readback: 1×1 bgra8unorm → 4 bytes; bytesPerRow = align(4, 256) = 256.
    constexpr uint32_t kBytesPerRow = 256;
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kBytesPerRow;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    t.copyTextureToBuffer(encoder, renderTarget, readback, kBytesPerRow, WGPUExtent3D{1, 1, 1});

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    // bgra8unorm byte layout is [B, G, R, A]; convert expected {R, G, B, A} accordingly.
    const double R = sc.expectedRGBA[0];
    const double G = sc.expectedRGBA[1];
    const double B = sc.expectedRGBA[2];
    const double A = sc.expectedRGBA[3];
    const uint8_t expB = static_cast<uint8_t>(std::round(B * 255.0));
    const uint8_t expG = static_cast<uint8_t>(std::round(G * 255.0));
    const uint8_t expR = static_cast<uint8_t>(std::round(R * 255.0));
    const uint8_t expA = static_cast<uint8_t>(std::round(A * 255.0));
    const std::array<uint8_t, 4> expectedBytes = {expB, expG, expR, expA};

    // Verify with per-channel maxDiff=1 (accounts for unorm rounding of the clear values).
    t.expectGPUBufferValuesPassCheck(
        readback,
        [expectedBytes](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                return "readback buffer too small: " + std::to_string(len) + " < 4";
            }
            for (int ch = 0; ch < 4; ++ch) {
                const int got = static_cast<int>(actual[ch]);
                const int exp = static_cast<int>(expectedBytes[ch]);
                if (std::abs(got - exp) > 1) {
                    static constexpr std::string_view kChannelNames[4] = {"B", "G", "R", "A"};
                    std::ostringstream msg;
                    msg << "bgra8unorm mismatch at channel " << kChannelNames[ch]
                        << " (byte " << ch << ")"
                        << ": expected " << exp << ", got " << got
                        << " (maxDiff=1)";
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(kBytesPerRow));
}

// basic:
//   1 case (isAsync=false is the case-level param); 6 subcases via combineWithParams.
//   Each subcase row encodes a distinct combination of vertex/fragment overrides.
CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false})
            .beginSubcases()
            .combineWithParams({
                // subcaseIndex 0: no overrides → white output
                ParamRecord{{"subcaseIndex", 0}},
                // subcaseIndex 1: xright=-3.0 → triangle collapses → clear color
                ParamRecord{{"subcaseIndex", 1}},
                // subcaseIndex 2: ytop=-3.0 → triangle collapses → clear color
                ParamRecord{{"subcaseIndex", 2}},
                // subcaseIndex 3: xright=4.0, ytop=4.0 → large triangle → white output
                ParamRecord{{"subcaseIndex", 3}},
                // subcaseIndex 4: R=0.0, B=0.0 → green {0,1,0,1}
                ParamRecord{{"subcaseIndex", 4}},
                // subcaseIndex 5: R=G=B=A=0.0 → black transparent {0,0,0,0}
                ParamRecord{{"subcaseIndex", 5}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBasicTest(t);
    });

} // namespace
