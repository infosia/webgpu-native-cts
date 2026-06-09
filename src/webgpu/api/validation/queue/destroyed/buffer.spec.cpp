// Ported from gpuweb/cts src/webgpu/api/validation/queue/destroyed/buffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,destroyed,buffer",
    "Tests using a destroyed buffer on a queue.");

// ---------------------------------------------------------------------------
// Shared render-encoder helpers (render pass + render bundle)
// ---------------------------------------------------------------------------

// A minimal 16x16 RGBA8Unorm render-attachment texture.
static WGPUTexture createSmallRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size          = WGPUExtent3D{16, 16, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount   = 1;
    desc.dimension     = WGPUTextureDimension_2D;
    desc.format        = WGPUTextureFormat_RGBA8Unorm;
    desc.usage         = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// Open a single-color-attachment render pass (no depth/stencil).
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

// ---------------------------------------------------------------------------
// EncoderContext for setBindGroup (compute pass + render pass + render bundle)
// ---------------------------------------------------------------------------

struct EncoderContext {
    std::string             encoderType;

    WGPUCommandEncoder      cmdEnc      = nullptr;

    // compute pass
    WGPUComputePassEncoder  computePass = nullptr;

    // render pass / render bundle: need a texture+view
    WGPUTexture             renderTex   = nullptr;
    WGPUTextureView         renderView  = nullptr;
    WGPURenderPassEncoder   renderPass  = nullptr;

    // render bundle: the bundle encoder + the surrounding render pass
    WGPURenderBundleEncoder bundleEnc   = nullptr;
    WGPURenderPassEncoder   bundlePass  = nullptr;
};

static EncoderContext makeEncoderContext(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType)
{
    EncoderContext ctx;
    ctx.encoderType = encoderType;
    ctx.cmdEnc      = t.createCommandEncoderTracked();

    if (encoderType == "compute pass") {
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        ctx.computePass = wgpuCommandEncoderBeginComputePass(ctx.cmdEnc, &cpDesc);
    } else if (encoderType == "render pass") {
        ctx.renderTex  = createSmallRenderTarget(t);
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        ctx.renderView = t.createViewTracked(ctx.renderTex, vDesc);
        ctx.renderPass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
    } else {
        // render bundle
        ctx.renderTex  = createSmallRenderTarget(t);
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        ctx.renderView = t.createViewTracked(ctx.renderTex, vDesc);

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

// Call setBindGroup on the appropriate sub-encoder.
static void ctxSetBindGroup(
    EncoderContext& ctx,
    uint32_t        group,
    WGPUBindGroup   bindGroup)
{
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderSetBindGroup(ctx.computePass, group, bindGroup, 0, nullptr);
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetBindGroup(ctx.renderPass, group, bindGroup, 0, nullptr);
    } else {
        wgpuRenderBundleEncoderSetBindGroup(ctx.bundleEnc, group, bindGroup, 0, nullptr);
    }
}

// Finish the context and return a WGPUCommandBuffer.
static WGPUCommandBuffer ctxFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    EncoderContext& ctx)
{
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderEnd(ctx.computePass);
        ctx.computePass = nullptr;
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
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
// RenderEncoderContext for setVertexBuffer / setIndexBuffer
// (render pass + render bundle only)
// ---------------------------------------------------------------------------

struct RenderEncoderContext {
    std::string             encoderType;

    WGPUCommandEncoder      cmdEnc     = nullptr;

    WGPUTexture             renderTex  = nullptr;
    WGPUTextureView         renderView = nullptr;
    WGPURenderPassEncoder   renderPass = nullptr;

    WGPURenderBundleEncoder bundleEnc  = nullptr;
    WGPURenderPassEncoder   bundlePass = nullptr;
};

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
        // render bundle
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

// Finish the render encoder context and return a WGPUCommandBuffer.
static WGPUCommandBuffer renderCtxFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    RenderEncoderContext& ctx)
{
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
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
// Tests
// ---------------------------------------------------------------------------

// g.test('writeBuffer')
// Tests that using a destroyed buffer in writeBuffer fails.
// - x= {destroyed, not destroyed (control case)}
//
// NOTE: wgpuQueueWriteBuffer is a direct queue operation that validates
// eagerly; the error fires at the writeBuffer call itself when the buffer
// is destroyed, not at a later submit.
CTS_TEST(g, "writeBuffer")
    .desc("Tests that using a destroyed buffer in writeBuffer fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool destroyed = t.param<bool>("destroyed");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 4;
        desc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        if (destroyed) {
            wgpuBufferDestroy(buffer);
        }

        const uint8_t data[4] = {0, 0, 0, 0};
        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 0, data, 4);
        }, destroyed);
    });

