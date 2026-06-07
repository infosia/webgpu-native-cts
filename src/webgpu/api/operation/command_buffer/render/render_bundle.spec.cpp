// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/render/render_bundle.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,render,render_bundle",
    "Render bundle execution tests.");

// Render target: 4×4 rgba8unorm.
constexpr uint32_t kWidth        = 4;
constexpr uint32_t kHeight       = 4;
constexpr uint32_t kBytesPerPixel = 4;

// Expected pixel values (rgba8unorm byte representation).
// Shader uses x.1/255 nudge to round to exact bytes.
constexpr std::array<uint8_t, 4> kColor0   = {1, 2, 3, 4};
constexpr std::array<uint8_t, 4> kColor1   = {5, 6, 7, 8};
constexpr std::array<uint8_t, 4> kColor0x3 = {3, 6, 9, 12};
constexpr std::array<uint8_t, 4> kZero     = {0, 0, 0, 0};

// One shader module used by all tests.
// Vertex shader emits two triangles covering the whole 4×4 target:
//   tri0 (verts 0,1,2): bottom-left half — instance 0 → kColor0
//   tri1 (verts 3,4,5): top-right  half — instance 1 → kColor1
// Fragment returns colors[inst] where:
//   colors[0] = vec4f(1.1/255, 2.1/255, 3.1/255, 4.1/255)
//   colors[1] = vec4f(5.1/255, 6.1/255, 7.1/255, 8.1/255)
// The .1 nudge biases values so they round to exact bytes.
constexpr std::string_view kShader = R"(
@vertex fn vs(
    @builtin(vertex_index)   vNdx : u32,
    @builtin(instance_index) inst : u32
) -> @builtin(position) vec4<f32> {
    // @location(0) @interpolate(flat) is declared in a struct in the original,
    // but here we pass inst via a struct so the fragment can use it.
    // Positions: tri0 covers bottom-left; tri1 covers top-right.
    var pos = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>(-1.0,  1.0),
        vec2<f32>(-1.0,  1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>( 1.0,  1.0)
    );
    return vec4<f32>(pos[vNdx], 0.0, 1.0);
}

struct VSOut {
    @builtin(position)                         pos  : vec4<f32>,
    @location(0) @interpolate(flat, either)    inst : u32,
};

@vertex fn vsInst(
    @builtin(vertex_index)   vNdx : u32,
    @builtin(instance_index) inst : u32
) -> VSOut {
    var pos = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>(-1.0,  1.0),
        vec2<f32>(-1.0,  1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>( 1.0,  1.0)
    );
    var out: VSOut;
    out.pos  = vec4<f32>(pos[vNdx], 0.0, 1.0);
    out.inst = inst;
    return out;
}

@fragment fn fs(in: VSOut) -> @location(0) vec4<f32> {
    var colors = array<vec4<f32>, 2>(
        vec4<f32>(1.1 / 255.0, 2.1 / 255.0, 3.1 / 255.0, 4.1 / 255.0),
        vec4<f32>(5.1 / 255.0, 6.1 / 255.0, 7.1 / 255.0, 8.1 / 255.0)
    );
    return colors[in.inst];
}
)";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Create the 4×4 rgba8unorm render target texture.
WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kWidth, kHeight, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

// Build a render pipeline using vsInst+fs.  Pass a non-null blend pointer to
// enable additive blending on the color attachment.
WGPURenderPipeline makeRenderPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    const WGPUBlendState* blend = nullptr) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(kShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.blend  = blend; // nullptr → no blending; non-null → caller-supplied blend

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module     = shaderModule;
    fragment.entryPoint = stringView("fs");
    fragment.targetCount = 1;
    fragment.targets    = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto — no bind groups needed.
    desc.layout                = nullptr;
    desc.vertex.module         = shaderModule;
    desc.vertex.entryPoint     = stringView("vsInst");
    desc.primitive.topology    = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count     = 1;
    desc.fragment              = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Begin a render pass on a 4×4 texture, clearing to (0,0,0,0).
WGPURenderPassEncoder beginRenderPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttachment;
    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

// Create a render bundle encoder for rgba8unorm.
WGPURenderBundleEncoder createBundleEncoder(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    desc.colorFormatCount = 1;
    desc.colorFormats     = &colorFormat;
    desc.sampleCount      = 1;
    return wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
}

// Submit an encoder and free the resulting command buffer.
void submitEncoder(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer cb = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cb);
}

// ---------------------------------------------------------------------------
// Verification helpers
// ---------------------------------------------------------------------------

// bytesPerRow for the 4×4 readback: align(4*4, 256) = 256.
constexpr uint32_t kBytesPerRow =
    ((kWidth * kBytesPerPixel + kBytesPerRowAlignment - 1) / kBytesPerRowAlignment)
    * kBytesPerRowAlignment; // = 256

