// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/buffer.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// (see docs/UPSTREAM.md).
//
// Port notes:
// - Pipelines do not depend on the case parameters, so they are cached per device by shader.
// - Upstream expectSingleColor(rgba8unorm) is a copy into a fresh (zero-initialized) buffer whose
//   texels are compared to the expected bytes.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,resource_init,buffer",
    R"(Test uninitialized buffers are initialized to zero when read
(or read-written, e.g. with depth write or atomics).

Note that:
-  We don't need 'copy_buffer_to_buffer_copy_destination' here because there has already been an
   operation test 'command_buffer.copyBufferToBuffer.single' that provides the same functionality.)");

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// GPUConst.BufferUsage values (identical bits in webgpu.h).
constexpr int kUsageCopyDstMapRead = static_cast<int>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
constexpr int kUsageCopySrcMapWrite = static_cast<int>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite);
constexpr int kUsageCopySrc = static_cast<int>(WGPUBufferUsage_CopySrc);
constexpr int kMapModeRead = static_cast<int>(WGPUMapMode_Read);
constexpr int kMapModeWrite = static_cast<int>(WGPUMapMode_Write);

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage, bool mappedAtCreation = false) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    desc.mappedAtCreation = mappedAtCreation ? WGPU_TRUE : WGPU_FALSE;
    return t.createBufferTracked(desc);
}

WGPUTexture createTexture2D(AllFeaturesMaxLimitsGpuTest& t,
                            WGPUTextureFormat format,
                            WGPUExtent3D size,
                            WGPUTextureUsage usage,
                            uint32_t mipLevelCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

WGPUTextureView createDefaultView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUBufferUsage getBufferUsageFromMapMode(int mapMode) {
    if (mapMode == kMapModeRead) {
        return WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    }
    return WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite;
}

std::optional<std::string> compareBytes(const uint8_t* actual, const uint8_t* expected, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (actual[i] != expected[i]) {
            std::ostringstream message;
            message << "buffer mismatch at byte " << i << ": expected " << static_cast<int>(expected[i])
                    << ", got " << static_cast<int>(actual[i]);
            return message.str();
        }
    }
    return std::nullopt;
}

// Upstream checkGPUBufferContent: read a MAP_READ buffer by mapping it, others by copy.
void checkGPUBufferContent(AllFeaturesMaxLimitsGpuTest& t,
                           WGPUBuffer buffer,
                           WGPUBufferUsage bufferUsage,
                           const std::vector<uint8_t>& expectedData) {
    const bool mappable = (bufferUsage & WGPUBufferUsage_MapRead) != 0;
    if (!mappable) {
        t.expectGPUBufferValuesEqual(buffer, expectedData.data(), expectedData.size());
        return;
    }
    t.expectMapAsync(buffer, WGPUMapMode_Read, true, 0, expectedData.size());
    const void* mapped = wgpuBufferGetConstMappedRange(buffer, 0, expectedData.size());
    if (mapped == nullptr) {
        wgpuBufferUnmap(buffer);
        t.fail("failed to get the mapped range of the MAP_READ buffer");
    }
    std::vector<uint8_t> actual(expectedData.size());
    std::memcpy(actual.data(), mapped, actual.size());
    wgpuBufferUnmap(buffer);
    if (auto message = compareBytes(actual.data(), expectedData.data(), actual.size())) {
        t.fail(*message);
    }
}

