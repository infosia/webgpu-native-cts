// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render/setIndexBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// kRenderEncodeTypes = ['render pass', 'render bundle']
static std::vector<Value> kRenderEncodeTypeValues() {
    return {
        std::string("render pass"),
        std::string("render bundle"),
    };
}

// ---------------------------------------------------------------------------
// RenderEncoderContext — wraps a render pass or render bundle encoder,
// providing setIndexBuffer and validateFinish/validateFinishAndSubmitGivenState.
//
// Design mirrors the EncoderContext pattern used in debug.spec.cpp but
// specialized for the two render encoder types that support setIndexBuffer.
// ---------------------------------------------------------------------------

struct RenderEncoderContext {
    std::string encoderType;

    WGPUCommandEncoder       cmdEnc     = nullptr;

    // For "render pass"
    WGPUTexture              renderTex  = nullptr;
    WGPUTextureView          renderView = nullptr;
    WGPURenderPassEncoder    renderPass = nullptr;

    // For "render bundle": bundle encoder + execution pass
    WGPURenderBundleEncoder  bundleEnc  = nullptr;
    WGPURenderPassEncoder    bundlePass = nullptr;
    // Shared render texture/view for both cases
};

// Create a 16×16 RGBA8Unorm render-attachment texture (no COPY_SRC needed).
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

// Begin a minimal single-color-attachment render pass.
static WGPURenderPassEncoder beginSimpleRenderPass(
    WGPUCommandEncoder cmdEnc,
    WGPUTextureView    view)
{
    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view       = view;
    colorAttach.loadOp     = WGPULoadOp_Clear;
    colorAttach.storeOp    = WGPUStoreOp_Store;
    colorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;
    return wgpuCommandEncoderBeginRenderPass(cmdEnc, &passDesc);
}

static RenderEncoderContext makeRenderEncoderContext(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType)
{
    RenderEncoderContext ctx;
    ctx.encoderType = encoderType;
    ctx.cmdEnc      = t.createCommandEncoderTracked();

    ctx.renderTex  = createSmallRenderTarget(t);
    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    ctx.renderView = t.createViewTracked(ctx.renderTex, vDesc);

    if (encoderType == "render pass") {
        ctx.renderPass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
    } else {
        // render bundle: open bundle encoder + a pass that executes it
        WGPUTextureFormat colorFmt = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDesc.colorFormatCount = 1;
        bDesc.colorFormats     = &colorFmt;
        bDesc.sampleCount      = 1;
        ctx.bundleEnc  = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);
        ctx.bundlePass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
    }
    return ctx;
}

// Call setIndexBuffer on the appropriate encoder.
static void ctxSetIndexBuffer(
    RenderEncoderContext& ctx,
    WGPUBuffer            buffer,
    WGPUIndexFormat       format,
    uint64_t              offset,
    uint64_t              size)
{
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetIndexBuffer(ctx.renderPass, buffer, format, offset, size);
    } else {
        wgpuRenderBundleEncoderSetIndexBuffer(ctx.bundleEnc, buffer, format, offset, size);
    }
}

// Finish the context: end pass/bundle, return WGPUCommandBuffer.
static WGPUCommandBuffer ctxFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    RenderEncoderContext& ctx)
{
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
        // Finish the bundle, execute it in the bundle pass, then end the pass.
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(ctx.bundleEnc, nullptr);
        ctx.bundleEnc = nullptr;

        wgpuRenderPassEncoderExecuteBundles(ctx.bundlePass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);

        wgpuRenderPassEncoderEnd(ctx.bundlePass);
        ctx.bundlePass = nullptr;
    }
    return t.finishTracked(ctx.cmdEnc);
}

