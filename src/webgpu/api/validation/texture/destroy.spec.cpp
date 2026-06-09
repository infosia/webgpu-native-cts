// Ported from gpuweb/cts src/webgpu/api/validation/texture/destroy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,texture,destroy",
    "Destroying a texture more than once is allowed.");

// Mirrors vtu.getSampledTexture(t) with default sampleCount=1:
// 16x16 rgba8unorm TEXTURE_BINDING texture.
static WGPUTexture getSampledTexture(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size.width = 16;
    desc.size.height = 16;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_TextureBinding;
    return t.createTextureTracked(desc);
}

// g.test('base')
CTS_TEST(g, "base")
    .desc("Test that it is valid to destroy a texture.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture texture = getSampledTexture(t);
        wgpuTextureDestroy(texture);
    });

// g.test('twice')
CTS_TEST(g, "twice")
    .desc("Test that it is valid to destroy a destroyed texture.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture texture = getSampledTexture(t);
        wgpuTextureDestroy(texture);
        wgpuTextureDestroy(texture);
    });

// g.test('invalid_texture')
CTS_TEST(g, "invalid_texture")
    .desc("Test that invalid textures may be destroyed without generating validation errors.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();

        // Create an invalid texture (size exceeds maxTextureDimension2D).
        // The harness wraps uncaptured error callbacks, so we push/pop an error scope
        // to absorb the expected validation error from creation.
        WGPUTexture invalidTexture = nullptr;
        t.expectValidationError([&] {
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.size.width = limits.maxTextureDimension2D + 1;
            desc.size.height = 1;
            desc.size.depthOrArrayLayers = 1;
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.usage = WGPUTextureUsage_TextureBinding;
            invalidTexture = t.createTextureTracked(desc);
        }, true);

        // Destroying an invalid texture must not generate a validation error.
        if (invalidTexture != nullptr) {
            wgpuTextureDestroy(invalidTexture);
        }
    });

// g.test('submit_a_destroyed_texture_as_attachment')
CTS_TEST(g, "submit_a_destroyed_texture_as_attachment")
    .desc(R"(
Test that it is invalid to submit with a texture as {color, depth, stencil, depth-stencil} attachment
that was destroyed {before, after} encoding finishes.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("depthStencilTextureAspect", {
                Value(std::string(textureAspectIdentifier(WGPUTextureAspect_All))),
                Value(std::string(textureAspectIdentifier(WGPUTextureAspect_DepthOnly))),
                Value(std::string(textureAspectIdentifier(WGPUTextureAspect_StencilOnly))),
            })
            .combine("colorTextureState", {
                Value(std::string("valid")),
                Value(std::string("destroyedBeforeEncode")),
                Value(std::string("destroyedAfterEncode")),
            })
            .combine("depthStencilTextureState", {
                Value(std::string("valid")),
                Value(std::string("destroyedBeforeEncode")),
                Value(std::string("destroyedAfterEncode")),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string colorTextureState      = t.param<std::string>("colorTextureState");
        const std::string depthStencilTextureState = t.param<std::string>("depthStencilTextureState");
        const WGPUTextureAspect depthStencilTextureAspect =
            parseTextureAspect(t.param<std::string>("depthStencilTextureAspect"));

        const bool isSubmitSuccess =
            colorTextureState == "valid" && depthStencilTextureState == "valid";

        // Pick depth-stencil format from the aspect parameter, mirroring the TS logic.
        WGPUTextureFormat depthStencilTextureFormat;
        if (depthStencilTextureAspect == WGPUTextureAspect_All) {
            depthStencilTextureFormat = WGPUTextureFormat_Depth24PlusStencil8;
        } else if (depthStencilTextureAspect == WGPUTextureAspect_DepthOnly) {
            depthStencilTextureFormat = WGPUTextureFormat_Depth32Float;
        } else {
            // StencilOnly
            depthStencilTextureFormat = WGPUTextureFormat_Stencil8;
        }

        // Create color texture.
        WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        colorDesc.size.width = 16;
        colorDesc.size.height = 16;
        colorDesc.size.depthOrArrayLayers = 1;
        colorDesc.format = WGPUTextureFormat_RGBA8Unorm;
        colorDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;
        WGPUTexture colorTexture = t.createTextureTracked(colorDesc);

        // Create depth-stencil texture.
        WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dsDesc.size.width = 16;
        dsDesc.size.height = 16;
        dsDesc.size.depthOrArrayLayers = 1;
        dsDesc.format = depthStencilTextureFormat;
        dsDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;
        WGPUTexture depthStencilTexture = t.createTextureTracked(dsDesc);

        // Destroy before encode if requested.
        if (colorTextureState == "destroyedBeforeEncode") {
            wgpuTextureDestroy(colorTexture);
        }
        if (depthStencilTextureState == "destroyedBeforeEncode") {
            wgpuTextureDestroy(depthStencilTexture);
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        // Build depth-stencil view with the requested aspect.
        WGPUTextureViewDescriptor dsViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        dsViewDesc.aspect = depthStencilTextureAspect;
        WGPUTextureView dsView = wgpuTextureCreateView(depthStencilTexture, &dsViewDesc);

        WGPURenderPassDepthStencilAttachment depthStencilAttachment =
            WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthStencilAttachment.view = dsView;
        if (isDepthTextureFormat(depthStencilTextureFormat)) {
            depthStencilAttachment.depthClearValue = 0.0f;
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Discard;
        }
        if (isStencilTextureFormat(depthStencilTextureFormat)) {
            depthStencilAttachment.stencilClearValue = 0;
            depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Discard;
        }

        // Build color view.
        WGPUTextureViewDescriptor colorViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView colorView = wgpuTextureCreateView(colorTexture, &colorViewDesc);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = colorView;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = &depthStencilAttachment;

        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);
        wgpuTextureViewRelease(colorView);
        wgpuTextureViewRelease(dsView);

        // finishTracked tracks the command buffer for cleanup on test finalization.
        WGPUCommandBuffer cmd = t.finishTracked(encoder);

        // Destroy after encode if requested.
        if (colorTextureState == "destroyedAfterEncode") {
            wgpuTextureDestroy(colorTexture);
        }
        if (depthStencilTextureState == "destroyedAfterEncode") {
            wgpuTextureDestroy(depthStencilTexture);
        }

        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 1, &cmd);
        }, !isSubmitSuccess);
    });

} // namespace