// Upstream expectSingleColor(texture, 'rgba8unorm', {size, exp}) for unorm8 bytes.
void expectSingleColorRGBA8(AllFeaturesMaxLimitsGpuTest& t,
                            WGPUTexture texture,
                            WGPUExtent3D size,
                            const uint8_t (&rgba)[4]) {
    const uint32_t bytesPerRow = (size.width * 4 + kBytesPerRowAlignment - 1) / kBytesPerRowAlignment *
                                 kBytesPerRowAlignment;
    const uint64_t byteLength = uint64_t{bytesPerRow} * size.height * size.depthOrArrayLayers;
    WGPUBuffer readback = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = texture;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = readback;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = size.height;
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &size);
    submit(t, encoder);

    const uint8_t expected[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    t.expectGPUBufferValuesPassCheck(
        readback,
        [size, bytesPerRow, expected](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z) {
                for (uint32_t y = 0; y < size.height; ++y) {
                    for (uint32_t x = 0; x < size.width; ++x) {
                        const size_t offset =
                            (size_t{z} * size.height + y) * bytesPerRow + size_t{x} * 4;
                        if (offset + 4 > len) {
                            return std::string("readback buffer too small");
                        }
                        const uint8_t* texel = actual + offset;
                        if (std::memcmp(texel, expected, 4) != 0) {
                            std::ostringstream message;
                            message << "texel (" << x << "," << y << "," << z << ") expected rgba8 {"
                                    << static_cast<int>(expected[0]) << "," << static_cast<int>(expected[1])
                                    << "," << static_cast<int>(expected[2]) << ","
                                    << static_cast<int>(expected[3]) << "}, got {"
                                    << static_cast<int>(texel[0]) << "," << static_cast<int>(texel[1]) << ","
                                    << static_cast<int>(texel[2]) << "," << static_cast<int>(texel[3]) << "}";
                            return message.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0, static_cast<size_t>(byteLength));
}

constexpr uint8_t kGreen[4] = {0, 255, 0, 255};

// Upstream checkBufferAndOutputTexture.
void checkBufferAndOutputTexture(AllFeaturesMaxLimitsGpuTest& t,
                                 WGPUBuffer buffer,
                                 uint64_t bufferSize,
                                 WGPUTexture outputTexture,
                                 WGPUExtent3D outputTextureSize = WGPUExtent3D{1, 1, 1},
                                 const uint8_t (&outputTextureColor)[4] = kGreen) {
    expectSingleColorRGBA8(t, outputTexture, outputTextureSize, outputTextureColor);
    const std::vector<uint8_t> expectedBufferData(static_cast<size_t>(bufferSize), 0);
    t.expectGPUBufferValuesEqual(buffer, expectedBufferData.data(), expectedBufferData.size());
}

// ---------------------------------------------------------------------------
// Shaders (verbatim from upstream)
// ---------------------------------------------------------------------------

constexpr std::string_view kUniformComputeShader = R"(
  struct UBO {
    value : vec4<u32>
  };
  @group(0) @binding(0) var<uniform> ubo : UBO;
  @group(0) @binding(1) var outImage : texture_storage_2d<rgba8unorm, write>;

  @compute @workgroup_size(1) fn main() {
      if (all(ubo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
          textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
      } else {
          textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
      }
  })";

constexpr std::string_view kReadonlyStorageComputeShader = R"(
    struct SSBO {
      value : vec4<u32>
    };
    @group(0) @binding(0) var<storage, read> ssbo : SSBO;
    @group(0) @binding(1) var outImage : texture_storage_2d<rgba8unorm, write>;

    @compute @workgroup_size(1) fn main() {
        if (all(ssbo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
            textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
        } else {
            textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
        }
    })";

constexpr std::string_view kStorageComputeShader = R"(
    struct SSBO {
      value : vec4<u32>
    };
    @group(0) @binding(0) var<storage, read_write> ssbo : SSBO;
    @group(0) @binding(1) var outImage : texture_storage_2d<rgba8unorm, write>;

    @compute @workgroup_size(1) fn main() {
        if (all(ssbo.value == vec4<u32>(0u, 0u, 0u, 0u))) {
            textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(0.0, 1.0, 0.0, 1.0));
        } else {
            textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
        }
    })";

constexpr std::string_view kDispatchIndirectComputeShader = R"(
        @group(0) @binding(0) var outImage : texture_storage_2d<rgba8unorm, write>;

        @compute @workgroup_size(1) fn main() {
          textureStore(outImage, vec2<i32>(0, 0), vec4<f32>(1.0, 0.0, 0.0, 1.0));
        })";

constexpr std::string_view kVertexBufferVertexShader = R"(
      struct VertexOut {
        @location(0) color : vec4<f32>,
        @builtin(position) position : vec4<f32>,
      };

      @vertex fn main(@location(0) pos : vec4<f32>) -> VertexOut {
        var output : VertexOut;
        if (all(pos == vec4<f32>(0.0, 0.0, 0.0, 0.0))) {
          output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
        } else {
          output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
        }
        output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        return output;
      })";

constexpr std::string_view kIndexBufferVertexShader = R"(
    struct VertexOut {
      @location(0) color : vec4<f32>,
      @builtin(position) position : vec4<f32>,
    };

    @vertex
    fn main(@builtin(vertex_index) VertexIndex : u32) -> VertexOut {
      var output : VertexOut;
      if (VertexIndex == 0u) {
        output.color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
      } else {
        output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
      }
      output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
      return output;
    })";

constexpr std::string_view kIndirectDrawVertexShader = R"(
    struct VertexOut {
      @location(0) color : vec4<f32>,
      @builtin(position) position : vec4<f32>,
    };

    @vertex fn main() -> VertexOut {
      var output : VertexOut;
      output.color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
      output.position = vec4<f32>(0.0, 0.0, 0.0, 1.0);
      return output;
    })";

constexpr std::string_view kFragmentShader = R"(
        @fragment
        fn main(@location(0) i_color : vec4<f32>) -> @location(0) vec4<f32> {
            return i_color;
        })";

// ---------------------------------------------------------------------------
// Per-device pipeline caches (pipelines depend only on the shader).
// ---------------------------------------------------------------------------

