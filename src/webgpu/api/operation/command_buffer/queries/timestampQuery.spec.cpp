// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/queries/timestampQuery.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// (see docs/UPSTREAM.md).

#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "common/webgpu/sync.h"
#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,queries,timestampQuery",
    R"(API operations tests for timestamp queries.

Given the values returned are implementation defined
there is not much we can test except that there are no errors.

- test query with
  - compute pass
  - render pass
  - 64k query objects
  - resolving unused slots)");

void skipIfNoTimestampQuery(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
        t.skip("device does not have feature 'timestamp-query'");
    }
}

WGPUTextureView createRenderTargetView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(desc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

WGPUQuerySet createTimestampQuerySet(AllFeaturesMaxLimitsGpuTest& t, uint32_t count) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type = WGPUQueryType_Timestamp;
    desc.count = count;
    return t.createQuerySetTracked(desc);
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

void encodeTimestampQueries(AllFeaturesMaxLimitsGpuTest& t,
                            WGPUCommandEncoder encoder,
                            WGPUTextureView view,
                            WGPUQuerySet querySet,
                            const std::string& stage,
                            uint32_t slot) {
    WGPUPassTimestampWrites timestampWrites = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    timestampWrites.querySet = querySet;
    timestampWrites.beginningOfPassWriteIndex = slot;
    timestampWrites.endOfPassWriteIndex = slot + 1;

    if (stage == "compute") {
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        passDesc.timestampWrites = &timestampWrites;
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
        wgpuComputePassEncoderEnd(pass);
    } else {
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.timestampWrites = &timestampWrites;
        WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
    }
}

struct QueryUsage {
    WGPUCommandEncoder encoder = nullptr;
    std::vector<WGPUQuerySet> querySets;
};

// Upstream encoderQueryUsage().
QueryUsage encoderQueryUsage(AllFeaturesMaxLimitsGpuTest& t,
                             const std::string& stage,
                             uint32_t numQuerySets,
                             uint32_t numSlots) {
    QueryUsage usage;
    usage.encoder = t.createCommandEncoderTracked();
    WGPUTextureView view = createRenderTargetView(t);
    for (uint32_t i = 0; i < numQuerySets; ++i) {
        WGPUQuerySet querySet = createTimestampQuerySet(t, numSlots);
        for (uint32_t slot = 0; slot < numSlots; slot += 2) {
            encodeTimestampQueries(t, usage.encoder, view, querySet, stage, slot);
        }
        usage.querySets.push_back(querySet);
    }
    return usage;
}

CTS_TEST(g, "many_query_sets")
    .desc(R"(
Test creating and using 64k query objects.

This test is because there is a Metal limit of 32 MTLCounterSampleBuffers
Implementations are supposed to work around this limit by internally allocating
larger MTLCounterSampleBuffers and having the WebGPU sets be subsets of those
larger buffers.

This is particular important as the limit is 32 per process
so a few pages making a few queries would easily hit the limit
and prevent pages from running.
    )")
    .params([](ParamsBuilder u) {
        return u.combine("numQuerySets", {8, 16, 32, 64, 256, 65536})
            .combine("stage", {"compute", "render"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const uint32_t numQuerySets = static_cast<uint32_t>(t.param<int>("numQuerySets"));

        skipIfNoTimestampQuery(t);

        // At large numQuerySets this incurs a lot of validation; wrap it in its own error scope.
        wgpuDevicePushErrorScope(t.device(), WGPUErrorFilter_Validation);
        try {
            WGPUTextureView view = createRenderTargetView(t);
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            for (uint32_t i = 0; i < numQuerySets; ++i) {
                WGPUQuerySet querySet = createTimestampQuerySet(t, 2);
                encodeTimestampQueries(t, encoder, view, querySet, stage, 0);
            }

            const bool shouldError = false;  // just expect no error
            t.expectValidationError(
                [&t, encoder] {
                    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
                    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
                },
                shouldError);
        } catch (...) {
            // Upstream's `finally`: always pop the outer scope.
            (void)popErrorScopeSync(t.testInstance(), t.device());
            throw;
        }
        const ScopeResult scope = popErrorScopeSync(t.testInstance(), t.device());
        if (scope.status != WGPUPopErrorScopeStatus_Success) {
            t.fail("popErrorScope failed: " + scope.message);
        }
        // Make sure there weren't any unexpected validation errors caught by the scope.
        t.expect(scope.type == WGPUErrorType_NoError, scope.message);
    });

CTS_TEST(g, "many_slots")
    .desc(R"(
Test creating and using 4k query slots.

Metal has the limit that a MTLCounterSampleBuffer can be max 32k which is 4k slots.
So, test we can use 4k slots across a few QuerySets
    )")
    .params([](ParamsBuilder u) { return u.combine("stage", {"compute", "render"}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stage = t.param<std::string>("stage");

        skipIfNoTimestampQuery(t);
        constexpr uint32_t kNumSlots = 4096;
        constexpr uint32_t kNumQuerySets = 4;

        QueryUsage usage = encoderQueryUsage(t, stage, kNumQuerySets, kNumSlots);
        WGPUCommandBuffer commandBuffer = t.finishTracked(usage.encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    });

CTS_TEST(g, "resolve_unused_slots")
    .desc(R"(
Test resolving query sets with unused slots.

We create a command buffer that uses the slots but don't actually submit it
to make sure the implementation doesn't mistakenly mark them as used.
    )")
    .params([](ParamsBuilder u) { return u.combine("stage", {"compute", "render"}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stage = t.param<std::string>("stage");

        skipIfNoTimestampQuery(t);

        constexpr uint32_t kNumSlots = 4096;
        constexpr uint32_t kNumQuerySets = 2;

        // Create a encoder and encode usage of every query and slot but do not submit it.
        std::vector<WGPUQuerySet> querySets;
        {
            QueryUsage usage = encoderQueryUsage(t, stage, kNumQuerySets, kNumSlots);
            t.finishTracked(usage.encoder);  // Intentionally not submitted.
            querySets = usage.querySets;
        }

        // Read the slots, they should all be zero.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        std::vector<WGPUBuffer> buffers;
        for (WGPUQuerySet querySet : querySets) {
            WGPUBuffer resolveBuffer =
                createBuffer(t, uint64_t{kNumSlots} * 8, WGPUBufferUsage_CopySrc | WGPUBufferUsage_QueryResolve);
            wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, kNumSlots, resolveBuffer, 0);
            buffers.push_back(resolveBuffer);
        }
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        const std::vector<uint8_t> expected(size_t{kNumSlots} * 8, 0);
        for (WGPUBuffer buffer : buffers) {
            t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size());
        }
    });

CTS_TEST(g, "multi_resolve")
    .desc("Test resolving more than once does not change the results")
    .params([](ParamsBuilder u) { return u.combine("stage", {"compute", "render"}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stage = t.param<std::string>("stage");
        constexpr uint32_t kNumQueries = 64;

        skipIfNoTimestampQuery(t);

        WGPUQuerySet querySet = createTimestampQuerySet(t, kNumQueries);
        WGPUTextureView view = createRenderTargetView(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        for (uint32_t slot = 0; slot < kNumQueries; slot += 2) {
            // skip every other pair so we can test resolving un-used slots
            const uint32_t pair = slot / 2;
            if (pair % 2) {
                continue;
            }
            encodeTimestampQueries(t, encoder, view, querySet, stage, slot);
        }

        constexpr uint64_t kSize = uint64_t{kNumQueries} * 8;
        WGPUBuffer resolveBuffer1 = createBuffer(t, kSize, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
        WGPUBuffer resolveBuffer2 = createBuffer(t, kSize, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
        WGPUBuffer resultBuffer1 = createBuffer(t, kSize, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);

        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, kNumQueries, resolveBuffer1, 0);
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, kNumQueries, resolveBuffer2, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, resolveBuffer1, 0, resultBuffer1, 0, kSize);

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Read back the first result.
        t.expectMapAsync(resultBuffer1, WGPUMapMode_Read, true, 0, static_cast<size_t>(kSize));
        const void* mapped = wgpuBufferGetConstMappedRange(resultBuffer1, 0, static_cast<size_t>(kSize));
        if (mapped == nullptr) {
            wgpuBufferUnmap(resultBuffer1);
            t.fail("failed to get the mapped range of the first resolve result");
        }
        std::vector<uint8_t> expected(static_cast<size_t>(kSize));
        std::memcpy(expected.data(), mapped, expected.size());
        wgpuBufferUnmap(resultBuffer1);
        t.expectGPUBufferValuesEqual(resolveBuffer2, expected.data(), expected.size());
    });

CTS_TEST(g, "unused_slots_are_zero")
    .desc("Test that unused slots are resolved to zero")
    .params([](ParamsBuilder u) { return u.combine("stage", {"compute", "render"}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stage = t.param<std::string>("stage");
        constexpr uint32_t kNumQueries = 64;

        skipIfNoTimestampQuery(t);

        WGPUQuerySet querySet = createTimestampQuerySet(t, kNumQueries);
        WGPUTextureView view = createRenderTargetView(t);
        WGPUCommandEncoder usedEncoder = t.createCommandEncoderTracked();
        WGPUCommandEncoder unusedEncoder = t.createCommandEncoderTracked();

        for (uint32_t slot = 0; slot < kNumQueries; slot += 2) {
            const uint32_t pair = slot / 2;
            WGPUCommandEncoder encoder = pair % 2 ? usedEncoder : unusedEncoder;
            encodeTimestampQueries(t, encoder, view, querySet, stage, slot);
        }

        t.finishTracked(unusedEncoder);  // don't submit this encoder

        constexpr uint64_t kSize = uint64_t{kNumQueries} * 8;
        WGPUBuffer resolveBuffer = createBuffer(t, kSize, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);

        wgpuCommandEncoderResolveQuerySet(usedEncoder, querySet, 0, kNumQueries, resolveBuffer, 0);

        WGPUCommandBuffer commandBuffer = t.finishTracked(usedEncoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        t.expectGPUBufferValuesPassCheck(
            resolveBuffer,
            [](const uint8_t* actualBytes, size_t len) -> std::optional<std::string> {
                std::ostringstream errors;
                bool hasError = false;
                for (uint32_t slot = 0; slot < kNumQueries && (slot + 1) * 8 <= len; ++slot) {
                    uint64_t actual = 0;
                    std::memcpy(&actual, actualBytes + size_t{slot} * 8, sizeof(actual));
                    const uint32_t pair = slot / 2;
                    if (pair % 2) {
                        // used slot, implementation defined so don't check
                    } else if (actual != 0) {
                        // unused slot, expect zero
                        if (hasError) {
                            errors << '\n';
                        }
                        errors << "slot " << slot << " expected 0 but got " << actual;
                        hasError = true;
                    }
                }
                if (hasError) {
                    return errors.str();
                }
                return std::nullopt;
            },
            0, static_cast<size_t>(kSize));
    });

} // namespace
