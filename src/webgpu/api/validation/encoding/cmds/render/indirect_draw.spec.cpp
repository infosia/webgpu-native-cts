// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render/indirect_draw.spec.ts

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// kRenderEncodeTypes — mirrors upstream ['render pass', 'render bundle']
// ---------------------------------------------------------------------------
static std::vector<Value> kRenderEncodeTypeValues() {
    return {
        std::string("render pass"),
        std::string("render bundle"),
    };
}

// ---------------------------------------------------------------------------
// WGPUStringView helper
// ---------------------------------------------------------------------------
static WGPUStringView sv(const char* s) {
    WGPUStringView view = WGPU_STRING_VIEW_INIT;
    view.data   = s;
    view.length = WGPU_STRLEN;
    return view;
}

// ---------------------------------------------------------------------------
// createNoOpRenderPipeline
// Mirrors vtu.createNoOpRenderPipeline: auto layout, no-op vertex/fragment,
// rgba8unorm color target with writeMask=0, triangle-list topology.
// ---------------------------------------------------------------------------
static WGPURenderPipeline createNoOpRenderPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    constexpr std::string_view kVertWGSL =
        "@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }";
    constexpr std::string_view kFragWGSL =
        "@fragment fn main() {}";

    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertWGSL);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragWGSL);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_None;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout             = nullptr; // auto
    desc.vertex.module      = vertModule;
    desc.vertex.entryPoint  = sv("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.fragment           = &fragment;

    return t.createRenderPipelineTracked(desc);
}

// ---------------------------------------------------------------------------
// RenderEncoderContext — wraps a render pass or render bundle encoder.
// Mirrors the CommandBufferMaker pattern used by other render encoder tests.
// ---------------------------------------------------------------------------
struct RenderEncoderContext {
    std::string encoderType;

    WGPUCommandEncoder      cmdEnc     = nullptr;

    // Shared render target
    WGPUTexture             colorTex   = nullptr;
    WGPUTextureView         colorView  = nullptr;

    // For "render pass"
    WGPURenderPassEncoder   renderPass = nullptr;

    // For "render bundle"
    WGPURenderBundleEncoder bundleEnc  = nullptr;
};

static RenderEncoderContext makeRenderEncoderContext(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType)
{
    RenderEncoderContext ctx;
    ctx.encoderType = encoderType;
    ctx.cmdEnc      = t.createCommandEncoderTracked();

    // Create a small 16x16 rgba8unorm render target.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size           = WGPUExtent3D{16, 16, 1};
    texDesc.mipLevelCount  = 1;
    texDesc.sampleCount    = 1;
    texDesc.dimension      = WGPUTextureDimension_2D;
    texDesc.format         = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage          = WGPUTextureUsage_RenderAttachment;
    ctx.colorTex  = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    ctx.colorView = t.createViewTracked(ctx.colorTex, viewDesc);

    if (encoderType == "render pass") {
        WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttach.view       = ctx.colorView;
        colorAttach.loadOp     = WGPULoadOp_Clear;
        colorAttach.storeOp    = WGPUStoreOp_Store;
        colorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttach;
        ctx.renderPass = wgpuCommandEncoderBeginRenderPass(ctx.cmdEnc, &passDesc);
    } else {
        // render bundle
        WGPUTextureFormat colorFmt = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDesc.colorFormatCount = 1;
        bDesc.colorFormats     = &colorFmt;
        bDesc.sampleCount      = 1;
        ctx.bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);
    }

    return ctx;
}

// Set the pipeline on whichever encoder is active.
static void ctxSetPipeline(RenderEncoderContext& ctx, WGPURenderPipeline pipeline) {
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetPipeline(ctx.renderPass, pipeline);
    } else {
        wgpuRenderBundleEncoderSetPipeline(ctx.bundleEnc, pipeline);
    }
}

// Set an index buffer on whichever encoder is active.
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

// Issue drawIndirect or drawIndexedIndirect on whichever encoder is active.
static void ctxDrawIndirect(
    RenderEncoderContext& ctx,
    bool                  indexed,
    WGPUBuffer            indirectBuffer,
    uint64_t              indirectOffset)
{
    if (indexed) {
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDrawIndexedIndirect(ctx.renderPass, indirectBuffer, indirectOffset);
        } else {
            wgpuRenderBundleEncoderDrawIndexedIndirect(ctx.bundleEnc, indirectBuffer, indirectOffset);
        }
    } else {
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDrawIndirect(ctx.renderPass, indirectBuffer, indirectOffset);
        } else {
            wgpuRenderBundleEncoderDrawIndirect(ctx.bundleEnc, indirectBuffer, indirectOffset);
        }
    }
}