struct BufferInitComputePipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<const char*, WGPUComputePipeline>> byDevice;

    ~BufferInitComputePipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                wgpuComputePipelineRelease(pipeline);
            }
        }
    }
};

struct BufferInitRenderPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<const char*, WGPURenderPipeline>> byDevice;

    ~BufferInitRenderPipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                wgpuRenderPipelineRelease(pipeline);
            }
        }
    }
};

// Compute pipeline with layout 'auto' for one of the constexpr shaders above (keyed by its address).
WGPUComputePipeline getComputePipeline(AllFeaturesMaxLimitsGpuTest& t, std::string_view shader) {
    auto& pipelines = deviceScoped<BufferInitComputePipelineCache>().byDevice[t.device()];
    auto it = pipelines.find(shader.data());
    if (it != pipelines.end()) {
        return it->second;
    }
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = t.createShaderModuleTracked(shader);
    desc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(t.device(), &desc);
    if (pipeline == nullptr) {
        t.fail("failed to create the compute pipeline");
    }
    pipelines.emplace(shader.data(), pipeline);
    return pipeline;
}

// Upstream createRenderPipelineForTest(vertexShaderModule, testVertexBuffer).
WGPURenderPipeline createRenderPipelineForTest(AllFeaturesMaxLimitsGpuTest& t,
                                               std::string_view vertexShader,
                                               bool testVertexBuffer) {
    auto& pipelines = deviceScoped<BufferInitRenderPipelineCache>().byDevice[t.device()];
    auto it = pipelines.find(vertexShader.data());
    if (it != pipelines.end()) {
        return it->second;
    }

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = t.createShaderModuleTracked(kFragmentShader);
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = t.createShaderModuleTracked(vertexShader);
    desc.vertex.entryPoint = sv("main");
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.fragment = &fragment;

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    if (testVertexBuffer) {
        attribute.format = WGPUVertexFormat_Float32x4;
        attribute.offset = 0;
        attribute.shaderLocation = 0;
        vertexBufferLayout.arrayStride = 16;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = 1;
        vertexBufferLayout.attributes = &attribute;
        desc.vertex.bufferCount = 1;
        desc.vertex.buffers = &vertexBufferLayout;
    }

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(t.device(), &desc);
    if (pipeline == nullptr) {
        t.fail("failed to create the render pipeline");
    }
    pipelines.emplace(vertexShader.data(), pipeline);
    return pipeline;
}

// Upstream TestBufferZeroInitInBindGroupInternal.
void testBufferZeroInitInBindGroupInternal(AllFeaturesMaxLimitsGpuTest& t,
                                           std::string_view computeShader,
                                           WGPUBuffer buffer,
                                           bool bindBufferResource,
                                           uint64_t bufferOffset,
                                           uint64_t boundBufferSize) {
    WGPUComputePipeline computePipeline = getComputePipeline(t, computeShader);
    WGPUTexture outputTexture = createTexture2D(t, WGPUTextureFormat_RGBA8Unorm, WGPUExtent3D{1, 1, 1},
                                                WGPUTextureUsage_CopySrc | WGPUTextureUsage_StorageBinding);

    WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = buffer;
    if (bindBufferResource) {
        // The whole buffer (GPUBuffer as a GPUBindingResource).
        entries[0].offset = 0;
        entries[0].size = WGPU_WHOLE_SIZE;
    } else {
        entries[0].offset = bufferOffset;
        entries[0].size = boundBufferSize;
    }
    entries[1].binding = 1;
    entries[1].textureView = createDefaultView(t, outputTexture);

    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(computePipeline, 0);
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = layout;
    bgDesc.entryCount = 2;
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(layout);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder computePass = t.beginComputePassTracked(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderSetPipeline(computePass, computePipeline);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, 1, 1, 1);
    wgpuComputePassEncoderEnd(computePass);
    submit(t, encoder);

    checkBufferAndOutputTexture(t, buffer, boundBufferSize + bufferOffset, outputTexture);
}

// Upstream testBufferZeroInitInBindGroup. `bufferSize` is buffer.size.
void testBufferZeroInitInBindGroup(AllFeaturesMaxLimitsGpuTest& t,
                                   std::string_view computeShader,
                                   WGPUBuffer buffer,
                                   uint64_t bufferSize,
                                   uint64_t bufferOffset,
                                   uint64_t boundBufferSize) {
    testBufferZeroInitInBindGroupInternal(t, computeShader, buffer, false, bufferOffset, boundBufferSize);
    const bool bindBufferResource = bufferOffset == 0 && boundBufferSize == bufferSize;
    if (bindBufferResource) {
        testBufferZeroInitInBindGroupInternal(t, computeShader, buffer, true, bufferOffset, boundBufferSize);
    }
}

