// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/buffer/single_buffer.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - The upstream helper modules ../operation_context_helper.ts and ./buffer_sync_test.ts are
//     ported in ../operation_context_helper.h and ./buffer_sync_test.h (shared with
//     multiple_buffers.spec.cpp).
//   - 'rw'/'wr' public params: boundary;readOp;readContext;writeOp;writeContext, as upstream (the
//     private `_context` is not in the query).
//   - 'ww' deviation: upstream's array params writeOps=[a,b] / contexts=[a,b] are comma-joined
//     strings (writeOps="storage,b2b-copy"), the spelling multiple_buffers already uses.
//   - The compatibility-mode-only storage-buffers-in-fragment-stage skips are no-ops on the core
//     devices this harness runs.

#include <array>
#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/api/operation/memory_sync/buffer/buffer_sync_test.h"

using namespace cts;
using namespace cts::memory_sync;

namespace {

TestGroup<BufferSyncTest> g = MakeTestGroup<BufferSyncTest>(
    "api,operation,memory_sync,buffer,single_buffer",
    R"(
Memory Synchronization Tests for Buffer: read before write, read after write, and write after write.

- Create a src buffer and initialize it to 0, wait on the fence to ensure the data is initialized.
Write Op: write a value (say 1) into the src buffer via render pass, compute pass, copy, write buffer, etc.
Read Op: read the value from the src buffer and write it to dst buffer via render pass (vertex, index, indirect input, uniform, storage), compute pass, copy etc.
Wait on another fence, then call expectContents to verify the dst buffer value.
  - x= write op: {storage buffer in {compute, render, render-via-bundle}, t2b copy dst, b2b copy dst, writeBuffer}
  - x= read op: {index buffer, vertex buffer, indirect buffer (draw, draw indexed, dispatch), uniform buffer, {readonly, readwrite} storage buffer in {compute, render, render-via-bundle}, b2b copy src, b2t copy src}
  - x= read-write sequence: {read then write, write then read, write then write}
  - x= op context: {queue, command-encoder, compute-pass-encoder, render-pass-encoder, render-bundle-encoder}, x= op boundary: {queue-op, command-buffer, pass, execute-bundles, render-bundle}
    - Not every context/boundary combinations are valid. We have the checkOpsValidForContext func to do the filtering.
  - If two writes are in the same passes, render result has loose guarantees.
)");

// The src value is what stores in the src buffer before any operation.
constexpr uint32_t kSrcValue = 0;
// The op value is what the read/write operation write into the target buffer.
constexpr uint32_t kOpValue = 1;

CTS_TEST(g, "rw")
    .desc(R"(
    Perform a 'read' operations on a buffer, followed by a 'write' operation.
    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).
    Test that the results are synchronized.
    The read should not see the contents written by the subsequent write.)")
    .params([](ParamsBuilder u) { return u.combineWithParams(readWriteCaseRecords()); })
    .fn([](BufferSyncTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const std::string readOp = t.param<std::string>("readOp");
        const OpContext readContext = parseContext(t.param<std::string>("readContext"));
        const std::string writeOp = t.param<std::string>("writeOp");
        const OpContext writeContext = parseContext(t.param<std::string>("writeContext"));
        OperationContextHelper helper(t);

        t.skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStageAndNoSupportStorageBuffersInFragmentShaders();

        const ReadOpBuffers buffers = createBuffersForReadOp(t, readOp, kSrcValue, kOpValue);
        createIntermediateBuffersAndTexturesForWriteOp(t, writeOp, 0, kOpValue);

        // The read op will read from src buffer and write to dst buffer based on what it reads.
        // The write op will write the given op value into src buffer as well.
        // The write op happens after read op. So we are expecting the src value to be in the dst
        // buffer.
        encodeReadOp(t, helper, readOp, readContext, buffers.srcBuffer, buffers.dstBuffer);
        helper.ensureBoundary(boundary);
        encodeWriteOp(t, helper, writeOp, writeContext, buffers.srcBuffer, 0, kOpValue);
        helper.ensureSubmit();
        // Only verify the value of the first element of the dstBuffer.
        verifyData(t, buffers.dstBuffer, kSrcValue);
    });

