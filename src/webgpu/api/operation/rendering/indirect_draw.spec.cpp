// Ported from gpuweb/cts src/webgpu/api/operation/rendering/indirect_draw.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Intentional deviation: upstream fills non-explicit indirect parameter slots with Math.random().
// This port uses deterministic 100u filler for reproducible wrong-offset coverage.

#include <array>
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
    "api,operation,rendering,indirect_draw",
    "Indirect draw operation tests.");

constexpr uint32_t kRenderTargetSize = 4;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kDrawIndirectParametersSize = 4;
constexpr uint32_t kDrawIndexedIndirectParametersSize = 5;
constexpr WGPUTextureFormat kRenderTargetFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr std::array<uint8_t, 4> kFilledPixel = {0, 255, 0, 255};
constexpr std::array<uint8_t, 4> kNotFilledPixel = {0, 0, 0, 0};

constexpr std::string_view kVertexShader = R"(
@vertex fn main(@location(0) pos : vec2<f32>) -> @builtin(position) vec4<f32> {
  return vec4<f32>(pos, 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.0, 1.0, 0.0, 1.0);
}
)";

struct PixelExpectation {
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

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUBuffer makeIndexBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    const std::array<uint32_t, 6> indices = {0, 1, 2, 1, 2, 3};
    return t.makeBufferWithContents(indices.data(), indices.size() * sizeof(uint32_t), WGPUBufferUsage_Index);
}

WGPUBuffer makeVertexBuffer(AllFeaturesMaxLimitsGpuTest& t, bool isIndexed) {
    const std::vector<float> vertices = isIndexed
        ? std::vector<float>{-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f}
        : std::vector<float>{
            -1.0f, 1.0f,
            1.0f, -1.0f,
            -1.0f, -1.0f,
            -1.0f, 1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
        };
    return t.makeBufferWithContents(vertices.data(), vertices.size() * sizeof(float), WGPUBufferUsage_Vertex);
}

void writeParameters(std::vector<uint32_t>& values, uint32_t offset, std::initializer_list<uint32_t> parameters) {
    uint32_t index = offset;
    for (uint32_t parameter : parameters) {
        values[index++] = parameter;
    }
}

WGPUBuffer makeIndirectBuffer(AllFeaturesMaxLimitsGpuTest& t, bool isIndexed, uint64_t indirectOffset) {
    const uint32_t o = static_cast<uint32_t>(indirectOffset / sizeof(uint32_t));
    const uint32_t parametersSize = isIndexed ? kDrawIndexedIndirectParametersSize : kDrawIndirectParametersSize;
    std::vector<uint32_t> values(o + parametersSize * 2, 100u);

    if (isIndexed) {
        writeParameters(values, o, {3, 1, 0, 0, 0});
        writeParameters(values, o + 5, {6, 1, 0, 0, 0});
        if (o >= 5) {
            writeParameters(values, o - 5, {3, 1, 3, 0, 0});
        }
        if (o >= 10) {
            writeParameters(values, 0, {0, 0, 0, 0, 0});
        }
    } else {
        writeParameters(values, o, {3, 1, 0, 0});
        writeParameters(values, o + 4, {6, 1, 0, 0});
        if (o >= 4) {
            writeParameters(values, o - 4, {3, 1, 3, 0});
        }
        if (o >= 8) {
            writeParameters(values, 0, {0, 0, 0, 0});
        }
    }

    return t.makeBufferWithContents(values.data(), values.size() * sizeof(uint32_t), WGPUBufferUsage_Indirect);
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

WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t) {
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
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexBuffer;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectPixelsInTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(
        alignTo(kRenderTargetSize * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kRenderTargetSize - 1)
            + static_cast<uint64_t>(kRenderTargetSize) * kBytesPerPixel,
        kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(
        encoder,
        texture,
        buffer,
        bytesPerRow,
        WGPUExtent3D{kRenderTargetSize, kRenderTargetSize, 1});
    submit(t, encoder);

    const std::array<PixelExpectation, 2> expectations = {{
        PixelExpectation{0, 1, kFilledPixel},
        PixelExpectation{1, 0, kNotFilledPixel},
    }};

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (const PixelExpectation& expectation : expectations) {
                const uint64_t offset = static_cast<uint64_t>(expectation.y) * bytesPerRow
                    + static_cast<uint64_t>(expectation.x) * kBytesPerPixel;
                if (offset + kBytesPerPixel > len) {
                    std::ostringstream message;
                    message << "rgba8unorm pixel offset out of range: " << offset;
                    return message.str();
                }
                for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                    const uint8_t got = actual[offset + channel];
                    const uint8_t expected = expectation.expected[channel];
                    if (got != expected) {
                        std::ostringstream message;
                        message << "rgba8unorm mismatch at (" << expectation.x << ", " << expectation.y
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

std::vector<Value> indirectOffsetValues(const ParamRecord& params) {
    const bool isIndexed = boolParam(*findParam(params, "isIndexed"));
    const uint64_t parametersSize = (isIndexed ? kDrawIndexedIndirectParametersSize : kDrawIndirectParametersSize)
        * sizeof(uint32_t);
    return {
        0,
        4,
        parametersSize,
        parametersSize + 4,
        3 * parametersSize,
        3 * parametersSize + 4,
        99 * parametersSize,
        99 * parametersSize + 4,
    };
}

void runIndirectDrawBasics(AllFeaturesMaxLimitsGpuTest& t) {
    const bool isIndexed = t.param<bool>("isIndexed");
    const uint64_t indirectOffset = static_cast<uint64_t>(t.param<int64_t>("indirectOffset"));

    WGPUTexture target = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(target, viewDesc);

    WGPURenderPipeline pipeline = createPipeline(t);
    WGPUBuffer vertexBuffer = makeVertexBuffer(t, isIndexed);
    WGPUBuffer indirectBuffer = makeIndirectBuffer(t, isIndexed, indirectOffset);

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
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
    if (isIndexed) {
        WGPUBuffer indexBuffer = makeIndexBuffer(t);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirectBuffer, indirectOffset);
    } else {
        wgpuRenderPassEncoderDrawIndirect(pass, indirectBuffer, indirectOffset);
    }
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectPixelsInTexture(t, target);
}

CTS_TEST(g, "basics")
    .params([](ParamsBuilder u) {
        return u.combine("isIndexed", {true, false})
            .beginSubcases()
            .expand("indirectOffset", indirectOffsetValues);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runIndirectDrawBasics(t);
    });

} // namespace