// Pixel (col, row) lives at row*kBytesPerRow + col*kBytesPerPixel in the readback buffer.
// Checks that actual[col][row] matches expected with per-channel maxDiff tolerance.
void checkPixel(
    std::ostringstream& msg,
    const uint8_t* actual,
    uint32_t col,
    uint32_t row,
    const std::array<uint8_t, 4>& expected,
    uint8_t maxDiff) {
    const uint64_t offset =
        static_cast<uint64_t>(row) * kBytesPerRow
        + static_cast<uint64_t>(col) * kBytesPerPixel;
    for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
        const uint8_t got = actual[offset + ch];
        const int diff = static_cast<int>(got) - static_cast<int>(expected[ch]);
        if (diff < -static_cast<int>(maxDiff) || diff > static_cast<int>(maxDiff)) {
            msg << "rgba8unorm mismatch at pixel (" << col << ", " << row << ") ch " << ch
                << ": expected " << static_cast<int>(expected[ch])
                << ", got " << static_cast<int>(got) << "\n";
        }
    }
}

// Readback the texture and run a per-pixel check lambda.
void verifyTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const std::function<std::optional<std::string>(const uint8_t* data)>& check) {
    constexpr uint64_t byteLength = static_cast<uint64_t>(kBytesPerRow) * kHeight;

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, readback, kBytesPerRow, WGPUExtent3D{kWidth, kHeight, 1});
    submitEncoder(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        readback,
        [check](const uint8_t* actual, size_t /*len*/) -> std::optional<std::string> {
            return check(actual);
        },
        0,
        static_cast<size_t>(byteLength));
}

// ---------------------------------------------------------------------------
// Test: basic
//   bundle: setPipeline; draw(6)
//   executeBundles([bundle])
//   Expect: all 4×4 texels == kColor0
// ---------------------------------------------------------------------------
CTS_TEST(g, "basic")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPURenderPipeline pipeline = makeRenderPipeline(t);
        WGPUTexture target = createRenderTarget(t);

        WGPURenderBundleEncoder bundleEncoder = createBundleEncoder(t);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline);
        wgpuRenderBundleEncoderDraw(bundleEncoder, 6, 1, 0, 0);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(target, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPass(encoder, view);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderPassEncoderEnd(pass);
        submitEncoder(t, encoder);

        wgpuRenderBundleRelease(bundle);

        verifyTexture(t, target, [](const uint8_t* data) -> std::optional<std::string> {
            std::ostringstream msg;
            for (uint32_t row = 0; row < kHeight; ++row) {
                for (uint32_t col = 0; col < kWidth; ++col) {
                    checkPixel(msg, data, col, row, kColor0, 1);
                }
            }
            const std::string s = msg.str();
            if (!s.empty()) { return s; }
            return std::nullopt;
        });
    });

// ---------------------------------------------------------------------------
// Test: two_bundles
//   bundle1: draw(3, 1, 0, 0) — verts 0,1,2; instance 0 → kColor0 (bottom-left)
//   bundle2: draw(3, 1, 3, 1) — verts 3,4,5; instance 1 → kColor1 (top-right)
//   executeBundles([bundle1, bundle2])
//   Expect: pixel(0,3)==kColor0 (bottom-left), pixel(3,0)==kColor1 (top-right)
// ---------------------------------------------------------------------------
CTS_TEST(g, "two_bundles")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPURenderPipeline pipeline = makeRenderPipeline(t);
        WGPUTexture target = createRenderTarget(t);

        WGPURenderBundleEncoder enc1 = createBundleEncoder(t);
        wgpuRenderBundleEncoderSetPipeline(enc1, pipeline);
        wgpuRenderBundleEncoderDraw(enc1, 3, 1, 0, 0);
        WGPURenderBundle bundle1 = wgpuRenderBundleEncoderFinish(enc1, nullptr);

        WGPURenderBundleEncoder enc2 = createBundleEncoder(t);
        wgpuRenderBundleEncoderSetPipeline(enc2, pipeline);
        wgpuRenderBundleEncoderDraw(enc2, 3, 1, 3, 1);
        WGPURenderBundle bundle2 = wgpuRenderBundleEncoderFinish(enc2, nullptr);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(target, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPass(encoder, view);
        std::array<WGPURenderBundle, 2> bundles = {{bundle1, bundle2}};
        wgpuRenderPassEncoderExecuteBundles(pass, 2, bundles.data());
        wgpuRenderPassEncoderEnd(pass);
        submitEncoder(t, encoder);

        wgpuRenderBundleRelease(bundle1);
        wgpuRenderBundleRelease(bundle2);

        // Pixel (col=0, row=3) is bottom-left corner → kColor0 (instance 0, tri0).
        // Pixel (col=3, row=0) is top-right corner  → kColor1 (instance 1, tri1).
        verifyTexture(t, target, [](const uint8_t* data) -> std::optional<std::string> {
            std::ostringstream msg;
            checkPixel(msg, data, 0, 3, kColor0, 1);
            checkPixel(msg, data, 3, 0, kColor1, 1);
            const std::string s = msg.str();
            if (!s.empty()) { return s; }
            return std::nullopt;
        });
    });

