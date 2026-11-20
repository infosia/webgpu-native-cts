// Ported from gpuweb/cts src/webgpu/api/operation/rendering/basic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,basic",
    "Basic rendering operation tests.");

constexpr std::string_view kFullscreenVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
      vec2<f32>(-1.0, -3.0), vec2<f32>(3.0, 1.0), vec2<f32>(-1.0, 1.0));
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

constexpr std::string_view kGreenFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUBuffer createCopyBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 4;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

WGPUTexture createColorAttachment(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPURenderPassEncoder beginColorRenderPass(GpuTest& t, 
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPUColor clearValue) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = clearValue;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;
    return t.beginRenderPassTracked(encoder, &passDesc);
}

void copyColorToBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder, WGPUTexture color, WGPUBuffer dst) {
    t.copyTextureToBuffer(encoder, color, dst, 256, WGPUExtent3D{1, 1, 1});
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectGreen(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer dst) {
    constexpr std::array<uint8_t, 4> expected = {0x00, 0xff, 0x00, 0xff};
    t.expectGPUBufferValuesEqual(dst, expected.data(), expected.size());
}

WGPURenderPipeline createFullscreenQuadPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kFullscreenVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kGreenFragmentShader);

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
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

CTS_TEST(g, "clear")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBuffer dst = createCopyBuffer(t);
        WGPUTexture color = createColorAttachment(t);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(color, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginColorRenderPass(t, encoder, view, WGPUColor{0.0, 1.0, 0.0, 1.0});
        wgpuRenderPassEncoderEnd(pass);
        copyColorToBuffer(t, encoder, color, dst);
        submit(t, encoder);

        expectGreen(t, dst);
    });

CTS_TEST(g, "fullscreen_quad")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBuffer dst = createCopyBuffer(t);
        WGPUTexture color = createColorAttachment(t);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(color, viewDesc);
        WGPURenderPipeline pipeline = createFullscreenQuadPipeline(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginColorRenderPass(t, encoder, view, WGPUColor{1.0, 0.0, 0.0, 1.0});
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        copyColorToBuffer(t, encoder, color, dst);
        submit(t, encoder);

        expectGreen(t, dst);
    });

constexpr std::string_view kLargeDrawVertexShader = R"(
struct Params {
  numVertices: u32,
  numInstances: u32,
};

fn selectValue(index: u32, maxIndex: u32) -> f32 {
  let highOrMid = select(0.0, 2.0 / 3.0, index == maxIndex - 1u);
  return select(highOrMid, -2.0 / 3.0, index == 0u);
}

@group(0) @binding(0) var<uniform> params: Params;

@vertex fn main(
    @builtin(vertex_index) v: u32,
    @builtin(instance_index) i: u32)
    -> @builtin(position) vec4<f32> {
  let x = selectValue(v, params.numVertices);
  let y = -selectValue(i, params.numInstances);
  return vec4<f32>(x, y, 0.0, 1.0);
}
)";

constexpr std::string_view kYellowFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(1.0, 1.0, 0.0, 1.0);
}
)";

// The large_draw bind-group layout and pipeline do not depend on the case: cached per device.
struct LargeDrawPipelineCache : DeviceScopedObject {
    struct Entry {
        WGPUBindGroupLayout bindGroupLayout = nullptr;
        WGPURenderPipeline pipeline = nullptr;
    };
    std::unordered_map<WGPUDevice, Entry> byDevice;

    ~LargeDrawPipelineCache() override {
        for (auto& [device, entry] : byDevice) {
            (void)device;
            wgpuRenderPipelineRelease(entry.pipeline);
            wgpuBindGroupLayoutRelease(entry.bindGroupLayout);
        }
    }
};

