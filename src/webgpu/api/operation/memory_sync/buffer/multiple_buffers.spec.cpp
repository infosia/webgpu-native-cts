// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/buffer/multiple_buffers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - The upstream file depends on two local helper modules which are inline-ported here:
//       * ../operation_context_helper.ts  -> kOperationBoundaries / kBoundaryInfo /
//         OperationContextHelper (context push/pop state machine, dummy attachments,
//         render-bundle and command-buffer flushing).
//       * ./buffer_sync_test.ts           -> kAllReadOps / kAllWriteOps /
//         checkOpsValidForContext and the BufferSyncTest fixture helpers (buffer/texture
//         creation, encodeReadOp/encodeWriteOp, verifyData*).
//   - Upstream case params for 'rw'/'wr' are produced by combine('boundary', ...) +
//     expand('_context', ...) + expandWithParams(...). `_context` is underscore-prefixed,
//     i.e. private in upstream and excluded from the case query. Our ParamsBuilder has no
//     expandWithParams and no private params, so the fully-expanded public param records
//     (boundary, readOp, readContext, writeOp, writeContext) are generated host-side with
//     combineWithParams(). The resulting public query identity matches upstream.
//   - 'ww' deviation: upstream's public params are the arrays writeOps=[first,second] and
//     contexts=[first,second]. The harness Value type has no array variant, so each array
//     is encoded as a single comma-joined string (e.g. writeOps="storage,b2b-copy");
//     param names, ordering, and case count match upstream, only the value encoding differs.
//   - skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStageAndNoSupportStorageBuffersInFragmentShaders
//     and skipIfNoSupportForStorageBuffersInFragmentStage are compatibility-mode-only skips
//     upstream. This harness runs core (non-compat) devices where storage buffers in the
//     fragment stage are always available, so they are no-ops (noted at each call site).
//   - Pipelines use layout 'auto' like upstream; bind group layouts obtained via
//     wgpu*PipelineGetBindGroupLayout are getter results and are manually released after
//     bind group creation (harness does not track them).
//   - All dst/readback buffers are created non-mapped (zero-initialized per WebGPU spec),
//     never pre-filled with expected values.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Fixture (port of BufferSyncTest from buffer_sync_test.ts)
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
};

TestGroup<BufferSyncTest> g = MakeTestGroup<BufferSyncTest>(
    "api,operation,memory_sync,buffer,multiple_buffers",
    R"(
Memory Synchronization Tests for multiple buffers: read before write, read after write, and write after write.

- Create multiple src buffers and initialize it to 0, wait on the fence to ensure the data is initialized.
Write Op: write a value (say 1) into the src buffer via render pass, compute pass, copy, write buffer, etc.
Read Op: read the value from the src buffer and write it to dst buffer via render pass (vertex, index, indirect input, uniform, storage), compute pass, copy etc.
Wait on another fence, then call expectContents to verify the dst buffer value.
  - x= write op: {storage buffer in {compute, render, render-via-bundle}, t2b copy dst, b2b copy dst, writeBuffer}
  - x= read op: {index buffer, vertex buffer, indirect buffer (draw, draw indexed, dispatch), uniform buffer, {readonly, readwrite} storage buffer in {compute, render, render-via-bundle}, b2b copy src, b2t copy src}
  - x= read-write sequence: {read then write, write then read, write then write}
  - x= op context: {queue, command-encoder, compute-pass-encoder, render-pass-encoder, render-bundle-encoder}, x= op boundary: {queue-op, command-buffer, pass, execute-bundles, render-bundle}
    - Not every context/boundary combinations are valid. We have the checkOpsValidForContext func to do the filtering.
  - If two writes are in the same passes, render result has loose guarantees.

TODO: Tests with more than one buffer to try to stress implementations a little bit more.
)");

// The src value is what stores in the src buffer before any operation.
constexpr uint32_t kSrcValue = 0;
// The op value is what the read/write operation write into the target buffer.
constexpr uint32_t kOpValue = 1;

constexpr uint32_t kBufferCount = 4;

constexpr uint64_t kBufferSize = sizeof(uint32_t);

constexpr WGPUBufferUsage kAllBufferUsage =
    WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage |
    WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_Indirect |
    WGPUBufferUsage_Uniform;

