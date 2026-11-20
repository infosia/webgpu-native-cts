// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/buffer/buffer_sync_test.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Shared by memory_sync/buffer/{single_buffer,multiple_buffers}.spec.cpp.
//
// Porting notes:
//   - Upstream case params for 'rw'/'wr' come from combine('boundary') + expand('_context') +
//     expandWithParams(...). `_context` is private upstream (not in the query), so the fully
//     expanded public records (boundary, readOp, readContext, writeOp, writeContext) are generated
//     host-side (readWriteCaseRecords) and fed to combineWithParams(); the query identity matches.
//   - 'ww' public params are the arrays writeOps=[first,second] and contexts=[first,second]
//     upstream. The harness Value type has no arrays, so each is a comma-joined string
//     (writeOps="storage,b2b-copy"); names, order and counts match upstream.
//   - skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStage... and
//     skipIfNoSupportForStorageBuffersInFragmentStage skip only in compatibility mode upstream. The
//     harness runs core devices, where fragment-stage storage buffers are always available, so the
//     ports are no-ops (kept so call sites mirror upstream).
//   - Pipelines use layout 'auto' like upstream. They do not depend on the case, so they are
//     created once per device (BufferSyncPipelineCache) instead of per call; bind groups and every
//     buffer/texture stay per case and tracked.
//   - All dst/readback buffers are created non-mapped (zero-initialized), never pre-filled with the
//     expected value.
#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
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
#include "webgpu/api/operation/memory_sync/operation_context_helper.h"

namespace cts::memory_sync {

// ---------------------------------------------------------------------------
// Fixture (port of BufferSyncTest)
// ---------------------------------------------------------------------------

class BufferSyncTest : public AllFeaturesMaxLimitsGpuTest {
  public:
    // Vertex and index buffers used in read render pass.
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer indexBuffer = nullptr;

    // Temp buffer and texture with values for buffer/texture copy write op.
    // There can be at most 2 write ops.
    std::array<WGPUBuffer, 2> tmpValueBuffers = {nullptr, nullptr};
    std::array<WGPUTexture, 2> tmpValueTextures = {nullptr, nullptr};

