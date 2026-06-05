// Ported from gpuweb/cts src/webgpu/api/operation/sampling/filter_mode.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,sampling,filter_mode",
    "Texture filter mode operation tests.");

constexpr uint32_t kCheckerTextureSize = 2;
constexpr uint32_t kNearestRenderSize = 6;
constexpr uint32_t kBytesPerPixel = 4;
constexpr std::array<uint8_t, 16> kCheckerTextureData = {{
    255, 255, 255, 255,
    0, 0, 0, 255,
    0, 0, 0, 255,
    255, 255, 255, 255,
}};

using NearestTable = std::array<std::array<uint8_t, kNearestRenderSize>, kNearestRenderSize>;

constexpr NearestTable kNearestURepeatVRepeat = {{
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
}};

constexpr NearestTable kNearestURepeatVClamped = {{
    {{1, 0, 1, 0, 1, 0}},
    {{1, 0, 1, 0, 1, 0}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{0, 1, 0, 1, 0, 1}},
    {{0, 1, 0, 1, 0, 1}},
}};

constexpr NearestTable kNearestURepeatVMirror = {{
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
}};

constexpr NearestTable kNearestUClampedVRepeat = {{
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
}};

constexpr NearestTable kNearestUClampedVClamped = {{
    {{1, 1, 1, 0, 0, 0}},
    {{1, 1, 1, 0, 0, 0}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{0, 0, 0, 1, 1, 1}},
    {{0, 0, 0, 1, 1, 1}},
}};

constexpr NearestTable kNearestUClampedVMirror = {{
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
}};

constexpr NearestTable kNearestUMirrorVRepeat = {{
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
}};

constexpr NearestTable kNearestUMirrorVClamped = {{
    {{0, 1, 1, 0, 0, 1}},
    {{0, 1, 1, 0, 0, 1}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{1, 0, 0, 1, 1, 0}},
    {{1, 0, 0, 1, 1, 0}},
}};

constexpr NearestTable kNearestUMirrorVMirror = {{
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
}};

constexpr std::string_view kMagFilterNearestVertexShader = R"(
struct VertexOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vi : u32,
           @builtin(instance_index) ii: u32) -> VertexOut {
  const grid = vec2f(6, 6);
  const posBases = array(
    vec2f(1, 1), vec2f(1, -1), vec2f(-1, -1),
    vec2f(1, 1), vec2f(-1, -1), vec2f(-1, 1),
  );
  const uvBases = array(
    vec2f(1., 0.), vec2f(1., 1.), vec2f(0., 1.),
    vec2f(1., 0.), vec2f(0., 1.), vec2f(0., 0.),
  );

  // Compute the offset of instance plane.
  let cell = vec2f(f32(ii) % grid.x, floor(f32(ii) / grid.y));
  let cellOffset = cell / grid * 2;
  let pos = (posBases[vi] + 1) / grid - 1 + cellOffset;

  // Compute the offset of the UVs.
  let uvBase = uvBases[vi] * 0.25 + vec2f(-0.875, 1.625);
  const uvPerPixelOffset = vec2f(0.5, -0.5);
  return VertexOut(vec4f(pos, 0.0, 1.0), uvBase + uvPerPixelOffset * cell);
}
)";

constexpr std::string_view kMinFilterNearestVertexShader = R"(
struct VertexOut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vi : u32,
           @builtin(instance_index) ii: u32) -> VertexOut {
  const grid = vec2f(6, 6);
  const posBases = array(
    vec2f(.5, .5), vec2f(.5, -.5), vec2f(-.5, -.5),
    vec2f(.5, .5), vec2f(-.5, -.5), vec2f(-.5, .5),
  );
  // Choose UVs so that the quad ends up being the 6x6 texture.
  const uvBases = array(
    vec2f(2., -1.), vec2f(2., 2.), vec2f(-1., 2.),
    vec2f(2., -1.), vec2f(-1., 2.), vec2f(-1., -1.),
  );

  let cell = vec2f(f32(ii) % grid.x, floor(f32(ii) / grid.y));

  // Compute the offset of instance plane (pre-grid transformation).
  const constantPlaneOffset = vec2f(5. / 12., 5. / 12.);
  const perPixelOffset = vec2f(1. / 6., 1. / 6.);
  let posBase = posBases[vi] + constantPlaneOffset - perPixelOffset * cell;

  // Apply the grid transformation.
  let cellOffset = cell / grid * 2;
  let absPos = (posBase + 1) / grid - 1 + cellOffset;

  return VertexOut(vec4f(absPos, 0.0, 1.0), uvBases[vi]);
}
)";