WGPUStringView sv(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// ---------------------------------------------------------------------------
// Operation contexts and boundaries (port of operation_context_helper.ts)
// ---------------------------------------------------------------------------

// Sorted such that the first is the most top-level context, and the last is
// the most nested (inside a render bundle, in a render pass, ...).
enum class OpContext : int {
    Queue = 0,
    CommandEncoder = 1,
    ComputePassEncoder = 2,
    RenderPassEncoder = 3,
    RenderBundleEncoder = 4,
};

constexpr std::array<const char*, 5> kOperationContexts = {
    "queue",
    "command-encoder",
    "compute-pass-encoder",
    "render-pass-encoder",
    "render-bundle-encoder",
};

OpContext parseContext(std::string_view name) {
    for (size_t i = 0; i < kOperationContexts.size(); ++i) {
        if (name == kOperationContexts[i]) {
            return static_cast<OpContext>(static_cast<int>(i));
        }
    }
    std::abort();
}

constexpr std::array<const char*, 7> kOperationBoundaries = {
    "queue-op",        // Operations are performed in different queue operations (submit, writeTexture).
    "command-buffer",  // Operations are in different command buffers.
    "pass",            // Operations are in different passes.
    "execute-bundles", // Operations are in different executeBundles(...) calls.
    "render-bundle",   // Operations are in different render bundles.
    "dispatch",        // Operations are in different dispatches.
    "draw",            // Operations are in different draws.
};

struct ContextPair {
    const char* first;
    const char* second;
};

// Mapping of operation boundary => set of context pairs the boundary can separate
// (port of kBoundaryInfo).
std::vector<ContextPair> boundaryContexts(std::string_view boundary) {
    constexpr const char* kCe = "command-encoder";
    constexpr const char* kCp = "compute-pass-encoder";
    constexpr const char* kRp = "render-pass-encoder";
    constexpr const char* kRb = "render-bundle-encoder";

    if (boundary == "queue-op") {
        std::vector<ContextPair> pairs;
        for (const char* a : kOperationContexts) {
            for (const char* b : kOperationContexts) {
                pairs.push_back(ContextPair{a, b});
            }
        }
        return pairs;
    }
    if (boundary == "command-buffer") {
        const std::array<const char*, 4> nonQueue = {kCe, kCp, kRp, kRb};
        std::vector<ContextPair> pairs;
        for (const char* a : nonQueue) {
            for (const char* b : nonQueue) {
                pairs.push_back(ContextPair{a, b});
            }
        }
        return pairs;
    }
    if (boundary == "pass") {
        return {
            {kCp, kCp}, {kCp, kRp}, {kRp, kCp}, {kRp, kRp},
            {kRb, kRp}, {kRp, kRb}, {kRb, kRb},
        };
    }
    if (boundary == "execute-bundles") {
        return {{kRb, kRb}};
    }
    if (boundary == "render-bundle") {
        return {{kRb, kRp}, {kRp, kRb}, {kRb, kRb}};
    }
    if (boundary == "dispatch") {
        return {{kCp, kCp}};
    }
    if (boundary == "draw") {
        return {{kRp, kRp}, {kRb, kRp}, {kRp, kRb}};
    }
    std::abort();
}

// ---------------------------------------------------------------------------
// Read/write op tables and validity filter (port of buffer_sync_test.ts)
// ---------------------------------------------------------------------------

constexpr std::array<const char*, 4> kAllWriteOps = {
    "storage",
    "b2b-copy",
    "t2b-copy",
    "write-buffer",
};

constexpr std::array<const char*, 9> kAllReadOps = {
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

// Port of kOpInfo: contexts each op is permitted in.
bool opValidInContext(std::string_view op, std::string_view context) {
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
    if (op == "input-vertex" || op == "input-index" || op == "input-indirect" ||
        op == "input-indirect-index" || op == "constant-uniform") {
        return context == "render-pass-encoder" || context == "render-bundle-encoder";
    }
    if (op == "input-indirect-dispatch") {
        return context == "compute-pass-encoder";
    }
    std::abort();
}

// In a render pass it is invalid to use a resource as both writable and another
// usage; also storage+storage is racy/untestable, so those pairs are filtered.
bool checkImplOpsPair(std::string_view op1, std::string_view op2) {
    if (op1 == "storage") {
        // Write+other, or racy: filtered out. (b2t/t2b/b2b/write-buffer don't
        // occur in a render pass; input-indirect-dispatch only occurs in a
        // compute pass — both fall through to valid, matching upstream.)
        if (op2 == "storage" || op2 == "storage-read" || op2 == "input-vertex" ||
            op2 == "input-index" || op2 == "input-indirect" || op2 == "input-indirect-index" ||
            op2 == "constant-uniform") {
            return false;
        }
    }
    return true;
}

bool checkOpsValidForContext(
    std::string_view op0,
    std::string_view op1,
    std::string_view context0,
    std::string_view context1) {
    if (!opValidInContext(op0, context0) || !opValidInContext(op1, context1)) {
        return false;
    }
    const bool anyRenderContext =
        context0 == "render-bundle-encoder" || context0 == "render-pass-encoder" ||
        context1 == "render-bundle-encoder" || context1 == "render-pass-encoder";
    if (anyRenderContext) {
        return checkImplOpsPair(op0, op1) && checkImplOpsPair(op1, op0);
    }
    return true;
}

// Fully expanded case records for 'rw' and 'wr' (same generator upstream).
// Public params mirror upstream: boundary;readOp;readContext;writeOp;writeContext
// (upstream's `_context` is private and excluded from the query).
std::vector<ParamRecord> readWriteCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* readOp : kAllReadOps) {
                for (const char* writeOp : kAllWriteOps) {
                    if (checkOpsValidForContext(readOp, writeOp, context.first, context.second)) {
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

// Fully expanded case records for 'ww'. Upstream public params are the arrays
// writeOps=[first,second] and contexts=[first,second]; our Value type has no
// arrays so they are encoded as comma-joined strings (documented deviation).
std::vector<ParamRecord> writeWriteCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* firstWriteOp : kAllWriteOps) {
                for (const char* secondWriteOp : kAllWriteOps) {
                    if (checkOpsValidForContext(
                            firstWriteOp, secondWriteOp, context.first, context.second)) {
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

std::array<std::string, 2> splitPair(const std::string& joined) {
    const size_t comma = joined.find(',');
    if (comma == std::string::npos) {
        std::abort();
    }
    return {{joined.substr(0, comma), joined.substr(comma + 1)}};
}

// ---------------------------------------------------------------------------
// WGSL shaders (port of buffer_sync_test.ts shader templates)
// ---------------------------------------------------------------------------

constexpr std::string_view kDummyVertexShader = R"(
@vertex fn vert_main() -> @builtin(position) vec4<f32> {
  return vec4<f32>(0.5, 0.5, 0.0, 1.0);
}
)";

constexpr std::string_view kStorageReadComputeShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read> srcData : Data;
@group(0) @binding(1) var<storage, read_write> dstData : Data;

@compute @workgroup_size(1) fn main() {
  dstData.a = srcData.a;
}
)";

constexpr std::string_view kVertexReadVertexShader = R"(
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

constexpr std::string_view kVertexReadFragmentShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read_write> data : Data;

@fragment fn frag_main(@location(0) @interpolate(flat, either) input : u32) -> @location(0) vec4<f32> {
  data.a = input;
  return vec4<f32>();
}
)";

constexpr std::string_view kUniformReadFragmentShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<uniform> constant: Data;
@group(0) @binding(1) var<storage, read_write> data : Data;

@fragment fn frag_main() -> @location(0) vec4<f32> {
  data.a = constant.a;
  return vec4<f32>();
}
)";

constexpr std::string_view kStorageReadFragmentShader = R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read> srcData : Data;
@group(0) @binding(1) var<storage, read_write> dstData : Data;

@fragment fn frag_main() -> @location(0) vec4<f32> {
  dstData.a = srcData.a;
  return vec4<f32>();
}
)";

std::string storageWriteComputeShader(uint32_t value) {
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

std::string storageWriteFragmentShader(uint32_t value) {
    std::ostringstream shader;
    shader << R"(
struct Data {
  a : u32
};

@group(0) @binding(0) var<storage, read_write> data : Data;
@fragment fn frag_main() -> @location(0) vec4<f32> {
  data.a = )" << value << R"(u;
  return vec4<f32>();
}
)";
    return shader.str();
}

// ---------------------------------------------------------------------------
// Pipeline / bind group helpers (all pipelines use layout 'auto')
// ---------------------------------------------------------------------------

WGPUComputePipeline createComputePipelineAuto(BufferSyncTest& t, std::string_view wgsl) {
    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // auto layout
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

// Create a compute pipeline and write given data into storage buffer.
WGPUComputePipeline createStorageWriteComputePipeline(BufferSyncTest& t, uint32_t value) {
    return createComputePipelineAuto(t, storageWriteComputeShader(value));
}

// Create a compute pipeline: read from src buffer and write it into the storage buffer.
WGPUComputePipeline createStorageReadComputePipeline(BufferSyncTest& t) {
    return createComputePipelineAuto(t, kStorageReadComputeShader);
}

WGPURenderPipeline createTrivialRenderPipeline(
    BufferSyncTest& t,
    std::string_view vertexWgsl,
    std::string_view fragmentWgsl) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(vertexWgsl);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentWgsl);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = sv("frag_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // auto layout
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = sv("vert_main");
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Create a render pipeline and write given data into storage buffer at fragment stage.
WGPURenderPipeline createStorageWriteRenderPipeline(BufferSyncTest& t, uint32_t value) {
    return createTrivialRenderPipeline(t, kDummyVertexShader, storageWriteFragmentShader(value));
}

// Create a render pipeline: read from vertex/index buffer and write it into the
// storage dst buffer at fragment stage.
WGPURenderPipeline createVertexReadRenderPipeline(BufferSyncTest& t) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexReadVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kVertexReadFragmentShader);

    // Lifetime rule: every struct referenced by the pipeline descriptor is declared
    // at function scope so it outlives the create call below.
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
    fragment.entryPoint = sv("frag_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // auto layout
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = sv("vert_main");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexBufferLayout;
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Create a render pipeline: read from uniform buffer and write it into the
// storage dst buffer at fragment stage.
WGPURenderPipeline createUniformReadRenderPipeline(BufferSyncTest& t) {
    return createTrivialRenderPipeline(t, kDummyVertexShader, kUniformReadFragmentShader);
}

// Create a render pipeline: read from storage src buffer and write it into the
// storage dst buffer at fragment stage.
WGPURenderPipeline createStorageReadRenderPipeline(BufferSyncTest& t) {
    return createTrivialRenderPipeline(t, kDummyVertexShader, kStorageReadFragmentShader);
}

WGPUBindGroup createSingleBufferBindGroup(
    BufferSyncTest& t,
    WGPUBindGroupLayout layout,
    WGPUBuffer buffer) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer; // whole-buffer binding (INIT default size = WGPU_WHOLE_SIZE)

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUBindGroup createSrcDstBufferBindGroup(
    BufferSyncTest& t,
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

// Port of BufferSyncTest.createBindGroup(pipeline, buffer) — bind group layout
// is a getter result and must be released manually.
WGPUBindGroup createBindGroup(BufferSyncTest& t, WGPUComputePipeline pipeline, WGPUBuffer buffer) {
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSingleBufferBindGroup(t, layout, buffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

WGPUBindGroup createBindGroup(BufferSyncTest& t, WGPURenderPipeline pipeline, WGPUBuffer buffer) {
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSingleBufferBindGroup(t, layout, buffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

WGPUBindGroup createBindGroupSrcDstBuffer(
    BufferSyncTest& t,
    WGPUComputePipeline pipeline,
    WGPUBuffer srcBuffer,
    WGPUBuffer dstBuffer) {
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bindGroup = createSrcDstBufferBindGroup(t, layout, srcBuffer, dstBuffer);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

WGPUBindGroup createBindGroupSrcDstBuffer(
    BufferSyncTest& t,
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

// Create a buffer initialized to the specified u32 values (mappedAtCreation),
// then wait for the queue like upstream's onSubmittedWorkDone await.
WGPUBuffer createBufferWithValues(BufferSyncTest& t, const std::vector<uint32_t>& initValues) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.mappedAtCreation = true;
    desc.size = sizeof(uint32_t) * initValues.size();
    desc.usage = kAllBufferUsage;
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
WGPUBuffer createBufferWithValue(BufferSyncTest& t, uint32_t initValue) {
    return createBufferWithValues(t, {initValue});
}

// Create a 1x1 r32uint texture, and initialize it to a specified value.
WGPUTexture createTextureWithValue(BufferSyncTest& t, uint32_t initValue) {
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

// Create extra buffers/textures needed by a write operation (one of two slots).
void createIntermediateBuffersAndTexturesForWriteOp(
    BufferSyncTest& t,
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
ReadOpBuffers createBuffersForReadOp(
    BufferSyncTest& t,
    const std::string& readOp,
    uint32_t srcValue,
    uint32_t opValue) {
    if (readOp == "input-index") {
        // The index buffer will be the src buffer of the read op.
        // index value 0 selects vertexBuffer[0] (src value); 1 selects vertexBuffer[1] (op value).
        t.vertexBuffer = createBufferWithValues(t, {srcValue, opValue});
    } else if (readOp == "input-indirect") {
        // If indirect vertexCount is 1, the op value in the vertex buffer is written to dst.
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

    // dst buffer is created non-mapped: zero-initialized per WebGPU spec (never
    // pre-filled with the expected value).
    WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    dstDesc.size = kBufferSize;
    dstDesc.usage = kAllBufferUsage;
    buffers.dstBuffer = t.createBufferTracked(dstDesc);
    return buffers;
}

// ---------------------------------------------------------------------------
// OperationContextHelper (port of operation_context_helper.ts)
// ---------------------------------------------------------------------------

class OperationContextHelper {
  public:
    explicit OperationContextHelper(BufferSyncTest& t) : t_(t) {}

    // Set based on the current context.
    WGPUCommandEncoder commandEncoder = nullptr;
    WGPUComputePassEncoder computePassEncoder = nullptr;
    WGPURenderPassEncoder renderPassEncoder = nullptr;
    WGPURenderBundleEncoder renderBundleEncoder = nullptr;

    // Ensure that all encoded commands are finished and submitted.
    void ensureSubmit() {
        ensureContext(OpContext::Queue);
        flushCommandBuffers();
    }

    void ensureContext(OpContext context) {
        // Find the common ancestor so we can transition currentContext -> context.
        const int ancestorIndex =
            std::min(static_cast<int>(context), static_cast<int>(currentContext_));
        const OpContext ancestor = static_cast<OpContext>(ancestorIndex);

        // Pop the context until we're at the common ancestor.
        while (currentContext_ != ancestor) {
            // About to pop the render pass encoder: execute outstanding render bundles.
            if (currentContext_ == OpContext::RenderPassEncoder) {
                flushRenderBundles();
            }
            popContext();
        }

        if (currentContext_ == context) {
            return;
        }

        switch (context) {
            case OpContext::Queue:
                std::abort(); // unreachable
            case OpContext::CommandEncoder:
                if (currentContext_ != OpContext::Queue) {
                    std::abort();
                }
                commandEncoder = t_.createCommandEncoderTracked();
                break;
            case OpContext::ComputePassEncoder: {
                if (currentContext_ == OpContext::Queue) {
                    commandEncoder = t_.createCommandEncoderTracked();
                } else if (currentContext_ != OpContext::CommandEncoder) {
                    std::abort();
                }
                WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                computePassEncoder = wgpuCommandEncoderBeginComputePass(commandEncoder, &passDesc);
                break;
            }
            case OpContext::RenderPassEncoder:
                if (currentContext_ == OpContext::Queue) {
                    commandEncoder = t_.createCommandEncoderTracked();
                } else if (currentContext_ != OpContext::CommandEncoder) {
                    std::abort();
                }
                beginDummyRenderPass();
                break;
            case OpContext::RenderBundleEncoder: {
                if (currentContext_ == OpContext::Queue) {
                    commandEncoder = t_.createCommandEncoderTracked();
                }
                if (currentContext_ == OpContext::Queue ||
                    currentContext_ == OpContext::CommandEncoder) {
                    beginDummyRenderPass();
                } else if (currentContext_ != OpContext::RenderPassEncoder) {
                    std::abort();
                }
                WGPUTextureFormat colorFormat = kTextureFormat;
                WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                desc.colorFormatCount = 1;
                desc.colorFormats = &colorFormat;
                desc.sampleCount = 1;
                renderBundleEncoder = wgpuDeviceCreateRenderBundleEncoder(t_.device(), &desc);
                break;
            }
        }
        currentContext_ = context;
    }

    void ensureBoundary(std::string_view boundary) {
        if (boundary == "command-buffer") {
            ensureContext(OpContext::Queue);
            return;
        }
        if (boundary == "queue-op") {
            ensureContext(OpContext::Queue);
            // Submit any command buffers so the next one is in a separate submit.
            flushCommandBuffers();
            return;
        }
        if (boundary == "dispatch") {
            // Nothing to do to separate dispatches.
            if (currentContext_ != OpContext::ComputePassEncoder) {
                std::abort();
            }
            return;
        }
        if (boundary == "draw") {
            // Nothing to do to separate draws.
            if (currentContext_ != OpContext::RenderPassEncoder &&
                currentContext_ != OpContext::RenderBundleEncoder) {
                std::abort();
            }
            return;
        }
        if (boundary == "pass") {
            ensureContext(OpContext::CommandEncoder);
            return;
        }
        if (boundary == "render-bundle") {
            ensureContext(OpContext::RenderPassEncoder);
            return;
        }
        if (boundary == "execute-bundles") {
            ensureContext(OpContext::RenderPassEncoder);
            // Execute render bundles so the next one is in a separate executeBundles.
            flushRenderBundles();
            return;
        }
        std::abort();
    }

  private:
    static constexpr WGPUTextureFormat kTextureFormat = WGPUTextureFormat_RGBA8Unorm;

    void beginDummyRenderPass() {
        // 4x4 rgba8unorm dummy attachment, loadOp 'load' (kTextureSize upstream).
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{4, 4, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = kTextureFormat;
        texDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = t_.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t_.createViewTracked(texture, viewDesc);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        renderPassEncoder = wgpuCommandEncoderBeginRenderPass(commandEncoder, &passDesc);
    }

    void popContext() {
        switch (currentContext_) {
            case OpContext::Queue:
                std::abort(); // unreachable
            case OpContext::CommandEncoder: {
                WGPUCommandBuffer commandBuffer = t_.finishTracked(commandEncoder);
                commandEncoder = nullptr;
                currentContext_ = OpContext::Queue;
                commandBuffers_.push_back(commandBuffer);
                break;
            }
            case OpContext::ComputePassEncoder:
                wgpuComputePassEncoderEnd(computePassEncoder);
                computePassEncoder = nullptr;
                currentContext_ = OpContext::CommandEncoder;
                break;
            case OpContext::RenderPassEncoder:
                wgpuRenderPassEncoderEnd(renderPassEncoder);
                renderPassEncoder = nullptr;
                currentContext_ = OpContext::CommandEncoder;
                break;
            case OpContext::RenderBundleEncoder: {
                WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(renderBundleEncoder, nullptr);
                renderBundleEncoder = nullptr;
                currentContext_ = OpContext::RenderPassEncoder;
                renderBundles_.push_back(bundle);
                break;
            }
        }
    }

    void flushRenderBundles() {
        if (renderPassEncoder == nullptr) {
            std::abort();
        }
        if (!renderBundles_.empty()) {
            wgpuRenderPassEncoderExecuteBundles(
                renderPassEncoder, renderBundles_.size(), renderBundles_.data());
            renderBundles_.clear();
        }
    }

    void flushCommandBuffers() {
        if (!commandBuffers_.empty()) {
            wgpuQueueSubmit(t_.queue(), commandBuffers_.size(), commandBuffers_.data());
            commandBuffers_.clear();
        }
    }

    BufferSyncTest& t_;
    // We start at the queue context which is top-level.
    OpContext currentContext_ = OpContext::Queue;
    std::vector<WGPUCommandBuffer> commandBuffers_;
    std::vector<WGPURenderBundle> renderBundles_;
};

// ---------------------------------------------------------------------------
// Render-encoder dispatch wrapper (GPURenderPassEncoder | GPURenderBundleEncoder)
// ---------------------------------------------------------------------------

struct RenderEncoder {
    WGPURenderPassEncoder pass = nullptr;
    WGPURenderBundleEncoder bundle = nullptr;

    void setPipeline(WGPURenderPipeline pipeline) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetPipeline(bundle, pipeline);
        } else {
            std::abort();
        }
    }
    void setBindGroup(uint32_t index, WGPUBindGroup group) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetBindGroup(pass, index, group, 0, nullptr);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetBindGroup(bundle, index, group, 0, nullptr);
        } else {
            std::abort();
        }
    }
    void setVertexBuffer(uint32_t slot, WGPUBuffer buffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, slot, buffer, 0, WGPU_WHOLE_SIZE);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetVertexBuffer(bundle, slot, buffer, 0, WGPU_WHOLE_SIZE);
        } else {
            std::abort();
        }
    }
    void setIndexBufferUint32(WGPUBuffer buffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetIndexBuffer(bundle, buffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        } else {
            std::abort();
        }
    }
    void draw(uint32_t vertexCount) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDraw(pass, vertexCount, 1, 0, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDraw(bundle, vertexCount, 1, 0, 0);
        } else {
            std::abort();
        }
    }
    void drawIndexed(uint32_t indexCount) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDrawIndexed(bundle, indexCount, 1, 0, 0, 0);
        } else {
            std::abort();
        }
    }
    void drawIndirect(WGPUBuffer indirectBuffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDrawIndirect(pass, indirectBuffer, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDrawIndirect(bundle, indirectBuffer, 0);
        } else {
            std::abort();
        }
    }
    void drawIndexedIndirect(WGPUBuffer indirectBuffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirectBuffer, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDrawIndexedIndirect(bundle, indirectBuffer, 0);
        } else {
            std::abort();
        }
    }
};

RenderEncoder rendererFor(const OperationContextHelper& helper, OpContext context) {
    RenderEncoder renderer;
    if (context == OpContext::RenderBundleEncoder) {
        renderer.bundle = helper.renderBundleEncoder;
    } else {
        renderer.pass = helper.renderPassEncoder;
    }
    return renderer;
}

// ---------------------------------------------------------------------------
// encodeWriteOp / encodeReadOp (port of BufferSyncTest methods)
// ---------------------------------------------------------------------------

// Write buffer via draw call in render pass (or bundle).
void encodeWriteAsStorageBufferInRenderPass(
    BufferSyncTest& t,
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
void encodeWriteAsStorageBufferInComputePass(
    BufferSyncTest& t,
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
void encodeWriteOp(
    BufferSyncTest& t,
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
                encodeWriteAsStorageBufferInRenderPass(t, rendererFor(helper, context), buffer, value);
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
        WGPUBuffer tmpBuffer = t.tmpValueBuffers[writeOpSlot];
        if (helper.commandEncoder == nullptr || tmpBuffer == nullptr) {
            std::abort();
        }
        wgpuCommandEncoderCopyBufferToBuffer(helper.commandEncoder, tmpBuffer, 0, buffer, 0, kBufferSize);
        return;
    }
    if (operation == "t2b-copy") {
        WGPUTexture tmpTexture = t.tmpValueTextures[writeOpSlot];
        if (helper.commandEncoder == nullptr || tmpTexture == nullptr) {
            std::abort();
        }
        t.copyTextureToBuffer(helper.commandEncoder, tmpTexture, buffer, 256, WGPUExtent3D{1, 1, 1});
        return;
    }
    std::abort();
}

// Issue a read operation (which writes what it reads into dstBuffer).
void encodeReadOp(
    BufferSyncTest& t,
    OperationContextHelper& helper,
    const std::string& operation,
    OpContext context,
    WGPUBuffer srcBuffer,
    WGPUBuffer dstBuffer) {
    helper.ensureContext(context);

    const RenderEncoder renderer = rendererFor(helper, context);

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
        // The srcBuffer is used as indexBuffer; drawIndexed writes vertexBuffer[srcBuffer[0]]
        // to dstBuffer[0].
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
        // srcBuffer[0] = 0 or 1 (vertexCount): decides whether the op value gets
        // written into dstBuffer.
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
        // srcBuffer[0] = 0 or 1 (indexCount): decides whether the op value gets
        // written into dstBuffer.
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
        // srcBuffer[0] = 0 or 1 (workgroupCountX): decides whether value 1 gets
        // written into dstBuffer (upstream passes the literal 1 here).
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
        wgpuCommandEncoderCopyBufferToBuffer(helper.commandEncoder, srcBuffer, 0, dstBuffer, 0, kBufferSize);
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

void verifyData(BufferSyncTest& t, WGPUBuffer buffer, uint32_t expectedValue) {
    t.expectGPUBufferValuesEqual(buffer, &expectedValue, sizeof(expectedValue));
}

void verifyDataTwoValidValues(
    BufferSyncTest& t,
    WGPUBuffer buffer,
    uint32_t expectedValue1,
    uint32_t expectedValue2) {
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
            message << "expected " << expectedValue1 << " or " << expectedValue2
                    << ", got " << value;
            return message.str();
        },
        0,
        sizeof(uint32_t));
}

// ---------------------------------------------------------------------------
// Standalone render pass / bundle helpers (used by the multiple_pairs_* tests)
// ---------------------------------------------------------------------------

WGPURenderPassEncoder beginSimpleRenderPass(BufferSyncTest& t, WGPUCommandEncoder encoder) {
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
    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

WGPURenderBundleEncoder createRenderBundleEncoder(BufferSyncTest& t) {
    WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    desc.colorFormatCount = 1;
    desc.colorFormats = &colorFormat;
    desc.sampleCount = 1;
    return wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

CTS_TEST(g, "rw")
    .desc(
        "\n"
        "    Perform a 'read' operations on multiple buffers, followed by a 'write' operation.\n"
        "    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).\n"
        "    Test that the results are synchronized.\n"
        "    The read should not see the contents written by the subsequent write.\n"
        "  ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(readWriteCaseRecords());
    })
    .fn([](BufferSyncTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const std::string readOp = t.param<std::string>("readOp");
        const OpContext readContext = parseContext(t.param<std::string>("readContext"));
        const std::string writeOp = t.param<std::string>("writeOp");
        const OpContext writeContext = parseContext(t.param<std::string>("writeContext"));

        // Upstream skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStage... is
        // compatibility-mode only; core devices always support storage buffers in
        // the fragment stage, so it is a no-op here.

        OperationContextHelper helper(t);

        std::vector<WGPUBuffer> srcBuffers;
        std::vector<WGPUBuffer> dstBuffers;
        for (uint32_t i = 0; i < kBufferCount; ++i) {
            const ReadOpBuffers buffers = createBuffersForReadOp(t, readOp, kSrcValue, kOpValue);
            srcBuffers.push_back(buffers.srcBuffer);
            dstBuffers.push_back(buffers.dstBuffer);
        }

        createIntermediateBuffersAndTexturesForWriteOp(t, writeOp, 0, kOpValue);

        // The read op will read from src buffers and write to dst buffers based on
        // what it reads. A boundary will separate multiple read and write operations.
        // The write op will write the given op value into each src buffer as well.
        // The write op happens after read op. So we are expecting each src value to
        // be in the mapped dst buffer.
        for (uint32_t i = 0; i < kBufferCount; ++i) {
            encodeReadOp(t, helper, readOp, readContext, srcBuffers[i], dstBuffers[i]);
        }

        helper.ensureBoundary(boundary);

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            encodeWriteOp(t, helper, writeOp, writeContext, srcBuffers[i], 0, kOpValue);
        }

        helper.ensureSubmit();

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            // Only verify the value of the first element of each dstBuffer.
            verifyData(t, dstBuffers[i], kSrcValue);
        }
    });

CTS_TEST(g, "wr")
    .desc(
        "\n"
        "    Perform a 'write' operation on on multiple buffers, followed by a 'read' operation.\n"
        "    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).\n"
        "    Test that the results are synchronized.\n"
        "    The read should see exactly the contents written by the previous write.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(readWriteCaseRecords());
    })
    .fn([](BufferSyncTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const std::string readOp = t.param<std::string>("readOp");
        const OpContext readContext = parseContext(t.param<std::string>("readContext"));
        const std::string writeOp = t.param<std::string>("writeOp");
        const OpContext writeContext = parseContext(t.param<std::string>("writeContext"));

        // Compat-only storage-buffers-in-fragment-stage skip: no-op on core devices.

        OperationContextHelper helper(t);

        std::vector<WGPUBuffer> srcBuffers;
        std::vector<WGPUBuffer> dstBuffers;
        for (uint32_t i = 0; i < kBufferCount; ++i) {
            const ReadOpBuffers buffers = createBuffersForReadOp(t, readOp, kSrcValue, kOpValue);
            srcBuffers.push_back(buffers.srcBuffer);
            dstBuffers.push_back(buffers.dstBuffer);
        }

        createIntermediateBuffersAndTexturesForWriteOp(t, writeOp, 0, kOpValue);

        // The write op will write the given op value into src buffers.
        // The read op will read from src buffers and write to dst buffers based on
        // what it reads. The write op happens before read op. So we are expecting
        // the op value to be in the dst buffers.
        for (uint32_t i = 0; i < kBufferCount; ++i) {
            encodeWriteOp(t, helper, writeOp, writeContext, srcBuffers[i], 0, kOpValue);
        }

        helper.ensureBoundary(boundary);

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            encodeReadOp(t, helper, readOp, readContext, srcBuffers[i], dstBuffers[i]);
        }

        helper.ensureSubmit();

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            // Only verify the value of the first element of the dstBuffer.
            verifyData(t, dstBuffers[i], kOpValue);
        }
    });

