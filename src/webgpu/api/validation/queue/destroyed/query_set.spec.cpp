// Ported from gpuweb/cts src/webgpu/api/validation/queue/destroyed/query_set.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,destroyed,query_set",
    "Tests using a destroyed query set on a queue.");

// Helper: create a minimal 1x1 rgba8unorm RENDER_ATTACHMENT texture and return its view.
// The texture is tracked for cleanup.
WGPUTextureView createMinimalRenderTargetView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture tex = t.createTextureTracked(texDesc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(tex, viewDesc);
}

// Helper: create a WGPUQuerySet (occlusion, count=2) in the given state.
// 'valid'     -> create and return
// 'destroyed' -> create, destroy, return (GPU validation catches use)
// The caller must release the returned query set.
WGPUQuerySet createQuerySetWithState(AllFeaturesMaxLimitsGpuTest& t,
                                     const std::string& state,
                                     WGPUQueryType type = WGPUQueryType_Occlusion,
                                     uint32_t count = 2) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type = type;
    desc.count = count;
    WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
    if (state == "destroyed") {
        wgpuQuerySetDestroy(qs);
    }
    return qs;
}

// Helper: mirror upstream validateFinishAndSubmitGivenState.
// - 'valid':     finish succeeds, submit succeeds.
// - 'destroyed': finish succeeds, submit should produce a validation error.
void validateFinishAndSubmitGivenState(AllFeaturesMaxLimitsGpuTest& t,
                                       WGPUCommandEncoder encoder,
                                       const std::string& state) {
    // For 'destroyed': finish succeeds, submit errors.
    // For 'valid':     finish succeeds, submit succeeds.
    // (Only 'invalid' state would make finish fail, but this test only uses valid/destroyed.)
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    const bool submitShouldError = (state == "destroyed");
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    }, submitShouldError);
}

// ----

CTS_TEST(g, "beginOcclusionQuery")
    .desc(
        "Tests that use a destroyed query set in occlusion query on render pass encoder.\n"
        "- x= {destroyed, not destroyed (control case)}")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("querySetState", {"valid", "destroyed"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string querySetState = t.param<std::string>("querySetState");

        WGPUQuerySet occlusionQuerySet = createQuerySetWithState(t, querySetState);

        WGPUTextureView view = createMinimalRenderTargetView(t);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.occlusionQuerySet = occlusionQuerySet;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
        wgpuRenderPassEncoderEnd(pass);

        validateFinishAndSubmitGivenState(t, encoder, querySetState);

        wgpuQuerySetRelease(occlusionQuerySet);
    });

CTS_TEST(g, "unusedOcclusionQuery")
    .desc(
        "Tests that use a destroyed query set in occlusion query on render pass encoder, "
        "even if no beginOcclusionQuery calls are done.\n"
        "- x= {destroyed, not destroyed (control case)}")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("querySetState", {"valid", "destroyed"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string querySetState = t.param<std::string>("querySetState");

        WGPUQuerySet occlusionQuerySet = createQuerySetWithState(t, querySetState);

        WGPUTextureView view = createMinimalRenderTargetView(t);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.occlusionQuerySet = occlusionQuerySet;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);

        validateFinishAndSubmitGivenState(t, encoder, querySetState);

        wgpuQuerySetRelease(occlusionQuerySet);
    });

CTS_TEST(g, "timestamps")
    .desc(
        "Tests that use a destroyed query set in timestamp query on "
        "{non-pass, compute, render} encoder.\n"
        "- x= {destroyed, not destroyed (control case)}")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("querySetState", {"valid", "destroyed"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }

        const std::string querySetState = t.param<std::string>("querySetState");

        WGPUQuerySet querySet = createQuerySetWithState(
            t, querySetState, WGPUQueryType_Timestamp, 2);

        // --- compute pass with timestampWrites ---
        {
            WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
            tw.querySet = querySet;
            tw.beginningOfPassWriteIndex = 0;
            // endOfPassWriteIndex left as WGPU_QUERY_SET_INDEX_UNDEFINED (from INIT)

            WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            passDesc.timestampWrites = &tw;

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderEnd(pass);

            validateFinishAndSubmitGivenState(t, encoder, querySetState);
        }

        // --- render pass with timestampWrites ---
        {
            WGPUTextureView view = createMinimalRenderTargetView(t);

            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.view = view;
            colorAttachment.loadOp = WGPULoadOp_Load;
            colorAttachment.storeOp = WGPUStoreOp_Store;

            WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
            tw.querySet = querySet;
            tw.beginningOfPassWriteIndex = 0;
            // endOfPassWriteIndex left as WGPU_QUERY_SET_INDEX_UNDEFINED (from INIT)

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.timestampWrites = &tw;

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);

            validateFinishAndSubmitGivenState(t, encoder, querySetState);
        }

        wgpuQuerySetRelease(querySet);
    });

CTS_TEST(g, "resolveQuerySet")
    .desc(
        "Tests that use a destroyed query set in resolveQuerySet.\n"
        "- x= {destroyed, not destroyed (control case)}")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("querySetState", {"valid", "destroyed"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string querySetState = t.param<std::string>("querySetState");

        WGPUQuerySet querySet = createQuerySetWithState(t, querySetState);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 8;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, buffer, 0);

        validateFinishAndSubmitGivenState(t, encoder, querySetState);

        wgpuQuerySetRelease(querySet);
    });

} // namespace
