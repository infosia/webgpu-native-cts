// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/overrides.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports all 4 upstream tests: basic, precision, shared_shader_module, multi_entry_points.

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texel_data.h"
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
    const bool isAsync = t.param<bool>("isAsync");
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
    WGPURenderPipeline pipeline = t.createRenderPipelineSyncOrAsync(isAsync, pipeDesc);

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
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
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
//   2 cases (isAsync=true/false is the case-level param); 6 subcases each via combineWithParams.
//   Each subcase row encodes a distinct combination of vertex/fragment overrides.
CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false})
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

// ---------------------------------------------------------------------------------------------
// Shared helpers for the tests after `basic` (port of upstream F.expectShaderOutputWithConstants
// and GPUTest.expectSingleColor).
// ---------------------------------------------------------------------------------------------

// A pipeline-constant record: WGSL name -> value.
using ConstantList = std::vector<std::pair<std::string_view, double>>;

// RGBA expectation, as upstream's PerTexelComponent<number> {R, G, B, A}.
using Rgba = std::array<double, 4>;

std::vector<WGPUConstantEntry> toConstantEntries(const ConstantList& constants) {
    std::vector<WGPUConstantEntry> entries;
    entries.reserve(constants.size());
    for (const auto& [key, value] : constants) {
        entries.push_back(makeConstantEntry(key, value));
    }
    return entries;
}

// One programmable stage of the render pipeline (module, entry point, constants).
struct StageDesc {
    WGPUShaderModule module;
    std::string_view entryPoint;
    ConstantList constants;
};

// Creates the layout:auto render pipeline upstream uses (triangle-list, ccw, cull back) with a
// single color target of `format`.
WGPURenderPipeline createOverridesPipeline(AllFeaturesMaxLimitsGpuTest& t,
                                           bool isAsync,
                                           WGPUTextureFormat format,
                                           const StageDesc& vertex,
                                           const StageDesc& fragment) {
    const std::vector<WGPUConstantEntry> vertEntries = toConstantEntries(vertex.constants);
    const std::vector<WGPUConstantEntry> fragEntries = toConstantEntries(fragment.constants);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = format;

    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    fragmentState.module        = fragment.module;
    fragmentState.entryPoint    = stringView(fragment.entryPoint);
    fragmentState.targetCount   = 1;
    fragmentState.targets       = &colorTarget;
    fragmentState.constantCount = fragEntries.size();
    fragmentState.constants     = fragEntries.empty() ? nullptr : fragEntries.data();

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto — desc.layout is null after INIT.
    desc.vertex.module        = vertex.module;
    desc.vertex.entryPoint    = stringView(vertex.entryPoint);
    desc.vertex.constantCount = vertEntries.size();
    desc.vertex.constants     = vertEntries.empty() ? nullptr : vertEntries.data();
    desc.primitive.topology   = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.frontFace  = WGPUFrontFace_CCW;
    desc.primitive.cullMode   = WGPUCullMode_Back;
    desc.multisample.count    = 1;
    desc.fragment             = &fragmentState;
    return t.createRenderPipelineSyncOrAsync(isAsync, desc);
}

// 1x1 render target of `format` (RenderAttachment | CopySrc).
WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size          = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount   = 1;
    desc.dimension     = WGPUTextureDimension_2D;
    desc.format        = format;
    desc.usage         = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// Encodes a render pass that clears `target` to kClearValueResult and draws 3 vertices.
void encodeDrawPass(AllFeaturesMaxLimitsGpuTest& t,
                    WGPUCommandEncoder encoder,
                    WGPUTexture target,
                    WGPURenderPipeline pipeline) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(target, viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{
        kClearValueResult[0], kClearValueResult[1], kClearValueResult[2], kClearValueResult[3]};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttachment;

    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
}