    // Upstream: skips only in compatibility mode (maxStorageBuffersInFragmentStage < 2). Core
    // devices always allow storage buffers in the fragment stage.
    void skipIfNoSupportForStorageBuffersInFragmentStage() {}
    void skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStageAndNoSupportStorageBuffersInFragmentShaders() {}
};

inline constexpr uint64_t kBufferSyncElementSize = sizeof(uint32_t);

inline constexpr WGPUBufferUsage kBufferSyncAllUsages =
    WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex |
    WGPUBufferUsage_Index | WGPUBufferUsage_Indirect | WGPUBufferUsage_Uniform;

inline WGPUStringView bufferSyncStringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// ---------------------------------------------------------------------------
// Read/write op tables and validity filter
// ---------------------------------------------------------------------------

inline constexpr std::array<const char*, 4> kAllWriteOps = {
    "storage",
    "b2b-copy",
    "t2b-copy",
    "write-buffer",
};

inline constexpr std::array<const char*, 9> kAllReadOps = {
    "input-vertex",
    "input-index",
    "input-indirect",
    "input-indirect-index",
    "input-indirect-dispatch",
    "constant-uniform",
    "storage-read",
    "b2b-copy",
    "b2t-copy",
};

// Port of kOpInfo: the contexts each op is permitted in.
inline bool bufferOpValidInContext(std::string_view op, std::string_view context) {
    if (op == "write-buffer") {
        return context == "queue";
    }
    if (op == "b2t-copy" || op == "b2b-copy" || op == "t2b-copy") {
        return context == "command-encoder";
    }
    if (op == "storage" || op == "storage-read") {
        return context == "compute-pass-encoder" || context == "render-pass-encoder" ||
               context == "render-bundle-encoder";
    }
    if (op == "input-vertex" || op == "input-index" || op == "input-indirect" || op == "input-indirect-index" ||
        op == "constant-uniform") {
        return context == "render-pass-encoder" || context == "render-bundle-encoder";
    }
    if (op == "input-indirect-dispatch") {
        return context == "compute-pass-encoder";
    }
    std::abort();
}

// In a render pass it is invalid to use a resource as both writable and another usage; also
// storage+storage is racy and cannot be reliably tested, so those pairs are filtered.
inline bool bufferCheckImpl(std::string_view op1, std::string_view op2) {
    if (op1 == "storage") {
        if (op2 == "storage" || op2 == "storage-read" || op2 == "input-vertex" || op2 == "input-index" ||
            op2 == "input-indirect" || op2 == "input-indirect-index" || op2 == "constant-uniform") {
            // Write+other, or racy.
            return false;
        }
    }
    return true;
}

inline bool checkBufferOpsValidForContext(std::string_view op0,
                                          std::string_view op1,
                                          std::string_view context0,
                                          std::string_view context1) {
    if (!bufferOpValidInContext(op0, context0) || !bufferOpValidInContext(op1, context1)) {
        return false;
    }
    if (isRenderContext(context0) || isRenderContext(context1)) {
        return bufferCheckImpl(op0, op1) && bufferCheckImpl(op1, op0);
    }
    return true;
}

// Fully expanded case records for 'rw' and 'wr' (the same generator upstream):
// boundary;readOp;readContext;writeOp;writeContext.
inline std::vector<ParamRecord> readWriteCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* readOp : kAllReadOps) {
                for (const char* writeOp : kAllWriteOps) {
                    if (checkBufferOpsValidForContext(readOp, writeOp, context.first, context.second)) {
                        records.push_back(ParamRecord{
                            {"boundary", Value(boundary)},
                            {"readOp", Value(readOp)},
                            {"readContext", Value(context.first)},
                            {"writeOp", Value(writeOp)},
                            {"writeContext", Value(context.second)},
                        });
                    }
                }
            }
        }
    }
    return records;
}

// Fully expanded case records for 'ww': boundary;writeOps;contexts (arrays comma-joined).
inline std::vector<ParamRecord> writeWriteCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* firstWriteOp : kAllWriteOps) {
                for (const char* secondWriteOp : kAllWriteOps) {
                    if (checkBufferOpsValidForContext(firstWriteOp, secondWriteOp, context.first, context.second)) {
                        records.push_back(ParamRecord{
                            {"boundary", Value(boundary)},
                            {"writeOps", Value(std::string(firstWriteOp) + "," + secondWriteOp)},
                            {"contexts", Value(std::string(context.first) + "," + context.second)},
                        });
                    }
                }
            }
        }
    }
    return records;
}

inline std::array<std::string, 2> splitPair(const std::string& joined) {
    const size_t comma = joined.find(',');
    if (comma == std::string::npos) {
        std::abort();
    }
    return {{joined.substr(0, comma), joined.substr(comma + 1)}};
}

// ---------------------------------------------------------------------------
// WGSL shaders
// ---------------------------------------------------------------------------

inline constexpr std::string_view kBufferSyncDummyVertexShader = R"(
@vertex fn vert_main() -> @builtin(position) vec4<f32> {
  return vec4<f32>(0.5, 0.5, 0.0, 1.0);
}
)";

inline constexpr std::string_view kStorageReadComputeShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read> srcData : Data;
@group(0) @binding(1) var<storage, read_write> dstData : Data;

@compute @workgroup_size(1) fn main() {
  dstData.a = srcData.a;
}
)";

inline constexpr std::string_view kVertexReadVertexShader = R"(
struct VertexOutput {
  @builtin(position) position : vec4<f32>,
  @location(0) @interpolate(flat, either) data : u32,
};

@vertex fn vert_main(@location(0) input: u32) -> VertexOutput {
  var output : VertexOutput;
  output.position = vec4<f32>(0.5, 0.5, 0.0, 1.0);
  output.data = input;
  return output;
}
)";

inline constexpr std::string_view kVertexReadFragmentShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read_write> data : Data;

