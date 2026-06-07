// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/culling_tests.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports the culling test for depthStencilFormat=null; depth/stencil variants deferred.

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
    "api,operation,render_pipeline,culling_tests",
    "Render pipeline culling (frontFace x cullMode x topology) tests.");

constexpr uint32_t kRenderTargetSize = 4;
constexpr uint32_t kBytesPerPixel = 4;
constexpr WGPUTextureFormat kRenderTargetFormat = WGPUTextureFormat_RGBA8Unorm;

// Vertex shader for triangle-list topology:
//   tri 0 (verts 0-2): (-1,1),(-1,0),(0,1)  — top-left corner, CCW winding
//   tri 1 (verts 3-5): (0,-1),(1,0),(1,-1)  — bottom-right corner, CW winding
constexpr std::string_view kVertexShaderTriangleList = R"(
@vertex fn main(@builtin(vertex_index) vi : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 6>(
    vec2<f32>(-1.0,  1.0),
    vec2<f32>(-1.0,  0.0),
    vec2<f32>( 0.0,  1.0),
    vec2<f32>( 0.0, -1.0),
    vec2<f32>( 1.0,  0.0),
    vec2<f32>( 1.0, -1.0)
  );
  return vec4<f32>(pos[vi], 0.0, 1.0);
}
)";

// Vertex shader for triangle-strip topology:
//   6 verts: (0,2),(-2,0),(0,0),(0,0),(0,-2),(2,0)
//   strips: 012 (CCW top-left), 213 (zero-area), 234 (zero-area), 345 (CW bottom-right)
constexpr std::string_view kVertexShaderTriangleStrip = R"(
@vertex fn main(@builtin(vertex_index) vi : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 6>(
    vec2<f32>( 0.0,  2.0),
    vec2<f32>(-2.0,  0.0),
    vec2<f32>( 0.0,  0.0),
    vec2<f32>( 0.0,  0.0),
    vec2<f32>( 0.0, -2.0),
    vec2<f32>( 2.0,  0.0)
  );
  return vec4<f32>(pos[vi], 0.0, 1.0);
}
)";

// Fragment shader: green if front-facing, red if back-facing.
// select(a, b, cond) returns b if cond is true.
constexpr std::string_view kFragmentShader = R"(
@fragment fn main(@builtin(front_facing) ff : bool) -> @location(0) vec4<f32> {
  return select(vec4<f32>(1.0, 0.0, 0.0, 1.0), vec4<f32>(0.0, 1.0, 0.0, 1.0), ff);
}
)";

// clear color = blue (RGBA bytes: 0, 0, 255, 255)
constexpr std::array<uint8_t, 4> kBlue  = {0,   0,   255, 255};
constexpr std::array<uint8_t, 4> kGreen = {0,   255, 0,   255};
constexpr std::array<uint8_t, 4> kRed   = {255, 0,   0,   255};

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Port of upstream faceIsCulled(face, frontFace, cullMode):
//   culled iff cullMode != none && ((frontFace == face) == (cullMode == front))
bool faceIsCulled(std::string_view face, std::string_view frontFace, std::string_view cullMode) {
    if (cullMode == "none") {
        return false;
    }
    return (frontFace == face) == (cullMode == "front");
}

// Port of upstream faceColor(face, frontFace, cullMode):
//   if culled      → blue  (clear color; face was not drawn)
//   if not culled && face == frontFace → green (front-facing fragment)
//   if not culled && face != frontFace → red   (back-facing fragment)
std::array<uint8_t, 4> faceColor(
    std::string_view face,
    std::string_view frontFace,
    std::string_view cullMode) {
    if (faceIsCulled(face, frontFace, cullMode)) {
        return kBlue;
    }
    if (face == frontFace) {
        return kGreen;
    }
    return kRed;
}

WGPUFrontFace toFrontFace(std::string_view s) {
    if (s == "ccw") { return WGPUFrontFace_CCW; }
    if (s == "cw")  { return WGPUFrontFace_CW; }
    std::abort();
}

WGPUCullMode toCullMode(std::string_view s) {
    if (s == "none")  { return WGPUCullMode_None; }
    if (s == "front") { return WGPUCullMode_Front; }
    if (s == "back")  { return WGPUCullMode_Back; }
    std::abort();
}

