// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/queries/timestampQuery.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal port: resolve_unused_slots over stage{compute,render} = 2 cases;
// many_query_sets, many_slots, multi_resolve, unused_slots_are_zero deferred.

#include <array>
#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,queries,timestampQuery",
    "API operations tests for timestamp queries.");

// resolve_unused_slots: create a command buffer using the timestamp query slots but do not submit
// it; then resolve the slots in a separate submitted encoder and verify all bytes are zero.
CTS_TEST(g, "resolve_unused_slots")
    .desc(
        "Test resolving query sets with unused slots. "
        "We create a command buffer that uses the slots but don't actually submit it "
        "to make sure the implementation doesn't mistakenly mark them as used.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("stage", {"compute", "render"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }

        const std::string stage = t.param<std::string>("stage");

        // Create a 1x1 rgba8unorm RENDER_ATTACHMENT texture (used for render case).
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture tex = t.createTextureTracked(texDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(tex, viewDesc);

        // Create the timestamp query set: type Timestamp, count 2.
        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = WGPUQueryType_Timestamp;
        qsDesc.count = 2;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        // Usage encoder: encode one pass with timestampWrites targeting both slots,
        // but do NOT submit the resulting command buffer.
        {
            WGPUCommandEncoder usedEncoder = t.createCommandEncoderTracked();

            WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
            tw.querySet = querySet;
            tw.beginningOfPassWriteIndex = 0;
            tw.endOfPassWriteIndex = 1;

            if (stage == "compute") {
                WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                passDesc.timestampWrites = &tw;
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(usedEncoder, &passDesc);
                wgpuComputePassEncoderEnd(pass);
            } else {
                // render: empty pass with a 1x1 color attachment (loadOp Load, storeOp Store).
                WGPURenderPassColorAttachment colorAttachment =
                    WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                colorAttachment.view = view;
                colorAttachment.loadOp = WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;

                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;
                passDesc.timestampWrites = &tw;
                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(usedEncoder, &passDesc);
                wgpuRenderPassEncoderEnd(pass);
            }

            // Finish the encoder but do NOT submit — the point of this test.
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer unusedCb = wgpuCommandEncoderFinish(usedEncoder, &cbDesc);
            // Intentionally not submitted; release immediately.
            wgpuCommandBufferRelease(unusedCb);
        }

        // Resolve encoder: resolveBuffer = 2 * 8 = 16 bytes (two u64 timestamp slots).
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 2 * sizeof(uint64_t);
        bufDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        WGPUBuffer resolveBuffer = t.createBufferTracked(bufDesc);

        {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 2, resolveBuffer, 0);
            WGPUCommandBuffer cb = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &cb);
        }

        wgpuQuerySetRelease(querySet);

        // Verify: all 16 bytes (two u64 timestamp slots) must be zero.
        const std::array<uint8_t, 16> zeros{};
        t.expectGPUBufferValuesEqual(resolveBuffer, zeros.data(), zeros.size());
    });

} // namespace