@fragment fn frag_main(@location(0) @interpolate(flat, either) input : u32) -> @location(0) vec4<f32> {
  data.a = input;
  return vec4<f32>();  // result does't matter
}
)";

inline constexpr std::string_view kUniformReadFragmentShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<uniform> constant: Data;
@group(0) @binding(1) var<storage, read_write> data : Data;

@fragment fn frag_main() -> @location(0) vec4<f32> {
  data.a = constant.a;
  return vec4<f32>();  // result does't matter
}
)";

inline constexpr std::string_view kStorageReadFragmentShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read> srcData : Data;
@group(0) @binding(1) var<storage, read_write> dstData : Data;

@fragment fn frag_main() -> @location(0) vec4<f32> {
  dstData.a = srcData.a;
  return vec4<f32>();  // result does't matter
}
)";

inline std::string storageWriteComputeShader(uint32_t value) {
    std::ostringstream shader;
    shader << R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read_write> data : Data;
@compute @workgroup_size(1) fn main() {
  data.a = )" << value << R"(u;
}
)";
    return shader.str();
}

inline std::string storageWriteFragmentShader(uint32_t value) {
    std::ostringstream shader;
    shader << R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read_write> data : Data;
@fragment fn frag_main() -> @location(0) vec4<f32> {
  data.a = )" << value << R"(u;
  return vec4<f32>();  // result does't matter
}
)";
    return shader.str();
}

// ---------------------------------------------------------------------------
// Per-device pipeline cache (pipelines use layout 'auto' and do not depend on the case)
// ---------------------------------------------------------------------------

struct BufferSyncPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<std::string, WGPUComputePipeline>> compute;
    std::unordered_map<WGPUDevice, std::map<std::string, WGPURenderPipeline>> render;

    ~BufferSyncPipelineCache() override {
        for (auto& [device, pipelines] : compute) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                if (pipeline != nullptr) {
                    wgpuComputePipelineRelease(pipeline);
                }
            }
        }
        for (auto& [device, pipelines] : render) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                if (pipeline != nullptr) {
                    wgpuRenderPipelineRelease(pipeline);
                }
            }
        }
    }
};

inline WGPUShaderModule createUntrackedShaderModule(WGPUDevice device, std::string_view wgsl) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = bufferSyncStringView(wgsl);
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