constexpr std::string_view kTextureSampleFragmentShader = R"(
@group(0) @binding(0) var s : sampler;
@group(0) @binding(1) var t : texture_2d<f32>;

@fragment
fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
  return textureSample(t, s, uv);
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUAddressMode parseAddressMode(std::string_view value) {
    if (value == "clamp-to-edge") {
        return WGPUAddressMode_ClampToEdge;
    }
    if (value == "repeat") {
        return WGPUAddressMode_Repeat;
    }
    if (value == "mirror-repeat") {
        return WGPUAddressMode_MirrorRepeat;
    }
    std::abort();
}

std::vector<Value> addressModeValues() {
    return {
        Value("clamp-to-edge"),
        Value("repeat"),
        Value("mirror-repeat"),
    };
}

const NearestTable& expectedNearestTable(std::string_view addressModeU, std::string_view addressModeV) {
    if (addressModeU == "clamp-to-edge") {
        if (addressModeV == "clamp-to-edge") {
            return kNearestUClampedVClamped;
        }
        if (addressModeV == "repeat") {
            return kNearestUClampedVRepeat;
        }
        if (addressModeV == "mirror-repeat") {
            return kNearestUClampedVMirror;
        }
    }
    if (addressModeU == "repeat") {
        if (addressModeV == "clamp-to-edge") {
            return kNearestURepeatVClamped;
        }
        if (addressModeV == "repeat") {
            return kNearestURepeatVRepeat;
        }
        if (addressModeV == "mirror-repeat") {
            return kNearestURepeatVMirror;
        }
    }
    if (addressModeU == "mirror-repeat") {
        if (addressModeV == "clamp-to-edge") {
            return kNearestUMirrorVClamped;
        }
        if (addressModeV == "repeat") {
            return kNearestUMirrorVRepeat;
        }
        if (addressModeV == "mirror-repeat") {
            return kNearestUMirrorVMirror;
        }
    }
    std::abort();
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createCheckerSampleTexture(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kCheckerTextureSize, kCheckerTextureSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture texture = t.createTextureTracked(desc);

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = kCheckerTextureSize * kBytesPerPixel;
    layout.rowsPerImage = kCheckerTextureSize;
    t.queueWriteTexture(
        texture,
        WGPUExtent3D{kCheckerTextureSize, kCheckerTextureSize, 1},
        layout,
        kCheckerTextureData.data(),
        kCheckerTextureData.size());
    return texture;
}

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kNearestRenderSize, kNearestRenderSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPUBindGroupLayout createFilterBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPUBindGroup createFilterBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUSampler sampler,
    WGPUTextureView sampleTextureView) {
    std::array<WGPUBindGroupEntry, 2> entries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].sampler = sampler;
    entries[1].binding = 1;
    entries[1].textureView = sampleTextureView;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