// Upstream recordInitializeTextureColor.
void recordInitializeTextureColor(AllFeaturesMaxLimitsGpuTest& t,
                                  WGPUCommandEncoder encoder,
                                  WGPUTexture texture,
                                  WGPUColor color) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = createDefaultView(t, texture);
    colorAttachment.clearValue = color;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    WGPURenderPassEncoder renderPass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderEnd(renderPass);
}

uint64_t appliedOffsetOf(int64_t offset, uint64_t bufferSize) {
    return offset >= 0 ? static_cast<uint64_t>(offset) : bufferSize - static_cast<uint64_t>(-offset);
}

int64_t paramInt(const ParamRecord& p, std::string_view key) {
    return valueAs<int64_t>(*findParam(p, key));
}

CTS_TEST(g, "partial_write_buffer")
    .desc(R"(Verify when we upload data to a part of a buffer with writeBuffer() just after the creation of
the buffer, the remaining part of that buffer will be initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("offset", {0, 8, -12}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int64_t offset = t.param<int64_t>("offset");
        constexpr uint64_t kBufferSize = 32;
        const uint64_t appliedOffset = appliedOffsetOf(offset, kBufferSize);

        WGPUBuffer buffer = createBuffer(t, kBufferSize, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        constexpr size_t kCopySize = 12;
        std::vector<uint8_t> writeData(kCopySize);
        std::vector<uint8_t> expectedData(kBufferSize, 0);
        for (size_t i = 0; i < kCopySize; ++i) {
            writeData[i] = static_cast<uint8_t>(i + 1);
            expectedData[appliedOffset + i] = writeData[i];
        }
        wgpuQueueWriteBuffer(t.queue(), buffer, appliedOffset, writeData.data(), writeData.size());

        t.expectGPUBufferValuesEqual(buffer, expectedData.data(), expectedData.size());
    });

CTS_TEST(g, "map_whole_buffer")
    .desc(R"(Verify when we map the whole range of a mappable GPUBuffer to a typed array buffer just after
creating the GPUBuffer, the contents of both the typed array buffer and the GPUBuffer itself
have already been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.combine("mapMode", {kMapModeRead, kMapModeWrite}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int mapMode = t.param<int>("mapMode");

        constexpr size_t kBufferSize = 32;
        const WGPUBufferUsage bufferUsage = getBufferUsageFromMapMode(mapMode);
        WGPUBuffer buffer = createBuffer(t, kBufferSize, bufferUsage);

        t.expectMapAsync(buffer, static_cast<WGPUMapMode>(mapMode), true, 0, WGPU_WHOLE_MAP_SIZE);
        const void* mapped = mapMode == kMapModeRead ? wgpuBufferGetConstMappedRange(buffer, 0, kBufferSize)
                                                     : wgpuBufferGetMappedRange(buffer, 0, kBufferSize);
        if (mapped == nullptr) {
            wgpuBufferUnmap(buffer);
            t.fail("failed to get the mapped range");
        }
        const auto* readData = static_cast<const uint8_t*>(mapped);
        for (size_t i = 0; i < kBufferSize; ++i) {
            t.expect(readData[i] == 0, "mapped byte " + std::to_string(i) + " is not zero");
        }
        wgpuBufferUnmap(buffer);

        const std::vector<uint8_t> expectedData(kBufferSize, 0);
        checkGPUBufferContent(t, buffer, bufferUsage, expectedData);
    });

CTS_TEST(g, "map_partial_buffer")
    .desc(R"(Verify when we map a subrange of a mappable GPUBuffer to a typed array buffer just after the
creation of the GPUBuffer, the contents of both the typed array buffer and the GPUBuffer have
already been initialized to 0.)")
    .params([](ParamsBuilder u) {
        return u.combine("mapMode", {kMapModeRead, kMapModeWrite}).beginSubcases().combine("offset", {0, 8, -16});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int mapMode = t.param<int>("mapMode");
        const int64_t offset = t.param<int64_t>("offset");
        constexpr uint64_t kBufferSize = 32;
        const uint64_t appliedOffset = appliedOffsetOf(offset, kBufferSize);

        const WGPUBufferUsage bufferUsage = getBufferUsageFromMapMode(mapMode);
        WGPUBuffer buffer = createBuffer(t, kBufferSize, bufferUsage);

        std::vector<uint8_t> expectedData(kBufferSize, 0);
        {
            constexpr size_t kMapSize = 16;
            const size_t mapOffset = static_cast<size_t>(appliedOffset);
            t.expectMapAsync(buffer, static_cast<WGPUMapMode>(mapMode), true, mapOffset, kMapSize);
            if (mapMode == kMapModeRead) {
                const auto* mappedData =
                    static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(buffer, mapOffset, kMapSize));
                if (mappedData == nullptr) {
                    wgpuBufferUnmap(buffer);
                    t.fail("failed to get the mapped range");
                }
                for (size_t i = 0; i < kMapSize; ++i) {
                    t.expect(mappedData[i] == 0, "mapped byte " + std::to_string(i) + " is not zero");
                }
            } else {
                auto* mappedData = static_cast<uint8_t*>(wgpuBufferGetMappedRange(buffer, mapOffset, kMapSize));
                if (mappedData == nullptr) {
                    wgpuBufferUnmap(buffer);
                    t.fail("failed to get the mapped range");
                }
                for (size_t i = 0; i < kMapSize; ++i) {
                    t.expect(mappedData[i] == 0, "mapped byte " + std::to_string(i) + " is not zero");
                    mappedData[i] = expectedData[mapOffset + i] = static_cast<uint8_t>(i + 1);
                }
            }
            wgpuBufferUnmap(buffer);
        }

        checkGPUBufferContent(t, buffer, bufferUsage, expectedData);
    });

