// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/debug.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstddef>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,debug",
    "API validation test for debug groups and markers");

// ---------------------------------------------------------------------------
// WGPUStringView helper
// ---------------------------------------------------------------------------

static WGPUStringView toStringView(const std::string& s) {
    WGPUStringView sv = WGPU_STRING_VIEW_INIT;
    sv.data   = s.data();
    sv.length = s.size();
    return sv;
}

// ---------------------------------------------------------------------------
// Encoder type strings — mirror upstream kEncoderTypes
//   ['non-pass', 'compute pass', 'render pass', 'render bundle']
// ---------------------------------------------------------------------------

static std::vector<Value> kEncoderTypeValues() {
    return {
        std::string("non-pass"),
        std::string("compute pass"),
        std::string("render pass"),
        std::string("render bundle"),
    };
}

// ---------------------------------------------------------------------------
// EncoderContext: wraps the four encoder-type variants.
//
// For each encoderType we create the underlying command encoder, begin any
// required pass/bundle encoder, expose a unified push/pop/insertMarker API,
// and provide a finish() that ends the pass/bundle and returns a
// WGPUCommandBuffer (or nullptr if finish itself is expected to fail).
// ---------------------------------------------------------------------------

// A minimal 16×16 rgba8unorm render target texture (no COPY_SRC needed).
static WGPUTexture createSmallRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size           = WGPUExtent3D{16, 16, 1};
    desc.mipLevelCount  = 1;
    desc.sampleCount    = 1;
    desc.dimension      = WGPUTextureDimension_2D;
    desc.format         = WGPUTextureFormat_RGBA8Unorm;
    desc.usage          = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// Begin a single-color-attachment render pass (no depth/stencil).
static WGPURenderPassEncoder beginSimpleRenderPass(WGPUCommandEncoder cmdEnc, WGPUTextureView view) {
    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view        = view;
    colorAttach.loadOp      = WGPULoadOp_Clear;
    colorAttach.storeOp     = WGPUStoreOp_Store;
    colorAttach.clearValue  = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;
    return wgpuCommandEncoderBeginRenderPass(cmdEnc, &passDesc);
}

// ---------------------------------------------------------------------------
// Dispatch helpers: push / pop / insertMarker on the four encoder types.
// ---------------------------------------------------------------------------

static void encoderPushDebugGroup(
    const std::string& encoderType,
    WGPUCommandEncoder       cmdEnc,
    WGPUComputePassEncoder   computePass,
    WGPURenderPassEncoder    renderPass,
    WGPURenderBundleEncoder  bundleEnc,
    const std::string& label)
{
    WGPUStringView sv = toStringView(label);
    if (encoderType == "non-pass") {
        wgpuCommandEncoderPushDebugGroup(cmdEnc, sv);
    } else if (encoderType == "compute pass") {
        wgpuComputePassEncoderPushDebugGroup(computePass, sv);
    } else if (encoderType == "render pass") {
        wgpuRenderPassEncoderPushDebugGroup(renderPass, sv);
    } else {
        // render bundle
        wgpuRenderBundleEncoderPushDebugGroup(bundleEnc, sv);
    }
}

static void encoderPopDebugGroup(
    const std::string& encoderType,
    WGPUCommandEncoder       cmdEnc,
    WGPUComputePassEncoder   computePass,
    WGPURenderPassEncoder    renderPass,
    WGPURenderBundleEncoder  bundleEnc)
{
    if (encoderType == "non-pass") {
        wgpuCommandEncoderPopDebugGroup(cmdEnc);
    } else if (encoderType == "compute pass") {
        wgpuComputePassEncoderPopDebugGroup(computePass);
    } else if (encoderType == "render pass") {
        wgpuRenderPassEncoderPopDebugGroup(renderPass);
    } else {
        wgpuRenderBundleEncoderPopDebugGroup(bundleEnc);
    }
}