WGPURenderPipeline createFilterPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUPipelineLayout layout,
    std::string_view vertexShaderSrc) {
    const std::string shaderSource = std::string(vertexShaderSrc) + std::string(kTextureSampleFragmentShader);
    WGPUShaderModule module = t.createShaderModuleTracked(shaderSource);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView("vs_main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUTexture runFilterRenderPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUSampler sampler,
    std::string_view vertexShaderSrc,
    uint32_t renderSize = kNearestRenderSize,
    uint32_t vertexCount = 6,
    uint32_t instanceCount = kNearestRenderSize * kNearestRenderSize) {
    WGPUTexture sampleTexture = createCheckerSampleTexture(t);
    WGPUTexture renderTexture = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView sampleTextureView = t.createViewTracked(sampleTexture, viewDesc);
    WGPUTextureView renderTextureView = t.createViewTracked(renderTexture, viewDesc);

    WGPUBindGroupLayout bindGroupLayout = createFilterBindGroupLayout(t);
    WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
    WGPURenderPipeline pipeline = createFilterPipeline(t, pipelineLayout, vertexShaderSrc);
    WGPUBindGroup bindGroup = createFilterBindGroup(t, bindGroupLayout, sampler, sampleTextureView);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = renderTextureView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    (void)renderSize;
    return renderTexture;
}

void expectNearestTableInTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const NearestTable& expected) {
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(alignTo(kNearestRenderSize * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kNearestRenderSize - 1)
            + static_cast<uint64_t>(kNearestRenderSize) * kBytesPerPixel,
        kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, bytesPerRow, WGPUExtent3D{kNearestRenderSize, kNearestRenderSize, 1});
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t y = 0; y < kNearestRenderSize; ++y) {
                for (uint32_t x = 0; x < kNearestRenderSize; ++x) {
                    const uint64_t offset = static_cast<uint64_t>(y) * bytesPerRow
                        + static_cast<uint64_t>(x) * kBytesPerPixel;
                    if (offset + kBytesPerPixel > len) {
                        std::ostringstream message;
                        message << "rgba8unorm pixel offset out of range: " << offset;
                        return message.str();
                    }
                    const uint8_t color = expected[y][x] * 255;
                    const std::array<uint8_t, 4> expectedPixel = {{color, color, color, 255}};
                    for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                        const uint8_t got = actual[offset + channel];
                        if (got != expectedPixel[channel]) {
                            std::ostringstream message;
                            message << "rgba8unorm mismatch at (" << x << ", " << y << ") channel "
                                    << channel << ": expected " << static_cast<int>(expectedPixel[channel])
                                    << ", got " << static_cast<int>(got);
                            return message.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

WGPUSampler createNearestSampler(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUAddressMode addressModeU,
    WGPUAddressMode addressModeV,
    bool magFilter) {
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.addressModeU = addressModeU;
    desc.addressModeV = addressModeV;
    if (magFilter) {
        desc.magFilter = WGPUFilterMode_Nearest;
    } else {
        desc.minFilter = WGPUFilterMode_Nearest;
    }
    return t.createSamplerTracked(desc);
}

void runNearestTest(AllFeaturesMaxLimitsGpuTest& t, bool magFilter) {
    const std::string format = t.param<std::string>("format");
    t.expect(format == "rgba8unorm", "T34 ports rgba8unorm only");
    const std::string addressModeU = t.param<std::string>("addressModeU");
    const std::string addressModeV = t.param<std::string>("addressModeV");

    WGPUSampler sampler = createNearestSampler(
        t,
        parseAddressMode(addressModeU),
        parseAddressMode(addressModeV),
        magFilter);
    WGPUTexture render = runFilterRenderPipeline(
        t,
        sampler,
        magFilter ? kMagFilterNearestVertexShader : kMinFilterNearestVertexShader);
    expectNearestTableInTexture(t, render, expectedNearestTable(addressModeU, addressModeV));
}

CTS_TEST(g, "magFilter,nearest")
    .params([](ParamsBuilder u) {
        return u.combine("format", {Value("rgba8unorm")})
            .beginSubcases()
            .combine("addressModeU", addressModeValues())
            .combine("addressModeV", addressModeValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runNearestTest(t, true);
    });

CTS_TEST(g, "magFilter,linear")
    .unimplemented("linear/mipmap filter + format matrix deferred to V6b/V6c");

CTS_TEST(g, "minFilter,nearest")
    .params([](ParamsBuilder u) {
        return u.combine("format", {Value("rgba8unorm")})
            .beginSubcases()
            .combine("addressModeU", addressModeValues())
            .combine("addressModeV", addressModeValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runNearestTest(t, false);
    });

CTS_TEST(g, "minFilter,linear")
    .unimplemented("linear/mipmap filter + format matrix deferred to V6b/V6c");

CTS_TEST(g, "mipmapFilter")
    .unimplemented("linear/mipmap filter + format matrix deferred to V6b/V6c");

} // namespace