CTS_TEST(g, "ww")
    .desc(
        "\n"
        "    Perform a 'first' write operation on multiple buffers, followed by a 'second' write operation.\n"
        "    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).\n"
        "    Test that the results are synchronized.\n"
        "    The second write should overwrite the contents of the first.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(writeWriteCaseRecords());
    })
    .fn([](BufferSyncTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        // Deviation: upstream params are arrays writeOps=[a,b] / contexts=[a,b];
        // here each is a comma-joined string (see file header notes).
        const std::array<std::string, 2> writeOps = splitPair(t.param<std::string>("writeOps"));
        const std::array<std::string, 2> contextNames = splitPair(t.param<std::string>("contexts"));
        const std::array<OpContext, 2> contexts = {{
            parseContext(contextNames[0]),
            parseContext(contextNames[1]),
        }};

        // Compat-only storage-buffers-in-fragment-stage skip: no-op on core devices.

        OperationContextHelper helper(t);

        std::vector<WGPUBuffer> buffers;
        for (uint32_t i = 0; i < kBufferCount; ++i) {
            buffers.push_back(createBufferWithValue(t, 0));
        }

        createIntermediateBuffersAndTexturesForWriteOp(t, writeOps[0], 0, 1);
        createIntermediateBuffersAndTexturesForWriteOp(t, writeOps[1], 1, 2);

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            encodeWriteOp(t, helper, writeOps[0], contexts[0], buffers[i], 0, 1);
        }

        helper.ensureBoundary(boundary);

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            encodeWriteOp(t, helper, writeOps[1], contexts[1], buffers[i], 1, 2);
        }

        helper.ensureSubmit();

        for (uint32_t i = 0; i < kBufferCount; ++i) {
            verifyData(t, buffers[i], 2);
        }
    });