// g.test('copyBufferToBuffer')
// Tests that using a destroyed buffer in copyBufferToBuffer fails.
// - x= {not destroyed (control case), src destroyed, dst destroyed, both destroyed}
//
// NOTE: Encoder records commands at finish() time. Buffer destroy happens
// AFTER finish(). The validation error surfaces at submit().
CTS_TEST(g, "copyBufferToBuffer")
    .desc("Tests that using a destroyed buffer in copyBufferToBuffer fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("destroyed", {
            std::string("none"),
            std::string("src"),
            std::string("dst"),
            std::string("both"),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string destroyed = t.param<std::string>("destroyed");

        WGPUBufferDescriptor srcDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        srcDesc.size  = 4;
        srcDesc.usage = WGPUBufferUsage_CopySrc;
        WGPUBuffer src = t.createBufferTracked(srcDesc);

        WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        dstDesc.size  = 4;
        dstDesc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer dst = t.createBufferTracked(dstDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyBufferToBuffer(encoder, src, 0, dst, 0, 4);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);

        bool shouldError = true;
        if (destroyed == "none") {
            shouldError = false;
        } else if (destroyed == "src") {
            wgpuBufferDestroy(src);
        } else if (destroyed == "dst") {
            wgpuBufferDestroy(dst);
        } else {
            // "both"
            wgpuBufferDestroy(src);
            wgpuBufferDestroy(dst);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, shouldError);
    });