CTS_TEST(g, "mapped_at_creation_whole_buffer")
    .desc(R"(Verify when we call getMappedRange() at the whole range of a GPUBuffer created with
mappedAtCreation === true just after its creation, the contents of both the returned typed
array buffer of getMappedRange() and the GPUBuffer itself have all been initialized to 0.)")
    .params([](ParamsBuilder u) {
        return u.combine("bufferUsage", {kUsageCopyDstMapRead, kUsageCopySrcMapWrite, kUsageCopySrc});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const auto bufferUsage = static_cast<WGPUBufferUsage>(t.param<int>("bufferUsage"));

        constexpr size_t kBufferSize = 32;
        WGPUBuffer buffer = createBuffer(t, kBufferSize, bufferUsage, /*mappedAtCreation=*/true);

        const auto* mapped = static_cast<const uint8_t*>(wgpuBufferGetMappedRange(buffer, 0, kBufferSize));
        if (mapped == nullptr) {
            wgpuBufferUnmap(buffer);
            t.fail("failed to get the mapped range");
        }
        for (size_t i = 0; i < kBufferSize; ++i) {
            t.expect(mapped[i] == 0, "mapped byte " + std::to_string(i) + " is not zero");
        }
        wgpuBufferUnmap(buffer);

        const std::vector<uint8_t> expectedData(kBufferSize, 0);
        checkGPUBufferContent(t, buffer, bufferUsage, expectedData);
    });

CTS_TEST(g, "mapped_at_creation_partial_buffer")
    .desc(R"(Verify when we call getMappedRange() at a subrange of a GPUBuffer created with
mappedAtCreation === true just after its creation, the contents of both the returned typed
array buffer of getMappedRange() and the GPUBuffer itself have all been initialized to 0.)")
    .params([](ParamsBuilder u) {
        return u.combine("bufferUsage", {kUsageCopyDstMapRead, kUsageCopySrcMapWrite, kUsageCopySrc})
            .beginSubcases()
            .combine("offset", {0, 8, -16});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const auto bufferUsage = static_cast<WGPUBufferUsage>(t.param<int>("bufferUsage"));
        const int64_t offset = t.param<int64_t>("offset");
        constexpr uint64_t kBufferSize = 32;
        const size_t appliedOffset = static_cast<size_t>(appliedOffsetOf(offset, kBufferSize));

        WGPUBuffer buffer = createBuffer(t, kBufferSize, bufferUsage, /*mappedAtCreation=*/true);

        std::vector<uint8_t> expectedData(kBufferSize, 0);
        {
            constexpr size_t kMappedSize = 12;
            auto* mapped = static_cast<uint8_t*>(wgpuBufferGetMappedRange(buffer, appliedOffset, kMappedSize));
            if (mapped == nullptr) {
                wgpuBufferUnmap(buffer);
                t.fail("failed to get the mapped range");
            }
            for (size_t i = 0; i < kMappedSize; ++i) {
                t.expect(mapped[i] == 0, "mapped byte " + std::to_string(i) + " is not zero");
                if ((bufferUsage & WGPUBufferUsage_MapRead) == 0) {
                    mapped[i] = expectedData[appliedOffset + i] = static_cast<uint8_t>(i + 1);
                }
            }
            wgpuBufferUnmap(buffer);
        }

        checkGPUBufferContent(t, buffer, bufferUsage, expectedData);
    });

CTS_TEST(g, "copy_buffer_to_buffer_copy_source")
    .desc(R"(Verify when the first usage of a GPUBuffer is being used as the source buffer of
CopyBufferToBuffer(), the contents of the GPUBuffer have already been initialized to 0.)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr size_t kBufferSize = 32;
        const WGPUBufferUsage bufferUsage = WGPUBufferUsage_CopySrc;
        WGPUBuffer buffer = createBuffer(t, kBufferSize, bufferUsage);

        const std::vector<uint8_t> expectedData(kBufferSize, 0);
        // copyBufferToBuffer() is called inside checkGPUBufferContent().
        checkGPUBufferContent(t, buffer, bufferUsage, expectedData);
    });

