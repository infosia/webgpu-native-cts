// Ported from gpuweb/cts src/webgpu/api/validation/queue/destroyed/texture.spec.ts @ 492fefb37fe3ba27c7bfb291149d31f107d40fd5

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,destroyed,texture",
    "Tests using a destroyed texture on a queue.");

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Open a single-color-attachment render pass with no depth/stencil.
static WGPURenderPassEncoder beginSimpleRenderPass(
    WGPUCommandEncoder cmdEnc,
    WGPUTextureView    view)
{
    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view      = view;
    colorAttach.loadOp    = WGPULoadOp_Clear;
    colorAttach.storeOp   = WGPUStoreOp_Store;
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
    } else {
        // render pass or render bundle: need a small render target
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{16, 16, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_RenderAttachment;
        ctx.renderTex  = t.createTextureTracked(texDesc);
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
// Tests
// ---------------------------------------------------------------------------

// g.test('writeTexture')
// Tests that using a destroyed texture in writeTexture fails.
// - x= {destroyed, not destroyed (control case)}
//
// NOTE: wgpuQueueWriteTexture is a direct queue operation that validates
// eagerly; the error fires at the writeTexture call itself when the texture
// is destroyed, not at a later submit.
CTS_TEST(g, "writeTexture")
    .desc("Tests that using a destroyed texture in writeTexture fails.")
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
        texDesc.usage         = WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        if (destroyed) {
            wgpuTextureDestroy(texture);
        }

        const uint8_t data[4] = {0, 0, 0, 0};

        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = texture;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow  = 256; // minimum bytesPerRow for a 1x1 rgba8unorm copy
        layout.rowsPerImage = 1;

        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};

        t.expectValidationError([&] {
            wgpuQueueWriteTexture(t.queue(), &dst, data, 4, &layout, &copySize);
        }, destroyed);
    });

// g.test('copyTextureToTexture')
// Tests that using a destroyed texture in copyTextureToTexture fails.
// - x= {not destroyed (control case), src destroyed, dst destroyed, both destroyed}
//
// NOTE: Encoder records the command; destroy happens AFTER finish().
// The validation error surfaces at submit().
CTS_TEST(g, "copyTextureToTexture")
    .desc("Tests that using a destroyed texture in copyTextureToTexture fails.")
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

        WGPUTextureDescriptor srcDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        srcDesc.size          = WGPUExtent3D{1, 1, 1};
        srcDesc.mipLevelCount = 1;
        srcDesc.sampleCount   = 1;
        srcDesc.dimension     = WGPUTextureDimension_2D;
        srcDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        srcDesc.usage         = WGPUTextureUsage_CopySrc;
        WGPUTexture src = t.createTextureTracked(srcDesc);

        WGPUTextureDescriptor dstDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dstDesc.size          = WGPUExtent3D{1, 1, 1};
        dstDesc.mipLevelCount = 1;
        dstDesc.sampleCount   = 1;
        dstDesc.dimension     = WGPUTextureDimension_2D;
        dstDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        dstDesc.usage         = WGPUTextureUsage_CopyDst;
        WGPUTexture dst = t.createTextureTracked(dstDesc);

        WGPUTexelCopyTextureInfo srcInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        srcInfo.texture = src;

        WGPUTexelCopyTextureInfo dstInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dstInfo.texture = dst;

        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToTexture(encoder, &srcInfo, &dstInfo, &copySize);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);

        bool shouldError = true;
        if (destroyed == "none") {
            shouldError = false;
        } else if (destroyed == "src") {
            wgpuTextureDestroy(src);
        } else if (destroyed == "dst") {
            wgpuTextureDestroy(dst);
        } else {
            // "both"
            wgpuTextureDestroy(src);
            wgpuTextureDestroy(dst);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, shouldError);
    });

// g.test('copyBufferToTexture')
// Tests that using a destroyed texture in copyBufferToTexture fails.
// - x= {not destroyed (control case), dst destroyed}
CTS_TEST(g, "copyBufferToTexture")
    .desc("Tests that using a destroyed texture in copyBufferToTexture fails.")
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
            wgpuTextureDestroy(texture);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('copyTextureToBuffer')
// Tests that using a destroyed texture in copyTextureToBuffer fails.
// - x= {not destroyed (control case), src destroyed}
CTS_TEST(g, "copyTextureToBuffer")
    .desc("Tests that using a destroyed texture in copyTextureToBuffer fails.")
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
            wgpuTextureDestroy(texture);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('setBindGroup')