// ---------------------------------------------------------------------------
// Test: one_bundle_used_multiple_times
//   bundle: draw(6)  (draws full-screen kColor0 in each viewport)
//   In one pass, 4× { setViewport(x,y,1,1,0,1); executeBundles([bundle]) }
//   for (x,y) in {(0,0),(2,0),(0,2),(2,2)}.
//   Expect: pixels (0,0),(2,0),(0,2),(2,2) == kColor0
//           pixels (1,0),(3,0),(0,1),(0,3),(3,3) == kZero
// ---------------------------------------------------------------------------
CTS_TEST(g, "one_bundle_used_multiple_times")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPURenderPipeline pipeline = makeRenderPipeline(t);
        WGPUTexture target = createRenderTarget(t);

        WGPURenderBundleEncoder bundleEncoder = createBundleEncoder(t);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline);
        wgpuRenderBundleEncoderDraw(bundleEncoder, 6, 1, 0, 0);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(target, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPass(encoder, view);

        // 4 viewport calls, each covering a 1×1 pixel region.
        constexpr std::array<std::array<float, 2>, 4> kViewports = {{
            {0.0f, 0.0f},
            {2.0f, 0.0f},
            {0.0f, 2.0f},
            {2.0f, 2.0f},
        }};
        for (const auto& vp : kViewports) {
            wgpuRenderPassEncoderSetViewport(pass, vp[0], vp[1], 1.0f, 1.0f, 0.0f, 1.0f);
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        }
        wgpuRenderPassEncoderEnd(pass);
        submitEncoder(t, encoder);

        wgpuRenderBundleRelease(bundle);

        verifyTexture(t, target, [](const uint8_t* data) -> std::optional<std::string> {
            std::ostringstream msg;
            // Pixels that should be kColor0 (written by one of the 4 viewports).
            checkPixel(msg, data, 0, 0, kColor0, 1);
            checkPixel(msg, data, 2, 0, kColor0, 1);
            checkPixel(msg, data, 0, 2, kColor0, 1);
            checkPixel(msg, data, 2, 2, kColor0, 1);
            // Pixels that should remain kZero (not covered by any viewport).
            checkPixel(msg, data, 1, 0, kZero, 1);
            checkPixel(msg, data, 3, 0, kZero, 1);
            checkPixel(msg, data, 0, 1, kZero, 1);
            checkPixel(msg, data, 0, 3, kZero, 1);
            checkPixel(msg, data, 3, 3, kZero, 1);
            const std::string s = msg.str();
            if (!s.empty()) { return s; }
            return std::nullopt;
        });
    });

// ---------------------------------------------------------------------------
// Test: one_bundle_used_multiple_times_same_executeBundles
//   Pipeline with additive blend {srcFactor One, dstFactor One, operation Add}.
//   bundle: draw(6) → draws kColor0 per execution.
//   executeBundles([bundle, bundle, bundle]) — 3× additive → kColor0x3.
//   Expect: all 4×4 texels == kColor0x3.
// ---------------------------------------------------------------------------
CTS_TEST(g, "one_bundle_used_multiple_times_same_executeBundles")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Additive blend: src*One + dst*One.
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_One;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_One;

        WGPURenderPipeline pipeline = makeRenderPipeline(t, &blend);
        WGPUTexture target = createRenderTarget(t);

        WGPURenderBundleEncoder bundleEncoder = createBundleEncoder(t);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline);
        wgpuRenderBundleEncoderDraw(bundleEncoder, 6, 1, 0, 0);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(target, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPass(encoder, view);
        // Execute the same bundle 3 times in a single executeBundles call.
        std::array<WGPURenderBundle, 3> bundles = {{bundle, bundle, bundle}};
        wgpuRenderPassEncoderExecuteBundles(pass, 3, bundles.data());
        wgpuRenderPassEncoderEnd(pass);
        submitEncoder(t, encoder);

        wgpuRenderBundleRelease(bundle);

        verifyTexture(t, target, [](const uint8_t* data) -> std::optional<std::string> {
            std::ostringstream msg;
            for (uint32_t row = 0; row < kHeight; ++row) {
                for (uint32_t col = 0; col < kWidth; ++col) {
                    checkPixel(msg, data, col, row, kColor0x3, 1);
                }
            }
            const std::string s = msg.str();
            if (!s.empty()) { return s; }
            return std::nullopt;
        });
    });

} // namespace