static void encoderInsertDebugMarker(
    const std::string& encoderType,
    WGPUCommandEncoder       cmdEnc,
    WGPUComputePassEncoder   computePass,
    WGPURenderPassEncoder    renderPass,
    WGPURenderBundleEncoder  bundleEnc,
    const std::string& label)
{
    WGPUStringView sv = toStringView(label);
    if (encoderType == "non-pass") {
        wgpuCommandEncoderInsertDebugMarker(cmdEnc, sv);
    } else if (encoderType == "compute pass") {
        wgpuComputePassEncoderInsertDebugMarker(computePass, sv);
    } else if (encoderType == "render pass") {
        wgpuRenderPassEncoderInsertDebugMarker(renderPass, sv);
    } else {
        wgpuRenderBundleEncoderInsertDebugMarker(bundleEnc, sv);
    }
}

// ---------------------------------------------------------------------------
// Context structure for one encoder "session" (mirrors CommandBufferMaker).
// ---------------------------------------------------------------------------

struct EncoderContext {
    std::string              encoderType;

    // Command encoder (always present)
    WGPUCommandEncoder       cmdEnc      = nullptr;

    // Typed pass/bundle encoders (one is non-null depending on encoderType)
    WGPUComputePassEncoder   computePass = nullptr;

    // For render pass and render bundle we need a texture + view.
    WGPUTexture              renderTex   = nullptr;
    WGPUTextureView          renderView  = nullptr;
    WGPURenderPassEncoder    renderPass  = nullptr;

    // render bundle: the bundle encoder itself + a render pass to execute it
    WGPURenderBundleEncoder  bundleEnc   = nullptr;
    // The render pass for executing the bundle is opened in the same cmdEnc.
    WGPURenderPassEncoder    bundlePass  = nullptr;
};

// Create an EncoderContext for the given encoderType.
// The caller is responsible for releasing resources after use.
static EncoderContext makeEncoderContext(AllFeaturesMaxLimitsGpuTest& t, const std::string& encoderType) {
    EncoderContext ctx;
    ctx.encoderType = encoderType;
    ctx.cmdEnc      = t.createCommandEncoderTracked();

    if (encoderType == "non-pass") {
        // nothing more needed
    } else if (encoderType == "compute pass") {
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        ctx.computePass = wgpuCommandEncoderBeginComputePass(ctx.cmdEnc, &cpDesc);
    } else if (encoderType == "render pass") {
        ctx.renderTex  = createSmallRenderTarget(t);
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        ctx.renderView = t.createViewTracked(ctx.renderTex, vDesc);
        ctx.renderPass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
    } else {
        // render bundle: open a bundle encoder + a render pass to execute it
        ctx.renderTex  = createSmallRenderTarget(t);
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        ctx.renderView = t.createViewTracked(ctx.renderTex, vDesc);

        WGPUTextureFormat colorFmt = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDesc.colorFormatCount = 1;
        bDesc.colorFormats     = &colorFmt;
        bDesc.sampleCount      = 1;
        ctx.bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);

        // The render pass that will execute the bundle is also opened here;
        // it is ended (and the bundle executed) during finish().
        ctx.bundlePass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
    }
    return ctx;
}

// Push a debug group label through the context.
static void ctxPushDebugGroup(EncoderContext& ctx, const std::string& label) {
    encoderPushDebugGroup(ctx.encoderType,
                          ctx.cmdEnc, ctx.computePass, ctx.renderPass, ctx.bundleEnc,
                          label);
}

// Pop a debug group through the context.
static void ctxPopDebugGroup(EncoderContext& ctx) {
    encoderPopDebugGroup(ctx.encoderType,
                         ctx.cmdEnc, ctx.computePass, ctx.renderPass, ctx.bundleEnc);
}

// Insert a debug marker through the context.
static void ctxInsertDebugMarker(EncoderContext& ctx, const std::string& label) {
    encoderInsertDebugMarker(ctx.encoderType,
                             ctx.cmdEnc, ctx.computePass, ctx.renderPass, ctx.bundleEnc,
                             label);
}

