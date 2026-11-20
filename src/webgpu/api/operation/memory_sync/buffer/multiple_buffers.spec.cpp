// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/buffer/multiple_buffers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - The upstream helper modules ../operation_context_helper.ts and ./buffer_sync_test.ts are
//     ported in ../operation_context_helper.h and ./buffer_sync_test.h (shared with
//     single_buffer.spec.cpp); their porting notes (query encoding of the expandWithParams cases,
//     comma-joined 'ww' arrays, compat-only skips, per-device pipeline cache) apply here.
//   - All dst/readback buffers are created non-mapped (zero-initialized per WebGPU spec),
//     never pre-filled with expected values.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/api/operation/memory_sync/buffer/buffer_sync_test.h"

using namespace cts;
using namespace cts::memory_sync;

namespace {

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
                    WGPURenderBundle renderBundle = t.finishRenderBundleTracked(bundleEncoder);
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

        WGPURenderBundle renderBundle = t.finishRenderBundleTracked(renderEncoder);
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
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);

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