const LargeDrawPipelineCache::Entry& largeDrawPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    auto& byDevice = deviceScoped<LargeDrawPipelineCache>().byDevice;
    auto it = byDevice.find(t.device());
    if (it != byDevice.end()) {
        return it->second;
    }

    LargeDrawPipelineCache::Entry entry;
    WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    entry.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(t.device(), &layoutDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &entry.bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(pipelineLayoutDesc);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = t.createShaderModuleTracked(kYellowFragmentShader);
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = pipelineLayout;
    desc.vertex.module = t.createShaderModuleTracked(kLargeDrawVertexShader);
    desc.vertex.entryPoint = stringView("main");
    desc.fragment = &fragment;
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    entry.pipeline = wgpuDeviceCreateRenderPipeline(t.device(), &desc);
    return byDevice.emplace(t.device(), entry).first->second;
}

CTS_TEST(g, "large_draw")
    .desc(R"(Test reasonably-sized large {draw, drawIndexed} (see also stress tests).

  Tests that draw calls behave reasonably with large vertex counts for
  non-indexed draws, large index counts for indexed draws, and large instance
  counts in both cases. Various combinations of these counts are tested with
  both direct and indirect draw calls.

  Draw call sizes are increased incrementally over these parameters until we the
  run out of values or completion of a draw call exceeds a fixed time limit of
  100ms.

  To validate that the drawn vertices actually made it though the pipeline on
  each draw call, we render a 3x3 target with the positions of the first and
  last vertices of the first and last instances in different respective corners,
  and everything else positioned to cover only one of the intermediate
  fragments. If the output image is completely yellow, then we can reasonably
  infer that all vertices were drawn.

  Params:
    - indexed= {true, false} - whether to test indexed or non-indexed draw calls
    - indirect= {true, false} - whether to use indirect or direct draw calls)")
    .params([](ParamsBuilder u) {
        return u.combine("indexed", {true, false}).combine("indirect", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool indexed = t.param<bool>("indexed");
        const bool indirect = t.param<bool>("indirect");

        constexpr uint32_t kBytesPerRow = 256;
        WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        dstDesc.size = 3 * kBytesPerRow;
        dstDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer dst = t.createBufferTracked(dstDesc);

        WGPUBufferDescriptor paramsDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        paramsDesc.size = 8;
        paramsDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        WGPUBuffer paramsBuffer = t.createBufferTracked(paramsDesc);

        WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        indirectDesc.size = 20;
        indirectDesc.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst;
        WGPUBuffer indirectBuffer = t.createBufferTracked(indirectDesc);
        auto writeIndirectParams = [&](uint32_t count, uint32_t instanceCount) {
            const std::array<uint32_t, 5> params = {
                count,          // Vertex or index count
                instanceCount,
                0,              // First vertex or index
                0,              // First instance (non-indexed) or base vertex (indexed)
                0,              // First instance (indexed)
            };
            wgpuQueueWriteBuffer(t.queue(), indirectBuffer, 0, params.data(), sizeof(params));
        };

        WGPUBuffer indexBuffer = nullptr;
        if (indexed) {
            constexpr uint32_t kMaxIndices = 16 * 1024 * 1024;
            WGPUBufferDescriptor indexDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            indexDesc.size = static_cast<uint64_t>(kMaxIndices) * sizeof(uint32_t);
            indexDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            indexDesc.mappedAtCreation = true;
            indexBuffer = t.createBufferTracked(indexDesc);
            void* mapped = wgpuBufferGetMappedRange(indexBuffer, 0, indexDesc.size);
            if (mapped == nullptr) {
                t.fail("large_draw: failed to map the index buffer at creation");
            }
            uint32_t* indexData = static_cast<uint32_t*>(mapped);
            for (uint32_t i = 0; i < kMaxIndices; ++i) {
                indexData[i] = i;
            }
            wgpuBufferUnmap(indexBuffer);
        }

        WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        colorDesc.size = WGPUExtent3D{3, 3, 1};
        colorDesc.format = WGPUTextureFormat_RGBA8Unorm;
        colorDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        WGPUTexture colorAttachment = t.createTextureTracked(colorDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView colorAttachmentView = t.createViewTracked(colorAttachment, viewDesc);

        const LargeDrawPipelineCache::Entry& cached = largeDrawPipeline(t);

        WGPUBindGroupEntry bindGroupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bindGroupEntry.binding = 0;
        bindGroupEntry.buffer = paramsBuffer;
        bindGroupEntry.size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = cached.bindGroupLayout;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &bindGroupEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

        // Encodes and submits one draw; returns without waiting.
        auto runPipeline = [&](uint32_t numVertices, uint32_t numInstances) {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass =
                beginColorRenderPass(t, encoder, colorAttachmentView, WGPUColor{0.0, 0.0, 1.0, 1.0});
            wgpuRenderPassEncoderSetPipeline(pass, cached.pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            if (indexBuffer != nullptr) {
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            }

            if (indirect) {
                writeIndirectParams(numVertices, numInstances);
                if (indexed) {
                    wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirectBuffer, 0);
                } else {
                    wgpuRenderPassEncoderDrawIndirect(pass, indirectBuffer, 0);
                }
            } else {
                if (indexed) {
                    wgpuRenderPassEncoderDrawIndexed(pass, numVertices, numInstances, 0, 0, 0);
                } else {
                    wgpuRenderPassEncoderDraw(pass, numVertices, numInstances, 0, 0);
                }
            }
            wgpuRenderPassEncoderEnd(pass);
            t.copyTextureToBuffer(encoder, colorAttachment, dst, kBytesPerRow, WGPUExtent3D{3, 3, 1});

            const std::array<uint32_t, 2> params = {numVertices, numInstances};
            wgpuQueueWriteBuffer(t.queue(), paramsBuffer, 0, params.data(), sizeof(params));
            submit(t, encoder);
        };

        // Upstream checks each row (12 bytes at row * 256) against all-yellow.
        auto checkAllYellow = [&](uint32_t numVertices, uint32_t numInstances) {
            t.expectGPUBufferValuesPassCheck(
                dst,
                [&](const uint8_t* data, size_t len) -> std::optional<std::string> {
                    constexpr std::array<uint8_t, 4> kYellow = {0xff, 0xff, 0x00, 0xff};
                    for (uint32_t row = 0; row < 3; ++row) {
                        for (uint32_t i = 0; i < 12; ++i) {
                            const size_t offset = static_cast<size_t>(row) * kBytesPerRow + i;
                            if (offset >= len || data[offset] != kYellow[i % 4]) {
                                std::ostringstream message;
                                message << "numVertices=" << numVertices << " numInstances=" << numInstances
                                        << ": row " << row << " byte " << i << " expected "
                                        << static_cast<int>(kYellow[i % 4]) << ", got "
                                        << (offset < len ? static_cast<int>(data[offset]) : -1);
                                return message.str();
                            }
                        }
                    }
                    return std::nullopt;
                },
                0,
                2 * kBytesPerRow + 12);
        };

        // If any iteration takes longer than this, we stop incrementing along that
        // branch and move on to the next instance count. Note that the max
        // supported vertex count for any iteration is 2**24 due to our choice of
        // index buffer size.
        const double maxDurationMs = 100;
        struct Counts {
            uint32_t numInstances;
            std::vector<uint32_t> vertexCounts;
        };
        const std::vector<Counts> counts = {
            {4, {1u << 10, 1u << 16, 1u << 18, 1u << 20, 1u << 22, 1u << 24}},
            {1u << 8, {1u << 10, 1u << 16, 1u << 18, 1u << 20, 1u << 22}},
            {1u << 10, {1u << 8, 1u << 10, 1u << 12, 1u << 16, 1u << 18, 1u << 20}},
            {1u << 16, {1u << 4, 1u << 8, 1u << 10, 1u << 12, 1u << 14}},
            {1u << 20, {1u << 4, 1u << 8, 1u << 10}},
        };
        for (const Counts& c : counts) {
            for (uint32_t numVertices : c.vertexCounts) {
                const auto start = std::chrono::steady_clock::now();
                runPipeline(numVertices, c.numInstances);
                t.onSubmittedWorkDoneSync();
                const double duration =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                // Upstream's expectGPUBufferValuesPassCheck only enqueues the readback inside the
                // timed region; the blocking check runs after the timing here.
                checkAllYellow(numVertices, c.numInstances);
                if (duration >= maxDurationMs) {
                    break;
                }
            }
        }
    });

} // namespace