// validateFinish: mirrors CommandBufferMaker.validateFinish(shouldSucceed).
// Errors are expected on finish() when !shouldSucceed.
static void validateFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    RenderEncoderContext& ctx,
    bool shouldSucceed)
{
    if (!shouldSucceed) {
        t.expectValidationError([&] {
            ctxFinish(t, ctx);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = ctxFinish(t, ctx);
    // Submit the valid command buffer (expected to succeed).
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, false);
}

// validateFinishAndSubmitGivenState: mirrors
//   CommandBufferMaker.validateFinishAndSubmitGivenState(state).
// - Invalid  → finish() validation error
// - Destroyed → submit() validation error
// - Valid    → both succeed
static void validateFinishAndSubmitGivenState(
    AllFeaturesMaxLimitsGpuTest& t,
    RenderEncoderContext& ctx,
    ResourceState state)
{
    const bool shouldFinishSucceed = (state != ResourceState::Invalid);
    const bool shouldSubmitSucceed = (state != ResourceState::Destroyed);

    if (!shouldFinishSucceed) {
        t.expectValidationError([&] {
            ctxFinish(t, ctx);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = ctxFinish(t, ctx);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, !shouldSubmitSucceed);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render,setIndexBuffer",
    "Validation tests for setIndexBuffer on render pass and render bundle.");

// g.test('index_buffer_state')
// Tests index buffer must be valid.
// .paramsSubcasesOnly(kRenderEncodeTypeParams.combine('state', kResourceStates))
CTS_TEST(g, "index_buffer_state")
    .desc("Tests index buffer must be valid.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("state", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const ResourceState state     = parseResourceState(t.param<std::string>("state"));

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 16;
        desc.usage = WGPUBufferUsage_Index;
        WGPUBuffer indexBuffer = t.createBufferWithState(state, desc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        validateFinishAndSubmitGivenState(t, ctx, state);
    });

// g.test('index_buffer,device_mismatch')
// Tests setIndexBuffer cannot be called with an index buffer created from another device.
// .paramsSubcasesOnly(kRenderEncodeTypeParams.combine('mismatched', [true, false]))
// .beforeAllSubcases(t => t.usesMismatchedDevice())
CTS_TEST(g, "index_buffer,device_mismatch")
    .desc("Tests setIndexBuffer cannot be called with an index buffer created from another device.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool mismatched         = t.param<bool>("mismatched");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 16;
        desc.usage = WGPUBufferUsage_Index;
        WGPUBuffer indexBuffer = mismatched
            ? t.createBufferOnMismatchedDevice(desc)
            : t.createBufferTracked(desc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        validateFinish(t, ctx, !mismatched);
    });

// g.test('index_buffer_usage')
// Tests index buffer must have 'Index' usage.
// .paramsSubcasesOnly(kRenderEncodeTypeParams.combine('usage', [INDEX, COPY_DST, COPY_DST|INDEX]))
CTS_TEST(g, "index_buffer_usage")
    .desc("Tests index buffer must have 'Index' usage.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("usage", {
                // INDEX                       — control case
                static_cast<int64_t>(WGPUBufferUsage_Index),
                // COPY_DST                    — missing Index usage
                static_cast<int64_t>(WGPUBufferUsage_CopyDst),
                // COPY_DST | INDEX
                static_cast<int64_t>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const WGPUBufferUsage usage   = t.param<WGPUBufferUsage>("usage");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 16;
        desc.usage = usage;
        WGPUBuffer indexBuffer = t.createBufferTracked(desc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        // Valid iff the INDEX usage bit is present.
        validateFinish(t, ctx, (usage & WGPUBufferUsage_Index) != 0);
    });

// g.test('offset_alignment')
// Tests offset must be a multiple of index format's byte size.
// uint16: offsets [0, 1, 2]; uint32: offsets [0, 2, 4]
// alignment = uint16 → 2, uint32 → 4
CTS_TEST(g, "offset_alignment")
    .desc("Tests offset must be a multiple of index format's byte size.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .combine("indexFormat", {
                std::string("uint16"),
                std::string("uint32"),
            })
            .beginSubcases()
            .expand("offset", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* fmtVal = findParam(p, "indexFormat");
                const std::string fmt = (fmtVal != nullptr)
                    ? valueAs<std::string>(*fmtVal)
                    : "uint32";
                if (fmt == "uint16") {
                    return {int64_t(0), int64_t(1), int64_t(2)};
                } else {
                    return {int64_t(0), int64_t(2), int64_t(4)};
                }
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string indexFormat = t.param<std::string>("indexFormat");
        const uint64_t    offset      = t.param<uint64_t>("offset");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 16;
        desc.usage = WGPUBufferUsage_Index;
        WGPUBuffer indexBuffer = t.createBufferTracked(desc);

        WGPUIndexFormat fmt;
        uint64_t alignment;
        if (indexFormat == "uint16") {
            fmt       = WGPUIndexFormat_Uint16;
            alignment = 2; // sizeof(uint16)
        } else {
            fmt       = WGPUIndexFormat_Uint32;
            alignment = 4; // sizeof(uint32)
        }

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetIndexBuffer(ctx, indexBuffer, fmt, offset, WGPU_WHOLE_SIZE);
        validateFinish(t, ctx, (offset % alignment) == 0);
    });

// g.test('offset_and_size_oob')
// Tests offset and size cannot be larger than index buffer size.
// buildBufferOffsetAndSizeOOBTestParams(minAlignment=4, bufferSize=256):
//   buffer size is 256; index format is uint32 (alignment 4).
CTS_TEST(g, "offset_and_size_oob")
    .desc("Tests offset and size cannot be larger than index buffer size.")
    .params([](ParamsBuilder u) {
        // Mirrors buildBufferOffsetAndSizeOOBTestParams(4, 256):
        //   Explicit size cases, then implicit-size (size=undefined → WGPU_WHOLE_SIZE) cases.
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combineWithParams({
                // Explicit size
                ParamRecord{{"offset",   int64_t(0)},   {"size",   int64_t(0)},   {"_valid", true}},
                ParamRecord{{"offset",   int64_t(0)},   {"size",   int64_t(1)},   {"_valid", true}},
                ParamRecord{{"offset",   int64_t(0)},   {"size",   int64_t(4)},   {"_valid", true}},
                ParamRecord{{"offset",   int64_t(0)},   {"size",   int64_t(5)},   {"_valid", true}},
                ParamRecord{{"offset",   int64_t(0)},   {"size",   int64_t(256)}, {"_valid", true}},
                ParamRecord{{"offset",   int64_t(0)},   {"size",   int64_t(260)}, {"_valid", false}},
                ParamRecord{{"offset",   int64_t(4)},   {"size",   int64_t(256)}, {"_valid", false}},
                ParamRecord{{"offset",   int64_t(4)},   {"size",   int64_t(252)}, {"_valid", true}},
                ParamRecord{{"offset",   int64_t(252)}, {"size",   int64_t(4)},   {"_valid", true}},
                ParamRecord{{"offset",   int64_t(256)}, {"size",   int64_t(1)},   {"_valid", false}},
                // Implicit size: buffer.size - offset (size=undefined → WGPU_WHOLE_SIZE)
                ParamRecord{{"offset",   int64_t(0)},   {"size",   Value::undef()}, {"_valid", true}},
                ParamRecord{{"offset",   int64_t(4)},   {"size",   Value::undef()}, {"_valid", true}},
                ParamRecord{{"offset",   int64_t(252)}, {"size",   Value::undef()}, {"_valid", true}},
                ParamRecord{{"offset",   int64_t(256)}, {"size",   Value::undef()}, {"_valid", true}},
                ParamRecord{{"offset",   int64_t(260)}, {"size",   Value::undef()}, {"_valid", false}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const uint64_t    offset      = t.param<uint64_t>("offset");
        const bool        valid       = t.param<bool>("_valid");

        // Resolve implicit size: undefined → WGPU_WHOLE_SIZE
        uint64_t size = WGPU_WHOLE_SIZE;
        if (!t.paramIsUndefined("size")) {
            size = t.param<uint64_t>("size");
        }

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 256;
        desc.usage = WGPUBufferUsage_Index;
        WGPUBuffer indexBuffer = t.createBufferTracked(desc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, offset, size);
        validateFinish(t, ctx, valid);
    });

} // namespace