CTS_TEST(g, "copy_buffer_to_texture")
    .desc(R"(Verify when the first usage of a GPUBuffer is being used as the source buffer of
CopyBufferToTexture(), the contents of the GPUBuffer have already been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 8}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");
        const WGPUExtent3D textureSize{8, 8, 1};
        const WGPUTextureFormat dstTextureFormat = WGPUTextureFormat_RGBA8Unorm;

        WGPUTexture dstTexture = createTexture2D(t, dstTextureFormat, textureSize,
                                                 WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
        const TextureCopyLayout layout =
            getTextureCopyLayout(dstTextureFormat, WGPUTextureDimension_2D, textureSize);
        const uint64_t srcBufferSize = layout.byteLength + bufferOffset;
        WGPUBuffer srcBuffer = createBuffer(t, srcBufferSize, WGPUBufferUsage_CopySrc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUTexelCopyBufferInfo src = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        src.buffer = srcBuffer;
        src.layout.offset = bufferOffset;
        src.layout.bytesPerRow = layout.bytesPerRow;
        src.layout.rowsPerImage = layout.rowsPerImage;
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = dstTexture;
        wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &textureSize);
        submit(t, encoder);

        const uint8_t transparentBlack[4] = {0, 0, 0, 0};
        checkBufferAndOutputTexture(t, srcBuffer, srcBufferSize, dstTexture, textureSize, transparentBlack);
    });

CTS_TEST(g, "resolve_query_set_to_partial_buffer")
    .desc(R"(Verify when we resolve a query set into a GPUBuffer just after creating that GPUBuffer, the
remaining part of it will be initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 256}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");
        const uint64_t bufferSize = bufferOffset + 8;
        const WGPUBufferUsage bufferUsage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_QueryResolve;
        WGPUBuffer dstBuffer = createBuffer(t, bufferSize, bufferUsage);

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = WGPUQueryType_Occlusion;
        qsDesc.count = 1;
        WGPUQuerySet querySet = t.createQuerySetTracked(qsDesc);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, dstBuffer, bufferOffset);
        submit(t, encoder);

        const std::vector<uint8_t> expectedBufferData(static_cast<size_t>(bufferSize), 0);
        checkGPUBufferContent(t, dstBuffer, bufferUsage, expectedBufferData);
    });

CTS_TEST(g, "copy_texture_to_partial_buffer")
    .desc(R"(Verify when we copy from a GPUTexture into a GPUBuffer just after creating that GPUBuffer, the
remaining part of it will be initialized to 0.)")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("bufferOffset", {0, 8, -16})
            .combine("arrayLayerCount", {1, 3})
            .combine("copyMipLevel", {0, 2})
            .combine("rowsPerImage", {16, 20})
            .filter([](const ParamRecord& p) {
                // We don't need to test the copies that will cover the whole GPUBuffer.
                return !(paramInt(p, "bufferOffset") == 0 && paramInt(p, "rowsPerImage") == 16);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int64_t bufferOffset = t.param<int64_t>("bufferOffset");
        const uint32_t arrayLayerCount = static_cast<uint32_t>(t.param<int>("arrayLayerCount"));
        const uint32_t copyMipLevel = static_cast<uint32_t>(t.param<int>("copyMipLevel"));
        const uint32_t rowsPerImage = static_cast<uint32_t>(t.param<int>("rowsPerImage"));
        const WGPUTextureFormat srcTextureFormat = WGPUTextureFormat_R8Uint;
        const WGPUExtent3D textureSize{32, 16, arrayLayerCount};

        WGPUTexture srcTexture =
            createTexture2D(t, srcTextureFormat, textureSize,
                            WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, copyMipLevel + 1);

        // getTextureCopyLayout(r8uint, '2d', textureSize, {mipLevel, bytesPerRow, rowsPerImage}).
        constexpr uint32_t kBytesPerRow = 256;
        const WGPUExtent3D mipSize =
            physicalMipSize(textureSize, srcTextureFormat, WGPUTextureDimension_2D, copyMipLevel);
        const uint64_t byteLength = uint64_t{kBytesPerRow} * rowsPerImage * (mipSize.depthOrArrayLayers - 1) +
                                    uint64_t{kBytesPerRow} * (mipSize.height - 1) + mipSize.width;

        const uint64_t absOffset = static_cast<uint64_t>(bufferOffset < 0 ? -bufferOffset : bufferOffset);
        const uint64_t dstBufferSize = byteLength + absOffset;
        WGPUBuffer dstBuffer = createBuffer(t, dstBufferSize, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        // Initialize srcTexture
        for (uint32_t layer = 0; layer < arrayLayerCount; ++layer) {
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseArrayLayer = layer;
            viewDesc.arrayLayerCount = 1;
            viewDesc.baseMipLevel = copyMipLevel;
            viewDesc.mipLevelCount = 1;
            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.view = t.createViewTracked(srcTexture, viewDesc);
            colorAttachment.clearValue = WGPUColor{static_cast<double>(layer + 1), 0.0, 0.0, 0.0};
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            WGPURenderPassEncoder renderPass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(renderPass);
        }

        // Do texture-to-buffer copy
        const uint64_t appliedOffset = bufferOffset > 0 ? static_cast<uint64_t>(bufferOffset) : 0;
        WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = srcTexture;
        src.mipLevel = copyMipLevel;
        WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer = dstBuffer;
        dst.layout.offset = appliedOffset;
        dst.layout.bytesPerRow = kBytesPerRow;
        dst.layout.rowsPerImage = rowsPerImage;
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &mipSize);
        submit(t, encoder);

        // Check if the contents of the destination buffer are what we expect.
        std::vector<uint8_t> expectedData(static_cast<size_t>(dstBufferSize), 0);
        for (uint32_t layer = 0; layer < arrayLayerCount; ++layer) {
            for (uint32_t y = 0; y < mipSize.height; ++y) {
                for (uint32_t x = 0; x < mipSize.width; ++x) {
                    const uint64_t index = appliedOffset + uint64_t{layer} * kBytesPerRow * rowsPerImage +
                                           uint64_t{y} * kBytesPerRow + x;
                    expectedData[static_cast<size_t>(index)] = static_cast<uint8_t>(layer + 1);
                }
            }
        }
        t.expectGPUBufferValuesEqual(dstBuffer, expectedData.data(), expectedData.size());
    });

