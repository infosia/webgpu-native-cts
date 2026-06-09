// Ported from gpuweb/cts src/webgpu/api/validation/encoding/beginRenderPass.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,beginRenderPass",
    "Note: render pass 'occlusionQuerySet' validation is tested in queries/general.spec.ts");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a 16x16 rgba8unorm RENDER_ATTACHMENT texture on the given device.
// sampleCount controls multisampling (1 = non-multisampled, 4 = 4x MSAA).
static WGPUTexture createRenderTexture(AllFeaturesMaxLimitsGpuTest& t, uint32_t sampleCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{16, 16, 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.sampleCount = sampleCount;
    return t.createTextureTracked(desc);
}

// Create a 4x4 rgba8unorm RENDER_ATTACHMENT texture on the given device.
// Used for mismatched-device textures (matching upstream getDeviceMismatchedRenderTexture).
static WGPUTexture createMismatchedRenderTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t sampleCount = 1)
{
    WGPUDevice sourceDevice = t.mismatchedDevice();
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{4, 4, 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.sampleCount = sampleCount;
    return wgpuDeviceCreateTexture(sourceDevice, &desc);
}

// Create a full-default texture view for a texture.
static WGPUTextureView createDefaultView(WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return wgpuTextureCreateView(texture, &desc);
}

// ---------------------------------------------------------------------------
// Test: color_attachments,device_mismatch
// Tests beginRenderPass cannot be called with color attachments whose texture
// view or resolve target is created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "color_attachments,device_mismatch")
    .desc(
        "Tests beginRenderPass cannot be called with color attachments whose texture view or "
        "resolve target is created from another device. "
        "The 'view' and 'resolveTarget' are: "
        "- created from same device in ColorAttachment0 and ColorAttachment1 "
        "- created from different device in ColorAttachment0 and ColorAttachment1 "
        "- created from same device in ColorAttachment0, but from different device in ColorAttachment1")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            // control case: all from same device
            ParamRecord{
                {"view0Mismatched", false},
                {"target0Mismatched", false},
                {"view1Mismatched", false},
                {"target1Mismatched", false}},
            // both resolve targets from different device
            ParamRecord{
                {"view0Mismatched", false},
                {"target0Mismatched", true},
                {"view1Mismatched", false},
                {"target1Mismatched", true}},
            // both views from different device
            ParamRecord{
                {"view0Mismatched", true},
                {"target0Mismatched", false},
                {"view1Mismatched", true},
                {"target1Mismatched", false}},
            // only target1 from different device
            ParamRecord{
                {"view0Mismatched", false},
                {"target0Mismatched", false},
                {"view1Mismatched", false},
                {"target1Mismatched", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool view0Mismatched   = t.param<bool>("view0Mismatched");
        const bool target0Mismatched = t.param<bool>("target0Mismatched");
        const bool view1Mismatched   = t.param<bool>("view1Mismatched");
        const bool target1Mismatched = t.param<bool>("target1Mismatched");

        const bool mismatched = view0Mismatched || target0Mismatched
                                || view1Mismatched || target1Mismatched;

        // Multisampled (sampleCount=4) textures for views, sampleCount=1 for resolve targets.
        WGPUTexture view0Tex = view0Mismatched
            ? createMismatchedRenderTexture(t, 4)
            : createRenderTexture(t, 4);
        WGPUTexture target0Tex = target0Mismatched
            ? createMismatchedRenderTexture(t, 1)
            : createRenderTexture(t, 1);
        WGPUTexture view1Tex = view1Mismatched
            ? createMismatchedRenderTexture(t, 4)
            : createRenderTexture(t, 4);
        WGPUTexture target1Tex = target1Mismatched
            ? createMismatchedRenderTexture(t, 1)
            : createRenderTexture(t, 1);

        WGPUTextureView view0   = createDefaultView(view0Tex);
        WGPUTextureView target0 = createDefaultView(target0Tex);
        WGPUTextureView view1   = createDefaultView(view1Tex);
        WGPUTextureView target1 = createDefaultView(target1Tex);

        WGPURenderPassColorAttachment colorAttachments[2];
        colorAttachments[0] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachments[0].view          = view0;
        colorAttachments[0].resolveTarget = target0;
        colorAttachments[0].loadOp        = WGPULoadOp_Clear;
        colorAttachments[0].storeOp       = WGPUStoreOp_Store;
        colorAttachments[0].clearValue    = WGPUColor{1.0, 0.0, 0.0, 1.0};

        colorAttachments[1] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachments[1].view          = view1;
        colorAttachments[1].resolveTarget = target1;
        colorAttachments[1].loadOp        = WGPULoadOp_Clear;
        colorAttachments[1].storeOp       = WGPUStoreOp_Store;
        colorAttachments[1].clearValue    = WGPUColor{1.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 2;
        passDesc.colorAttachments     = colorAttachments;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, mismatched);

        // Release mismatched-device resources
        wgpuTextureViewRelease(view0);
        wgpuTextureViewRelease(target0);
        wgpuTextureViewRelease(view1);
        wgpuTextureViewRelease(target1);
        if (view0Mismatched)   { wgpuTextureRelease(view0Tex); }
        if (target0Mismatched) { wgpuTextureRelease(target0Tex); }
        if (view1Mismatched)   { wgpuTextureRelease(view1Tex); }
        if (target1Mismatched) { wgpuTextureRelease(target1Tex); }
    });

// ---------------------------------------------------------------------------
// Test: depth_stencil_attachment,device_mismatch
// Tests beginRenderPass cannot be called with a depth stencil attachment
// whose texture view is created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "depth_stencil_attachment,device_mismatch")
    .desc(
        "Tests beginRenderPass cannot be called with a depth stencil attachment "
        "whose texture view is created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.size = WGPUExtent3D{4, 4, 1};
        descriptor.format = WGPUTextureFormat_Depth24PlusStencil8;
        descriptor.usage = WGPUTextureUsage_RenderAttachment;

        WGPUTexture depthStencilTexture = nullptr;
        if (mismatched) {
            depthStencilTexture = wgpuDeviceCreateTexture(t.mismatchedDevice(), &descriptor);
        } else {
            depthStencilTexture = t.createTextureTracked(descriptor);
        }

        WGPUTextureView depthStencilView = createDefaultView(depthStencilTexture);

        WGPURenderPassDepthStencilAttachment dsAttachment =
            WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        dsAttachment.view             = depthStencilView;
        dsAttachment.depthClearValue  = 0.0f;
        dsAttachment.depthLoadOp      = WGPULoadOp_Clear;
        dsAttachment.depthStoreOp     = WGPUStoreOp_Store;
        dsAttachment.stencilClearValue = 0;
        dsAttachment.stencilLoadOp    = WGPULoadOp_Clear;
        dsAttachment.stencilStoreOp   = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount      = 0;
        passDesc.colorAttachments          = nullptr;
        passDesc.depthStencilAttachment    = &dsAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, mismatched);

        wgpuTextureViewRelease(depthStencilView);
        if (mismatched) {
            wgpuTextureRelease(depthStencilTexture);
        }
    });

