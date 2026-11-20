// Ported from gpuweb/cts src/webgpu/api/operation/vertex_state/index_format.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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

// Pipelines depend only on (topology, stripIndexFormat), so they are cached per device.
struct IndexFormatPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<std::pair<int, int>, WGPURenderPipeline>> byDevice;

    ~IndexFormatPipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                wgpuRenderPipelineRelease(pipeline);
            }
        }
    }
};

// Upstream IndexFormatTest.makeRenderPipeline(topology, stripIndexFormat?).
WGPURenderPipeline makeRenderPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList,
    WGPUIndexFormat stripIndexFormat = WGPUIndexFormat_Undefined) {
    auto& pipelines = deviceScoped<IndexFormatPipelineCache>().byDevice[t.device()];
    const std::pair<int, int> key{static_cast<int>(topology), static_cast<int>(stripIndexFormat)};
    auto it = pipelines.find(key);
    if (it != pipelines.end()) {
        return it->second;
    }

    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.bindGroupLayoutCount = 0;
    layoutDesc.bindGroupLayouts = nullptr;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

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
    desc.primitive.topology = topology;
    desc.primitive.stripIndexFormat = stripIndexFormat;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(t.device(), &desc);
    pipelines.emplace(key, pipeline);
    return pipeline;
}

// Upstream createIndexBuffer(indices, indexFormat): -1 is uploaded as 0xFFFF / 0xFFFFFFFF.
WGPUBuffer createIndexBuffer(AllFeaturesMaxLimitsGpuTest& t,
                             const std::vector<int64_t>& values,
                             WGPUIndexFormat format) {
    if (format == WGPUIndexFormat_Uint16) {
        std::vector<uint16_t> indices;
        indices.reserve(values.size());
        for (int64_t index : values) {
            indices.push_back(static_cast<uint16_t>(index));
        }
        return t.makeBufferWithContents(indices.data(), indices.size() * sizeof(uint16_t), WGPUBufferUsage_Index);
    }

    std::vector<uint32_t> indices;
    indices.reserve(values.size());
    for (int64_t index : values) {
        indices.push_back(static_cast<uint32_t>(index));
    }
    return t.makeBufferWithContents(indices.data(), indices.size() * sizeof(uint32_t), WGPUBufferUsage_Index);
}

WGPUBuffer createIndexBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUIndexFormat format) {
    return createIndexBuffer(t, std::vector<int64_t>(kIndices.begin(), kIndices.end()), format);
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

// Begins a render pass that clears `targetView` to 0.
WGPURenderPassEncoder beginClearPass(AllFeaturesMaxLimitsGpuTest& t,
                                     WGPUCommandEncoder encoder,
                                     WGPUTextureView targetView) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    return t.beginRenderPassTracked(encoder, &passDesc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

// Upstream IndexFormatTest.run(): strip topologies get `stripIndexFormat = indexFormat`.
void runIndexed(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBuffer indexBuffer,
    uint32_t indexCount,
    WGPUIndexFormat format,
    uint32_t indexOffset,
    WGPUPrimitiveTopology topology,
    const Raster8x4& expected) {
    const bool isStrip =
        topology == WGPUPrimitiveTopology_LineStrip || topology == WGPUPrimitiveTopology_TriangleStrip;
    WGPURenderPipeline pipeline =
        makeRenderPipeline(t, topology, isStrip ? format : WGPUIndexFormat_Undefined);
    WGPUTexture target = createRenderTarget(t);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginClearPass(t, encoder, createView(t, target));
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, format, indexOffset, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectRasterInTexture(t, target, expected);
}

void runIndexFormatTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUIndexFormat format,
    uint32_t indexOffset,
    uint32_t indexCount,
    const Raster8x4& expected) {
    WGPUBuffer indexBuffer = createIndexBuffer(t, format);
    runIndexed(t, indexBuffer, indexCount, format, indexOffset, WGPUPrimitiveTopology_TriangleList, expected);
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

CTS_TEST(g, "index_format,change_pipeline_after_setIndexBuffer")
    .desc("Test that setting the index buffer before the pipeline works correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("setPipelineBeforeSetIndexBuffer", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t indexOffset = 12;
        const uint32_t indexCount = 7;
        const Raster8x4& expectedShape = kBottomLeftTriangle;

        WGPUBuffer indexBuffer = createIndexBuffer(t, WGPUIndexFormat_Uint32);

        const WGPUPrimitiveTopology kPrimitiveTopology = WGPUPrimitiveTopology_TriangleStrip;
        WGPURenderPipeline pipeline32 = makeRenderPipeline(t, kPrimitiveTopology, WGPUIndexFormat_Uint32);
        WGPURenderPipeline pipeline16 = makeRenderPipeline(t, kPrimitiveTopology, WGPUIndexFormat_Uint16);

        WGPUTexture colorAttachment = createRenderTarget(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginClearPass(t, encoder, createView(t, colorAttachment));
        if (t.param<bool>("setPipelineBeforeSetIndexBuffer")) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline16);
        }
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, indexOffset, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline32);  // Set the pipeline for uint32 again.
        wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        submit(t, encoder);

        expectRasterInTexture(t, colorAttachment, expectedShape);
    });