void runBindGroupZeroInitTest(AllFeaturesMaxLimitsGpuTest& t, std::string_view shader, WGPUBufferUsage usage) {
    const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");
    constexpr uint64_t kBoundBufferSize = 16;
    const uint64_t bufferSize = bufferOffset + kBoundBufferSize;
    WGPUBuffer buffer = createBuffer(t, bufferSize, WGPUBufferUsage_CopySrc | usage);

    // Verify the whole range of the buffer has been initialized to 0 in a compute shader.
    testBufferZeroInitInBindGroup(t, shader, buffer, bufferSize, bufferOffset, kBoundBufferSize);
}

CTS_TEST(g, "uniform_buffer")
    .desc(R"(Verify when we use a GPUBuffer as a uniform buffer just after the creation of that GPUBuffer,
    all the contents in that GPUBuffer have been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 256}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBindGroupZeroInitTest(t, kUniformComputeShader, WGPUBufferUsage_Uniform);
    });

CTS_TEST(g, "readonly_storage_buffer")
    .desc(R"(Verify when we use a GPUBuffer as a read-only storage buffer just after the creation of that
    GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 256}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBindGroupZeroInitTest(t, kReadonlyStorageComputeShader, WGPUBufferUsage_Storage);
    });

CTS_TEST(g, "storage_buffer")
    .desc(R"(Verify when we use a GPUBuffer as a storage buffer just after the creation of that
    GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 256}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBindGroupZeroInitTest(t, kStorageComputeShader, WGPUBufferUsage_Storage);
    });

WGPUTexture createRenderOutputTexture(AllFeaturesMaxLimitsGpuTest& t) {
    return createTexture2D(t, WGPUTextureFormat_RGBA8Unorm, WGPUExtent3D{1, 1, 1},
                           WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment);
}

WGPURenderPassEncoder beginOutputPass(AllFeaturesMaxLimitsGpuTest& t,
                                      WGPUCommandEncoder encoder,
                                      WGPUTexture outputTexture,
                                      WGPULoadOp loadOp) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = createDefaultView(t, outputTexture);
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    colorAttachment.loadOp = loadOp;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    return t.beginRenderPassTracked(encoder, &passDesc);
}

CTS_TEST(g, "vertex_buffer")
    .desc(R"(Verify when we use a GPUBuffer as a vertex buffer just after the creation of that
  GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 16}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");

        WGPURenderPipeline renderPipeline = createRenderPipelineForTest(t, kVertexBufferVertexShader, true);

        const uint64_t bufferSize = 16 + bufferOffset;
        WGPUBuffer vertexBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopySrc);
        WGPUTexture outputTexture = createRenderOutputTexture(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder renderPass = beginOutputPass(t, encoder, outputTexture, WGPULoadOp_Clear);
        wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, bufferOffset, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
        wgpuRenderPassEncoderDraw(renderPass, 1, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);
        submit(t, encoder);

        checkBufferAndOutputTexture(t, vertexBuffer, bufferSize, outputTexture);
    });