// Tests that using a destroyed texture referenced by a bindGroup set with
// setBindGroup fails.
// - x= {not destroyed (control case), destroyed}
//   x encoderType= {compute pass, render pass, render bundle}
//   x bindingType= {texture, storageTexture}
CTS_TEST(g, "setBindGroup")
    .desc("Tests that using a destroyed texture referenced by a bindGroup "
          "set with setBindGroup fails.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", {
                std::string("compute pass"),
                std::string("render pass"),
                std::string("render bundle"),
            })
            .combine("bindingType", {
                std::string("texture"),
                std::string("storageTexture"),
            })
            .beginSubcases()
            .combine("destroyed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool        destroyed   = t.param<bool>("destroyed");
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string bindingType = t.param<std::string>("bindingType");

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView texView = t.createViewTracked(texture, viewDesc);

        // Build bind group layout entry for the appropriate binding type.
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding    = 0;
        entry.visibility = WGPUShaderStage_Compute;

        if (bindingType == "texture") {
            entry.texture.sampleType    = WGPUTextureSampleType_Float;
            entry.texture.viewDimension = WGPUTextureViewDimension_2D;
        } else {
            // storageTexture
            entry.storageTexture.access        = WGPUStorageTextureAccess_WriteOnly;
            entry.storageTexture.format        = WGPUTextureFormat_RGBA8Unorm;
            entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        }

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries    = &entry;
        WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(bglDesc);

        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding     = 0;
        bgEntry.textureView = texView;

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
            wgpuTextureDestroy(texture);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, destroyed);
    });

// g.test('beginRenderPass')
// Tests that using a destroyed texture referenced by a render pass fails.
// - x= {not destroyed (control case), colorAttachment destroyed,
//        resolveAttachment destroyed, depthStencilAttachment destroyed}
//
// NOTE: beginRenderPass records into the command encoder; the validation
// error for a destroyed attachment surfaces at submit().
CTS_TEST(g, "beginRenderPass")
    .desc("Tests that using a destroyed texture referenced by a render pass fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("textureToDestroy", {
            std::string("none"),
            std::string("colorAttachment"),
            std::string("resolveAttachment"),
            std::string("depthStencilAttachment"),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string textureToDestroy = t.param<std::string>("textureToDestroy");

        // 4x MSAA color attachment (sampleCount=4, rgba8unorm, RENDER_ATTACHMENT).
        WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        colorDesc.size          = WGPUExtent3D{1, 1, 1};
        colorDesc.mipLevelCount = 1;
        colorDesc.sampleCount   = 4;
        colorDesc.dimension     = WGPUTextureDimension_2D;
        colorDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        colorDesc.usage         = WGPUTextureUsage_RenderAttachment;
        WGPUTexture colorAttachment = t.createTextureTracked(colorDesc);

        // Resolve target (sampleCount=1, rgba8unorm, RENDER_ATTACHMENT).
        WGPUTextureDescriptor resolveDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        resolveDesc.size          = WGPUExtent3D{1, 1, 1};
        resolveDesc.mipLevelCount = 1;
        resolveDesc.sampleCount   = 1;
        resolveDesc.dimension     = WGPUTextureDimension_2D;
        resolveDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        resolveDesc.usage         = WGPUTextureUsage_RenderAttachment;
        WGPUTexture resolveAttachment = t.createTextureTracked(resolveDesc);

        // Depth/stencil attachment (sampleCount=4, depth32float, RENDER_ATTACHMENT).
        WGPUTextureDescriptor depthDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        depthDesc.size          = WGPUExtent3D{1, 1, 1};
        depthDesc.mipLevelCount = 1;
        depthDesc.sampleCount   = 4;
        depthDesc.dimension     = WGPUTextureDimension_2D;
        depthDesc.format        = WGPUTextureFormat_Depth32Float;
        depthDesc.usage         = WGPUTextureUsage_RenderAttachment;
        WGPUTexture depthStencilAttachment = t.createTextureTracked(depthDesc);

        // Create views.
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView colorView   = t.createViewTracked(colorAttachment, viewDesc);
        WGPUTextureView resolveView = t.createViewTracked(resolveAttachment, viewDesc);
        WGPUTextureView depthView   = t.createViewTracked(depthStencilAttachment, viewDesc);

        // Build the render pass descriptor with color (MSAA) + resolve + depth/stencil.
        WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttach.view          = colorView;
        colorAttach.resolveTarget = resolveView;
        colorAttach.loadOp        = WGPULoadOp_Clear;
        colorAttach.storeOp       = WGPUStoreOp_Store;
        colorAttach.clearValue    = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDepthStencilAttachment depthAttach = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthAttach.view           = depthView;
        depthAttach.depthClearValue = 0.0f;
        depthAttach.depthLoadOp    = WGPULoadOp_Clear;
        depthAttach.depthStoreOp   = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount    = 1;
        passDesc.colorAttachments        = &colorAttach;
        passDesc.depthStencilAttachment  = &depthAttach;

        // Record and finish the render pass before destroying anything.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);

        // Destroy the selected texture after finish, before submit.
        if (textureToDestroy == "colorAttachment") {
            wgpuTextureDestroy(colorAttachment);
        } else if (textureToDestroy == "resolveAttachment") {
            wgpuTextureDestroy(resolveAttachment);
        } else if (textureToDestroy == "depthStencilAttachment") {
            wgpuTextureDestroy(depthStencilAttachment);
        }

        const bool shouldError = (textureToDestroy != "none");

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }, shouldError);
    });

} // namespace