// Port of upstream expectSingleColor for a 1x1 2d texture: copies `src` into a zeroed buffer and
// expects the texel bytes to equal exactly rep.pack(rep.encode(exp)) for `format`.
void expectSingleColor(AllFeaturesMaxLimitsGpuTest& t,
                       WGPUTexture src,
                       WGPUTextureFormat format,
                       const Rgba& exp) {
    const TexelRepresentation& rep = texelRepresentation(format);
    TexelComponents components;
    components.values = exp;
    const std::vector<uint8_t> expectedTexel = rep.packBits(rep.numberToBits(components));

    constexpr uint32_t kBytesPerRow = 256;
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = kBytesPerRow;
    bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, src, buffer, kBytesPerRow, WGPUExtent3D{1, 1, 1});
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    t.expectGPUBufferValuesEqual(buffer, expectedTexel.data(), expectedTexel.size());
}

// Port of upstream F.expectShaderOutputWithConstants.
void expectShaderOutputWithConstants(AllFeaturesMaxLimitsGpuTest& t,
                                     bool isAsync,
                                     WGPUTextureFormat format,
                                     const Rgba& expected,
                                     const StageDesc& vertex,
                                     const StageDesc& fragment) {
    WGPUTexture renderTarget = createRenderTarget(t, format);
    WGPURenderPipeline pipeline = createOverridesPipeline(t, isAsync, format, vertex, fragment);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    encodeDrawPass(t, encoder, renderTarget, pipeline);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    expectSingleColor(t, renderTarget, format, expected);
}

// precision subcases (upstream combineWithParams row order).
struct PrecisionSubcase {
    Rgba expected;
    ConstantList vertexConstants;
    ConstantList fragmentConstants;
};

PrecisionSubcase getPrecisionSubcase(int index) {
    switch (index) {
        case 0:
            return {{3.14159, 1.0, 1.0, 1.0}, {}, {{"R", 3.14159}}};
        case 1:
            return {{3.141592653589793, 1.0, 1.0, 1.0}, {}, {{"R", 3.141592653589793}}};
        default:
            std::abort();
    }
}