CTS_TEST(g, "wr")
    .desc(R"(
    Perform a 'write' operation on a buffer, followed by a 'read' operation.
    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).
    Test that the results are synchronized.
    The read should see exactly the contents written by the previous write.)")
    .params([](ParamsBuilder u) { return u.combineWithParams(readWriteCaseRecords()); })
    .fn([](BufferSyncTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const std::string readOp = t.param<std::string>("readOp");
        const OpContext readContext = parseContext(t.param<std::string>("readContext"));
        const std::string writeOp = t.param<std::string>("writeOp");
        const OpContext writeContext = parseContext(t.param<std::string>("writeContext"));

        t.skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStageAndNoSupportStorageBuffersInFragmentShaders();

        OperationContextHelper helper(t);

        const ReadOpBuffers buffers = createBuffersForReadOp(t, readOp, kSrcValue, kOpValue);
        createIntermediateBuffersAndTexturesForWriteOp(t, writeOp, 0, kOpValue);

        // The write op will write the given op value into src buffer.
        // The read op will read from src buffer and write to dst buffer based on what it reads.
        // The write op happens before read op. So we are expecting the op value to be in the dst
        // buffer.
        encodeWriteOp(t, helper, writeOp, writeContext, buffers.srcBuffer, 0, kOpValue);
        helper.ensureBoundary(boundary);
        encodeReadOp(t, helper, readOp, readContext, buffers.srcBuffer, buffers.dstBuffer);
        helper.ensureSubmit();
        // Only verify the value of the first element of the dstBuffer.
        verifyData(t, buffers.dstBuffer, kOpValue);
    });

CTS_TEST(g, "ww")
    .desc(R"(
    Perform a 'first' write operation on a buffer, followed by a 'second' write operation.
    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).
    Test that the results are synchronized.
    The second write should overwrite the contents of the first.)")
    .params([](ParamsBuilder u) { return u.combineWithParams(writeWriteCaseRecords()); })
    .fn([](BufferSyncTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        // Deviation: upstream params are arrays writeOps=[a,b] / contexts=[a,b]; here each is a
        // comma-joined string (see the file header).
        const std::array<std::string, 2> writeOps = splitPair(t.param<std::string>("writeOps"));
        const std::array<std::string, 2> contextNames = splitPair(t.param<std::string>("contexts"));
        const std::array<OpContext, 2> contexts = {{
            parseContext(contextNames[0]),
            parseContext(contextNames[1]),
        }};

        t.skipIfReadOpsOrWriteOpsUsesStorageBufferInFragmentStageAndNoSupportStorageBuffersInFragmentShaders();

        OperationContextHelper helper(t);

        WGPUBuffer buffer = createBufferWithValue(t, 0);
        createIntermediateBuffersAndTexturesForWriteOp(t, writeOps[0], 0, 1);
        createIntermediateBuffersAndTexturesForWriteOp(t, writeOps[1], 1, 2);

        encodeWriteOp(t, helper, writeOps[0], contexts[0], buffer, 0, 1);
        helper.ensureBoundary(boundary);
        encodeWriteOp(t, helper, writeOps[1], contexts[1], buffer, 1, 2);
        helper.ensureSubmit();
        verifyData(t, buffer, 2);
    });

// Cases with loose render result guarantees.

