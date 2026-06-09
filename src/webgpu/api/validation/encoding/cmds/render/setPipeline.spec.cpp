// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render/setPipeline.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render,setPipeline",
    "Validation tests for setPipeline on render pass and render bundle.");

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
// kRenderEncodeTypes — mirrors upstream ['render pass', 'render bundle']
// ---------------------------------------------------------------------------
static std::vector<Value> kRenderEncodeTypeValues() {
    return {
        std::string("render pass"),
        std::string("render bundle"),
    };
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
// createErrorRenderPipeline
// Mirrors vtu.createErrorRenderPipeline: creates an invalid pipeline by
// using an empty/invalid shader, wrapped in a single expectValidationError
// scope so the device doesn't surface an uncaptured error.  The pipeline
// handle (which may be null or an error-object depending on the backend) is
// captured via reference and returned to callers.
// ---------------------------------------------------------------------------
static WGPURenderPipeline createErrorRenderPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    // An empty WGSL string is invalid; the shader module and/or pipeline
    // creation will fail with a validation error, which is absorbed here.
    constexpr std::string_view kEmpty = "";

    WGPURenderPipeline pipeline = nullptr;
    t.expectValidationError([&] {
        WGPUShaderModule invalidModule = t.createShaderModuleTracked(kEmpty);

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout            = nullptr; // auto
        desc.vertex.module     = invalidModule;
        desc.vertex.entryPoint = sv("");

        pipeline = t.createRenderPipelineTracked(desc);
    }, true);

    return pipeline;
}

// ---------------------------------------------------------------------------
// createRenderPipelineWithState
// Mirrors vtu.createRenderPipelineWithState(t, state).
// ---------------------------------------------------------------------------
static WGPURenderPipeline createRenderPipelineWithState(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& state)
{
    if (state == "valid") {
        return createNoOpRenderPipeline(t);
    }
    return createErrorRenderPipeline(t);
}

// ---------------------------------------------------------------------------
// createRenderPipelineOnDevice
// Creates a valid no-op render pipeline on the given device (which may be the
// mismatched device for the device_mismatch test).
// ---------------------------------------------------------------------------
static WGPURenderPipeline createRenderPipelineOnDevice(WGPUDevice device) {
    constexpr std::string_view kVertWGSL =
        "@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }";
    constexpr std::string_view kFragWGSL =
        "@fragment fn main() {}";

    WGPUShaderSourceWGSL vertSource = WGPU_SHADER_SOURCE_WGSL_INIT;
    vertSource.code = WGPUStringView{kVertWGSL.data(), kVertWGSL.size()};
    WGPUShaderModuleDescriptor vertSmDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    vertSmDesc.nextInChain = &vertSource.chain;
    WGPUShaderModule vertModule = wgpuDeviceCreateShaderModule(device, &vertSmDesc);

    WGPUShaderSourceWGSL fragSource = WGPU_SHADER_SOURCE_WGSL_INIT;
    fragSource.code = WGPUStringView{kFragWGSL.data(), kFragWGSL.size()};
    WGPUShaderModuleDescriptor fragSmDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    fragSmDesc.nextInChain = &fragSource.chain;
    WGPUShaderModule fragModule = wgpuDeviceCreateShaderModule(device, &fragSmDesc);

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

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);

    // Release modules (pipeline retains them)
    if (vertModule != nullptr) { wgpuShaderModuleRelease(vertModule); }
    if (fragModule != nullptr) { wgpuShaderModuleRelease(fragModule); }

    return pipeline;
}

// ---------------------------------------------------------------------------
// Render encoder context — mirrors CommandBufferMaker for render types.
//
// For "render pass": open a render pass on a 16x16 rgba8unorm texture.
// For "render bundle": open a render bundle encoder (colorFormats=[rgba8unorm]).
//
// setPipelineOnEncoder: calls wgpuRenderPassEncoderSetPipeline or
//   wgpuRenderBundleEncoderSetPipeline.
//
// finishContext: ends the pass/bundle and finishes the command encoder.
//   Returns the WGPUCommandBuffer (may be nullptr if creation fails).
// ---------------------------------------------------------------------------

struct RenderEncoderContext {
    std::string encoderType;

    WGPUCommandEncoder      cmdEnc     = nullptr;
    WGPUTexture             colorTex   = nullptr;
    WGPUTextureView         colorView  = nullptr;
    WGPURenderPassEncoder   renderPass = nullptr;
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

static void setPipelineOnEncoder(RenderEncoderContext& ctx, WGPURenderPipeline pipeline) {
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetPipeline(ctx.renderPass, pipeline);
    } else {
        wgpuRenderBundleEncoderSetPipeline(ctx.bundleEnc, pipeline);
    }
}

// Finishes the render encoder context and returns the command buffer.
// Must be called inside expectValidationError (or unconditionally when valid).
static WGPUCommandBuffer finishRenderEncoderContext(
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
        // (If bundle is null due to a prior validation error, we still open
        // and end the render pass so the command encoder can be finished.)
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// g.test('invalid_pipeline')
// Tests setPipeline should generate an error iff using an 'invalid' pipeline.
CTS_TEST(g, "invalid_pipeline")
    .desc(
        "Tests setPipeline should generate an error iff using an 'invalid' pipeline.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("state", {std::string("valid"), std::string("invalid")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string state       = t.param<std::string>("state");

        WGPURenderPipeline pipeline = createRenderPipelineWithState(t, state);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        setPipelineOnEncoder(ctx, pipeline);

        const bool shouldError = (state == "invalid");
        t.expectValidationError([&] {
            finishRenderEncoderContext(t, ctx);
        }, shouldError);
    });

// g.test('pipeline,device_mismatch')
// Tests setPipeline cannot be called with a render pipeline created from another device.
CTS_TEST(g, "pipeline,device_mismatch")
    .desc("Tests setPipeline cannot be called with a render pipeline created from another device")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kRenderEncodeTypeValues())
            .beginSubcases()
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool mismatched         = t.param<bool>("mismatched");

        // Use the mismatched device or the test device to create the pipeline.
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();
        WGPURenderPipeline pipeline = createRenderPipelineOnDevice(sourceDevice);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);
        setPipelineOnEncoder(ctx, pipeline);

        t.expectValidationError([&] {
            finishRenderEncoderContext(t, ctx);
        }, mismatched);

        if (pipeline != nullptr) {
            wgpuRenderPipelineRelease(pipeline);
        }
    });

} // namespace