WGPUPrimitiveTopology toTopology(std::string_view s) {
    if (s == "triangle-list")  { return WGPUPrimitiveTopology_TriangleList; }
    if (s == "triangle-strip") { return WGPUPrimitiveTopology_TriangleStrip; }
    std::abort();
}

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kRenderTargetSize, kRenderTargetSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = kRenderTargetFormat;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPURenderPipeline createCullingPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view topology,
    std::string_view frontFace,
    std::string_view cullMode) {
    const std::string_view vertSrc = (topology == "triangle-strip")
        ? kVertexShaderTriangleStrip
        : kVertexShaderTriangleList;

    WGPUShaderModule vertexModule  = t.createShaderModuleTracked(vertSrc);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = kRenderTargetFormat;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology  = toTopology(topology);
    desc.primitive.frontFace = toFrontFace(frontFace);
    desc.primitive.cullMode  = toCullMode(cullMode);
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    // No depth/stencil attachment (depthStencilFormat = null).
    return t.createRenderPipelineTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void verifyCullingResult(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    std::string_view frontFace,
    std::string_view cullMode) {
    // bytesPerRow = align(4 * 4, 256) = 256
    const uint32_t bytesPerRow = static_cast<uint32_t>(
        alignTo(kRenderTargetSize * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kRenderTargetSize - 1)
            + static_cast<uint64_t>(kRenderTargetSize) * kBytesPerPixel,
        kBufferCopyAlignment);

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(
        encoder,
        texture,
        readback,
        bytesPerRow,
        WGPUExtent3D{kRenderTargetSize, kRenderTargetSize, 1});
    submit(t, encoder);

    // Two corner pixels to verify:
    //   (0,0) — CCW triangle (top-left corner)
    //   (3,3) — CW  triangle (bottom-right corner)
    const std::array<uint8_t, 4> expectedCCW = faceColor("ccw", frontFace, cullMode);
    const std::array<uint8_t, 4> expectedCW  = faceColor("cw",  frontFace, cullMode);

    struct CornerCheck {
        uint32_t col;
        uint32_t row;
        std::array<uint8_t, 4> expected;
    };
    const std::array<CornerCheck, 2> checks = {{
        {0u, 0u, expectedCCW},
        {3u, 3u, expectedCW},
    }};

    // Capture bytesPerRow by value so the lambda is self-contained.
    const uint32_t bpr = bytesPerRow;

    t.expectGPUBufferValuesPassCheck(
        readback,
        [checks, bpr](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (const CornerCheck& c : checks) {
                const uint64_t offset =
                    static_cast<uint64_t>(c.row) * bpr
                    + static_cast<uint64_t>(c.col) * kBytesPerPixel;
                if (offset + kBytesPerPixel > len) {
                    std::ostringstream msg;
                    msg << "rgba8unorm pixel offset out of range: " << offset;
                    return msg.str();
                }
                for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
                    const uint8_t got      = actual[offset + ch];
                    const uint8_t expected = c.expected[ch];
                    if (got != expected) {
                        std::ostringstream msg;
                        msg << "rgba8unorm mismatch at (" << c.col << ", " << c.row
                            << ") channel " << ch
                            << ": expected " << static_cast<int>(expected)
                            << ", got " << static_cast<int>(got);
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void runCullingTest(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string frontFace = t.param<std::string>("frontFace");
    const std::string cullMode  = t.param<std::string>("cullMode");
    const std::string topology  = t.param<std::string>("topology");

    WGPUTexture target = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(target, viewDesc);

    WGPURenderPipeline pipeline = createCullingPipeline(t, topology, frontFace, cullMode);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 1.0, 1.0}; // blue

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    verifyCullingResult(t, target, frontFace, cullMode);
}

CTS_TEST(g, "culling")
    .params([](ParamsBuilder u) {
        return u.combine("frontFace", {"ccw", "cw"})
            .combine("cullMode", {"none", "front", "back"})
            .beginSubcases()
            .combine("topology", {"triangle-list", "triangle-strip"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runCullingTest(t);
    });

} // namespace
