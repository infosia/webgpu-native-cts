// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/sampling/anisotropy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.
//
// Port deviations:
//  - The fixture class SamplerAnisotropicFilteringSlantedPlaneTest uses init() to build the
//    render pipeline instead of a class field initialized by the async init() in JS.
//  - anisotropic_filter_mipmap_color: upstream uses paramsSimple([...]) with complex _results
//    array objects that cannot be stored as cts::Value. We map paramsSimple to
//    combineWithParams({maxAnisotropy}) at the case level and look up per-case expectations in
//    the body via a compile-time table. Query identity still matches upstream:
//      webgpu:api,operation,sampling,anisotropy:anisotropic_filter_mipmap_color:maxAnisotropy=1
//      webgpu:api,operation,sampling,anisotropy:anisotropic_filter_mipmap_color:maxAnisotropy=4
//  - _generateWarningOnly is inlined into the per-case expectations table (not a query param).
//  - checkElementsEqual (JS) is implemented inline as a byte-by-byte comparison of the mapped
//    buffer data, returning true when all bytes match.
//  - readGPUBufferRangeTyped / result.cleanup() are replaced by expectGPUBufferValuesPassCheck
//    which handles map/unmap internally.
//  - createTextureFromTexelViewsMultipleMipmaps (JS helper that uses TexelView.fromTexelsAsBytes
//    to fill each mip level with a constant colour) is inlined using queueWriteTexture for each
//    mip level; the solid-colour fills are equivalent.
//  - expectSinglePixelBetweenTwoValuesIn2DTexture / expectSinglePixelComparisonsAreOkInTexture
//    from upstream gpu_test / texture_test_utils are both inlined using copyTextureToBuffer +
//    expectGPUBufferValuesPassCheck, reading the single pixel of interest.

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Constants (mirror upstream)
// ---------------------------------------------------------------------------
constexpr uint32_t kRTSize = 16;
constexpr uint32_t kBytesPerRow = 256; // >= kRTSize * 4; matches upstream kBytesPerRow
constexpr uint32_t xMiddle = kRTSize / 2;
// Mip-level solid colours used by anisotropic_filter_mipmap_color
constexpr std::array<uint8_t, 4> kColorMip0 = {{0xff, 0x00, 0x00, 0xff}}; // red
constexpr std::array<uint8_t, 4> kColorMip1 = {{0x00, 0xff, 0x00, 0xff}}; // green
constexpr std::array<uint8_t, 4> kColorMip2 = {{0x00, 0x00, 0xff, 0xff}}; // blue

// Checkerboard colours for anisotropic_filter_checkerboard
constexpr std::array<uint8_t, 4> kCheckerColor0 = {{0xff, 0x00, 0x00, 0xff}};
constexpr std::array<uint8_t, 4> kCheckerColor1 = {{0x00, 0xff, 0x00, 0xff}};

// ---------------------------------------------------------------------------
// WGSL shaders (inlined from upstream)
// ---------------------------------------------------------------------------
constexpr std::string_view kVertexShader = R"(
struct Outputs {
  @builtin(position) Position : vec4<f32>,
  @location(0) fragUV : vec2<f32>,
};

@vertex fn main(
  @builtin(vertex_index) VertexIndex : u32) -> Outputs {
  var position : array<vec3<f32>, 6> = array<vec3<f32>, 6>(
    vec3<f32>(-0.5, 0.5, -0.5),
    vec3<f32>(0.5, 0.5, -0.5),
    vec3<f32>(-0.5, 0.5, 0.5),
    vec3<f32>(-0.5, 0.5, 0.5),
    vec3<f32>(0.5, 0.5, -0.5),
    vec3<f32>(0.5, 0.5, 0.5));
  // uv is pre-scaled to mimic repeating tiled texture
  var uv : array<vec2<f32>, 6> = array<vec2<f32>, 6>(
    vec2<f32>(0.0, 0.0),
    vec2<f32>(1.0, 0.0),
    vec2<f32>(0.0, 50.0),
    vec2<f32>(0.0, 50.0),
    vec2<f32>(1.0, 0.0),
    vec2<f32>(1.0, 50.0));
  // draw a slanted plane in a specific way
  let matrix : mat4x4<f32> = mat4x4<f32>(
    vec4<f32>(-1.7320507764816284, 1.8322050568049563e-16, -6.176817699518044e-17, -6.170640314703498e-17),
    vec4<f32>(-2.1211504944260596e-16, -1.496108889579773, 0.5043753981590271, 0.5038710236549377),
    vec4<f32>(0.0, -43.63650894165039, -43.232173919677734, -43.18894577026367),
    vec4<f32>(0.0, 21.693578720092773, 21.789791107177734, 21.86800193786621));

  var output : Outputs;
  output.fragUV = uv[VertexIndex];
  output.Position = matrix * vec4<f32>(position[VertexIndex], 1.0);
  return output;
}
)";

