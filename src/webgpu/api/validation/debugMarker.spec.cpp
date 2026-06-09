// Ported from gpuweb/cts src/webgpu/api/validation/debugMarker.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// Helper: construct a WGPUStringView from a string literal / std::string_view.
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// Mirrors the beginRenderPass() helper on the upstream fixture class F.
// Creates a minimal 16×16 rgba8unorm render-attachment texture and begins a
// render pass on it, returning the WGPURenderPassEncoder.
static WGPURenderPassEncoder beginRenderPass(GpuTest& t, WGPUCommandEncoder encoder) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.size.width = 16;
    texDesc.size.height = 16;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture attachmentTexture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(attachmentTexture, viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,debugMarker",
    "Test validation of pushDebugGroup, popDebugGroup, and insertDebugMarker.");

// g.test('push_pop_call_count_unbalance,command_encoder')
CTS_TEST(g, "push_pop_call_count_unbalance,command_encoder")
    .desc(
        "Test that a validation error is generated if {push,pop} debug group call count is not "
        "paired.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("pushCount", {Value(1), Value(2), Value(3)})
            .combine("popCount", {Value(1), Value(2), Value(3)});
    })
    .fn([](GpuTest& t) {
        const int pushCount = t.param<int>("pushCount");
        const int popCount = t.param<int>("popCount");

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        for (int i = 0; i < pushCount; ++i) {
            wgpuCommandEncoderPushDebugGroup(encoder, sv("EventStart"));
        }

        wgpuCommandEncoderInsertDebugMarker(encoder, sv("Marker"));

        for (int i = 0; i < popCount; ++i) {
            wgpuCommandEncoderPopDebugGroup(encoder);
        }

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, pushCount != popCount);
    });

// g.test('push_pop_call_count_unbalance,render_compute_pass')
CTS_TEST(g, "push_pop_call_count_unbalance,render_compute_pass")
    .desc(
        "Test that a validation error is generated if {push,pop} debug group call count is not "
        "paired in ComputePassEncoder and RenderPassEncoder.")
    .params([](ParamsBuilder u) {
        return u.combine("passType", {Value(std::string("compute")), Value(std::string("render"))})
            .beginSubcases()
            .combine("pushCount", {Value(1), Value(2), Value(3)})
            .combine("popCount", {Value(1), Value(2), Value(3)});
    })
    .fn([](GpuTest& t) {
        const std::string passType = t.param<std::string>("passType");
        const int pushCount = t.param<int>("pushCount");
        const int popCount = t.param<int>("popCount");

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        const bool shouldError = pushCount != popCount;

        if (passType == "compute") {
            WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

            for (int i = 0; i < pushCount; ++i) {
                wgpuComputePassEncoderPushDebugGroup(pass, sv("EventStart"));
            }

            wgpuComputePassEncoderInsertDebugMarker(pass, sv("Marker"));

            for (int i = 0; i < popCount; ++i) {
                wgpuComputePassEncoderPopDebugGroup(pass);
            }

            t.expectValidationError([&] {
                wgpuComputePassEncoderEnd(pass);
                t.finishTracked(encoder);
            }, shouldError);
        } else {
            // passType == "render"
            WGPURenderPassEncoder pass = beginRenderPass(t, encoder);

            for (int i = 0; i < pushCount; ++i) {
                wgpuRenderPassEncoderPushDebugGroup(pass, sv("EventStart"));
            }

            wgpuRenderPassEncoderInsertDebugMarker(pass, sv("Marker"));

            for (int i = 0; i < popCount; ++i) {
                wgpuRenderPassEncoderPopDebugGroup(pass);
            }

            t.expectValidationError([&] {
                wgpuRenderPassEncoderEnd(pass);
                t.finishTracked(encoder);
            }, shouldError);
        }
    });

} // namespace