CTS_TEST(g, "two_draws_in_the_same_render_pass")
    .desc(R"(Test write-after-write operations in the same render pass. The first write will write 1 into
    a storage buffer. The second write will write 2 into the same buffer in the same pass. Expected
    data in buffer is either 1 or 2. It may use bundle in each draw.)")
    .params([](ParamsBuilder u) {
        // Upstream .paramsSubcasesOnly(...): all params are subcases.
        return u.beginSubcases()
            .combine("firstDrawUseBundle", {false, true})
            .combine("secondDrawUseBundle", {false, true});
    })
    .fn([](BufferSyncTest& t) {
        t.skipIfNoSupportForStorageBuffersInFragmentStage();

        const std::array<bool, 2> useBundle = {{
            t.param<bool>("firstDrawUseBundle"),
            t.param<bool>("secondDrawUseBundle"),
        }};

        WGPUBuffer buffer = createBufferWithValue(t, 0);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder passEncoder = beginSimpleRenderPass(t, encoder);

        for (uint32_t i = 0; i < 2; ++i) {
            RenderEncoder renderer;
            WGPURenderBundleEncoder bundleEncoder = nullptr;
            if (useBundle[i]) {
                bundleEncoder = createRenderBundleEncoder(t);
                renderer.bundle = bundleEncoder;
            } else {
                renderer.pass = passEncoder;
            }
            WGPURenderPipeline pipeline = createStorageWriteRenderPipeline(t, i + 1);
            WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
            renderer.setPipeline(pipeline);
            renderer.setBindGroup(0, bindGroup);
            renderer.draw(1);
            if (useBundle[i]) {
                WGPURenderBundle bundle = t.finishRenderBundleTracked(bundleEncoder);
                wgpuRenderPassEncoderExecuteBundles(passEncoder, 1, &bundle);
            }
        }

        wgpuRenderPassEncoderEnd(passEncoder);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        verifyDataTwoValidValues(t, buffer, 1, 2);
    });

CTS_TEST(g, "two_draws_in_the_same_render_bundle")
    .desc(R"(Test write-after-write operations in the same render bundle. The first write will write 1 into
    a storage buffer. The second write will write 2 into the same buffer in the same pass. Expected
    data in buffer is either 1 or 2.)")
    .fn([](BufferSyncTest& t) {
        t.skipIfNoSupportForStorageBuffersInFragmentStage();

        WGPUBuffer buffer = createBufferWithValue(t, 0);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder passEncoder = beginSimpleRenderPass(t, encoder);
        WGPURenderBundleEncoder renderEncoder = createRenderBundleEncoder(t);

        for (uint32_t i = 0; i < 2; ++i) {
            WGPURenderPipeline pipeline = createStorageWriteRenderPipeline(t, i + 1);
            WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
            wgpuRenderBundleEncoderSetPipeline(renderEncoder, pipeline);
            wgpuRenderBundleEncoderSetBindGroup(renderEncoder, 0, bindGroup, 0, nullptr);
            wgpuRenderBundleEncoderDraw(renderEncoder, 1, 1, 0, 0);
        }

        WGPURenderBundle bundle = t.finishRenderBundleTracked(renderEncoder);
        wgpuRenderPassEncoderExecuteBundles(passEncoder, 1, &bundle);
        wgpuRenderPassEncoderEnd(passEncoder);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        verifyDataTwoValidValues(t, buffer, 1, 2);
    });

CTS_TEST(g, "two_dispatches_in_the_same_compute_pass")
    .desc(R"(Test write-after-write operations in the same compute pass. The first write will write 1 into
    a storage buffer. The second write will write 2 into the same buffer in the same pass. Expected
    data in buffer is 2.)")
    .fn([](BufferSyncTest& t) {
        t.skipIfNoSupportForStorageBuffersInFragmentStage();

        WGPUBuffer buffer = createBufferWithValue(t, 0);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);

        for (uint32_t i = 0; i < 2; ++i) {
            WGPUComputePipeline pipeline = createStorageWriteComputePipeline(t, i + 1);
            WGPUBindGroup bindGroup = createBindGroup(t, pipeline, buffer);
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        }

        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        verifyData(t, buffer, 2);
    });

} // namespace