constexpr std::string_view kFragmentShader = R"(
@group(0) @binding(0) var sampler0 : sampler;
@group(0) @binding(1) var texture0 : texture_2d<f32>;

@fragment fn main(
  @builtin(position) FragCoord : vec4<f32>,
  @location(0) fragUV: vec2<f32>)
  -> @location(0) vec4<f32> {
    return textureSample(texture0, sampler0, fragUV);
}
)";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
WGPUStringView stringView(std::string_view sv) {
    return WGPUStringView{sv.data(), sv.size()};
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// ---------------------------------------------------------------------------
// Fixture class
// ---------------------------------------------------------------------------

// C++ port of SamplerAnisotropicFilteringSlantedPlaneTest.
// The JS class inherits AllFeaturesMaxLimitsGPUTest; we do the same.
// The render pipeline is created once in init() and stored as a member.
class AnisotropyTest : public AllFeaturesMaxLimitsGpuTest {
  public:
    void init() override {
        AllFeaturesMaxLimitsGpuTest::init();

        // Build the slanted-plane render pipeline used by both tests.
        // layout: auto → pipelineDesc.layout = nullptr (default from INIT).
        // Upstream creates two separate shader modules (both entry points are named
        // "main", so they cannot live in one module without a duplicate declaration).
        WGPUShaderModule vsModule = createShaderModuleTracked(kVertexShader);
        WGPUShaderModule fsModule = createShaderModuleTracked(kFragmentShader);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fsModule;
        fragment.entryPoint = stringView("main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr; // auto layout
        pipeDesc.vertex.module = vsModule;
        pipeDesc.vertex.entryPoint = stringView("main");
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeDesc.multisample.count = 1;
        pipeDesc.fragment = &fragment;
        pipeline_ = createRenderPipelineTracked(pipeDesc);
    }

    // Render the slanted plane textured with the given view+sampler.
    // Returns the color attachment texture (kRTSize x kRTSize, RGBA8Unorm, CopySrc).
    WGPUTexture drawSlantedPlane(WGPUTextureView textureView, WGPUSampler sampler) {
        // Obtain bind group layout from auto-layout pipeline (getter → must release manually).
        WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline_, 0);

        // Bind group entries must outlive createBindGroupTracked.
        std::array<WGPUBindGroupEntry, 2> bgEntries = {{
            WGPU_BIND_GROUP_ENTRY_INIT,
            WGPU_BIND_GROUP_ENTRY_INIT,
        }};
        bgEntries[0].binding = 0;
        bgEntries[0].sampler = sampler;
        bgEntries[1].binding = 1;
        bgEntries[1].textureView = textureView;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = bgEntries.size();
        bgDesc.entries = bgEntries.data();
        WGPUBindGroup bindGroup = createBindGroupTracked(bgDesc);
        // Release the getter result now that the bind group has been created.
        wgpuBindGroupLayoutRelease(bgl);

        // Create the color attachment render target.
        WGPUTextureDescriptor rtDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        rtDesc.size = WGPUExtent3D{kRTSize, kRTSize, 1};
        rtDesc.mipLevelCount = 1;
        rtDesc.sampleCount = 1;
        rtDesc.dimension = WGPUTextureDimension_2D;
        rtDesc.format = WGPUTextureFormat_RGBA8Unorm;
        rtDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture colorAttachment = createTextureTracked(rtDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView colorView = createViewTracked(colorAttachment, viewDesc);

        WGPURenderPassColorAttachment passColorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        passColorAttachment.view = colorView;
        passColorAttachment.loadOp = WGPULoadOp_Clear;
        passColorAttachment.storeOp = WGPUStoreOp_Store;
        passColorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &passColorAttachment;

        WGPUCommandEncoder encoder = createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = finishTracked(encoder);
        wgpuQueueSubmit(queue(), 1, &commandBuffer);

        return colorAttachment;
    }

    // Copy the render target into a CopySrc|CopyDst buffer and return it.
    // Buffer layout: kRTSize rows, kBytesPerRow bytes each (zero-filled).
    WGPUBuffer copyRenderTargetToBuffer(WGPUTexture rt) {
        const uint64_t byteLength = static_cast<uint64_t>(kRTSize) * kBytesPerRow;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = byteLength;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = createBufferTracked(bufDesc);
        // Buffer is zero-filled by the WebGPU spec (zero-initialized at creation).

        WGPUCommandEncoder encoder = createCommandEncoderTracked();
        copyTextureToBuffer(encoder, rt, buffer, kBytesPerRow, WGPUExtent3D{kRTSize, kRTSize, 1});
        WGPUCommandBuffer commandBuffer = finishTracked(encoder);
        wgpuQueueSubmit(queue(), 1, &commandBuffer);

        return buffer;
    }

  private:
    WGPURenderPipeline pipeline_ = nullptr;
};

// ---------------------------------------------------------------------------
// Group registration
// ---------------------------------------------------------------------------
TestGroup<AnisotropyTest> g = MakeTestGroup<AnisotropyTest>(
    "api,operation,sampling,anisotropy",
    R"(Tests the behavior of anisotropic filtering.

TODO:
Note that anisotropic filtering is never guaranteed to occur, but we might be able to test some
things. If there are no guarantees we can issue warnings instead of failures. Ideas:
  - No *more* than the provided maxAnisotropy samples are used, by testing how many unique
    sample values come out of the sample operation.
  - Check anisotropy is done in the correct direction (by having a 2D gradient and checking we get
    more of the color in the correct direction).
)");

// ---------------------------------------------------------------------------
// Test: anisotropic_filter_checkerboard
// ---------------------------------------------------------------------------
CTS_TEST(g, "anisotropic_filter_checkerboard")
    .desc(
        R"(Anisotropic filter rendering tests that draws a slanted plane and samples from a texture
    that only has a top level mipmap, the content of which is like a checkerboard.
    We will check the rendering result using sampler with maxAnisotropy values to be
    different from each other, as the sampling rate is different.
    We will also check if those large maxAnisotropy values are clamped so that rendering is the
    same as the supported upper limit say 16.
    A similar webgl demo is at https://jsfiddle.net/yqnbez24)")
    .fn([](AnisotropyTest& t) {
        // Build a 32x32 checkerboard texture with only mip level 0.
        constexpr uint32_t textureSize = 32;
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{textureSize, textureSize, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        // Build checkerboard data: row-major, stride = kBytesPerRow (256 bytes).
        const uint32_t bufferSize = kBytesPerRow * textureSize;
        std::vector<uint8_t> data(bufferSize, 0);
        for (uint32_t r = 0; r < textureSize; ++r) {
            const uint32_t rowOffset = r * kBytesPerRow;
            for (uint32_t c = 0; c < textureSize; ++c) {
                const uint32_t pixelOffset = rowOffset + c * 4;
                const uint32_t cid = (r + c) % 2;
                const std::array<uint8_t, 4>& color = (cid == 0) ? kCheckerColor0 : kCheckerColor1;
                std::memcpy(data.data() + pixelOffset, color.data(), 4);
            }
        }

        // Upload via staging buffer (mirrors upstream's makeBufferWithContents + copyBufferToTexture).
        WGPUBuffer stagingBuffer = t.makeBufferWithContents(
            data.data(), data.size(),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        t.copyBufferToTexture(encoder, stagingBuffer, kBytesPerRow, texture,
                              WGPUExtent3D{textureSize, textureSize, 1});
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView textureView = t.createViewTracked(texture, viewDesc);

        const uint64_t byteLength = static_cast<uint64_t>(kRTSize) * kBytesPerRow;

        // Render with maxAnisotropy = 1, 16, 1024.
        constexpr std::array<uint16_t, 3> kMaxAnisotropyValues = {{1, 16, 1024}};
        std::array<WGPUBuffer, 3> resultBuffers = {};

        for (size_t i = 0; i < kMaxAnisotropyValues.size(); ++i) {
            WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDesc.maxAnisotropy = kMaxAnisotropyValues[i];
            WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

            WGPUTexture rt = t.drawSlantedPlane(textureView, sampler);
            resultBuffers[i] = t.copyRenderTargetToBuffer(rt);
        }

        // Compare results[0] (aniso=1) vs results[1] (aniso=16):
        // They should differ; if identical, issue a warning (not a failure).
        // Compare results[1] (aniso=16) vs results[2] (aniso=1024):
        // They should be identical; if different, fail.
        //
        // Implementation: map each buffer in turn and compare bytes.
        // We use expectGPUBufferValuesPassCheck for each comparison.
        //
        // Step 1: Read all three buffers synchronously into vectors.
        // We reuse expectGPUBufferValuesPassCheck with a capturing lambda.
        std::vector<uint8_t> result0(byteLength, 0);
        std::vector<uint8_t> result1(byteLength, 0);
        std::vector<uint8_t> result2(byteLength, 0);

        t.expectGPUBufferValuesPassCheck(
            resultBuffers[0],
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                std::memcpy(result0.data(), actual, std::min(len, result0.size()));
                return std::nullopt; // always pass; just capture
            },
            0, static_cast<size_t>(byteLength));

        t.expectGPUBufferValuesPassCheck(
            resultBuffers[1],
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                std::memcpy(result1.data(), actual, std::min(len, result1.size()));
                return std::nullopt;
            },
            0, static_cast<size_t>(byteLength));

        t.expectGPUBufferValuesPassCheck(
            resultBuffers[2],
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                std::memcpy(result2.data(), actual, std::min(len, result2.size()));
                return std::nullopt;
            },
            0, static_cast<size_t>(byteLength));

        // check0: results[0] == results[1]? If equal → warn (aniso=1 vs aniso=16 should differ).
        bool check0Equal = (std::memcmp(result0.data(), result1.data(), byteLength) == 0);
        if (check0Equal) {
            t.warn("Render results with sampler.maxAnisotropy being 1 and 16 should be different.");
        }

        // check1: results[1] == results[2]? If not equal → fail (aniso=16 vs aniso=1024 clamped).
        bool check1Equal = (std::memcmp(result1.data(), result2.data(), byteLength) == 0);
        if (!check1Equal) {
            t.expect(false,
                     "Render results with sampler.maxAnisotropy being 16 and 1024 should be the same.");
        }
    });

// ---------------------------------------------------------------------------
// Test: anisotropic_filter_mipmap_color
//
// Upstream params (paramsSimple) expand to two cases by maxAnisotropy.
// _results and _generateWarningOnly are compile-time tables looked up by maxAnisotropy.
// ---------------------------------------------------------------------------

// Per-pixel expectation: a single pixel at (x, y) is expected to equal exactly one colour,
// OR to be a lerp between two colours (in which case expA and expB are the two endpoints and
// expectExact = false).
struct PixelExpectation {
    uint32_t x = 0;
    uint32_t y = 0;
    std::array<uint8_t, 4> expA = {};
    std::array<uint8_t, 4> expB = {};
    bool expectExact = true; // if false: pixel must be between expA and expB channel-wise
};

// Read one pixel (x, y) from a texture into a 4-byte array.
// Uses copyTextureToBuffer for a 1x1 region at that coordinate.
void readPixel(
    AnisotropyTest& t,
    WGPUTexture texture,
    uint32_t x, uint32_t y,
    std::array<uint8_t, 4>& outPixel) {
    // bytesPerRow must be >= 1*4 and aligned to kBytesPerRowAlignment
    const uint32_t bytesPerRow = kBytesPerRowAlignment;
    const uint64_t bufSize = static_cast<uint64_t>(bytesPerRow);

    // Buffer needs CopyDst (written by CopyTextureToBuffer) and CopySrc (read by
    // expectGPUBufferValuesPassCheck via CopyBufferToBuffer to a MapRead staging buffer).
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = bufSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buf = t.createBufferTracked(bufDesc);

    // Copy a 1x1 region starting at (x, y, 0).
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

    WGPUTexelCopyTextureInfo srcInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    srcInfo.texture = texture;
    srcInfo.mipLevel = 0;
    srcInfo.origin = WGPUOrigin3D{x, y, 0};
    srcInfo.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dstInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dstInfo.buffer = buf;
    dstInfo.layout.offset = 0;
    dstInfo.layout.bytesPerRow = bytesPerRow;
    dstInfo.layout.rowsPerImage = 1;

    WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcInfo, &dstInfo, &copySize);

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    t.expectGPUBufferValuesPassCheck(
        buf,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len >= 4) {
                std::memcpy(outPixel.data(), actual, 4);
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(bufSize));
}

// Check that all channels of `pixel` lie between `a` and `b` channel-wise (inclusive).
// Returns true if check passes.
bool pixelIsBetween(
    const std::array<uint8_t, 4>& pixel,
    const std::array<uint8_t, 4>& a,
    const std::array<uint8_t, 4>& b) {
    for (int c = 0; c < 4; ++c) {
        uint8_t lo = std::min(a[c], b[c]);
        uint8_t hi = std::max(a[c], b[c]);
        if (pixel[c] < lo || pixel[c] > hi) {
            return false;
        }
    }
    return true;
}

// Helper: create a texture with N solid-colour mip levels.
// texelViews[i] is the colour for mip level i.
// Base size is given by `baseSize`; mip i has size max(1, baseSize >> i).
// Matches upstream createTextureFromTexelViewsMultipleMipmaps with TexelView.fromTexelsAsBytes.
WGPUTexture createSolidColorMipmapTexture(
    AnisotropyTest& t,
    const std::vector<std::array<uint8_t, 4>>& colors,
    WGPUExtent3D baseSize,
    WGPUTextureUsage usage) {
    const uint32_t mipLevelCount = static_cast<uint32_t>(colors.size());

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = baseSize;
    texDesc.mipLevelCount = mipLevelCount;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    // Always add CopyDst so we can upload via queueWriteTexture.
    texDesc.usage = usage | WGPUTextureUsage_CopyDst;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel) {
        const uint32_t mipW = std::max(1u, baseSize.width >> mipLevel);
        const uint32_t mipH = std::max(1u, baseSize.height >> mipLevel);
        const uint32_t mipD = 1u; // 2D texture, single layer

        // Build solid-colour buffer for this mip level.
        // bytesPerRow must be >= mipW * 4 and aligned to 256.
        const uint32_t bytesPerRow =
            static_cast<uint32_t>(alignTo(static_cast<uint64_t>(mipW) * 4, kBytesPerRowAlignment));
        const uint64_t dataSize = static_cast<uint64_t>(bytesPerRow) * mipH * mipD;
        std::vector<uint8_t> mipData(dataSize, 0);
        for (uint32_t row = 0; row < mipH; ++row) {
            for (uint32_t col = 0; col < mipW; ++col) {
                const uint64_t offset = static_cast<uint64_t>(row) * bytesPerRow + col * 4;
                std::memcpy(mipData.data() + offset, colors[mipLevel].data(), 4);
            }
        }

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = bytesPerRow;
        layout.rowsPerImage = mipH;
        t.queueWriteTexture(
            texture,
            WGPUExtent3D{mipW, mipH, mipD},
            layout,
            mipData.data(),
            mipData.size(),
            mipLevel,
            WGPUOrigin3D{0, 0, 0});
    }

    return texture;
}

