// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/storeOp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports color_attachment_only (rgba8unorm) + color_attachment_with_depth_stencil_attachment; format/mip/layer/multiple/depth-only deferred.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
    "api,operation,render_pass,storeOp",
    "Render pass storeOp operation tests.");

constexpr uint32_t kWidth = 2;
constexpr uint32_t kHeight = 2;
constexpr uint32_t kBytesPerPixel = 4;  // rgba8unorm: 4 bytes/texel
constexpr uint32_t kDepthBytesPerPixel = 4;  // depth32float: 4 bytes/texel (f32)

// bytesPerRow = align(kWidth * kBytesPerPixel, 256) = align(8, 256) = 256
constexpr uint32_t kColorBytesPerRow = kBytesPerRowAlignment;
// depth bytesPerRow = align(kWidth * kDepthBytesPerPixel, 256) = align(8, 256) = 256
constexpr uint32_t kDepthBytesPerRow = kBytesPerRowAlignment;

// color_attachment_only clear values: {R:0.8, G:0.75, B:0.5, A:1.0}
// Rounded to unorm8: {204, 191, 128, 255}  (tol 2/255)
constexpr WGPUColor kColorOnlyClearValue = {0.8, 0.75, 0.5, 1.0};
constexpr std::array<uint8_t, 4> kColorOnlyStoredBytes = {204, 191, 128, 255};
constexpr std::array<uint8_t, 4> kColorOnlyDiscardedBytes = {0, 0, 0, 0};
constexpr uint32_t kColorOnlyTolerance = 2;

// color_attachment_with_depth_stencil_attachment clear values
constexpr WGPUColor kColorDepthClearColor = {1.0, 1.0, 1.0, 1.0};
constexpr float kDepthClearValue = 1.0f;
constexpr std::array<uint8_t, 4> kColorDepthStoredColorBytes = {255, 255, 255, 255};
constexpr std::array<uint8_t, 4> kColorDepthDiscardedColorBytes = {0, 0, 0, 0};
constexpr float kDepthStoredValue = 1.0f;
constexpr float kDepthDiscardedValue = 0.0f;
constexpr double kDepthTolerance = 1.0 / 256.0;