CTS_TEST(g, "multiple_pairs_of_draws_in_one_render_pass")
    .desc(
        "\n"
        "  Test write-after-write operations on multiple buffers via the one render pass. The first write\n"
        "  will write the buffer index * 2 + 1 into all storage buffers. The second write will write the\n"
        "  buffer index * 2 + 2 into the all buffers in the same pass. Expected data in all buffers is either\n"
        "  buffer index * 2 + 1 or buffer index * 2 + 2. It may use bundle in each draw.\n"
        "  ")
    .params([](ParamsBuilder u) {
        // Upstream .paramsSubcasesOnly(...): all params are subcases.
        return u.beginSubcases()
            .combine("firstDrawUseBundle", {false, true})
            .combine("secondDrawUseBundle", {false, true});
    })
    .fn([](BufferSyncTest& t) {
        const std::array<bool, 2> useBundle = {{
            t.param<bool>("firstDrawUseBundle"),
            t.param<bool>("secondDrawUseBundle"),
        }};

        // Compat-only storage-buffers-in-fragment-stage skip: no-op on core devices.

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder passEncoder = beginSimpleRenderPass(t, encoder);

        std::vector<WGPUBuffer> buffers;
        for (uint32_t b = 0; b < kBufferCount; ++b) {
            WGPUBuffer buffer = createBufferWithValue(t, 0);
            buffers.push_back(buffer);

            for (uint32_t i = 0; i < 2; ++i) {
                RenderEncoder renderer;
                WGPURenderBundleEncoder bundleEncoder = nullptr;
                if (useBundle[i]) {
                    bundleEncoder = createRenderBundleEncoder(t);
                    renderer.bundle = bundleEncoder;
                } else {
                    renderer.pass = passEncoder;
                }
                WGPURenderPipeline pipeline = createStorageWriteRenderPipeline(t, 2 * b + i + 1);
                WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
                renderer.setPipeline(pipeline);
                renderer.setBindGroup(0, bindGroup);
                renderer.draw(1);
                if (useBundle[i]) {
                    WGPURenderBundle renderBundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);
                    wgpuRenderPassEncoderExecuteBundles(passEncoder, 1, &renderBundle);
                }
            }
        }

        wgpuRenderPassEncoderEnd(passEncoder);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        for (uint32_t b = 0; b < kBufferCount; ++b) {
            verifyDataTwoValidValues(t, buffers[b], 2 * b + 1, 2 * b + 2);
        }
    });