// g.test('copyBufferToTexture')
// Tests that using a destroyed buffer in copyBufferToTexture fails.
// - x= {not destroyed (control case), src destroyed}
CTS_TEST(g, "copyBufferToTexture")
    .desc("Tests that using a destroyed buffer in copyBufferToTexture fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool destroyed = t.param<bool>("destroyed");

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4;
        bufDesc.usage = WGPUBufferUsage_CopySrc;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        WGPUTexelCopyBufferInfo src = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        src.buffer             = buffer;
        src.layout.bytesPerRow = 256; // minimum bytesPerRow for a 1x1 rgba8unorm copy

        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = texture;

        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &copySize);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);

        if (destroyed) {
            wgpuBufferDestroy(buffer);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('copyTextureToBuffer')
// Tests that using a destroyed buffer in copyTextureToBuffer fails.
// - x= {not destroyed (control case), dst destroyed}
CTS_TEST(g, "copyTextureToBuffer")
    .desc("Tests that using a destroyed buffer in copyTextureToBuffer fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool destroyed = t.param<bool>("destroyed");

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_CopySrc;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4;
        bufDesc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = texture;

        WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer             = buffer;
        dst.layout.bytesPerRow = 256; // minimum bytesPerRow for a 1x1 rgba8unorm copy

        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);

        if (destroyed) {
            wgpuBufferDestroy(buffer);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('setBindGroup')
// Tests that using a destroyed buffer referenced by a bindGroup set with
// setBindGroup fails.
// - x= {not destroyed (control case), destroyed}
//   x encoderType= {compute pass, render pass, render bundle}
CTS_TEST(g, "setBindGroup")
    .desc("Tests that using a destroyed buffer referenced by a bindGroup "
          "set with setBindGroup fails.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", {
                std::string("compute pass"),
                std::string("render pass"),
                std::string("render bundle"),
            })
            .beginSubcases()
            .combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool        destroyed   = t.param<bool>("destroyed");
        const std::string encoderType = t.param<std::string>("encoderType");

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4;
        bufDesc.usage = WGPUBufferUsage_Uniform;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // Create bind group layout: binding 0, COMPUTE | VERTEX visibility, buffer uniform.
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding               = 0;
        entry.visibility            = WGPUShaderStage_Compute | WGPUShaderStage_Vertex;
        entry.buffer.type           = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries    = &entry;
        WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(bglDesc);

        // Create bind group referencing the buffer.
        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = buffer;
        bgEntry.offset  = 0;
        bgEntry.size    = 4;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = layout;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        // Record setBindGroup into the appropriate encoder type and finish.
        EncoderContext ctx = makeEncoderContext(t, encoderType);
        ctxSetBindGroup(ctx, 0, bindGroup);
        WGPUCommandBuffer commandBuffer = ctxFinish(t, ctx);

        if (destroyed) {
            wgpuBufferDestroy(buffer);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('setVertexBuffer')
// Tests that using a destroyed buffer referenced in a render pass fails.
// - x= {not destroyed (control case), destroyed}
//   x encoderType= {render pass, render bundle}
CTS_TEST(g, "setVertexBuffer")
    .desc("Tests that using a destroyed buffer referenced in a render pass fails.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", {
                std::string("render pass"),
                std::string("render bundle"),
            })
            .beginSubcases()
            .combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool        destroyed   = t.param<bool>("destroyed");
        const std::string encoderType = t.param<std::string>("encoderType");

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4;
        bufDesc.usage = WGPUBufferUsage_Vertex;
        WGPUBuffer vertexBuffer = t.createBufferTracked(bufDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);

        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderSetVertexBuffer(ctx.renderPass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
        } else {
            wgpuRenderBundleEncoderSetVertexBuffer(ctx.bundleEnc, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
        }

        WGPUCommandBuffer commandBuffer = renderCtxFinish(t, ctx);

        if (destroyed) {
            wgpuBufferDestroy(vertexBuffer);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('setIndexBuffer')
// Tests that using a destroyed buffer referenced in a render pass fails.
// - x= {not destroyed (control case), destroyed}
//   x encoderType= {render pass, render bundle}
CTS_TEST(g, "setIndexBuffer")
    .desc("Tests that using a destroyed buffer referenced in a render pass fails.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", {
                std::string("render pass"),
                std::string("render bundle"),
            })
            .beginSubcases()
            .combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool        destroyed   = t.param<bool>("destroyed");
        const std::string encoderType = t.param<std::string>("encoderType");

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4;
        bufDesc.usage = WGPUBufferUsage_Index;
        WGPUBuffer indexBuffer = t.createBufferTracked(bufDesc);

        RenderEncoderContext ctx = makeRenderEncoderContext(t, encoderType);

        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderSetIndexBuffer(ctx.renderPass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        } else {
            wgpuRenderBundleEncoderSetIndexBuffer(ctx.bundleEnc, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        }

        WGPUCommandBuffer commandBuffer = renderCtxFinish(t, ctx);

        if (destroyed) {
            wgpuBufferDestroy(indexBuffer);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('resolveQuerySet')
// Tests that using a destroyed buffer referenced via resolveQuerySet fails.
// - x= {not destroyed (control case), destroyed}
CTS_TEST(g, "resolveQuerySet")
    .desc("Tests that using a destroyed buffer referenced via resolveQuerySet fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool destroyed = t.param<bool>("destroyed");

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = 1;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 8;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer querySetBuffer = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, querySetBuffer, 0);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);

        if (destroyed) {
            wgpuBufferDestroy(querySetBuffer);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);

        wgpuQuerySetRelease(querySet);
    });

} // namespace