WGPUStoreOp parseStoreOp(std::string_view value) {
    if (value == "store") {
        return WGPUStoreOp_Store;
    }
    if (value == "discard") {
        return WGPUStoreOp_Discard;
    }
    std::abort();
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// Verify all 4 texels of the 2x2 rgba8unorm color texture.
// expected: the 4-byte RGBA value each texel should have.
// tolerance: per-channel absolute tolerance (for unorm rounding).
void expectColorTexture2x2(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture colorTexture,
    const std::array<uint8_t, 4>& expected,
    uint32_t tolerance) {
    // Total buffer size = bytesPerRow * kHeight = 256 * 2 = 512, already >= kBufferCopyAlignment.
    const uint64_t byteLength = static_cast<uint64_t>(kColorBytesPerRow) * kHeight;

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = colorTexture;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = kColorBytesPerRow;
    destination.layout.rowsPerImage = kHeight;

    WGPUExtent3D copyExtent = WGPUExtent3D{kWidth, kHeight, 1};
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copyExtent);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t row = 0; row < kHeight; ++row) {
                for (uint32_t col = 0; col < kWidth; ++col) {
                    const uint64_t offset =
                        static_cast<uint64_t>(row) * kColorBytesPerRow
                        + static_cast<uint64_t>(col) * kBytesPerPixel;
                    if (offset + kBytesPerPixel > len) {
                        std::ostringstream msg;
                        msg << "color texel offset out of range at (" << col << "," << row << ")";
                        return msg.str();
                    }
                    for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
                        const int diff = static_cast<int>(actual[offset + ch])
                                       - static_cast<int>(expected[ch]);
                        if (std::abs(diff) > static_cast<int>(tolerance)) {
                            std::ostringstream msg;
                            msg << "rgba8unorm mismatch at (" << col << "," << row
                                << ") channel " << ch
                                << ": expected " << static_cast<int>(expected[ch])
                                << " got " << static_cast<int>(actual[offset + ch])
                                << " (tol " << tolerance << ")";
                            return msg.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

// Verify all 4 texels of the 2x2 depth32float depth texture.
// expected: the f32 depth value each texel should have.
void expectDepthTexture2x2(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture depthTexture,
    float expectedDepth) {
    // Total buffer size = bytesPerRow * kHeight = 256 * 2 = 512.
    const uint64_t byteLength = static_cast<uint64_t>(kDepthBytesPerRow) * kHeight;

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = depthTexture;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_DepthOnly;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = kDepthBytesPerRow;
    destination.layout.rowsPerImage = kHeight;

    WGPUExtent3D copyExtent = WGPUExtent3D{kWidth, kHeight, 1};
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copyExtent);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t row = 0; row < kHeight; ++row) {
                for (uint32_t col = 0; col < kWidth; ++col) {
                    const uint64_t offset =
                        static_cast<uint64_t>(row) * kDepthBytesPerRow
                        + static_cast<uint64_t>(col) * kDepthBytesPerPixel;
                    if (offset + kDepthBytesPerPixel > len) {
                        std::ostringstream msg;
                        msg << "depth texel offset out of range at (" << col << "," << row << ")";
                        return msg.str();
                    }
                    float actualDepth = 0.0f;
                    std::memcpy(&actualDepth, actual + offset, sizeof(actualDepth));
                    if (std::abs(static_cast<double>(actualDepth)
                                 - static_cast<double>(expectedDepth)) > kDepthTolerance) {
                        std::ostringstream msg;
                        msg << "depth32float mismatch at (" << col << "," << row
                            << "): expected " << expectedDepth
                            << " got " << actualDepth;
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

// color_attachment_only: empty render pass on a 2x2 rgba8unorm target.
// Verify that 'store' keeps the clear value and 'discard' produces zeros.
void runColorAttachmentOnly(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string storeOpStr = t.param<std::string>("storeOperation");
    const WGPUStoreOp storeOp = parseStoreOp(storeOpStr);

    // Create 2x2 rgba8unorm render target.
    WGPUTextureDescriptor colorTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    colorTexDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    colorTexDesc.mipLevelCount = 1;
    colorTexDesc.sampleCount = 1;
    colorTexDesc.dimension = WGPUTextureDimension_2D;
    colorTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    colorTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture colorTexture = t.createTextureTracked(colorTexDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTexture, viewDesc);

    // Color attachment: clear to {0.8,0.75,0.5,1.0}, storeOp from param.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = storeOp;
    colorAttachment.clearValue = kColorOnlyClearValue;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    // Empty render pass: no pipeline, no draw.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    // Verify all 4 texels.
    const std::array<uint8_t, 4>& expected =
        (storeOpStr == "store") ? kColorOnlyStoredBytes : kColorOnlyDiscardedBytes;
    expectColorTexture2x2(t, colorTexture, expected, kColorOnlyTolerance);
}

// Verify all 4 texels of the 2x2 stencil8 texture (stencil aspect, 1 byte/texel).
// expectedStencil: the uint8 stencil value each texel should have.
void expectStencilTexture2x2(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture stencilTexture,
    uint8_t expectedStencil) {
    // stencil8: 1 byte/texel, bytesPerRow = align(kWidth * 1, 256) = 256, total = 256 * 2 = 512.
    constexpr uint32_t kStencilBytesPerPixel = 1;
    constexpr uint32_t kStencilBytesPerRow = kBytesPerRowAlignment;  // align(2*1, 256) = 256
    const uint64_t byteLength = static_cast<uint64_t>(kStencilBytesPerRow) * kHeight;

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = stencilTexture;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_StencilOnly;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = kStencilBytesPerRow;
    destination.layout.rowsPerImage = kHeight;

    WGPUExtent3D copyExtent = WGPUExtent3D{kWidth, kHeight, 1};
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copyExtent);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t row = 0; row < kHeight; ++row) {
                for (uint32_t col = 0; col < kWidth; ++col) {
                    const uint64_t offset =
                        static_cast<uint64_t>(row) * kStencilBytesPerRow
                        + static_cast<uint64_t>(col) * kStencilBytesPerPixel;
                    if (offset + kStencilBytesPerPixel > len) {
                        std::ostringstream msg;
                        msg << "stencil texel offset out of range at (" << col << "," << row << ")";
                        return msg.str();
                    }
                    if (actual[offset] != expectedStencil) {
                        std::ostringstream msg;
                        msg << "stencil8 mismatch at (" << col << "," << row
                            << "): expected " << static_cast<int>(expectedStencil)
                            << " got " << static_cast<int>(actual[offset]);
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

// color_attachment_with_depth_stencil_attachment: empty render pass on a 2x2 rgba8unorm color
// target and a 2x2 depth32float depth target. Verify color and depth independently.
void runColorWithDepthStencil(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string colorStoreOpStr = t.param<std::string>("colorStoreOperation");
    const std::string depthStoreOpStr = t.param<std::string>("depthStencilStoreOperation");
    const WGPUStoreOp colorStoreOp = parseStoreOp(colorStoreOpStr);
    const WGPUStoreOp depthStoreOp = parseStoreOp(depthStoreOpStr);

    // Create 2x2 rgba8unorm color target.
    WGPUTextureDescriptor colorTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    colorTexDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    colorTexDesc.mipLevelCount = 1;
    colorTexDesc.sampleCount = 1;
    colorTexDesc.dimension = WGPUTextureDimension_2D;
    colorTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    colorTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture colorTexture = t.createTextureTracked(colorTexDesc);

    // Create 2x2 depth32float depth target.
    WGPUTextureDescriptor depthTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    depthTexDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    depthTexDesc.mipLevelCount = 1;
    depthTexDesc.sampleCount = 1;
    depthTexDesc.dimension = WGPUTextureDimension_2D;
    depthTexDesc.format = WGPUTextureFormat_Depth32Float;
    depthTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture depthTexture = t.createTextureTracked(depthTexDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTexture, viewDesc);
    WGPUTextureView depthView = t.createViewTracked(depthTexture, viewDesc);

    // Color attachment: clear to white {1,1,1,1}, colorStoreOp from param.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = colorStoreOp;
    colorAttachment.clearValue = kColorDepthClearColor;

    // Depth-stencil attachment: depthClearValue 1.0, depthStoreOp from param.
    WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    dsAttachment.view = depthView;
    dsAttachment.depthLoadOp = WGPULoadOp_Clear;
    dsAttachment.depthStoreOp = depthStoreOp;
    dsAttachment.depthClearValue = kDepthClearValue;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &dsAttachment;

    // Empty render pass: no pipeline, no draw.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    // Verify color: all 4 texels.
    const std::array<uint8_t, 4>& expectedColor =
        (colorStoreOpStr == "store") ? kColorDepthStoredColorBytes : kColorDepthDiscardedColorBytes;
    expectColorTexture2x2(t, colorTexture, expectedColor, 0);

    // Verify depth: all 4 texels.
    const float expectedDepth =
        (depthStoreOpStr == "store") ? kDepthStoredValue : kDepthDiscardedValue;
    expectDepthTexture2x2(t, depthTexture, expectedDepth);
}

// multiple_color_attachments: colorAttachments x storeOperation1 x storeOperation2 matrix.
// Each attachment cleared to {1,1,1,1}; even index uses storeOp1, odd index uses storeOp2.
// Empty render pass. Verify per-attachment: store→{255,255,255,255}, discard→{0,0,0,0}.
void runMultipleColorAttachments(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string storeOpStr1 = t.param<std::string>("storeOperation1");
    const std::string storeOpStr2 = t.param<std::string>("storeOperation2");
    const WGPUStoreOp storeOp1 = parseStoreOp(storeOpStr1);
    const WGPUStoreOp storeOp2 = parseStoreOp(storeOpStr2);

    const uint32_t colorAttachmentCount = static_cast<uint32_t>(t.param<uint64_t>("colorAttachments"));

    // Create 2 rgba8unorm render targets.
    std::array<WGPUTexture, 4> colorTextures{};
    std::array<WGPUTextureView, 4> colorViews{};
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        colorTextures[i] = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        colorViews[i] = t.createViewTracked(colorTextures[i], viewDesc);
    }

    // Set up color attachments: even index uses storeOp1, odd index uses storeOp2.
    std::array<WGPURenderPassColorAttachment, 4> colorAttachments{};
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachments[i].view = colorViews[i];
        colorAttachments[i].loadOp = WGPULoadOp_Clear;
        colorAttachments[i].storeOp = (i % 2 == 0) ? storeOp1 : storeOp2;
        colorAttachments[i].clearValue = WGPUColor{1.0, 1.0, 1.0, 1.0};
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colorAttachmentCount;
    passDesc.colorAttachments = colorAttachments.data();

    // Empty render pass: no pipeline, no draw.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    // Verify each attachment independently.
    constexpr std::array<uint8_t, 4> kStored = {255, 255, 255, 255};
    constexpr std::array<uint8_t, 4> kDiscarded = {0, 0, 0, 0};

    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        const std::string& opStr = (i % 2 == 0) ? storeOpStr1 : storeOpStr2;
        const std::array<uint8_t, 4>& expected = (opStr == "store") ? kStored : kDiscarded;
        expectColorTexture2x2(t, colorTextures[i], expected, 0);
    }
}

// depth_stencil_attachment_only: colorAttachmentCount=0, depth32float or stencil8 attachment.
// depth32float: depthClearValue 1.0, depthLoadOp clear, depthStoreOp=param (stencil Undefined).
// stencil8: stencilClearValue 1, stencilLoadOp clear, stencilStoreOp=param (depth Undefined).
// Empty render pass. Verify: store keeps clear value, discard produces 0.
void runDepthStencilAttachmentOnly(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string formatStr = t.param<std::string>("depthStencilFormat");
    const std::string storeOpStr = t.param<std::string>("storeOperation");
    const WGPUStoreOp storeOp = parseStoreOp(storeOpStr);

    const bool isDepth32float = (formatStr == "depth32float");
    const WGPUTextureFormat texFormat = isDepth32float
        ? WGPUTextureFormat_Depth32Float
        : WGPUTextureFormat_Stencil8;

    // Create 2x2 depth-or-stencil render target.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = texFormat;
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture dsTexture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView dsView = t.createViewTracked(dsTexture, viewDesc);

    // Build depth-stencil attachment, setting only the relevant aspect fields.
    WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    dsAttachment.view = dsView;
    if (isDepth32float) {
        // depth32float: only depth aspect; stencil fields remain Undefined.
        dsAttachment.depthClearValue = 1.0f;
        dsAttachment.depthLoadOp = WGPULoadOp_Clear;
        dsAttachment.depthStoreOp = storeOp;
    } else {
        // stencil8: only stencil aspect; depth fields remain Undefined.
        dsAttachment.stencilClearValue = 1;
        dsAttachment.stencilLoadOp = WGPULoadOp_Clear;
        dsAttachment.stencilStoreOp = storeOp;
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 0;
    passDesc.colorAttachments = nullptr;
    passDesc.depthStencilAttachment = &dsAttachment;

    // Empty render pass: no pipeline, no draw.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    // Verify the stored/discarded value.
    if (isDepth32float) {
        const float expectedDepth = (storeOpStr == "store") ? 1.0f : 0.0f;
        expectDepthTexture2x2(t, dsTexture, expectedDepth);
    } else {
        const uint8_t expectedStencil = (storeOpStr == "store") ? 1 : 0;
        expectStencilTexture2x2(t, dsTexture, expectedStencil);
    }
}

CTS_TEST(g, "render_pass_store_op,color_attachment_only")
    .params([](ParamsBuilder u) {
        return u
            .combine("colorFormat", {Value("rgba8unorm")})
            .combine("storeOperation", {Value("store"), Value("discard")})
            .beginSubcases()
            .combine("mipLevel", {uint64_t(0)})
            .combine("arrayLayer", {uint64_t(0)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runColorAttachmentOnly(t);
    });

CTS_TEST(g, "render_pass_store_op,color_attachment_with_depth_stencil_attachment")
    .params([](ParamsBuilder u) {
        return u
            .combine("colorStoreOperation", {Value("store"), Value("discard")})
            .combine("depthStencilStoreOperation", {Value("store"), Value("discard")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runColorWithDepthStencil(t);
    });

CTS_TEST(g, "render_pass_store_op,multiple_color_attachments")
    .params([](ParamsBuilder u) {
        return u
            .combine("storeOperation1", {Value("store"), Value("discard")})
            .combine("storeOperation2", {Value("store"), Value("discard")})
            .beginSubcases()
            .combine("colorAttachments", {uint64_t(1), uint64_t(2), uint64_t(3), uint64_t(4)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runMultipleColorAttachments(t);
    });

CTS_TEST(g, "render_pass_store_op,depth_stencil_attachment_only")
    .params([](ParamsBuilder u) {
        return u
            .combine("depthStencilFormat", {Value("depth32float"), Value("stencil8")})
            .combine("storeOperation", {Value("store"), Value("discard")})
            .beginSubcases()
            .combine("mipLevel", {uint64_t(0)})
            .combine("arrayLayer", {uint64_t(0)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runDepthStencilAttachmentOnly(t);
    });

} // namespace
