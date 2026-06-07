// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/clear_value.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pass,clear_value",
    "Render pass clear value tests.");

// Fullscreen quad vertex shader: 6 vertices covering the whole clip space.
constexpr std::string_view kFullscreenQuadVertexShader = R"(
@vertex fn main(@builtin(vertex_index) vi : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 6>(
    vec2<f32>(-1.0,  1.0), vec2<f32>(-1.0, -1.0), vec2<f32>( 1.0,  1.0),
    vec2<f32>(-1.0, -1.0), vec2<f32>( 1.0,  1.0), vec2<f32>( 1.0, -1.0));
  return vec4<f32>(pos[vi], 0.0, 1.0);
}
)";

// Fragment shader: always outputs green.
constexpr std::string_view kGreenFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.0, 1.0, 0.0, 1.0);
}
)";

constexpr uint32_t kWidth = 1;
constexpr uint32_t kHeight = 1;
constexpr uint32_t kBytesPerPixel = 4;

// Stencil formats to test; all have an 8-bit stencil aspect.
constexpr std::array<WGPUTextureFormat, 3> kStencilTextureFormats = {{
    WGPUTextureFormat_Stencil8,
    WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_Depth32FloatStencil8,
}};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

std::vector<Value> stencilFormatValues() {
    return formatIdentifierValues(kStencilTextureFormats);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// Run the stencil_clear_value test for the given parameters.
//
// Strategy:
//   1. Clear a 1x1 stencil attachment to stencilClearValue (hardware masks to low 8 bits).
//   2. Draw a fullscreen quad using stencilCompare:equal + setStencilReference.
//   3. Verify the color attachment is green (stencil test passed).
//   4. Copy the stencil aspect to a buffer and verify byte 0 == expectedStencilValue.
void runStencilClearValueTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat stencilFormat,
    uint32_t stencilClearValue,
    bool applyStencilClearValueAsStencilReferenceValue) {
    // All three formats have an 8-bit stencil aspect.
    const uint32_t kStencilMask = 0xff;
    const uint32_t expectedStencilValue = stencilClearValue & kStencilMask;

    // Both branches of applyStencilClearValueAsStencilReferenceValue mask to the same 8 low bits,
    // so the 'equal' test always passes — the green draw always happens.
    const uint32_t stencilReference = applyStencilClearValueAsStencilReferenceValue
        ? stencilClearValue
        : expectedStencilValue;

    const bool hasDepth = isDepthTextureFormat(stencilFormat);

    // Create textures.
    WGPUTextureDescriptor stencilTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    stencilTexDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    stencilTexDesc.mipLevelCount = 1;
    stencilTexDesc.sampleCount = 1;
    stencilTexDesc.dimension = WGPUTextureDimension_2D;
    stencilTexDesc.format = stencilFormat;
    stencilTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture stencilTexture = t.createTextureTracked(stencilTexDesc);

    WGPUTextureDescriptor colorTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    colorTexDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    colorTexDesc.mipLevelCount = 1;
    colorTexDesc.sampleCount = 1;
    colorTexDesc.dimension = WGPUTextureDimension_2D;
    colorTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    colorTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture colorTexture = t.createTextureTracked(colorTexDesc);

    // Create texture views.
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView stencilView  = t.createViewTracked(stencilTexture, viewDesc);
    WGPUTextureView colorView    = t.createViewTracked(colorTexture, viewDesc);

    // Build the render pipeline with layout:auto, triangle-list, stencilCompare:equal.
    WGPUShaderModule vertexModule   = t.createShaderModuleTracked(kFullscreenQuadVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kGreenFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPUStencilFaceState stencilFace = WGPU_STENCIL_FACE_STATE_INIT;
    stencilFace.compare    = WGPUCompareFunction_Equal;
    stencilFace.failOp     = WGPUStencilOperation_Keep;
    stencilFace.depthFailOp = WGPUStencilOperation_Keep;
    stencilFace.passOp     = WGPUStencilOperation_Keep;

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format            = stencilFormat;
    depthStencil.depthCompare      = WGPUCompareFunction_Always;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencil.stencilFront      = stencilFace;
    depthStencil.stencilBack       = stencilFace;
    depthStencil.stencilReadMask   = 0xff;
    depthStencil.stencilWriteMask  = 0xff;

    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    // layout = nullptr → auto
    pipelineDesc.vertex.module      = vertexModule;
    pipelineDesc.vertex.entryPoint  = stringView("main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count  = 1;
    pipelineDesc.fragment           = &fragment;
    pipelineDesc.depthStencil       = &depthStencil;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);

    // Set up the depth-stencil attachment.
    WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    dsAttachment.view               = stencilView;
    dsAttachment.stencilLoadOp      = WGPULoadOp_Clear;
    dsAttachment.stencilStoreOp     = WGPUStoreOp_Store;
    dsAttachment.stencilClearValue  = stencilClearValue;
    dsAttachment.depthClearValue    = 0.0f;
    // Only set depth ops for formats that actually have a depth aspect.
    if (hasDepth) {
        dsAttachment.depthLoadOp  = WGPULoadOp_Clear;
        dsAttachment.depthStoreOp = WGPUStoreOp_Store;
    }

    // Set up the color attachment: clear to red (1,0,0,1); replaced by green on stencil pass.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view        = colorView;
    colorAttachment.loadOp      = WGPULoadOp_Clear;
    colorAttachment.storeOp     = WGPUStoreOp_Store;
    colorAttachment.clearValue  = WGPUColor{1.0, 0.0, 0.0, 1.0};

    // Stencil readback buffer: 4 bytes (minimum buffer size), check byte 0.
    WGPUBufferDescriptor stencilBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    stencilBufDesc.size  = 4;
    stencilBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer stencilBuf = t.createBufferTracked(stencilBufDesc);

    // Encode: render pass + stencil-aspect copyTextureToBuffer.
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount     = 1;
    passDesc.colorAttachments         = &colorAttachment;
    passDesc.depthStencilAttachment   = &dsAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetStencilReference(pass, stencilReference);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Copy the stencil aspect to a 4-byte buffer for readback.
    WGPUTexelCopyTextureInfo stencilSrc = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    stencilSrc.texture  = stencilTexture;
    stencilSrc.mipLevel = 0;
    stencilSrc.origin   = WGPUOrigin3D{0, 0, 0};
    stencilSrc.aspect   = WGPUTextureAspect_StencilOnly;

    WGPUTexelCopyBufferInfo stencilDst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    stencilDst.buffer              = stencilBuf;
    stencilDst.layout.offset       = 0;
    stencilDst.layout.bytesPerRow  = static_cast<uint32_t>(
        alignTo(static_cast<uint64_t>(kWidth), kBytesPerRowAlignment));
    stencilDst.layout.rowsPerImage = kHeight;

    WGPUExtent3D copyExtent = WGPUExtent3D{kWidth, kHeight, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &stencilSrc, &stencilDst, &copyExtent);

    submit(t, encoder);

    // Verify color pixel (0,0) == green {0, 255, 0, 255}.
    const uint32_t colorBytesPerRow = static_cast<uint32_t>(
        alignTo(static_cast<uint64_t>(kWidth) * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t colorByteLength = alignTo(
        static_cast<uint64_t>(colorBytesPerRow) * (kHeight - 1)
            + static_cast<uint64_t>(kWidth) * kBytesPerPixel,
        kBufferCopyAlignment);

    WGPUBufferDescriptor colorBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    colorBufDesc.size  = colorByteLength;
    colorBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer colorBuf = t.createBufferTracked(colorBufDesc);

    WGPUCommandEncoder colorEncoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(colorEncoder, colorTexture, colorBuf, colorBytesPerRow,
                          WGPUExtent3D{kWidth, kHeight, 1});
    submit(t, colorEncoder);

    // Color check: pixel (0,0) must be green {0, 255, 0, 255}.
    t.expectGPUBufferValuesPassCheck(
        colorBuf,
        [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            constexpr std::array<uint8_t, 4> kGreen = {0, 255, 0, 255};
            const uint64_t offset = 0; // pixel (0,0) — row 0, col 0
            if (offset + kBytesPerPixel > len) {
                std::ostringstream msg;
                msg << "color pixel offset out of range";
                return msg.str();
            }
            for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
                if (actual[offset + ch] != kGreen[ch]) {
                    std::ostringstream msg;
                    msg << "color mismatch at (0,0) channel " << ch
                        << ": expected " << static_cast<int>(kGreen[ch])
                        << ", got " << static_cast<int>(actual[offset + ch]);
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(colorByteLength));

    // Stencil check: byte 0 must equal expectedStencilValue.
    const uint8_t expectedByte = static_cast<uint8_t>(expectedStencilValue);
    t.expectGPUBufferValuesPassCheck(
        stencilBuf,
        [expectedByte](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 1) {
                return std::string("stencil buffer too small");
            }
            if (actual[0] != expectedByte) {
                std::ostringstream msg;
                msg << "stencil mismatch: expected " << static_cast<int>(expectedByte)
                    << ", got " << static_cast<int>(actual[0]);
                return msg.str();
            }
            return std::nullopt;
        },
        0,
        4);
}

CTS_TEST(g, "stencil_clear_value")
    .params([](ParamsBuilder u) {
        return u.combine("stencilFormat", stencilFormatValues())
            .combine("stencilClearValue", {
                static_cast<int64_t>(0),
                static_cast<int64_t>(1),
                static_cast<int64_t>(0xff),
                static_cast<int64_t>(0x102),
                static_cast<int64_t>(0x10003),
            })
            .combine("applyStencilClearValueAsStencilReferenceValue", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("stencilFormat"));
        t.skipIfTextureFormatNotSupported(format);
        const uint32_t stencilClearValue =
            static_cast<uint32_t>(t.param<int64_t>("stencilClearValue"));
        const bool applyAsRef =
            t.param<bool>("applyStencilClearValueAsStencilReferenceValue");
        runStencilClearValueTest(t, format, stencilClearValue, applyAsRef);
    });

CTS_TEST(g, "loaded")
    .unimplemented("loaded stencil clear value test is deferred (upstream stub)");

} // namespace
