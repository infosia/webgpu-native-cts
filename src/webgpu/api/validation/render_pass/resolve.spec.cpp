// Ported from gpuweb/cts src/webgpu/api/validation/render_pass/resolve.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: bindTextureResource — in JS, a GPUTexture can be passed where a GPUTextureView is expected
//   (creating an implicit default view). In the C API every attachment needs an explicit
//   WGPUTextureView; the default view is created explicitly, matching the valid semantics.

#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

static const int kNumColorAttachments = 4;

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pass,resolve",
    "Validation tests for render pass resolve.");

// ---------------------------------------------------------------------------
// getErrorTextureView — mirrors vtu.getErrorTextureView(t).
// Creates an invalid WGPUTexture (zero-size → validation error) and returns
// a view created from it.  The error scope absorbs the device error so it
// does not surface as an uncaptured error.
// ---------------------------------------------------------------------------
static WGPUTextureView getErrorTextureView(AllFeaturesMaxLimitsGpuTest& t) {
    // Create an invalid texture (usage=None triggers a validation error).
    WGPUTextureDescriptor invalidDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    invalidDesc.size   = WGPUExtent3D{1, 1, 1};
    invalidDesc.format = WGPUTextureFormat_RGBA8Unorm;
    invalidDesc.usage  = WGPUTextureUsage_None;  // invalid: no usages
    WGPUTexture errorTex = t.createTextureWithState(ResourceState::Invalid, invalidDesc);

    // Creating a view from an already-invalid texture is itself a validation
    // error ("texture is invalid due to a previous error"). Upstream wraps both
    // the texture and the view creation in one validation error scope so neither
    // leaks as an uncaptured error; mirror that by swallowing the view error.
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = nullptr;
    t.expectValidationError([&] { view = t.createViewTracked(errorTex, viewDesc); }, true);
    return view;
}

// ---------------------------------------------------------------------------
// test: resolve_attachment
// ---------------------------------------------------------------------------