// ---------------------------------------------------------------------------
// Test: occlusion_query_set,device_mismatch
// Tests beginRenderPass cannot be called with an occlusion query set
// created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query_set,device_mismatch")
    .desc(
        "Tests beginRenderPass cannot be called with an occlusion query set "
        "created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = 1;
        WGPUQuerySet occlusionQuerySet = wgpuDeviceCreateQuerySet(sourceDevice, &qsDesc);

        // Need a color attachment to make the render pass valid.
        WGPUTexture colorTex = createRenderTexture(t, 1);
        WGPUTextureView colorView = createDefaultView(colorTex);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view    = colorView;
        colorAttachment.loadOp  = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;
        passDesc.occlusionQuerySet    = occlusionQuerySet;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, mismatched);

        wgpuTextureViewRelease(colorView);
        wgpuQuerySetRelease(occlusionQuerySet);
    });

// ---------------------------------------------------------------------------
// Test: timestamp_query_set,device_mismatch
// Tests beginRenderPass cannot be called with a timestamp query set created
// from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "timestamp_query_set,device_mismatch")
    .desc(
        "Tests beginRenderPass cannot be called with a timestamp query set "
        "created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }

        const bool mismatched = t.param<bool>("mismatched");

        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        // The query set is created on sourceDevice; that device must have the
        // timestamp-query feature, otherwise creating the query set raises an
        // uncaptured error rather than the validation error the test is checking.
        if (!wgpuDeviceHasFeature(sourceDevice, WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available on source device");
        }

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Timestamp;
        qsDesc.count = 1;
        WGPUQuerySet timestampQuerySet = wgpuDeviceCreateQuerySet(sourceDevice, &qsDesc);

        WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        tw.querySet                  = timestampQuerySet;
        tw.beginningOfPassWriteIndex = 0;
        // endOfPassWriteIndex left as WGPU_QUERY_SET_INDEX_UNDEFINED (from INIT)

        // Need a color attachment to make the render pass valid.
        WGPUTexture colorTex = createRenderTexture(t, 1);
        WGPUTextureView colorView = createDefaultView(colorTex);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view    = colorView;
        colorAttachment.loadOp  = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;
        passDesc.timestampWrites      = &tw;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, mismatched);

        wgpuTextureViewRelease(colorView);
        wgpuQuerySetRelease(timestampQuerySet);
    });

} // namespace