CTS_TEST(g, "index_buffer")
    .desc(R"(Verify when we use a GPUBuffer as an index buffer just after the creation of that
GPUBuffer, all the contents in that GPUBuffer have been initialized to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 16}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");

        WGPURenderPipeline renderPipeline = createRenderPipelineForTest(t, kIndexBufferVertexShader, false);

        // The size of GPUBuffer must be at least 4.
        const uint64_t bufferSize = 4 + bufferOffset;
        WGPUBuffer indexBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Index | WGPUBufferUsage_CopySrc);
        WGPUTexture outputTexture = createRenderOutputTexture(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder renderPass = beginOutputPass(t, encoder, outputTexture, WGPULoadOp_Clear);
        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
        wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer, WGPUIndexFormat_Uint16, bufferOffset, 4);
        wgpuRenderPassEncoderDrawIndexed(renderPass, 1, 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);
        submit(t, encoder);

        checkBufferAndOutputTexture(t, indexBuffer, bufferSize, outputTexture);
    });

CTS_TEST(g, "indirect_buffer_for_draw_indirect")
    .desc(R"(Verify when we use a GPUBuffer as an indirect buffer for drawIndirect() or
drawIndexedIndirect() just after the creation of that GPUBuffer, all the contents in that GPUBuffer
have been initialized to 0.)")
    .params([](ParamsBuilder u) {
        return u.combine("test_indexed_draw", {true, false}).beginSubcases().combine("bufferOffset", {0, 16});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool testIndexedDraw = t.param<bool>("test_indexed_draw");
        const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");

        WGPURenderPipeline renderPipeline = createRenderPipelineForTest(t, kIndirectDrawVertexShader, false);

        constexpr uint64_t kDrawIndirectParametersSize = 16;
        constexpr uint64_t kDrawIndexedIndirectParametersSize = 20;
        const uint64_t bufferSize =
            std::max(kDrawIndirectParametersSize, kDrawIndexedIndirectParametersSize) + bufferOffset;
        WGPUBuffer indirectBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_CopySrc | WGPUBufferUsage_Indirect);
        WGPUTexture outputTexture = createRenderOutputTexture(t);

        // Initialize outputTexture to green.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        recordInitializeTextureColor(t, encoder, outputTexture, WGPUColor{0.0, 1.0, 0.0, 1.0});

        WGPURenderPassEncoder renderPass = beginOutputPass(t, encoder, outputTexture, WGPULoadOp_Load);
        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
        if (testIndexedDraw) {
            WGPUBuffer indexBuffer = createBuffer(t, 4, WGPUBufferUsage_Index);
            wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexedIndirect(renderPass, indirectBuffer, bufferOffset);
        } else {
            wgpuRenderPassEncoderDrawIndirect(renderPass, indirectBuffer, bufferOffset);
        }
        wgpuRenderPassEncoderEnd(renderPass);
        submit(t, encoder);

        // The indirect buffer should be lazily cleared to 0, so we actually draw nothing and the color
        // attachment will keep its original color (green) after we end the render pass.
        checkBufferAndOutputTexture(t, indirectBuffer, bufferSize, outputTexture);
    });

CTS_TEST(g, "indirect_buffer_for_dispatch_indirect")
    .desc(R"(Verify when we use a GPUBuffer as an indirect buffer for dispatchWorkgroupsIndirect() just
    after the creation of that GPUBuffer, all the contents in that GPUBuffer have been initialized
    to 0.)")
    .params([](ParamsBuilder u) { return u.beginSubcases().combine("bufferOffset", {0, 16}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t bufferOffset = t.param<uint64_t>("bufferOffset");

        WGPUComputePipeline computePipeline = getComputePipeline(t, kDispatchIndirectComputeShader);

        constexpr uint64_t kDispatchIndirectParametersSize = 12;
        const uint64_t bufferSize = kDispatchIndirectParametersSize + bufferOffset;
        WGPUBuffer indirectBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_CopySrc | WGPUBufferUsage_Indirect);

        WGPUTexture outputTexture =
            createTexture2D(t, WGPUTextureFormat_RGBA8Unorm, WGPUExtent3D{1, 1, 1},
                            WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment |
                                WGPUTextureUsage_StorageBinding);

        // Initialize outputTexture to green.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        recordInitializeTextureColor(t, encoder, outputTexture, WGPUColor{0.0, 1.0, 0.0, 1.0});

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.textureView = createDefaultView(t, outputTexture);
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(computePipeline, 0);
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(layout);

        // The indirect buffer should be lazily cleared to 0, so we actually don't execute the compute
        // shader and the output texture should keep its original color (green).
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder computePass = t.beginComputePassTracked(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(computePass, computePipeline);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(computePass, indirectBuffer, bufferOffset);
        wgpuComputePassEncoderEnd(computePass);
        submit(t, encoder);

        // The indirect buffer should be lazily cleared to 0, so we actually draw nothing and the color
        // attachment will keep its original color (green) after we end the compute pass.
        checkBufferAndOutputTexture(t, indirectBuffer, bufferSize, outputTexture);
    });

} // namespace