CTS_TEST(g, "resolve_attachment")
    .desc(
        "Test various validation behaviors when a resolveTarget is provided.\n"
        "\n"
        "- base case (valid).\n"
        "- resolve source is not multisampled.\n"
        "- resolve target is not single sampled.\n"
        "- resolve target missing RENDER_ATTACHMENT usage.\n"
        "- resolve target has TRANSIENT_ATTACHMENT usage.\n"
        "- resolve target must have exactly one subresource:\n"
        "    - base mip level {0, >0}, mip level count {1, >1}.\n"
        "    - base array layer {0, >0}, array layer count {1, >1}.\n"
        "- resolve target GPUTextureView is invalid\n"
        "- resolve source and target have different formats.\n"
        "    - rgba8unorm -> {bgra8unorm, rgba8unorm-srgb}\n"
        "    - {bgra8unorm, rgba8unorm-srgb} -> rgba8unorm\n"
        "    - test with other color attachments having a different format\n"
        "- resolve source and target have different sizes.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            // control cases should be valid
            ParamRecord{{"bindTextureResource", false}, {"_valid", true}},
            ParamRecord{{"bindTextureResource", true},  {"_valid", true}},
            // resolveTargetUsage: RENDER_ATTACHMENT only — valid
            ParamRecord{{"resolveTargetUsage_renderAttachmentOnly", true}, {"_valid", true}},
            // a single sampled resolve source should cause a validation error.
            ParamRecord{{"colorAttachmentSamples", 1}, {"_valid", false}},
            // a multisampled resolve target should cause a validation error.
            ParamRecord{{"resolveTargetSamples", 4}, {"_valid", false}},
            // resolveTargetUsage without RENDER_ATTACHMENT — invalid
            ParamRecord{{"resolveTargetUsage_copySrcOnly", true}, {"_valid", false}},
            // resolveTargetUsage with TRANSIENT_ATTACHMENT | RENDER_ATTACHMENT — invalid
            ParamRecord{{"resolveTargetUsage_transient", true}, {"_valid", false}},
            // non-zero resolve target base mip level should be valid.
            ParamRecord{{"resolveTargetViewBaseMipLevel", 1},
                        {"resolveTargetHeight", 4},
                        {"resolveTargetWidth", 4},
                        {"_valid", true}},
            // a validation error should be created when resolveTarget is invalid.
            ParamRecord{{"resolveTargetInvalid", true}, {"_valid", false}},
            // a validation error should be created when mip count > 1
            ParamRecord{{"resolveTargetViewMipCount", 2}, {"_valid", false}},
            ParamRecord{{"resolveTargetViewBaseMipLevel", 1},
                        {"resolveTargetViewMipCount", 2},
                        {"resolveTargetHeight", 4},
                        {"resolveTargetWidth", 4},
                        {"_valid", false}},
            // non-zero resolve target base array layer should be valid.
            ParamRecord{{"resolveTargetViewBaseArrayLayer", 1}, {"_valid", true}},
            // a validation error should be created when array layer count > 1
            ParamRecord{{"resolveTargetViewArrayLayerCount", 2}, {"_valid", false}},
            ParamRecord{{"resolveTargetViewBaseArrayLayer", 1},
                        {"resolveTargetViewArrayLayerCount", 2},
                        {"_valid", false}},
            // other color attachments resolving with a different format should be valid.
            ParamRecord{{"otherAttachmentFormat", std::string("bgra8unorm")}, {"_valid", true}},
            // mismatched colorAttachment and resolveTarget formats — invalid
            ParamRecord{{"colorAttachmentFormat", std::string("bgra8unorm")}, {"_valid", false}},
            ParamRecord{{"colorAttachmentFormat", std::string("rgba8unorm-srgb")}, {"_valid", false}},
            ParamRecord{{"resolveTargetFormat", std::string("bgra8unorm")}, {"_valid", false}},
            ParamRecord{{"resolveTargetFormat", std::string("rgba8unorm-srgb")}, {"_valid", false}},
            // mismatched colorAttachment and resolveTarget sizes — invalid
            ParamRecord{{"colorAttachmentHeight", 4}, {"_valid", false}},
            ParamRecord{{"colorAttachmentWidth", 4}, {"_valid", false}},
            ParamRecord{{"resolveTargetHeight", 4}, {"_valid", false}},
            ParamRecord{{"resolveTargetWidth", 4}, {"_valid", false}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Read boolean flags with defaults (use hasParam to check presence).
        const bool bindTextureResource =
            t.hasParam("bindTextureResource") ? t.param<bool>("bindTextureResource") : false;
        const bool resolveTargetInvalid =
            t.hasParam("resolveTargetInvalid") ? t.param<bool>("resolveTargetInvalid") : false;
        const bool resolveTargetUsage_renderAttachmentOnly =
            t.hasParam("resolveTargetUsage_renderAttachmentOnly")
            ? t.param<bool>("resolveTargetUsage_renderAttachmentOnly") : false;
        const bool resolveTargetUsage_copySrcOnly =
            t.hasParam("resolveTargetUsage_copySrcOnly")
            ? t.param<bool>("resolveTargetUsage_copySrcOnly") : false;
        const bool resolveTargetUsage_transient =
            t.hasParam("resolveTargetUsage_transient")
            ? t.param<bool>("resolveTargetUsage_transient") : false;

        // Decode resolveTargetUsage from flags (mirrors upstream GPUConst.TextureUsage values).
        WGPUTextureUsage resolveTargetUsage =
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        if (resolveTargetUsage_renderAttachmentOnly) {
            resolveTargetUsage = WGPUTextureUsage_RenderAttachment;
        } else if (resolveTargetUsage_copySrcOnly) {
            resolveTargetUsage = WGPUTextureUsage_CopySrc;
        } else if (resolveTargetUsage_transient) {
            resolveTargetUsage =
                WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment;
        }

        // MAINTENANCE_TODO(#4509): Remove this after all implementations have TRANSIENT_ATTACHMENT.
        if (resolveTargetUsage_transient) {
            t.skipIfTransientAttachmentNotSupported();
        }

        // Integer params with defaults.
        const uint32_t colorAttachmentSamples =
            t.hasParam("colorAttachmentSamples")
            ? static_cast<uint32_t>(t.param<int64_t>("colorAttachmentSamples")) : 4u;
        const uint32_t resolveTargetSamples =
            t.hasParam("resolveTargetSamples")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetSamples")) : 1u;

        const uint32_t resolveTargetViewMipCount =
            t.hasParam("resolveTargetViewMipCount")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetViewMipCount")) : 1u;
        const uint32_t resolveTargetViewBaseMipLevel =
            t.hasParam("resolveTargetViewBaseMipLevel")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetViewBaseMipLevel")) : 0u;
        const uint32_t resolveTargetViewArrayLayerCount =
            t.hasParam("resolveTargetViewArrayLayerCount")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetViewArrayLayerCount")) : 1u;
        const uint32_t resolveTargetViewBaseArrayLayer =
            t.hasParam("resolveTargetViewBaseArrayLayer")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetViewBaseArrayLayer")) : 0u;

        const uint32_t colorAttachmentHeight =
            t.hasParam("colorAttachmentHeight")
            ? static_cast<uint32_t>(t.param<int64_t>("colorAttachmentHeight")) : 2u;
        const uint32_t colorAttachmentWidth =
            t.hasParam("colorAttachmentWidth")
            ? static_cast<uint32_t>(t.param<int64_t>("colorAttachmentWidth")) : 2u;
        const uint32_t resolveTargetHeight =
            t.hasParam("resolveTargetHeight")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetHeight")) : 2u;
        const uint32_t resolveTargetWidth =
            t.hasParam("resolveTargetWidth")
            ? static_cast<uint32_t>(t.param<int64_t>("resolveTargetWidth")) : 2u;

        // String params with defaults.
        const std::string colorAttachmentFormatStr =
            t.hasParam("colorAttachmentFormat")
            ? t.param<std::string>("colorAttachmentFormat") : "rgba8unorm";
        const std::string resolveTargetFormatStr =
            t.hasParam("resolveTargetFormat")
            ? t.param<std::string>("resolveTargetFormat") : "rgba8unorm";
        const std::string otherAttachmentFormatStr =
            t.hasParam("otherAttachmentFormat")
            ? t.param<std::string>("otherAttachmentFormat") : "rgba8unorm";

        const bool _valid = t.param<bool>("_valid");

        // Map format strings to WGPUTextureFormat.
        auto parseFormat = [](const std::string& s) -> WGPUTextureFormat {
            if (s == "rgba8unorm")      return WGPUTextureFormat_RGBA8Unorm;
            if (s == "bgra8unorm")      return WGPUTextureFormat_BGRA8Unorm;
            if (s == "rgba8unorm-srgb") return WGPUTextureFormat_RGBA8UnormSrgb;
            return WGPUTextureFormat_Undefined;
        };

        const WGPUTextureFormat colorAttachmentFormat = parseFormat(colorAttachmentFormatStr);
        const WGPUTextureFormat resolveTargetFormat    = parseFormat(resolveTargetFormatStr);
        const WGPUTextureFormat otherAttachmentFormat  = parseFormat(otherAttachmentFormatStr);

        // Run the test in a nested loop such that the configured color attachment with resolve
        // target is tested while occupying each individual colorAttachment slot.
        for (int resolveSlot = 0; resolveSlot < kNumColorAttachments; resolveSlot++) {
            std::vector<WGPURenderPassColorAttachment> colorAttachments(kNumColorAttachments);

            for (int colorAttachmentSlot = 0;
                 colorAttachmentSlot < kNumColorAttachments;
                 colorAttachmentSlot++)
            {
                WGPURenderPassColorAttachment& ca = colorAttachments[colorAttachmentSlot];
                ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;

                if (resolveSlot == colorAttachmentSlot) {
                    // Create the color attachment with resolve target using the configurable params.
                    WGPUTextureDescriptor srcDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    srcDesc.format      = colorAttachmentFormat;
                    srcDesc.size        = WGPUExtent3D{colorAttachmentWidth, colorAttachmentHeight, 1};
                    srcDesc.sampleCount = colorAttachmentSamples;
                    srcDesc.usage       = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
                    WGPUTexture resolveSource = t.createTextureTracked(srcDesc);

                    WGPUTextureDescriptor dstDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    dstDesc.format        = resolveTargetFormat;
                    dstDesc.size          = WGPUExtent3D{
                        resolveTargetWidth,
                        resolveTargetHeight,
                        resolveTargetViewBaseArrayLayer + resolveTargetViewArrayLayerCount};
                    dstDesc.sampleCount   = resolveTargetSamples;
                    dstDesc.mipLevelCount = resolveTargetViewBaseMipLevel + resolveTargetViewMipCount;
                    dstDesc.usage         = resolveTargetUsage;
                    WGPUTexture resolveTarget = t.createTextureTracked(dstDesc);

                    // Source view: default view of the resolve source.
                    WGPUTextureViewDescriptor srcViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    WGPUTextureView sourceView = t.createViewTracked(resolveSource, srcViewDesc);

                    // Resolve target view.
                    WGPUTextureView targetView = nullptr;
                    if (resolveTargetInvalid) {
                        // Use an invalid texture view (mirrors vtu.getErrorTextureView).
                        targetView = getErrorTextureView(t);
                    } else {
                        // bindTextureResource: in JS this passes the texture directly as a
                        // GPUTextureView (creating the default view implicitly).  In the C API we
                        // always need an explicit WGPUTextureView, so we create the default view
                        // in both cases — the valid semantics are identical.
                        WGPUTextureViewDescriptor tvDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                        if (!bindTextureResource) {
                            // Explicit view with the subresource parameters from params.
                            tvDesc.dimension      = (resolveTargetViewArrayLayerCount == 1)
                                                    ? WGPUTextureViewDimension_2D
                                                    : WGPUTextureViewDimension_2DArray;
                            tvDesc.mipLevelCount  = resolveTargetViewMipCount;
                            tvDesc.arrayLayerCount = resolveTargetViewArrayLayerCount;
                            tvDesc.baseMipLevel   = resolveTargetViewBaseMipLevel;
                            tvDesc.baseArrayLayer = resolveTargetViewBaseArrayLayer;
                        }
                        // If bindTextureResource==true: use default INIT descriptor (all zeroes
                        // → backend picks dimension/levels automatically, same as createView()).
                        targetView = t.createViewTracked(resolveTarget, tvDesc);
                    }

                    ca.view          = sourceView;
                    ca.resolveTarget = targetView;
                    ca.loadOp        = WGPULoadOp_Load;
                    ca.storeOp       = WGPUStoreOp_Discard;
                } else {
                    // Create a basic texture to fill other color attachment slots.
                    // Its dimensions and sample count must match the resolve source.
                    WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    colorDesc.format      = otherAttachmentFormat;
                    colorDesc.size        = WGPUExtent3D{colorAttachmentWidth, colorAttachmentHeight, 1};
                    colorDesc.sampleCount = colorAttachmentSamples;
                    colorDesc.usage       = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
                    WGPUTexture colorTex = t.createTextureTracked(colorDesc);

                    WGPUTextureDescriptor otherRtDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    otherRtDesc.format      = otherAttachmentFormat;
                    otherRtDesc.size        = WGPUExtent3D{colorAttachmentWidth, colorAttachmentHeight, 1};
                    otherRtDesc.sampleCount = 1;
                    otherRtDesc.usage       = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
                    WGPUTexture otherResolveTex = t.createTextureTracked(otherRtDesc);

                    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    WGPUTextureView colorView       = t.createViewTracked(colorTex, vDesc);
                    WGPUTextureView otherResolveView = t.createViewTracked(otherResolveTex, vDesc);

                    ca.view          = colorView;
                    ca.resolveTarget = otherResolveView;
                    ca.loadOp        = WGPULoadOp_Load;
                    ca.storeOp       = WGPUStoreOp_Discard;
                }
            }

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = static_cast<size_t>(kNumColorAttachments);
            passDesc.colorAttachments     = colorAttachments.data();

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);

            // Validation of render pass attachments is deferred to encoder finish().
            t.expectValidationError([&] {
                t.finishTracked(encoder);
            }, !_valid);
        }
    });

} // namespace