// Finish the context: end pass/bundle, return the WGPUCommandBuffer.
// May be called inside expectValidationError when finish is expected to fail.
// Note: pass encoders are not released here — they are owned by the WebGPU
// implementation and cleaned up when the command encoder is finished/released.
static WGPUCommandBuffer ctxFinish(AllFeaturesMaxLimitsGpuTest& t, EncoderContext& ctx) {
    if (ctx.encoderType == "non-pass") {
        // nothing to end
    } else if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderEnd(ctx.computePass);
        ctx.computePass = nullptr;
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
        // render bundle: finish the bundle, execute it, end the bundle pass
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(ctx.bundleEnc, nullptr);
        ctx.bundleEnc = nullptr;

        wgpuRenderPassEncoderExecuteBundles(ctx.bundlePass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);

        wgpuRenderPassEncoderEnd(ctx.bundlePass);
        ctx.bundlePass = nullptr;
    }
    return t.finishTracked(ctx.cmdEnc);
}

// ---------------------------------------------------------------------------
// validateFinishAndSubmit:
//   mirrors CommandBufferMaker.validateFinishAndSubmit(shouldBeValid, submitShouldSucceedIfValid)
// ---------------------------------------------------------------------------

static void validateFinishAndSubmit(
    AllFeaturesMaxLimitsGpuTest& t,
    EncoderContext& ctx,
    bool shouldBeValid,
    bool submitShouldSucceedIfValid)
{
    if (!shouldBeValid) {
        // Finish is expected to produce a validation error.
        t.expectValidationError([&] {
            ctxFinish(t, ctx);
        }, true);
        return;
    }

    // Finish should succeed.
    WGPUCommandBuffer cb = ctxFinish(t, ctx);

    // Submit — expect error only if !submitShouldSucceedIfValid.
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, !submitShouldSucceedIfValid);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

CTS_TEST(g, "debug_group_balanced")
    .desc(
        "Test that all pushDebugGroup calls must have a corresponding popDebugGroup.\n"
        "Push and pop counts of 0, 1, and 2 will be used.\n"
        "An error must be generated for non-matching counts.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kEncoderTypeValues())
            .beginSubcases()
            .combine("pushCount", {0, 1, 2})
            .combine("popCount",  {0, 1, 2});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const int pushCount           = t.param<int>("pushCount");
        const int popCount            = t.param<int>("popCount");

        EncoderContext ctx = makeEncoderContext(t, encoderType);

        for (int i = 0; i < pushCount; ++i) {
            ctxPushDebugGroup(ctx, std::to_string(i));
        }
        for (int i = 0; i < popCount; ++i) {
            ctxPopDebugGroup(ctx);
        }

        const bool shouldBeValid = (pushCount == popCount);
        validateFinishAndSubmit(t, ctx, shouldBeValid, true);
    });

CTS_TEST(g, "debug_group")
    .desc(
        "Test calling pushDebugGroup with empty and non-empty strings,\n"
        "strings with embedded NUL characters, and non-ASCII strings.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kEncoderTypeValues())
            .beginSubcases()
            .combine("label", {
                std::string(""),
                std::string("group"),
                std::string("null\0in\0group\0label", 19),
                std::string("\0null at beginning", 18),
                std::string("\xF0\x9F\x8C\x9E\xF0\x9F\x91\x86"),  // 🌞👆 as UTF-8
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string label       = t.param<std::string>("label");

        EncoderContext ctx = makeEncoderContext(t, encoderType);
        ctxPushDebugGroup(ctx, label);
        ctxPopDebugGroup(ctx);
        validateFinishAndSubmit(t, ctx, true, true);
    });

CTS_TEST(g, "debug_marker")
    .desc(
        "Test inserting a debug marker with empty and non-empty strings,\n"
        "strings with embedded NUL characters, and non-ASCII strings.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kEncoderTypeValues())
            .beginSubcases()
            .combine("label", {
                std::string(""),
                std::string("marker"),
                std::string("null\0in\0marker", 14),
                std::string("\0null at beginning", 18),
                std::string("\xF0\x9F\x8C\x9E\xF0\x9F\x91\x86"),  // 🌞👆 as UTF-8
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string label       = t.param<std::string>("label");

        EncoderContext ctx = makeEncoderContext(t, encoderType);
        ctxInsertDebugMarker(ctx, label);
        validateFinishAndSubmit(t, ctx, true, true);
    });

} // namespace