inline WGPUComputePipeline cachedComputePipelineAuto(GpuTest& t, const std::string& key, std::string_view wgsl) {
    auto& pipelines = deviceScoped<BufferSyncPipelineCache>().compute[t.device()];
    auto it = pipelines.find(key);
    if (it != pipelines.end()) {
        return it->second;
    }
    WGPUShaderModule module = createUntrackedShaderModule(t.device(), wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // auto layout
    desc.compute.module = module;
    desc.compute.entryPoint = bufferSyncStringView("main");
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(t.device(), &desc);
    wgpuShaderModuleRelease(module);
    pipelines.emplace(key, pipeline);
    return pipeline;
}

// Upstream createTrivialRenderPipeline / createVertexReadRenderPipeline: point-list, one
// rgba8unorm target, layout 'auto'; `withVertexBuffer` adds the uint32 vertex buffer layout.
inline WGPURenderPipeline cachedRenderPipelineAuto(GpuTest& t,
                                                   const std::string& key,
                                                   std::string_view vertexWgsl,
                                                   std::string_view fragmentWgsl,
                                                   bool withVertexBuffer) {
    auto& pipelines = deviceScoped<BufferSyncPipelineCache>().render[t.device()];
    auto it = pipelines.find(key);
    if (it != pipelines.end()) {
        return it->second;
    }
    WGPUShaderModule vertexModule = createUntrackedShaderModule(t.device(), vertexWgsl);
    WGPUShaderModule fragmentModule = createUntrackedShaderModule(t.device(), fragmentWgsl);

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    attribute.format = WGPUVertexFormat_Uint32;
    attribute.offset = 0;
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.arrayStride = sizeof(uint32_t);
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &attribute;

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = bufferSyncStringView("frag_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // auto layout
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = bufferSyncStringView("vert_main");
    if (withVertexBuffer) {
        desc.vertex.bufferCount = 1;
        desc.vertex.buffers = &vertexBufferLayout;
    }
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(t.device(), &desc);
    wgpuShaderModuleRelease(vertexModule);
    wgpuShaderModuleRelease(fragmentModule);
    pipelines.emplace(key, pipeline);
    return pipeline;
}

// Create a compute pipeline and write given data into storage buffer.
inline WGPUComputePipeline createStorageWriteComputePipeline(GpuTest& t, uint32_t value) {
    return cachedComputePipelineAuto(t, "storage-write:" + std::to_string(value), storageWriteComputeShader(value));
}

// Create a compute pipeline: read from src buffer and write it into the storage buffer.
inline WGPUComputePipeline createStorageReadComputePipeline(GpuTest& t) {
    return cachedComputePipelineAuto(t, "storage-read", kStorageReadComputeShader);
}

// Create a render pipeline and write given data into storage buffer at fragment stage.
inline WGPURenderPipeline createStorageWriteRenderPipeline(GpuTest& t, uint32_t value) {
    return cachedRenderPipelineAuto(t, "storage-write:" + std::to_string(value), kBufferSyncDummyVertexShader,
                                    storageWriteFragmentShader(value), false);
}

// Create a render pipeline: read from vertex/index buffer and write it into the storage dst
// buffer at fragment stage.
inline WGPURenderPipeline createVertexReadRenderPipeline(GpuTest& t) {
    return cachedRenderPipelineAuto(t, "vertex-read", kVertexReadVertexShader, kVertexReadFragmentShader, true);
}

// Create a render pipeline: read from uniform buffer and write it into the storage dst buffer
// at fragment stage.
inline WGPURenderPipeline createUniformReadRenderPipeline(GpuTest& t) {
    return cachedRenderPipelineAuto(t, "uniform-read", kBufferSyncDummyVertexShader, kUniformReadFragmentShader,
                                    false);
}

// Create a render pipeline: read from storage src buffer and write it into the storage dst
// buffer at fragment stage.
inline WGPURenderPipeline createStorageReadRenderPipeline(GpuTest& t) {
    return cachedRenderPipelineAuto(t, "storage-read", kBufferSyncDummyVertexShader, kStorageReadFragmentShader,
                                    false);
}

// ---------------------------------------------------------------------------
// Bind group helpers
// ---------------------------------------------------------------------------

inline WGPUBindGroup createSingleBufferBindGroup(GpuTest& t, WGPUBindGroupLayout layout, WGPUBuffer buffer) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer; // whole-buffer binding (INIT default size = WGPU_WHOLE_SIZE)

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

inline WGPUBindGroup createSrcDstBufferBindGroup(GpuTest& t,
                                                 WGPUBindGroupLayout layout,
                                                 WGPUBuffer srcBuffer,
                                                 WGPUBuffer dstBuffer) {
    std::array<WGPUBindGroupEntry, 2> entries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].buffer = srcBuffer;
    entries[1].binding = 1;
    entries[1].buffer = dstBuffer;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

// Port of createBindGroup(pipeline, buffer). The layout is a getter result, released manually.
inline WGPUBindGroup createBindGroup(GpuTest& t, WGPUComputePipeline pipeline, WGPUBuffer buffer) {
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSingleBufferBindGroup(t, layout, buffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

inline WGPUBindGroup createBindGroup(GpuTest& t, WGPURenderPipeline pipeline, WGPUBuffer buffer) {
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSingleBufferBindGroup(t, layout, buffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

inline WGPUBindGroup createBindGroupSrcDstBuffer(GpuTest& t,
                                                 WGPUComputePipeline pipeline,
                                                 WGPUBuffer srcBuffer,
                                                 WGPUBuffer dstBuffer) {
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSrcDstBufferBindGroup(t, layout, srcBuffer, dstBuffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

inline WGPUBindGroup createBindGroupSrcDstBuffer(GpuTest& t,
                                                 WGPURenderPipeline pipeline,
                                                 WGPUBuffer srcBuffer,
                                                 WGPUBuffer dstBuffer) {
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSrcDstBufferBindGroup(t, layout, srcBuffer, dstBuffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

// ---------------------------------------------------------------------------
// Buffer / texture creation helpers
// ---------------------------------------------------------------------------

// Create a buffer initialized to the specified u32 values (mappedAtCreation), then wait for the
// queue like upstream's onSubmittedWorkDone await.
inline WGPUBuffer createBufferWithValues(BufferSyncTest& t, const std::vector<uint32_t>& initValues) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.mappedAtCreation = true;
    desc.size = sizeof(uint32_t) * initValues.size();
    desc.usage = kBufferSyncAllUsages;
    WGPUBuffer buffer = t.createBufferTracked(desc);
    void* mapped = wgpuBufferGetMappedRange(buffer, 0, static_cast<size_t>(desc.size));
    if (mapped == nullptr) {
        t.fail("getMappedRange failed on mappedAtCreation buffer");
    }
    std::memcpy(mapped, initValues.data(), static_cast<size_t>(desc.size));
    wgpuBufferUnmap(buffer);
    t.onSubmittedWorkDoneSync();
    return buffer;
}

// Create a buffer with 1 uint32 element, and initialize it to a specified value.
inline WGPUBuffer createBufferWithValue(BufferSyncTest& t, uint32_t initValue) {
    return createBufferWithValues(t, {initValue});
}

// Create a 1x1 r32uint texture, and initialize it to a specified value.
inline WGPUTexture createTextureWithValue(BufferSyncTest& t, uint32_t initValue) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_R32Uint;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    WGPUTexture texture = t.createTextureTracked(desc);

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = 256;
    layout.rowsPerImage = 1;
    const uint32_t data = initValue;
    t.queueWriteTexture(texture, WGPUExtent3D{1, 1, 1}, layout, &data, sizeof(data));
    t.onSubmittedWorkDoneSync();
    return texture;
}

// These intermediate buffers/textures are created before any read/write op to avoid extra memory
// synchronization between ops introduced by waiting on buffer/texture creation.
inline void createIntermediateBuffersAndTexturesForWriteOp(BufferSyncTest& t,
                                                           const std::string& writeOp,
                                                           size_t slot,
                                                           uint32_t value) {
    if (writeOp == "b2b-copy") {
        t.tmpValueBuffers[slot] = createBufferWithValue(t, value);
    } else if (writeOp == "t2b-copy") {
        t.tmpValueTextures[slot] = createTextureWithValue(t, value);
    }
}

struct ReadOpBuffers {
    WGPUBuffer srcBuffer = nullptr;
    WGPUBuffer dstBuffer = nullptr;
};

// Create extra buffers needed by a read operation (port of createBuffersForReadOp).
inline ReadOpBuffers createBuffersForReadOp(BufferSyncTest& t,
                                            const std::string& readOp,
                                            uint32_t srcValue,
                                            uint32_t opValue) {
    if (readOp == "input-index") {
        // The index buffer will be the src buffer of the read op. Index value 0 selects
        // vertexBuffer[0] (src value); index value 1 selects vertexBuffer[1] (op value).
        t.vertexBuffer = createBufferWithValues(t, {srcValue, opValue});
    } else if (readOp == "input-indirect") {
        // If indirect vertexCount is 1, the op value in the vertex buffer is written to dst;
        // if it is 0, nothing is written.
        t.vertexBuffer = createBufferWithValues(t, {opValue});
    } else if (readOp == "input-indirect-index") {
        t.vertexBuffer = createBufferWithValues(t, {opValue});
        t.indexBuffer = createBufferWithValues(t, {0});
    }

    ReadOpBuffers buffers;
    if (readOp == "input-indirect") {
        // vertexCount = {0, 1}, instanceCount = 1, firstVertex = 0, firstInstance = 0
        buffers.srcBuffer = createBufferWithValues(t, {srcValue, 1, 0, 0});
    } else if (readOp == "input-indirect-index") {
        // indexCount = {0, 1}, instanceCount = 1, firstIndex = 0, baseVertex = 0, firstInstance = 0
        buffers.srcBuffer = createBufferWithValues(t, {srcValue, 1, 0, 0, 0});
    } else if (readOp == "input-indirect-dispatch") {
        // workgroupCountX = {0, 1}, workgroupCountY = 1, workgroupCountZ = 1
        buffers.srcBuffer = createBufferWithValues(t, {srcValue, 1, 1});
    } else {
        buffers.srcBuffer = createBufferWithValue(t, srcValue);
    }

    // Non-mapped dst buffer: zero-initialized, never pre-filled with the expected value.
    WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    dstDesc.size = kBufferSyncElementSize;
    dstDesc.usage = kBufferSyncAllUsages;
    buffers.dstBuffer = t.createBufferTracked(dstDesc);
    return buffers;
}

// ---------------------------------------------------------------------------
// encodeWriteOp / encodeReadOp
// ---------------------------------------------------------------------------

// Write buffer via draw call in render pass (or bundle).
inline void encodeWriteAsStorageBufferInRenderPass(BufferSyncTest& t,
                                                   const RenderEncoder& renderer,
                                                   WGPUBuffer buffer,
                                                   uint32_t value) {
    WGPURenderPipeline pipeline = createStorageWriteRenderPipeline(t, value);
    WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
    renderer.setBindGroup(0, bindGroup);
    renderer.setPipeline(pipeline);
    renderer.draw(1);
}

// Write buffer via dispatch call in compute pass.
inline void encodeWriteAsStorageBufferInComputePass(BufferSyncTest& t,
                                                    WGPUComputePassEncoder pass,
                                                    WGPUBuffer buffer,
                                                    uint32_t value) {
    WGPUComputePipeline pipeline = createStorageWriteComputePipeline(t, value);
    WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
}

// Issue a write operation via render pass, compute pass, copy, writeBuffer, etc.
inline void encodeWriteOp(BufferSyncTest& t,
                          OperationContextHelper& helper,
                          const std::string& operation,
                          OpContext context,
                          WGPUBuffer buffer,
                          size_t writeOpSlot,
                          uint32_t value) {
    helper.ensureContext(context);

    if (operation == "write-buffer") {
        const uint32_t data = value;
        t.queueWriteBuffer(buffer, 0, &data, sizeof(data));
        return;
    }
    if (operation == "storage") {
        switch (context) {
            case OpContext::RenderPassEncoder:
            case OpContext::RenderBundleEncoder:
                encodeWriteAsStorageBufferInRenderPass(t, helper.rendererFor(context), buffer, value);
                return;
            case OpContext::ComputePassEncoder:
                if (helper.computePassEncoder == nullptr) {
                    std::abort();
                }
                encodeWriteAsStorageBufferInComputePass(t, helper.computePassEncoder, buffer, value);
                return;
            default:
                std::abort();
        }
    }
    if (operation == "b2b-copy") {
        // The write operation via b2b copy is just encoded into the command encoder.
        WGPUBuffer tmpBuffer = t.tmpValueBuffers[writeOpSlot];
        if (helper.commandEncoder == nullptr || tmpBuffer == nullptr) {
            std::abort();
        }
        wgpuCommandEncoderCopyBufferToBuffer(helper.commandEncoder, tmpBuffer, 0, buffer, 0, kBufferSyncElementSize);
        return;
    }
    if (operation == "t2b-copy") {
        // The write operation via t2b copy is just encoded into the command encoder.
        WGPUTexture tmpTexture = t.tmpValueTextures[writeOpSlot];
        if (helper.commandEncoder == nullptr || tmpTexture == nullptr) {
            std::abort();
        }
        t.copyTextureToBuffer(helper.commandEncoder, tmpTexture, buffer, 256, WGPUExtent3D{1, 1, 1});
        return;
    }
    std::abort();
}

// Issue a read operation, which writes what it reads into dstBuffer.
inline void encodeReadOp(BufferSyncTest& t,
                         OperationContextHelper& helper,
                         const std::string& operation,
                         OpContext context,
                         WGPUBuffer srcBuffer,
                         WGPUBuffer dstBuffer) {
    helper.ensureContext(context);

    const RenderEncoder renderer = helper.rendererFor(context);

    if (operation == "input-vertex") {
        // The srcBuffer is used as vertexBuffer; draw writes srcBuffer[0] to dstBuffer[0].
        WGPURenderPipeline pipeline = createVertexReadRenderPipeline(t);
        WGPUBindGroup bindGroup = createBindGroup(t, pipeline, dstBuffer);
        renderer.setBindGroup(0, bindGroup);
        renderer.setPipeline(pipeline);
        renderer.setVertexBuffer(0, srcBuffer);
        renderer.draw(1);
        return;
    }
    if (operation == "input-index") {
        // The srcBuffer is used as indexBuffer; with this vertexBuffer, drawIndexed writes the
        // same value as srcBuffer[0] to dstBuffer[0].
        if (t.vertexBuffer == nullptr) {
            std::abort();
        }
        WGPURenderPipeline pipeline = createVertexReadRenderPipeline(t);
        WGPUBindGroup bindGroup = createBindGroup(t, pipeline, dstBuffer);
        renderer.setBindGroup(0, bindGroup);
        renderer.setPipeline(pipeline);
        renderer.setVertexBuffer(0, t.vertexBuffer);
        renderer.setIndexBufferUint32(srcBuffer);
        renderer.drawIndexed(1);
        return;
    }
    if (operation == "input-indirect") {
        // srcBuffer[0] = 0 or 1 (vertexCount) decides whether the op value is written to dstBuffer.
        if (t.vertexBuffer == nullptr) {
            std::abort();
        }
        WGPURenderPipeline pipeline = createVertexReadRenderPipeline(t);
        WGPUBindGroup bindGroup = createBindGroup(t, pipeline, dstBuffer);
        renderer.setBindGroup(0, bindGroup);
        renderer.setPipeline(pipeline);
        renderer.setVertexBuffer(0, t.vertexBuffer);
        renderer.drawIndirect(srcBuffer);
        return;
    }
    if (operation == "input-indirect-index") {
        // srcBuffer[0] = 0 or 1 (indexCount) decides whether the op value is written to dstBuffer.
        if (t.vertexBuffer == nullptr || t.indexBuffer == nullptr) {
            std::abort();
        }
        WGPURenderPipeline pipeline = createVertexReadRenderPipeline(t);
        WGPUBindGroup bindGroup = createBindGroup(t, pipeline, dstBuffer);
        renderer.setBindGroup(0, bindGroup);
        renderer.setPipeline(pipeline);
        renderer.setVertexBuffer(0, t.vertexBuffer);
        renderer.setIndexBufferUint32(t.indexBuffer);
        renderer.drawIndexedIndirect(srcBuffer);
        return;
    }
    if (operation == "input-indirect-dispatch") {
        // srcBuffer[0] = 0 or 1 (workgroupCountX) decides whether 1 is written to dstBuffer
        // (upstream passes the literal 1 here).
        if (helper.computePassEncoder == nullptr) {
            std::abort();
        }
        WGPUComputePipeline pipeline = createStorageWriteComputePipeline(t, 1);
        WGPUBindGroup bindGroup = createBindGroup(t, pipeline, dstBuffer);
        wgpuComputePassEncoderSetPipeline(helper.computePassEncoder, pipeline);
        wgpuComputePassEncoderSetBindGroup(helper.computePassEncoder, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(helper.computePassEncoder, srcBuffer, 0);
        return;
    }
    if (operation == "constant-uniform") {
        // The srcBuffer is used as uniform buffer.
        WGPURenderPipeline pipeline = createUniformReadRenderPipeline(t);
        WGPUBindGroup bindGroup = createBindGroupSrcDstBuffer(t, pipeline, srcBuffer, dstBuffer);
        renderer.setBindGroup(0, bindGroup);
        renderer.setPipeline(pipeline);
        renderer.draw(1);
        return;
    }
    if (operation == "storage-read") {
        switch (context) {
            case OpContext::RenderPassEncoder:
            case OpContext::RenderBundleEncoder: {
                WGPURenderPipeline pipeline = createStorageReadRenderPipeline(t);
                WGPUBindGroup bindGroup = createBindGroupSrcDstBuffer(t, pipeline, srcBuffer, dstBuffer);
                renderer.setBindGroup(0, bindGroup);
                renderer.setPipeline(pipeline);
                renderer.draw(1);
                return;
            }
            case OpContext::ComputePassEncoder: {
                if (helper.computePassEncoder == nullptr) {
                    std::abort();
                }
                WGPUComputePipeline pipeline = createStorageReadComputePipeline(t);
                WGPUBindGroup bindGroup = createBindGroupSrcDstBuffer(t, pipeline, srcBuffer, dstBuffer);
                wgpuComputePassEncoderSetPipeline(helper.computePassEncoder, pipeline);
                wgpuComputePassEncoderSetBindGroup(helper.computePassEncoder, 0, bindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(helper.computePassEncoder, 1, 1, 1);
                return;
            }
            default:
                std::abort();
        }
    }
    if (operation == "b2b-copy") {
        if (helper.commandEncoder == nullptr) {
            std::abort();
        }
        wgpuCommandEncoderCopyBufferToBuffer(helper.commandEncoder, srcBuffer, 0, dstBuffer, 0,
                                             kBufferSyncElementSize);
        return;
    }
    if (operation == "b2t-copy") {
        if (helper.commandEncoder == nullptr) {
            std::abort();
        }
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_R32Uint;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture tmpTexture = t.createTextureTracked(texDesc);
        t.copyBufferToTexture(helper.commandEncoder, srcBuffer, 256, tmpTexture, WGPUExtent3D{1, 1, 1});
        t.copyTextureToBuffer(helper.commandEncoder, tmpTexture, dstBuffer, 256, WGPUExtent3D{1, 1, 1});
        return;
    }
    std::abort();
}

// ---------------------------------------------------------------------------
// Verification helpers
// ---------------------------------------------------------------------------

inline void verifyData(GpuTest& t, WGPUBuffer buffer, uint32_t expectedValue) {
    t.expectGPUBufferValuesEqual(buffer, &expectedValue, sizeof(expectedValue));
}

inline void verifyDataTwoValidValues(GpuTest& t, WGPUBuffer buffer, uint32_t expectedValue1, uint32_t expectedValue2) {
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [expectedValue1, expectedValue2](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < sizeof(uint32_t)) {
                return std::string("buffer readback is too small");
            }
            uint32_t value = 0;
            std::memcpy(&value, actual, sizeof(value));
            if (value == expectedValue1 || value == expectedValue2) {
                return std::nullopt;
            }
            std::ostringstream message;
            message << "expected " << expectedValue1 << " or " << expectedValue2 << ", got " << value;
            return message.str();
        },
        0,
        sizeof(uint32_t));
}

// ---------------------------------------------------------------------------
// Standalone render pass / bundle helpers (used by the same-pass / same-bundle tests)
// ---------------------------------------------------------------------------

inline WGPURenderPassEncoder beginSimpleRenderPass(GpuTest& t, WGPUCommandEncoder encoder) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 1.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    return t.beginRenderPassTracked(encoder, &passDesc);
}

inline WGPURenderBundleEncoder createRenderBundleEncoder(GpuTest& t) {
    WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    desc.colorFormatCount = 1;
    desc.colorFormats = &colorFormat;
    desc.sampleCount = 1;
    return t.createRenderBundleEncoderTracked(desc);
}

} // namespace cts::memory_sync
