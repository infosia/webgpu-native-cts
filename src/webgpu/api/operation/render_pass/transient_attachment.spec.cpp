// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/transient_attachment.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pass,transient_attachment",
    "API Operation Tests for transient attachment in render passes.");

constexpr uint32_t kSize = 4;

// increasing_attachments_count: run empty render passes with increasing numbers of transient
// color attachments (1..maxColorAttachments). Passes if submit completes without error.
CTS_TEST(g, "increasing_attachments_count")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        t.skipIfTransientAttachmentNotSupported();

        WGPULimits limits = WGPU_LIMITS_INIT;
        wgpuDeviceGetLimits(t.device(), &limits);
        const uint32_t kRgba8UnormByteCost = 8;  // WebGPU color-attachment byte cost of RGBA8Unorm
        const uint32_t maxByBytes = limits.maxColorAttachmentBytesPerSample / kRgba8UnormByteCost;
        const uint32_t maxAttachments = std::min(limits.maxColorAttachments, maxByBytes);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        for (uint32_t count = 1; count <= maxAttachments; ++count) {
            // Create `count` transient textures and their views.
            std::vector<WGPUTexture> textures;
            std::vector<WGPUTextureView> views;
            textures.reserve(count);
            views.reserve(count);

            WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            texDesc.size = WGPUExtent3D{kSize, kSize, 1};
            texDesc.mipLevelCount = 1;
            texDesc.sampleCount = 1;
            texDesc.dimension = WGPUTextureDimension_2D;
            texDesc.format = WGPUTextureFormat_RGBA8Unorm;
            texDesc.usage =
                WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment;

            for (uint32_t i = 0; i < count; ++i) {
                WGPUTexture tex = t.createTextureTracked(texDesc);
                textures.push_back(tex);
                WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                views.push_back(t.createViewTracked(tex, viewDesc));
            }

            // Build the color attachment array.
            std::vector<WGPURenderPassColorAttachment> colorAttachments(count);
            for (uint32_t i = 0; i < count; ++i) {
                colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                colorAttachments[i].view = views[i];
                colorAttachments[i].loadOp = WGPULoadOp_Clear;
                colorAttachments[i].storeOp = WGPUStoreOp_Discard;
                colorAttachments[i].clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
            }

            // Empty render pass: begin then end immediately.
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = count;
            passDesc.colorAttachments = colorAttachments.data();

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
        }

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    });

// overlapping_transient_attachments: run 3 render passes with transient attachments in a circular
// overlap pattern — (T1,T2), (T2,T3), (T3,T1) — to stress the driver's transient memory
// allocator. Passes if submit completes without error.
CTS_TEST(g, "overlapping_transient_attachments")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        t.skipIfTransientAttachmentNotSupported();

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{kSize, kSize, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage =
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment;

        WGPUTexture tex1 = t.createTextureTracked(texDesc);
        WGPUTexture tex2 = t.createTextureTracked(texDesc);
        WGPUTexture tex3 = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view1 = t.createViewTracked(tex1, viewDesc);
        WGPUTextureView view2 = t.createViewTracked(tex2, viewDesc);
        WGPUTextureView view3 = t.createViewTracked(tex3, viewDesc);

        // Three passes in a circular overlap pattern:
        //   Pass 1: (T1, T2)
        //   Pass 2: (T2, T3)
        //   Pass 3: (T3, T1)
        const WGPUTextureView passViews[3][2] = {
            {view1, view2},
            {view2, view3},
            {view3, view1},
        };

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        for (int p = 0; p < 3; ++p) {
            WGPURenderPassColorAttachment colorAttachments[2];
            for (int i = 0; i < 2; ++i) {
                colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                colorAttachments[i].view = passViews[p][i];
                colorAttachments[i].loadOp = WGPULoadOp_Clear;
                colorAttachments[i].storeOp = WGPUStoreOp_Discard;
                colorAttachments[i].clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
            }

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 2;
            passDesc.colorAttachments = colorAttachments;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
        }

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    });

} // namespace