CTS_TEST(g, "multiple_pairs_of_draws_in_one_render_bundle")
    .desc(
        "\n"
        "  Test write-after-write operations on multiple buffers via the one render bundle. The first write\n"
        "  will write the buffer index * 2 + 1 into all storage buffers. The second write will write the\n"
        "  buffer index * 2 + 2 into the all buffers in the same pass. Expected data in all buffers is either\n"
        "  buffer index * 2 + 1 or buffer index * 2 + 2.\n"
        "  ")
    .fn([](BufferSyncTest& t) {
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder passEncoder = beginSimpleRenderPass(t, encoder);
        WGPURenderBundleEncoder renderEncoder = createRenderBundleEncoder(t);

        // Compat-only storage-buffers-in-fragment-stage skip: no-op on core devices.

        std::vector<WGPUBuffer> buffers;
        for (uint32_t b = 0; b < kBufferCount; ++b) {
            WGPUBuffer buffer = createBufferWithValue(t, 0);
            buffers.push_back(buffer);

            for (uint32_t i = 0; i < 2; ++i) {
                WGPURenderPipeline pipeline = createStorageWriteRenderPipeline(t, 2 * b + i + 1);
                WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
                wgpuRenderBundleEncoderSetPipeline(renderEncoder, pipeline);
                wgpuRenderBundleEncoderSetBindGroup(renderEncoder, 0, bindGroup, 0, nullptr);
                wgpuRenderBundleEncoderDraw(renderEncoder, 1, 1, 0, 0);
            }
        }

        WGPURenderBundle renderBundle = wgpuRenderBundleEncoderFinish(renderEncoder, nullptr);
        wgpuRenderPassEncoderExecuteBundles(passEncoder, 1, &renderBundle);
        wgpuRenderPassEncoderEnd(passEncoder);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        for (uint32_t b = 0; b < kBufferCount; ++b) {
            verifyDataTwoValidValues(t, buffers[b], 2 * b + 1, 2 * b + 2);
        }
    });

CTS_TEST(g, "multiple_pairs_of_dispatches_in_one_compute_pass")
    .desc(
        "\n"
        "  Test write-after-write operations on multiple buffers via the one compute pass. The first write\n"
        "  will write the buffer index * 2 + 1 into all storage buffers. The second write will write the\n"
        "  buffer index * 2 + 2 into the all buffers in the same pass. Expected data in all buffers is the\n"
        "  buffer index * 2 + 2.\n"
        "  ")
    .fn([](BufferSyncTest& t) {
        // Compat-only storage-buffers-in-fragment-stage skip (called by upstream
        // even for this compute-only test): no-op on core devices.

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        std::vector<WGPUBuffer> buffers;
        for (uint32_t b = 0; b < kBufferCount; ++b) {
            WGPUBuffer buffer = createBufferWithValue(t, 0);
            buffers.push_back(buffer);

            for (uint32_t i = 0; i < 2; ++i) {
                WGPUComputePipeline pipeline = createStorageWriteComputePipeline(t, 2 * b + i + 1);
                WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            }
        }

        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        for (uint32_t b = 0; b < kBufferCount; ++b) {
            verifyData(t, buffers[b], 2 * b + 2);
        }
    });

} // namespace
