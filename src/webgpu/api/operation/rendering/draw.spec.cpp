// Ported from gpuweb/cts src/webgpu/api/operation/rendering/draw.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,draw",
    "Draw operation tests.");

constexpr uint32_t kWidth = 72;
constexpr uint32_t kHeight = 36;
constexpr uint32_t kNumX = 6;
constexpr uint32_t kNumY = 6;
constexpr uint32_t kTileSizeX = kWidth / kNumX;
constexpr uint32_t kTileSizeY = kHeight / kNumY;
constexpr uint32_t kBytesPerPixel = 4;

constexpr std::string_view kVertexShader = R"(
@vertex fn main(
  @location(0) vertexPosition : vec2<f32>,
  @builtin(vertex_index) vertexIndex : u32,
  @builtin(instance_index) instanceIndex : u32
) -> @builtin(position) vec4<f32> {
  var vertexId : u32 = vertexIndex / 3u;
  var x : f32 = (vertexPosition.x + f32(vertexId)) / 6.0;
  var y : f32 = (vertexPosition.y + f32(instanceIndex)) / 6.0;
  x = 2.0 * x - 1.0;
  y = -2.0 * y + 1.0;
  return vec4<f32>(x, y, 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentShader = R"(
struct Result { value : u32 };
@group(0) @binding(0) var<storage, read_write> result : Result;

@fragment fn main() -> @location(0) vec4<f32> {
  result.value = 1u;
  return vec4<f32>(0.0, 1.0, 0.0, 1.0);
}
)";

struct DrawOptions {
    uint32_t firstIndex = 0;
    uint32_t count = 0;
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 1;
    bool indexed = false;
    bool indirect = false;
    uint64_t vertexBufferOffset = 0;
    uint64_t indexBufferOffset = 0;
    int32_t baseVertex = 0;
};

struct PixelComparison {
    uint32_t x = 0;
    uint32_t y = 0;
    std::array<uint8_t, 4> expected = {};
};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

bool boolParam(const Value& value) {
    return valueAs<bool>(value);
}

bool recordBool(const ParamRecord& params, std::string_view key) {
    return boolParam(*findParam(params, key));
}

std::vector<Value> indexedOnlyValues(const ParamRecord& params, std::initializer_list<Value> indexedValues) {
    if (!recordBool(params, "indexed")) {
        return {Value::undef()};
    }
    return std::vector<Value>(indexedValues);
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
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPUBindGroupLayout createResultBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Fragment;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPURenderPipeline createDrawPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    attribute.format = WGPUVertexFormat_Float32x2;
    attribute.offset = 0;
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBuffer = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBuffer.arrayStride = 2 * sizeof(float);
    vertexBuffer.stepMode = WGPUVertexStepMode_Vertex;
    vertexBuffer.attributeCount = 1;
    vertexBuffer.attributes = &attribute;

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexBuffer;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPUBindGroup createResultBindGroup(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout layout, WGPUBuffer result) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = result;
    entry.offset = 0;
    entry.size = 4;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

std::vector<float> triangleVertices(uint32_t repeatCount, uint64_t vertexBufferOffset) {
    std::vector<float> vertices(static_cast<size_t>(vertexBufferOffset / sizeof(float)), 0.0f);
    vertices.reserve(vertices.size() + static_cast<size_t>(repeatCount) * 6);
    for (uint32_t i = 0; i < repeatCount; ++i) {
        vertices.insert(vertices.end(), {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
    }
    return vertices;
}

WGPUBuffer createVertexBuffer(AllFeaturesMaxLimitsGpuTest& t, uint32_t repeatCount, uint64_t vertexBufferOffset) {
    std::vector<float> vertices = triangleVertices(repeatCount, vertexBufferOffset);
    return t.makeBufferWithContents(
        vertices.data(),
        vertices.size() * sizeof(float),
        WGPUBufferUsage_Vertex);
}

WGPUBuffer createIndexBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t indexBufferOffset) {
    std::vector<uint32_t> indices(static_cast<size_t>(indexBufferOffset / sizeof(uint32_t)), 0);
    indices.insert(indices.end(), {0, 1, 2, 3, 4, 5, 6, 7, 8});
    return t.makeBufferWithContents(
        indices.data(),
        indices.size() * sizeof(uint32_t),
        WGPUBufferUsage_Index);
}

WGPUBuffer createIndirectBuffer(AllFeaturesMaxLimitsGpuTest& t, const std::vector<uint32_t>& values) {
    return t.makeBufferWithContents(
        values.data(),
        values.size() * sizeof(uint32_t),
        WGPUBufferUsage_Indirect);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectSinglePixelComparisonsAreOkInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const std::vector<PixelComparison>& comparisons) {
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
            for (const PixelComparison& comparison : comparisons) {
                const uint64_t offset = static_cast<uint64_t>(comparison.y) * bytesPerRow
                    + static_cast<uint64_t>(comparison.x) * kBytesPerPixel;
                if (offset + kBytesPerPixel > len) {
                    std::ostringstream message;
                    message << "pixel comparison offset out of range: " << offset;
                    return message.str();
                }
                for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                    const uint8_t got = actual[offset + channel];
                    const uint8_t expected = comparison.expected[channel];
                    if (got != expected) {
                        std::ostringstream message;
                        message << "pixel mismatch at (" << comparison.x << ", " << comparison.y
                                << ") channel " << channel
                                << ": expected " << static_cast<int>(expected)
                                << ", got " << static_cast<int>(got);
                        return message.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

std::vector<PixelComparison> expectedPixels(const DrawOptions& opts) {
    constexpr std::array<uint8_t, 4> kGreen = {0x00, 0xff, 0x00, 0xff};
    constexpr std::array<uint8_t, 4> kTransparent = {0x00, 0x00, 0x00, 0x00};

    std::vector<PixelComparison> comparisons;
    comparisons.reserve(kNumX * kNumY);
    const bool anyDraw = opts.count != 0 && opts.instanceCount != 0;
    for (uint32_t primitiveId = 0; primitiveId < kNumX; ++primitiveId) {
        for (uint32_t instanceId = 0; instanceId < kNumY; ++instanceId) {
            const int64_t primitiveVertex = static_cast<int64_t>(primitiveId) * 3;
            const int64_t firstDrawnVertex = static_cast<int64_t>(opts.firstIndex) + opts.baseVertex;
            const int64_t lastDrawnVertex = firstDrawnVertex + opts.count;
            const bool primitiveDrawn = firstDrawnVertex <= primitiveVertex && primitiveVertex < lastDrawnVertex;
            const bool instanceDrawn = opts.firstInstance <= instanceId && instanceId < opts.firstInstance + opts.instanceCount;
            PixelComparison comparison;
            comparison.x = static_cast<uint32_t>(std::floor((1.0 / 3.0 + primitiveId) * kTileSizeX));
            comparison.y = static_cast<uint32_t>(std::floor((2.0 / 3.0 + instanceId) * kTileSizeY));
            comparison.expected = anyDraw && primitiveDrawn && instanceDrawn ? kGreen : kTransparent;
            comparisons.push_back(comparison);
        }
    }
    return comparisons;
}

void checkTriangleDraw(AllFeaturesMaxLimitsGpuTest& t, const DrawOptions& opts) {
    WGPUTexture target = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(target, viewDesc);

    WGPUBindGroupLayout bindGroupLayout = createResultBindGroupLayout(t);
    WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
    WGPURenderPipeline pipeline = createDrawPipeline(t, pipelineLayout);

    WGPUBuffer result = createBuffer(t, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBindGroup bindGroup = createResultBindGroup(t, bindGroupLayout, result);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    if (opts.indexed) {
        WGPUBuffer indexBuffer = createIndexBuffer(t, opts.indexBufferOffset);
        WGPUBuffer vertexBuffer = createVertexBuffer(t, 6, opts.vertexBufferOffset);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, opts.indexBufferOffset, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, opts.vertexBufferOffset, WGPU_WHOLE_SIZE);
        if (opts.indirect) {
            WGPUBuffer indirectBuffer = createIndirectBuffer(t, {
                opts.count,
                opts.instanceCount,
                opts.firstIndex,
                static_cast<uint32_t>(opts.baseVertex),
                opts.firstInstance,
            });
            wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirectBuffer, 0);
        } else {
            wgpuRenderPassEncoderDrawIndexed(
                pass,
                opts.count,
                opts.instanceCount,
                opts.firstIndex,
                opts.baseVertex,
                opts.firstInstance);
        }
    } else {
        WGPUBuffer vertexBuffer = createVertexBuffer(t, 3, opts.vertexBufferOffset);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, opts.vertexBufferOffset, WGPU_WHOLE_SIZE);
        if (opts.indirect) {
            WGPUBuffer indirectBuffer = createIndirectBuffer(t, {
                opts.count,
                opts.instanceCount,
                opts.firstIndex,
                opts.firstInstance,
            });
            wgpuRenderPassEncoderDrawIndirect(pass, indirectBuffer, 0);
        } else {
            wgpuRenderPassEncoderDraw(pass, opts.count, opts.instanceCount, opts.firstIndex, opts.firstInstance);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    const uint32_t expectedResult = opts.count != 0 && opts.instanceCount != 0 ? 1 : 0;
    t.expectGPUBufferValuesEqual(result, &expectedResult, sizeof(expectedResult));
    expectSinglePixelComparisonsAreOkInTexture(t, target, expectedPixels(opts));
}

DrawOptions optionsFromArguments(AllFeaturesMaxLimitsGpuTest& t) {
    DrawOptions opts;
    opts.firstIndex = static_cast<uint32_t>(t.param<int64_t>("first"));
    opts.count = static_cast<uint32_t>(t.param<int64_t>("count"));
    opts.firstInstance = static_cast<uint32_t>(t.param<int64_t>("first_instance"));
    opts.instanceCount = static_cast<uint32_t>(t.param<int64_t>("instance_count"));
    opts.indexed = t.param<bool>("indexed");
    opts.indirect = t.param<bool>("indirect");
    opts.vertexBufferOffset = static_cast<uint64_t>(t.param<int64_t>("vertex_buffer_offset"));
    opts.indexBufferOffset = opts.indexed ? static_cast<uint64_t>(t.param<int64_t>("index_buffer_offset")) : 0;
    opts.baseVertex = opts.indexed ? static_cast<int32_t>(t.param<int64_t>("base_vertex")) : 0;
    return opts;
}

DrawOptions optionsFromDefaultArguments(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string mode = t.param<std::string>("mode");
    DrawOptions opts;
    opts.count = 3;
    opts.instanceCount = t.paramIsUndefined("instance_count") ? 1 : static_cast<uint32_t>(t.param<int64_t>("instance_count"));
    opts.firstIndex = t.paramIsUndefined("first_index") ? 0 : static_cast<uint32_t>(t.param<int64_t>("first_index"));
    opts.firstInstance = t.paramIsUndefined("first_instance") ? 0 : static_cast<uint32_t>(t.param<int64_t>("first_instance"));
    opts.indexed = mode == "drawIndexed";
    opts.indirect = false;
    opts.vertexBufferOffset = 32;
    opts.indexBufferOffset = opts.indexed ? 16 : 0;
    opts.baseVertex = opts.indexed && !t.paramIsUndefined("base_vertex") ? static_cast<int32_t>(t.param<int64_t>("base_vertex")) : 0;
    return opts;
}

CTS_TEST(g, "arguments")
    .params([](ParamsBuilder u) {
        return u.combine("first", {0, 3})
            .combine("count", {0, 3, 6})
            .combine("first_instance", {0, 2})
            .combine("instance_count", {0, 1, 4})
            .combine("indexed", {false, true})
            .combine("indirect", {false, true})
            .combine("vertex_buffer_offset", {0, 32})
            .expand("index_buffer_offset", [](const ParamRecord& params) {
                return indexedOnlyValues(params, {0, 16});
            })
            .expand("base_vertex", [](const ParamRecord& params) {
                return indexedOnlyValues(params, {0, 9});
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        DrawOptions opts = optionsFromArguments(t);
        if (opts.firstInstance > 0 && opts.indirect
            && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_IndirectFirstInstance)) {
            t.skip("indirect-first-instance feature is not supported");
        }
        checkTriangleDraw(t, opts);
    });

CTS_TEST(g, "default_arguments")
    .params([](ParamsBuilder u) {
        return u.combine("mode", {"draw", "drawIndexed"})
            .beginSubcases()
            .combine("instance_count", {Value::undef(), 4})
            .combine("first_index", {Value::undef(), 3})
            .combine("first_instance", {Value::undef(), 2})
            .expand("base_vertex", [](const ParamRecord& params) {
                const std::string mode = valueAs<std::string>(*findParam(params, "mode"));
                if (mode == "drawIndexed") {
                    return std::vector<Value>{Value::undef(), 9};
                }
                return std::vector<Value>{Value::undef()};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        checkTriangleDraw(t, optionsFromDefaultArguments(t));
    });

CTS_TEST(g, "vertex_attributes,basic")
    .unimplemented("vertex attribute basic rendering tests are deferred");

CTS_TEST(g, "vertex_attributes,formats")
    .unimplemented("vertex attribute format rendering tests are deferred");

CTS_TEST(g, "largeish_buffer")
    .unimplemented("largeish vertex buffer rendering test is deferred");

} // namespace