// Finish the context: end pass/bundle, return WGPUCommandBuffer.
// draw/drawIndirect commands are deferred — errors surface on finish().
static WGPUCommandBuffer ctxFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    RenderEncoderContext& ctx)
{
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
        // Finish the bundle encoder to get a bundle.
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(ctx.bundleEnc, nullptr);
        ctx.bundleEnc = nullptr;

        // Execute the bundle in a render pass on the same command encoder.
        WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttach.view       = ctx.colorView;
        colorAttach.loadOp     = WGPULoadOp_Clear;
        colorAttach.storeOp    = WGPUStoreOp_Store;
        colorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttach;

        WGPURenderPassEncoder execPass = wgpuCommandEncoderBeginRenderPass(ctx.cmdEnc, &passDesc);
        if (bundle != nullptr) {
            wgpuRenderPassEncoderExecuteBundles(execPass, 1, &bundle);
            wgpuRenderBundleRelease(bundle);
        }
        wgpuRenderPassEncoderEnd(execPass);
        wgpuRenderPassEncoderRelease(execPass);
    }
    return t.finishTracked(ctx.cmdEnc);
}

// validateFinish: mirrors CommandBufferMaker.validateFinish(shouldSucceed).
// Errors are deferred to finish() for render encoder commands.
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

// makeIndexBuffer: mirrors F.makeIndexBuffer() in the upstream spec.
// Creates a 16-byte INDEX buffer on the test device.
static WGPUBuffer makeIndexBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size  = 16;
    desc.usage = WGPUBufferUsage_Index;
    return t.createBufferTracked(desc);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render,indirect_draw",
    "Validation tests for drawIndirect/drawIndexedIndirect on render pass and render bundle.");

