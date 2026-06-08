// Ported from gpuweb/cts src/webgpu/api/operation/rendering/3d_texture_slices.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// This is the first render-to-3D-slice port and exercises WGPURenderPassColorAttachment.depthSlice.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,3d_texture_slices",
    "3D texture slice rendering operation tests.");

constexpr uint32_t kSize = 4;
constexpr uint32_t kDepth = 2;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kMaxDiff = 1;
constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr WGPUColor kClearValue = {0.3, 0.4, 0.5, 0.6};
constexpr std::array<uint8_t, 4> kRenderBytes = {11, 21, 31, 41};
constexpr std::array<uint8_t, 4> kClearBytes = {77, 102, 128, 153};
constexpr std::array<uint8_t, 4> kZeroBytes = {0, 0, 0, 0};

// Fixed params for multiple_color_attachments,same_mip_level
constexpr uint32_t kMultiAttachmentCount = 4;
constexpr uint32_t kMultiDepthSlices = 1u << kMultiAttachmentCount; // 16
// outputForLocationByChannel(location, ch) = (ch + location + 1) * 10 + 1 + location
// location 0: {11,21,31,41}, 1: {22,32,42,52}, 2: {33,43,53,63}, 3: {44,54,64,74}
constexpr std::array<std::array<uint8_t, 4>, kMultiAttachmentCount> kLocationBytes = {{
    {11, 21, 31, 41},
    {22, 32, 42, 52},
    {33, 43, 53, 63},
    {44, 54, 64, 74},
}};

constexpr std::string_view kMultiAttachmentShader = R"(
struct Output {
  @location(0) color0 : vec4f,
  @location(1) color1 : vec4f,
  @location(2) color2 : vec4f,
  @location(3) color3 : vec4f,
}

@vertex
fn main_vs(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f {
  let pos = array(
    vec2f(-1.0, 1.01),
    vec2f(1.01, -1.0),
    vec2f(-1.0, -1.0),
  );
  return vec4f(pos[VertexIndex], 0.0, 1.0);
}

const d = 255.0;

@fragment
fn main_fs() -> Output {
  var output : Output;
  output.color0 = vec4f(11.0 / d, 21.0 / d, 31.0 / d, 41.0 / d);
  output.color1 = vec4f(22.0 / d, 32.0 / d, 42.0 / d, 52.0 / d);
  output.color2 = vec4f(33.0 / d, 43.0 / d, 53.0 / d, 63.0 / d);
  output.color3 = vec4f(44.0 / d, 54.0 / d, 64.0 / d, 74.0 / d);
  return output;
}
)";

constexpr std::string_view kShader = R"(
@vertex fn main_vs(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f {
  let pos = array(vec2f(-1.0, 1.01), vec2f(1.01, -1.0), vec2f(-1.0, -1.0));
  return vec4f(pos[VertexIndex], 0.0, 1.0);
}

struct Output {
  @location(0) color0 : vec4f,
};

@fragment fn main_fs() -> Output {
  var output : Output;
  output.color0 = vec4f(11.0 / 255.0, 21.0 / 255.0, 31.0 / 255.0, 41.0 / 255.0);
  return output;
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createTexture(AllFeaturesMaxLimitsGpuTest& t, uint32_t mipLevel) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kSize << mipLevel, kSize << mipLevel, kDepth << mipLevel};
    desc.mipLevelCount = mipLevel + 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_3D;
    desc.format = kFormat;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPUTextureView createMipView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, uint32_t mipLevel) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.baseMipLevel = mipLevel;
    desc.mipLevelCount = 1;
    return t.createViewTracked(texture, desc);
}

WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule module = t.createShaderModuleTracked(kShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = kFormat;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("main_fs");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView("main_vs");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

bool closeEnough(uint8_t actual, uint8_t expected) {
    return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= static_cast<int>(kMaxDiff);
}

const std::array<uint8_t, 4>& expectedBytes(uint32_t z, uint32_t y, uint32_t x, uint32_t depthSlice) {
    if (z != depthSlice) {
        return kZeroBytes;
    }
    return x <= y ? kRenderBytes : kClearBytes;
}

void expectTextureContent(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, uint32_t mipLevel, uint32_t depthSlice) {
    const uint32_t mipWidth = kSize;
    const uint32_t mipHeight = kSize;
    const uint32_t mipDepth = kDepth;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(mipWidth * kBytesPerPixel, kBytesPerRowAlignment));
    const uint32_t rowsPerImage = mipHeight;
    const uint64_t bytesPerImage = static_cast<uint64_t>(bytesPerRow) * rowsPerImage;
    const uint64_t byteLength = alignTo(bytesPerImage * mipDepth, kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = mipLevel;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = rowsPerImage;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUExtent3D copySize = WGPUExtent3D{mipWidth, mipHeight, mipDepth};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t z = 0; z < mipDepth; ++z) {
                for (uint32_t y = 0; y < mipHeight; ++y) {
                    for (uint32_t x = 0; x < mipWidth; ++x) {
                        const uint64_t offset = static_cast<uint64_t>(z) * bytesPerImage
                            + static_cast<uint64_t>(y) * bytesPerRow
                            + static_cast<uint64_t>(x) * kBytesPerPixel;
                        if (offset + kBytesPerPixel > len) {
                            std::ostringstream message;
                            message << "rgba8unorm texel offset out of range: " << offset;
                            return message.str();
                        }
                        const std::array<uint8_t, 4>& expected = expectedBytes(z, y, x, depthSlice);
                        for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                            if (!closeEnough(actual[offset + channel], expected[channel])) {
                                std::ostringstream message;
                                message << "rgba8unorm mismatch at (" << x << ", " << y << ", " << z
                                        << ") channel " << channel
                                        << ": expected " << static_cast<int>(expected[channel])
                                        << ", got " << static_cast<int>(actual[offset + channel]);
                                return message.str();
                            }
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void runOneColorAttachmentMipLevels(AllFeaturesMaxLimitsGpuTest& t) {
    const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
    const uint32_t depthSlice = static_cast<uint32_t>(t.param<int64_t>("depthSlice"));

    WGPUTexture texture = createTexture(t, mipLevel);
    WGPUTextureView view = createMipView(t, texture, mipLevel);
    WGPURenderPipeline pipeline = createPipeline(t);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.depthSlice = depthSlice;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = kClearValue;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectTextureContent(t, texture, mipLevel, depthSlice);
}

CTS_TEST(g, "one_color_attachment,mip_levels")
    .params([](ParamsBuilder u) {
        return u.combine("mipLevel", {0, 1, 2})
            .combine("depthSlice", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runOneColorAttachmentMipLevels(t);
    });

// ---------------------------------------------------------------------------
// multiple_color_attachments,same_mip_level helpers
// ---------------------------------------------------------------------------

// Create a 4×4×16 3D rgba8unorm texture for the multi-attachment test.
WGPUTexture createMultiAttachmentTexture(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kSize, kSize, kMultiDepthSlices};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_3D;
    desc.format = kFormat;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    return t.createTextureTracked(desc);
}

// Create a 3D texture view (dimension=3d, baseMipLevel=0, mipLevelCount=1) for depthSlice selection.
WGPUTextureView create3DView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.dimension = WGPUTextureViewDimension_3D;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = 1;
    return t.createViewTracked(texture, desc);
}

// Create a render pipeline with 4 color targets (all rgba8unorm) and the 4-output fragment shader.
WGPURenderPipeline createMultiAttachmentPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule module = t.createShaderModuleTracked(kMultiAttachmentShader);

    std::array<WGPUColorTargetState, kMultiAttachmentCount> colorTargets;
    for (uint32_t i = 0; i < kMultiAttachmentCount; ++i) {
        colorTargets[i] = WGPU_COLOR_TARGET_STATE_INIT;
        colorTargets[i].format = kFormat;
    }

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("main_fs");
    fragment.targetCount = kMultiAttachmentCount;
    fragment.targets = colorTargets.data();

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView("main_vs");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Verify all 16 slices of the 4×4×16 texture:
//   z < 4 && x <= y → kLocationBytes[z]; z < 4 && x > y → kClearBytes; z >= 4 → kZeroBytes.
void expectMultiAttachmentContent(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    const uint32_t mipWidth = kSize;
    const uint32_t mipHeight = kSize;
    const uint32_t mipDepth = kMultiDepthSlices;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(mipWidth * kBytesPerPixel, kBytesPerRowAlignment));
    const uint32_t rowsPerImage = mipHeight;
    const uint64_t bytesPerImage = static_cast<uint64_t>(bytesPerRow) * rowsPerImage;
    const uint64_t byteLength = alignTo(bytesPerImage * mipDepth, kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = rowsPerImage;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUExtent3D copySize = WGPUExtent3D{mipWidth, mipHeight, mipDepth};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t z = 0; z < mipDepth; ++z) {
                for (uint32_t y = 0; y < mipHeight; ++y) {
                    for (uint32_t x = 0; x < mipWidth; ++x) {
                        const uint64_t offset = static_cast<uint64_t>(z) * bytesPerImage
                            + static_cast<uint64_t>(y) * bytesPerRow
                            + static_cast<uint64_t>(x) * kBytesPerPixel;
                        if (offset + kBytesPerPixel > len) {
                            std::ostringstream message;
                            message << "rgba8unorm texel offset out of range: " << offset;
                            return message.str();
                        }
                        // Select expected bytes:
                        //   z >= 4 → never attached, lazy-zero-init → kZeroBytes
                        //   z < 4 && x <= y → rendered triangle → kLocationBytes[z]
                        //   z < 4 && x > y  → cleared but not rendered → kClearBytes
                        const std::array<uint8_t, 4>& expected =
                            (z >= kMultiAttachmentCount) ? kZeroBytes
                            : (x <= y)                   ? kLocationBytes[z]
                                                         : kClearBytes;
                        for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                            if (!closeEnough(actual[offset + channel], expected[channel])) {
                                std::ostringstream message;
                                message << "rgba8unorm mismatch at (" << x << ", " << y << ", " << z
                                        << ") channel " << channel
                                        << ": expected " << static_cast<int>(expected[channel])
                                        << ", got " << static_cast<int>(actual[offset + channel]);
                                return message.str();
                            }
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void runMultipleColorAttachmentsSameMipLevel(AllFeaturesMaxLimitsGpuTest& t) {
    // Fixed params: format=rgba8unorm, mipLevel=0, sameTexture=true, samePass=true, attachmentCount=4.
    WGPUTexture texture = createMultiAttachmentTexture(t);
    // One shared 3D view; depthSlice is set per attachment in the render pass.
    WGPUTextureView view = create3DView(t, texture);
    WGPURenderPipeline pipeline = createMultiAttachmentPipeline(t);

    // 4 color attachments, each targeting slice i of the same 3D texture.
    std::array<WGPURenderPassColorAttachment, kMultiAttachmentCount> colorAttachments;
    for (uint32_t i = 0; i < kMultiAttachmentCount; ++i) {
        colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachments[i].view = view;
        colorAttachments[i].depthSlice = i;
        colorAttachments[i].loadOp = WGPULoadOp_Clear;
        colorAttachments[i].storeOp = WGPUStoreOp_Store;
        colorAttachments[i].clearValue = kClearValue;
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = kMultiAttachmentCount;
    passDesc.colorAttachments = colorAttachments.data();

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectMultiAttachmentContent(t, texture);
}

CTS_TEST(g, "multiple_color_attachments,same_mip_level")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runMultipleColorAttachmentsSameMipLevel(t);
    });

} // namespace
