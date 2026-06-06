// Ported from gpuweb/cts src/webgpu/api/operation/vertex_state/index_format.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
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

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,vertex_state,index_format",
    "Index format vertex state operation tests.");

constexpr uint32_t kWidth = 8;
constexpr uint32_t kHeight = 4;
constexpr uint32_t kBytesPerPixel = 1;

using Raster8x4 = std::array<std::array<uint8_t, kWidth>, kHeight>;

constexpr Raster8x4 kSquare = {{
    {{0, 0, 0, 0, 1, 1, 1, 1}},
    {{0, 0, 0, 0, 1, 1, 1, 1}},
    {{0, 0, 0, 0, 1, 1, 1, 1}},
    {{0, 0, 0, 0, 1, 1, 1, 1}},
}};

constexpr Raster8x4 kBottomLeftTriangle = {{
    {{0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 1, 0, 0, 0}},
    {{0, 0, 0, 0, 1, 1, 0, 0}},
    {{0, 0, 0, 0, 1, 1, 1, 0}},
}};

constexpr Raster8x4 kNothing = {{
    {{0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0}},
}};

constexpr std::array<uint32_t, 10> kIndices = {{1, 2, 0, 0, 0, 0, 0, 1, 3, 0}};

constexpr std::string_view kVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 4>(
    vec2<f32>(0.01, 0.98),
    vec2<f32>(0.99, -0.98),
    vec2<f32>(0.99, 0.98),
    vec2<f32>(0.01, -0.98)
  );
  if (VertexIndex == 0xFFFFu || VertexIndex == 0xFFFFFFFFu) {
    return vec4<f32>(-0.99, -0.98, 0.0, 1.0);
  }
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentShader = R"(
@fragment fn main() -> @location(0) u32 {
  return 1u;
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

const Raster8x4& expectedRaster(std::string_view name) {
    if (name == "square") {
        return kSquare;
    }
    if (name == "triangle") {
        return kBottomLeftTriangle;
    }
    if (name == "nothing") {
        return kNothing;
    }
    std::abort();
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kWidth, kHeight, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_R8Uint;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPUPipelineLayout createEmptyPipelineLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 0;
    desc.bindGroupLayouts = nullptr;
    return t.createPipelineLayoutTracked(desc);
}

WGPURenderPipeline makeRenderPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);
    WGPUPipelineLayout layout = createEmptyPipelineLayout(t);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_R8Uint;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPUBuffer createIndexBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUIndexFormat format) {
    if (format == WGPUIndexFormat_Uint16) {
        std::vector<uint16_t> indices;
        indices.reserve(kIndices.size());
        for (uint32_t index : kIndices) {
            indices.push_back(static_cast<uint16_t>(index));
        }
        return t.makeBufferWithContents(indices.data(), indices.size() * sizeof(uint16_t), WGPUBufferUsage_Index);
    }

    std::vector<uint32_t> indices(kIndices.begin(), kIndices.end());
    return t.makeBufferWithContents(indices.data(), indices.size() * sizeof(uint32_t), WGPUBufferUsage_Index);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectRasterInTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const Raster8x4& expected) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(kWidth * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kHeight - 1) + static_cast<uint64_t>(kWidth) * kBytesPerPixel,
        kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, bytesPerRow, WGPUExtent3D{kWidth, kHeight, 1});
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t row = 0; row < kHeight; ++row) {
                for (uint32_t col = 0; col < kWidth; ++col) {
                    const uint64_t offset = static_cast<uint64_t>(row) * bytesPerRow + col;
                    if (offset >= len) {
                        std::ostringstream message;
                        message << "r8uint texel offset out of range: " << offset;
                        return message.str();
                    }
                    const uint8_t got = actual[offset];
                    const uint8_t want = expected[row][col];
                    if (got != want) {
                        std::ostringstream message;
                        message << "r8uint mismatch at (" << col << ", " << row << "): expected "
                                << static_cast<int>(want) << ", got " << static_cast<int>(got);
                        return message.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void runIndexFormatTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUIndexFormat format,
    uint32_t indexOffset,
    uint32_t indexCount,
    const Raster8x4& expected) {
    WGPUBuffer indexBuffer = createIndexBuffer(t, format);
    WGPUTexture target = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView targetView = t.createViewTracked(target, viewDesc);
    WGPURenderPipeline pipeline = makeRenderPipeline(t);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, format, indexOffset, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectRasterInTexture(t, target, expected);
}

void runFromParams(AllFeaturesMaxLimitsGpuTest& t, WGPUIndexFormat format) {
    const uint32_t indexOffset = static_cast<uint32_t>(t.param<int64_t>("indexOffset"));
    const uint32_t indexCount = static_cast<uint32_t>(t.param<int64_t>("indexCount"));
    const Raster8x4& expected = expectedRaster(t.param<std::string>("expected"));
    runIndexFormatTest(t, format, indexOffset, indexCount, expected);
}

CTS_TEST(g, "index_format,uint16")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"indexOffset", 0}, {"indexCount", 10}, {"expected", "square"}},
            ParamRecord{{"indexOffset", 6}, {"indexCount", 6}, {"expected", "triangle"}},
            ParamRecord{{"indexOffset", 18}, {"indexCount", 0}, {"expected", "nothing"}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFromParams(t, WGPUIndexFormat_Uint16);
    });

CTS_TEST(g, "index_format,uint32")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"indexOffset", 0}, {"indexCount", 10}, {"expected", "square"}},
            ParamRecord{{"indexOffset", 12}, {"indexCount", 7}, {"expected", "triangle"}},
            ParamRecord{{"indexOffset", 36}, {"indexCount", 0}, {"expected", "nothing"}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFromParams(t, WGPUIndexFormat_Uint32);
    });

} // namespace