CTS_TEST(g, "precision")
    .desc("Test that the float number precision is preserved for constants")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false})
            .beginSubcases()
            .combineWithParams({
                // expected R=3.14159, fragmentConstants {R: 3.14159}
                ParamRecord{{"subcaseIndex", 0}},
                // expected R=3.141592653589793, fragmentConstants {R: 3.141592653589793}
                ParamRecord{{"subcaseIndex", 1}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const PrecisionSubcase sc = getPrecisionSubcase(t.param<int>("subcaseIndex"));
        constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA32Float;
        expectShaderOutputWithConstants(
            t, isAsync, kFormat, sc.expected,
            StageDesc{t.createShaderModuleTracked(kVertexShader), "main", sc.vertexConstants},
            StageDesc{t.createShaderModuleTracked(kFragmentShader), "main", sc.fragmentConstants});
    });

// shared_shader_module subcases (upstream combineWithParams row order).
struct SharedModuleSubcase {
    Rgba expected0;
    ConstantList vertexConstants0;
    ConstantList fragmentConstants0;
    Rgba expected1;
    ConstantList vertexConstants1;
    ConstantList fragmentConstants1;
};

SharedModuleSubcase getSharedModuleSubcase(int index) {
    switch (index) {
        case 0:
            return {kClearValueResult, {{"xright", -3.0}}, {},
                    kDefaultValueResult, {}, {}};
        case 1:
            return {{0.0, 0.0, 0.0, 0.0}, {}, {{"R", 0.0}, {"G", 0.0}, {"B", 0.0}, {"A", 0.0}},
                    kDefaultValueResult, {}, {}};
        case 2:
            return {{1.0, 0.0, 1.0, 0.0}, {}, {{"R", 1.0}, {"G", 0.0}, {"B", 1.0}, {"A", 0.0}},
                    {0.0, 1.0, 0.0, 1.0}, {}, {{"R", 0.0}, {"G", 1.0}, {"B", 0.0}, {"A", 1.0}}};
        default:
            std::abort();
    }
}

CTS_TEST(g, "shared_shader_module")
    .desc("Test that when the same module is shared by different pipelines, the constant values are "
          "still being used correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false})
            .beginSubcases()
            .combineWithParams({
                // pipeline0: xright=-3 -> clear color; pipeline1: defaults -> white
                ParamRecord{{"subcaseIndex", 0}},
                // pipeline0: RGBA=0 -> {0,0,0,0}; pipeline1: defaults -> white
                ParamRecord{{"subcaseIndex", 1}},
                // pipeline0: {1,0,1,0}; pipeline1: {0,1,0,1}
                ParamRecord{{"subcaseIndex", 2}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const SharedModuleSubcase sc = getSharedModuleSubcase(t.param<int>("subcaseIndex"));
        constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_BGRA8Unorm;

        WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

        WGPURenderPipeline pipeline0 = createOverridesPipeline(
            t, isAsync, kFormat,
            StageDesc{vertexModule, "main", sc.vertexConstants0},
            StageDesc{fragmentModule, "main", sc.fragmentConstants0});
        WGPURenderPipeline pipeline1 = createOverridesPipeline(
            t, isAsync, kFormat,
            StageDesc{vertexModule, "main", sc.vertexConstants1},
            StageDesc{fragmentModule, "main", sc.fragmentConstants1});

        WGPUTexture renderTarget0 = createRenderTarget(t, kFormat);
        WGPUTexture renderTarget1 = createRenderTarget(t, kFormat);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        encodeDrawPass(t, encoder, renderTarget0, pipeline0);
        encodeDrawPass(t, encoder, renderTarget1, pipeline1);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        expectSingleColor(t, renderTarget0, kFormat, sc.expected0);
        expectSingleColor(t, renderTarget1, kFormat, sc.expected1);
    });

// multi_entry_points subcases (upstream combineWithParams row order).
struct MultiEntrySubcase {
    Rgba expected;
    ConstantList vertexConstants;
    ConstantList fragmentConstants;
};

MultiEntrySubcase getMultiEntrySubcase(int index) {
    const ConstantList fragmentConstants = {{"A", 0.8}, {"B", 0.4}, {"C", 0.2}, {"D", 1.0}};
    switch (index) {
        case 0:
            return {{0.8, 0.4, 0.2, 1.0}, {{"A", 4.0}, {"B", 4.0}}, fragmentConstants};
        case 1:
            return {{0.8, 0.4, 0.2, 1.0}, {}, fragmentConstants};
        case 2:
            return {kClearValueResult, {{"A", -3.0}}, fragmentConstants};
        default:
            std::abort();
    }
}

constexpr std::string_view kMultiEntryPointsShader = R"(
      override A: f32 = 3.0;
      override B: f32 = 3.0;
      override C: f32;
      override D: f32;

      @vertex fn vertexMain(
          @builtin(vertex_index) VertexIndex : u32
          ) -> @builtin(position) vec4<f32> {
          var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
              vec2<f32>(-1.0,  A),
              vec2<f32>(-1.0,  -A),
              vec2<f32>(B, 0.0));
          return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
      }

      @fragment fn fragmentMain()
          -> @location(0) vec4<f32> {
          return vec4<f32>(A, B, C, D);
      }
      )";

CTS_TEST(g, "multi_entry_points")
    .desc("Test that when the same module is shared by vertex and fragment shader, the constant values "
          "are still being used correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false})
            .beginSubcases()
            .combineWithParams({
                // vertex {A:4, B:4}, fragment {A:.8, B:.4, C:.2, D:1} -> {.8,.4,.2,1}
                ParamRecord{{"subcaseIndex", 0}},
                // vertex {}, same fragment constants -> {.8,.4,.2,1}
                ParamRecord{{"subcaseIndex", 1}},
                // vertex {A:-3} -> triangle collapses -> clear color
                ParamRecord{{"subcaseIndex", 2}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const MultiEntrySubcase sc = getMultiEntrySubcase(t.param<int>("subcaseIndex"));
        constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_BGRA8Unorm;
        WGPUShaderModule module = t.createShaderModuleTracked(kMultiEntryPointsShader);
        expectShaderOutputWithConstants(t, isAsync, kFormat, sc.expected,
                                        StageDesc{module, "vertexMain", sc.vertexConstants},
                                        StageDesc{module, "fragmentMain", sc.fragmentConstants});
    });

} // namespace