// g.test('indirect_buffer_state')
// Tests indirect buffer must be valid.
// .paramsSubcasesOnly(kIndirectDrawTestParams.combine('state', kResourceStates))
// kIndirectDrawTestParams = kRenderEncodeTypeParams.combine('indexed', [true, false])
CTS_TEST(g, "indirect_buffer_state")
    .desc("Tests indirect buffer must be valid.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("indexed", {true, false})
            .combine("state", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool        indexed     = t.param<bool>("indexed");
        const ResourceState state     = parseResourceState(t.param<std::string>("state"));

        WGPURenderPipeline pipeline = createNoOpRenderPipeline(t);

        WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        indirectDesc.size  = 256;
        indirectDesc.usage = WGPUBufferUsage_Indirect;
        WGPUBuffer indirectBuffer = t.createBufferWithState(state, indirectDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetPipeline(ctx, pipeline);
        if (indexed) {
            WGPUBuffer indexBuffer = makeIndexBuffer(t);
            ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            ctxDrawIndirect(ctx, true, indirectBuffer, 0);
        } else {
            ctxDrawIndirect(ctx, false, indirectBuffer, 0);
        }

        validateFinishAndSubmitGivenState(t, ctx, state);
    });

// g.test('indirect_buffer,device_mismatch')
// Tests draw(Indexed)Indirect cannot be called with an indirect buffer created from another device.
// .paramsSubcasesOnly(kIndirectDrawTestParams.combine('mismatched', [true, false]))
// .beforeAllSubcases(t => t.usesMismatchedDevice())
CTS_TEST(g, "indirect_buffer,device_mismatch")
    .desc("Tests draw(Indexed)Indirect cannot be called with an indirect buffer created from another device.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("indexed", {true, false})
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool        indexed     = t.param<bool>("indexed");
        const bool        mismatched  = t.param<bool>("mismatched");

        WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        indirectDesc.size  = 256;
        indirectDesc.usage = WGPUBufferUsage_Indirect;
        WGPUBuffer indirectBuffer = mismatched
            ? t.createBufferOnMismatchedDevice(indirectDesc)
            : t.createBufferTracked(indirectDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetPipeline(ctx, createNoOpRenderPipeline(t));
        if (indexed) {
            WGPUBuffer indexBuffer = makeIndexBuffer(t);
            ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            ctxDrawIndirect(ctx, true, indirectBuffer, 0);
        } else {
            ctxDrawIndirect(ctx, false, indirectBuffer, 0);
        }

        validateFinish(t, ctx, !mismatched);
    });

// g.test('indirect_buffer_usage')
// Tests indirect buffer must have 'Indirect' usage.
// .paramsSubcasesOnly(kIndirectDrawTestParams.combine('usage', [INDIRECT, COPY_DST, COPY_DST|INDIRECT]))
CTS_TEST(g, "indirect_buffer_usage")
    .desc("Tests indirect buffer must have 'Indirect' usage.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("indexed", {true, false})
            .combine("usage", {
                // INDIRECT                          — control case
                static_cast<int64_t>(WGPUBufferUsage_Indirect),
                // COPY_DST                          — missing Indirect usage
                static_cast<int64_t>(WGPUBufferUsage_CopyDst),
                // COPY_DST | INDIRECT
                static_cast<int64_t>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_Indirect),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool        indexed     = t.param<bool>("indexed");
        const WGPUBufferUsage usage   = t.param<WGPUBufferUsage>("usage");

        WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        indirectDesc.size  = 256;
        indirectDesc.usage = usage;
        WGPUBuffer indirectBuffer = t.createBufferTracked(indirectDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetPipeline(ctx, createNoOpRenderPipeline(t));
        if (indexed) {
            WGPUBuffer indexBuffer = makeIndexBuffer(t);
            ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            ctxDrawIndirect(ctx, true, indirectBuffer, 0);
        } else {
            ctxDrawIndirect(ctx, false, indirectBuffer, 0);
        }

        // Valid iff the INDIRECT usage bit is present.
        validateFinish(t, ctx, (usage & WGPUBufferUsage_Indirect) != 0);
    });

// g.test('indirect_offset_alignment')
// Tests indirect offset must be a multiple of 4.
// .paramsSubcasesOnly(kIndirectDrawTestParams.combine('indirectOffset', [0, 2, 4]))
CTS_TEST(g, "indirect_offset_alignment")
    .desc("Tests indirect offset must be a multiple of 4.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("indexed", {true, false})
            .combine("indirectOffset", {
                int64_t(0),
                int64_t(2),
                int64_t(4),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType   = t.param<std::string>("encoderType");
        const bool        indexed       = t.param<bool>("indexed");
        const uint64_t    indirectOffset = t.param<uint64_t>("indirectOffset");

        WGPURenderPipeline pipeline = createNoOpRenderPipeline(t);

        WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        indirectDesc.size  = 256;
        indirectDesc.usage = WGPUBufferUsage_Indirect;
        WGPUBuffer indirectBuffer = t.createBufferTracked(indirectDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetPipeline(ctx, pipeline);
        if (indexed) {
            WGPUBuffer indexBuffer = makeIndexBuffer(t);
            ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            ctxDrawIndirect(ctx, true, indirectBuffer, indirectOffset);
        } else {
            ctxDrawIndirect(ctx, false, indirectBuffer, indirectOffset);
        }

        validateFinish(t, ctx, (indirectOffset % 4) == 0);
    });

// g.test('indirect_offset_oob')
// Tests indirect draw calls with various indirect offsets and buffer sizes.
// indirectParamsSize: drawIndirect = 16 bytes, drawIndexedIndirect = 20 bytes.
// Cases vary (indirectOffset, bufferSize, _valid) — see upstream expandWithParams.
CTS_TEST(g, "indirect_offset_oob")
    .desc(
        "Tests indirect draw calls with various indirect offsets and buffer sizes.\n"
        "- (offset, b.size) is tested for both drawIndirect (min size 16) and\n"
        "  drawIndexedIndirect (min size 20).")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            // indexed=false: indirectParamsSize=16
            .combineWithParams({
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(0)},  {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(16)}, {"_valid", true}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(17)}, {"_valid", true}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(15)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(12)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(4)},  {"bufferSize", int64_t(20)}, {"_valid", true}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(4)},  {"bufferSize", int64_t(19)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(2)},  {"bufferSize", int64_t(20)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(3)},  {"bufferSize", int64_t(20)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(5)},  {"bufferSize", int64_t(20)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(16)}, {"bufferSize", int64_t(16)}, {"_valid", false}},
                ParamRecord{{"indexed", false}, {"indirectOffset", int64_t(20)}, {"bufferSize", int64_t(16)}, {"_valid", false}},
                // indexed=true: indirectParamsSize=20
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(0)},  {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(20)}, {"_valid", true}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(21)}, {"_valid", true}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(19)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(0)},  {"bufferSize", int64_t(16)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(4)},  {"bufferSize", int64_t(24)}, {"_valid", true}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(4)},  {"bufferSize", int64_t(23)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(2)},  {"bufferSize", int64_t(24)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(3)},  {"bufferSize", int64_t(24)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(5)},  {"bufferSize", int64_t(24)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(20)}, {"bufferSize", int64_t(20)}, {"_valid", false}},
                ParamRecord{{"indexed", true},  {"indirectOffset", int64_t(24)}, {"bufferSize", int64_t(20)}, {"_valid", false}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType   = t.param<std::string>("encoderType");
        const bool        indexed       = t.param<bool>("indexed");
        const uint64_t    indirectOffset = t.param<uint64_t>("indirectOffset");
        const uint64_t    bufferSize    = t.param<uint64_t>("bufferSize");
        const bool        valid         = t.param<bool>("_valid");

        WGPURenderPipeline pipeline = createNoOpRenderPipeline(t);

        WGPUBufferDescriptor indirectDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        indirectDesc.size  = bufferSize;
        indirectDesc.usage = WGPUBufferUsage_Indirect;
        WGPUBuffer indirectBuffer = t.createBufferTracked(indirectDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        ctxSetPipeline(ctx, pipeline);
        if (indexed) {
            WGPUBuffer indexBuffer = makeIndexBuffer(t);
            ctxSetIndexBuffer(ctx, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            ctxDrawIndirect(ctx, true, indirectBuffer, indirectOffset);
        } else {
            ctxDrawIndirect(ctx, false, indirectBuffer, indirectOffset);
        }

        validateFinish(t, ctx, valid);
    });

} // namespace