CTS_TEST(g, "anisotropic_filter_mipmap_color")
    .desc(
        R"(Anisotropic filter rendering tests that draws a slanted plane and samples from a texture
    containing mipmaps of different colors. Given the same fragment with dFdx and dFdy for uv being different,
    sampler with bigger maxAnisotropy value tends to bigger mip levels to provide better details.
    We can then look at the color of the fragment to know which mip level is being sampled from and to see
    if it fits expectations.
    A similar webgl demo is at https://jsfiddle.net/t8k7c95o/5/)")
    // Port deviation: upstream uses paramsSimple([{maxAnisotropy:1,...},{maxAnisotropy:4,...}]).
    // We use combineWithParams at the case level since _results and _generateWarningOnly cannot
    // be stored as cts::Value; they are looked up by maxAnisotropy in the body.
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"maxAnisotropy", Value(int64_t(1))}},
            ParamRecord{{"maxAnisotropy", Value(int64_t(4))}},
        });
    })
    .fn([](AnisotropyTest& t) {
        const int64_t maxAnisotropy = t.param<int64_t>("maxAnisotropy");

        // Create the multi-mip texture with solid-colour mip levels:
        //   mip 0 = red,  mip 1 = green,  mip 2 = blue
        // Size: [4, 4, 1] (matches upstream: 4x4 → 2x2 → 1x1)
        const std::vector<std::array<uint8_t, 4>> colors = {
            kColorMip0, kColorMip1, kColorMip2};
        WGPUTexture texture = createSolidColorMipmapTexture(
            t, colors,
            WGPUExtent3D{4, 4, 1},
            WGPUTextureUsage_TextureBinding);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView textureView = t.createViewTracked(texture, viewDesc);

        WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.maxAnisotropy = static_cast<uint16_t>(maxAnisotropy);
        WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

        WGPUTexture colorAttachment = t.drawSlantedPlane(textureView, sampler);

        // Per-case expectations (inlined from upstream _results and _generateWarningOnly):
        //
        //   maxAnisotropy=1, _generateWarningOnly=false:
        //     { coord: {x:xMiddle, y:2}, expected: colors[2] }             → exact blue
        //     { coord: {x:xMiddle, y:6}, expected: [colors[0], colors[1]] } → between red and green
        //
        //   maxAnisotropy=4, _generateWarningOnly=true:
        //     { coord: {x:xMiddle, y:2}, expected: [colors[0], colors[1]] } → between red and green
        //     { coord: {x:xMiddle, y:6}, expected: colors[0] }              → exact red
        //
        // The upstream comment says: "lerp between two colors" entries go to
        // expectSinglePixelBetweenTwoValuesIn2DTexture (generates a warning when
        // _generateWarningOnly is true), while exact-color entries go to
        // expectSinglePixelComparisonsAreOkInTexture (always a hard failure).

        struct CaseExpectation {
            std::vector<PixelExpectation> pixels;
            bool generateWarningOnly = false;
        };

        CaseExpectation caseExp;
        if (maxAnisotropy == 1) {
            caseExp.generateWarningOnly = false;
            caseExp.pixels = {
                // exact blue at (xMiddle, 2) → hard fail on mismatch
                {xMiddle, 2, kColorMip2, kColorMip2, /*expectExact=*/true},
                // between red and green at (xMiddle, 6) — generateWarningOnly=false → hard fail
                {xMiddle, 6, kColorMip0, kColorMip1, /*expectExact=*/false},
            };
        } else {
            // maxAnisotropy == 4
            caseExp.generateWarningOnly = true;
            caseExp.pixels = {
                // between red and green at (xMiddle, 2) — generateWarningOnly=true → warn on mismatch
                {xMiddle, 2, kColorMip0, kColorMip1, /*expectExact=*/false},
                // exact red at (xMiddle, 6) — upstream uses expectSinglePixelComparisonsAreOkInTexture
                // (always hard fail) regardless of _generateWarningOnly.
                {xMiddle, 6, kColorMip0, kColorMip0, /*expectExact=*/true},
            };
        }

        // Evaluate expectations.
        for (const PixelExpectation& exp : caseExp.pixels) {
            std::array<uint8_t, 4> pixel = {};
            readPixel(t, colorAttachment, exp.x, exp.y, pixel);

            if (exp.expectExact) {
                // Exact-colour comparison. Upstream routes these through
                // expectSinglePixelComparisonsAreOkInTexture which is always a hard failure,
                // regardless of _generateWarningOnly. We mirror that here.
                bool ok = (pixel == exp.expA);
                if (!ok) {
                    std::ostringstream msg;
                    msg << "pixel at (" << exp.x << "," << exp.y << ") expected ("
                        << static_cast<int>(exp.expA[0]) << "," << static_cast<int>(exp.expA[1])
                        << "," << static_cast<int>(exp.expA[2]) << "," << static_cast<int>(exp.expA[3])
                        << ") got ("
                        << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1])
                        << "," << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3])
                        << ")";
                    t.expect(false, msg.str());
                }
            } else {
                // Between-two-colours comparison.
                bool ok = pixelIsBetween(pixel, exp.expA, exp.expB);
                if (!ok) {
                    std::ostringstream msg;
                    msg << "pixel at (" << exp.x << "," << exp.y << ") expected between ("
                        << static_cast<int>(exp.expA[0]) << "," << static_cast<int>(exp.expA[1])
                        << "," << static_cast<int>(exp.expA[2]) << "," << static_cast<int>(exp.expA[3])
                        << ") and ("
                        << static_cast<int>(exp.expB[0]) << "," << static_cast<int>(exp.expB[1])
                        << "," << static_cast<int>(exp.expB[2]) << "," << static_cast<int>(exp.expB[3])
                        << ") got ("
                        << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1])
                        << "," << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3])
                        << ")";
                    // For between-two-values checks, upstream uses t.params._generateWarningOnly
                    // to decide warn vs fail; mirror that here.
                    if (caseExp.generateWarningOnly) {
                        t.warn(msg.str());
                    } else {
                        t.expect(false, msg.str());
                    }
                }
            }
        }
    });

} // namespace