CTS_TEST(g, "index_format,setIndexBuffer_before_setPipeline")
    .desc("Test that setting the index buffer before the pipeline works correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("setIndexBufferBeforeSetPipeline", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t indexOffset = 12;
        const uint32_t indexCount = 7;
        const Raster8x4& expectedShape = kBottomLeftTriangle;
        const WGPUIndexFormat indexFormat = WGPUIndexFormat_Uint32;

        WGPUBuffer indexBuffer = createIndexBuffer(t, indexFormat);
        WGPURenderPipeline pipeline = makeRenderPipeline(t, WGPUPrimitiveTopology_TriangleStrip, indexFormat);

        WGPUTexture colorAttachment = createRenderTarget(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginClearPass(t, encoder, createView(t, colorAttachment));
        if (t.param<bool>("setIndexBufferBeforeSetPipeline")) {
            wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, indexFormat, indexOffset, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        } else {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, indexFormat, indexOffset, WGPU_WHOLE_SIZE);
        }
        wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        submit(t, encoder);

        expectRasterInTexture(t, colorAttachment, expectedShape);
    });

CTS_TEST(g, "index_format,setIndexBuffer_different_formats")
    .desc(R"(
  Test that index buffers of multiple formats can be used with a pipeline that doesn't use strip
  primitive topology.
  )")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Create a pipeline to be used by different index formats.
        WGPURenderPipeline pipeline = makeRenderPipeline(t, WGPUPrimitiveTopology_TriangleList);
        WGPUTexture colorAttachment = createRenderTarget(t);

        struct Draw {
            WGPUIndexFormat indexFormat;
            uint32_t indexOffset;
            uint32_t indexCount;
        };
        // First uint32, then call setIndexBuffer with the same pipeline and a uint16 index buffer.
        const std::array<Draw, 2> draws = {{
            {WGPUIndexFormat_Uint32, 12, 7},
            {WGPUIndexFormat_Uint16, 6, 6},
        }};
        for (const Draw& draw : draws) {
            WGPUBuffer indexBuffer = createIndexBuffer(t, draw.indexFormat);
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = beginClearPass(t, encoder, createView(t, colorAttachment));
            wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, draw.indexFormat, draw.indexOffset, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderDrawIndexed(pass, draw.indexCount, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            submit(t, encoder);

            expectRasterInTexture(t, colorAttachment, kBottomLeftTriangle);
        }
    });

// primitive_restart data: upstream's private `_indices` / `_expectedShape` per topology.
struct PrimitiveRestartCase {
    std::string_view topology;
    WGPUPrimitiveTopology wgpuTopology;
    std::vector<int64_t> indices;
    Raster8x4 expectedShape;
};

const PrimitiveRestartCase& primitiveRestartCase(std::string_view topology) {
    static const std::array<PrimitiveRestartCase, 5> kCases = {{
        {"point-list", WGPUPrimitiveTopology_PointList, {0, 1, -1, 2, 3, 0},
         {{{{0, 0, 0, 0, 1, 0, 0, 1}},
           {{0, 0, 0, 0, 0, 0, 0, 0}},
           {{0, 0, 0, 0, 0, 0, 0, 0}},
           {{1, 0, 0, 0, 1, 0, 0, 1}}}}},
        {"line-list", WGPUPrimitiveTopology_LineList, {0, 1, -1, 2, 3, 0},
         {{{{0, 0, 0, 0, 1, 0, 1, 1}},
           {{0, 0, 0, 0, 1, 1, 0, 0}},
           {{0, 0, 1, 1, 1, 0, 1, 0}},
           {{1, 1, 0, 0, 1, 0, 0, 1}}}}},
        {"line-strip", WGPUPrimitiveTopology_LineStrip, {0, 1, -1, 2, 3, 0},
         {{{{0, 0, 0, 0, 1, 0, 0, 1}},
           {{0, 0, 0, 0, 1, 1, 1, 0}},
           {{0, 0, 0, 0, 1, 1, 1, 0}},
           {{0, 0, 0, 0, 1, 0, 0, 1}}}}},
        {"triangle-list", WGPUPrimitiveTopology_TriangleList, {0, 1, 3, -1, 2, 1, 0, 0},
         {{{{0, 0, 0, 0, 0, 0, 0, 1}},
           {{0, 0, 0, 0, 1, 1, 1, 1}},
           {{0, 0, 0, 1, 1, 1, 1, 1}},
           {{0, 1, 1, 1, 1, 1, 1, 1}}}}},
        {"triangle-strip", WGPUPrimitiveTopology_TriangleStrip, {3, 1, 0, -1, 2, 2, 1, 3},
         {{{{0, 0, 0, 0, 0, 0, 0, 1}},
           {{0, 0, 0, 0, 1, 0, 1, 1}},
           {{0, 0, 0, 0, 1, 1, 1, 1}},
           {{0, 0, 0, 0, 1, 1, 1, 1}}}}},
    }};
    for (const PrimitiveRestartCase& c : kCases) {
        if (c.topology == topology) {
            return c;
        }
    }
    std::abort();
}

CTS_TEST(g, "primitive_restart")
    .desc(R"(
Test primitive restart with each primitive topology.

Primitive restart should be always active with strip primitive topologies
('line-strip' or 'triangle-strip') and never active for other topologies, where
the primitive restart value isn't special and should be treated as a regular index value.

The value -1 gets uploaded as 0xFFFF or 0xFFFF_FFFF according to the format.
)")
    .params([](ParamsBuilder u) {
        return u.combine("indexFormat", {"uint16", "uint32"})
            .combine("primitiveTopology", {"point-list", "line-list", "line-strip", "triangle-list", "triangle-strip"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUIndexFormat indexFormat =
            t.param<std::string>("indexFormat") == "uint16" ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        const PrimitiveRestartCase& c = primitiveRestartCase(t.param<std::string>("primitiveTopology"));

        WGPUBuffer indexBuffer = createIndexBuffer(t, c.indices, indexFormat);
        runIndexed(t, indexBuffer, static_cast<uint32_t>(c.indices.size()), indexFormat, 0, c.wgpuTopology,
                   c.expectedShape);
    });

} // namespace
