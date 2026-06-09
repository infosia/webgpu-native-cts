// Ported from gpuweb/cts src/webgpu/api/validation/encoding/render_bundle.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,render_bundle",
    "Tests execution of render bundles.");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a 16x16 RGBA8Unorm render-attachment texture on the test device.
// sampleCount=4 → multisampled.
static WGPUTexture makeColorTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    uint32_t sampleCount = 1)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size        = WGPUExtent3D{16, 16, 1};
    desc.format      = format;
    desc.usage       = WGPUTextureUsage_RenderAttachment;
    desc.sampleCount = sampleCount;
    return t.createTextureTracked(desc);
}

// Create a 16x16 depth/stencil render-attachment texture on the test device.
static WGPUTexture makeDepthStencilTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    uint32_t sampleCount = 1)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size        = WGPUExtent3D{16, 16, 1};
    desc.format      = format;
    desc.usage       = WGPUTextureUsage_RenderAttachment;
    desc.sampleCount = sampleCount;
    return t.createTextureTracked(desc);
}

// Create a default texture view.
static WGPUTextureView makeDefaultView(WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return wgpuTextureCreateView(texture, &desc);
}

// ---------------------------------------------------------------------------
// Test: empty_bundle_list
// Tests that it is valid to execute an empty list of render bundles.
// ---------------------------------------------------------------------------
CTS_TEST(g, "empty_bundle_list")
    .desc("Test that it is valid to execute an empty list of render bundles")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Create a simple color render pass
        WGPUTexture colorTex = makeColorTexture(t, WGPUTextureFormat_RGBA8Unorm);
        WGPUTextureView colorView = makeDefaultView(colorTex);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view    = colorView;
        colorAtt.loadOp  = WGPULoadOp_Load;
        colorAtt.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAtt;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        // Execute an empty list of bundles — valid per spec
        wgpuRenderPassEncoderExecuteBundles(pass, 0, nullptr);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Should succeed
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, false);

        wgpuTextureViewRelease(colorView);
    });

// ---------------------------------------------------------------------------
// Test: device_mismatch
// Tests executeBundles cannot be called with render bundles created from another device.
// Two bundles are tested so we know all bundles are validated:
//   - bundle0 and bundle1 from same device (control)
//   - bundle0 from different device
//   - bundle1 from different device
// ---------------------------------------------------------------------------
CTS_TEST(g, "device_mismatch")
    .desc(
        "Tests executeBundles cannot be called with render bundles created from another device. "
        "Test with two bundles to make sure all bundles can be validated: "
        "bundle0 and bundle1 from same device; "
        "bundle0 and bundle1 from different device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            // control case: both from same device
            ParamRecord{{"bundle0Mismatched", false}, {"bundle1Mismatched", false}},
            ParamRecord{{"bundle0Mismatched", true},  {"bundle1Mismatched", false}},
            ParamRecord{{"bundle0Mismatched", false}, {"bundle1Mismatched", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool bundle0Mismatched = t.param<bool>("bundle0Mismatched");
        const bool bundle1Mismatched = t.param<bool>("bundle1Mismatched");

        // Create both bundles on the appropriate device
        WGPUTextureFormat colorFmt = WGPUTextureFormat_RGBA8Unorm;

        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount = 1;
        bundleDesc.colorFormats     = &colorFmt;

        WGPUDevice bundle0Device = bundle0Mismatched ? t.mismatchedDevice() : t.device();
        WGPURenderBundleEncoder bundleEnc0 = wgpuDeviceCreateRenderBundleEncoder(bundle0Device, &bundleDesc);
        WGPURenderBundle bundle0 = wgpuRenderBundleEncoderFinish(bundleEnc0, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEnc0);

        WGPUDevice bundle1Device = bundle1Mismatched ? t.mismatchedDevice() : t.device();
        WGPURenderBundleEncoder bundleEnc1 = wgpuDeviceCreateRenderBundleEncoder(bundle1Device, &bundleDesc);
        WGPURenderBundle bundle1 = wgpuRenderBundleEncoderFinish(bundleEnc1, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEnc1);

        const bool shouldError = bundle0Mismatched || bundle1Mismatched;

        // Create a render pass matching the bundle descriptor (rgba8unorm color)
        WGPUTexture colorTex  = makeColorTexture(t, WGPUTextureFormat_RGBA8Unorm);
        WGPUTextureView colorView = makeDefaultView(colorTex);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view    = colorView;
        colorAtt.loadOp  = WGPULoadOp_Load;
        colorAtt.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAtt;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        WGPURenderBundle bundles[2] = {bundle0, bundle1};
        wgpuRenderPassEncoderExecuteBundles(pass, 2, bundles);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, shouldError);

        wgpuRenderBundleRelease(bundle0);
        wgpuRenderBundleRelease(bundle1);
        wgpuTextureViewRelease(colorView);
    });

// ---------------------------------------------------------------------------
// Test: color_formats_mismatch
// Tests executeBundles cannot be called with render bundles that do not match
// the colorFormats of the render pass. This includes:
//   - formats don't match
//   - formats match but are in a different order
//   - formats match but there is a different count
// ---------------------------------------------------------------------------
CTS_TEST(g, "color_formats_mismatch")
    .desc(
        "Tests executeBundles cannot be called with render bundles that do match the colorFormats of "
        "the render pass. This includes: formats don't match; formats match but are in a different "
        "order; formats match but there is a different count")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            // control case — compatible
            ParamRecord{
                {"bundleFormats", std::string("bgra8unorm,rg8unorm")},
                {"passFormats",   std::string("bgra8unorm,rg8unorm")},
                {"_compatible",   true}},
            ParamRecord{
                {"bundleFormats", std::string("bgra8unorm,rg8unorm")},
                {"passFormats",   std::string("bgra8unorm,bgra8unorm")},
                {"_compatible",   false}},
            ParamRecord{
                {"bundleFormats", std::string("bgra8unorm,rg8unorm")},
                {"passFormats",   std::string("rg8unorm,bgra8unorm")},
                {"_compatible",   false}},
            // bundle has 3 formats, pass has 2
            ParamRecord{
                {"bundleFormats", std::string("bgra8unorm,rg8unorm,rgba8unorm")},
                {"passFormats",   std::string("rg8unorm,bgra8unorm")},
                {"_compatible",   false}},
            // bundle has 2 formats, pass has 3
            ParamRecord{
                {"bundleFormats", std::string("bgra8unorm,rg8unorm")},
                {"passFormats",   std::string("rg8unorm,bgra8unorm,rgba8unorm")},
                {"_compatible",   false}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string bundleFormatsStr = t.param<std::string>("bundleFormats");
        const std::string passFormatsStr   = t.param<std::string>("passFormats");
        const bool compatible              = t.param<bool>("_compatible");

        // Parse comma-separated format strings
        auto parseFormats = [](const std::string& s) -> std::vector<WGPUTextureFormat> {
            std::vector<WGPUTextureFormat> result;
            std::string token;
            for (char c : s) {
                if (c == ',') {
                    if (!token.empty()) {
                        result.push_back(parseTextureFormat(token));
                        token.clear();
                    }
                } else {
                    token += c;
                }
            }
            if (!token.empty()) {
                result.push_back(parseTextureFormat(token));
            }
            return result;
        };

        std::vector<WGPUTextureFormat> bundleFormats = parseFormats(bundleFormatsStr);
        std::vector<WGPUTextureFormat> passFormats   = parseFormats(passFormatsStr);

        // Create the render bundle encoder with bundleFormats
        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount = bundleFormats.size();
        bundleDesc.colorFormats     = bundleFormats.data();

        WGPURenderBundleEncoder bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEnc, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEnc);

        // Create color textures and views for the render pass
        std::vector<WGPUTexture>     passTex(passFormats.size());
        std::vector<WGPUTextureView> passViews(passFormats.size());
        for (size_t i = 0; i < passFormats.size(); ++i) {
            passTex[i]   = makeColorTexture(t, passFormats[i]);
            passViews[i] = makeDefaultView(passTex[i]);
        }

        std::vector<WGPURenderPassColorAttachment> colorAtts(passFormats.size());
        for (size_t i = 0; i < passFormats.size(); ++i) {
            colorAtts[i]         = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAtts[i].view    = passViews[i];
            colorAtts[i].loadOp  = WGPULoadOp_Load;
            colorAtts[i].storeOp = WGPUStoreOp_Store;
        }

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = static_cast<uint32_t>(passFormats.size());
        passDesc.colorAttachments     = colorAtts.data();

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !compatible);

        wgpuRenderBundleRelease(bundle);
        for (WGPUTextureView v : passViews) {
            wgpuTextureViewRelease(v);
        }
    });

// ---------------------------------------------------------------------------
// Test: depth_stencil_formats_mismatch
// Tests executeBundles cannot be called with render bundles that do not match
// the depthStencil format of the render pass.
// ---------------------------------------------------------------------------
CTS_TEST(g, "depth_stencil_formats_mismatch")
    .desc(
        "Tests executeBundles cannot be called with render bundles that do match the depthStencil of "
        "the render pass. This includes: formats don't match; formats have matching depth or stencil "
        "aspects, but other aspects are missing")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            // control case — same format
            ParamRecord{{"bundleFormat", std::string("depth24plus")},             {"passFormat", std::string("depth24plus")}},
            ParamRecord{{"bundleFormat", std::string("depth24plus")},             {"passFormat", std::string("depth16unorm")}},
            ParamRecord{{"bundleFormat", std::string("depth24plus")},             {"passFormat", std::string("depth24plus-stencil8")}},
            ParamRecord{{"bundleFormat", std::string("stencil8")},               {"passFormat", std::string("depth24plus-stencil8")}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat bundleFormat = parseTextureFormat(t.param<std::string>("bundleFormat"));
        const WGPUTextureFormat passFormat   = parseTextureFormat(t.param<std::string>("passFormat"));

        t.skipIfTextureFormatNotSupported(bundleFormat);
        t.skipIfTextureFormatNotSupported(passFormat);

        const bool compatible = (bundleFormat == passFormat);

        // Create the render bundle encoder with no color formats, only depth stencil
        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount    = 0;
        bundleDesc.colorFormats        = nullptr;
        bundleDesc.depthStencilFormat  = bundleFormat;

        WGPURenderBundleEncoder bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEnc, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEnc);

        // Create a render pass with passFormat as depth stencil
        WGPUTexture dsTex  = makeDepthStencilTexture(t, passFormat);
        WGPUTextureView dsView = makeDefaultView(dsTex);

        const bool hasDepth   = isDepthTextureFormat(passFormat);
        const bool hasStencil = isStencilTextureFormat(passFormat);

        WGPURenderPassDepthStencilAttachment dsAtt = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        dsAtt.view = dsView;
        if (hasDepth) {
            dsAtt.depthLoadOp     = WGPULoadOp_Load;
            dsAtt.depthStoreOp    = WGPUStoreOp_Store;
            dsAtt.depthClearValue = 0.0f;
        }
        if (hasStencil) {
            dsAtt.stencilLoadOp     = WGPULoadOp_Load;
            dsAtt.stencilStoreOp    = WGPUStoreOp_Store;
            dsAtt.stencilClearValue = 0;
        }

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 0;
        passDesc.colorAttachments       = nullptr;
        passDesc.depthStencilAttachment = &dsAtt;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !compatible);

        wgpuRenderBundleRelease(bundle);
        wgpuTextureViewRelease(dsView);
    });

// ---------------------------------------------------------------------------
// Test: depth_stencil_readonly_mismatch
// Tests executeBundles cannot be called with render bundles that do not match
// the depthStencil readonly state of the render pass.
// ---------------------------------------------------------------------------
CTS_TEST(g, "depth_stencil_readonly_mismatch")
    .desc(
        "Tests executeBundles cannot be called with render bundles that do match the depthStencil "
        "readonly state of the render pass.")
    .params([](ParamsBuilder u) {
        // Build format values from kDepthStencilFormats
        std::vector<Value> formatValues;
        for (WGPUTextureFormat fmt : kDepthStencilFormats) {
            formatValues.emplace_back(std::string(textureFormatIdentifier(fmt)));
        }
        return u
            .combine("depthStencilFormat", formatValues)
            .beginSubcases()
            .combine("bundleDepthReadOnly",   {true, false})
            .combine("bundleStencilReadOnly", {true, false})
            .combine("passDepthReadOnly",     {true, false})
            .combine("passStencilReadOnly",   {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat depthStencilFormat =
            parseTextureFormat(t.param<std::string>("depthStencilFormat"));

        t.skipIfTextureFormatNotSupported(depthStencilFormat);

        const bool bundleDepthReadOnly   = t.param<bool>("bundleDepthReadOnly");
        const bool bundleStencilReadOnly = t.param<bool>("bundleStencilReadOnly");
        const bool passDepthReadOnly     = t.param<bool>("passDepthReadOnly");
        const bool passStencilReadOnly   = t.param<bool>("passStencilReadOnly");

        // Compatible when: for each aspect that is read-only in the pass, the bundle
        // must also declare that aspect read-only.
        const bool compatible =
            (!passDepthReadOnly   || bundleDepthReadOnly   == passDepthReadOnly) &&
            (!passStencilReadOnly || bundleStencilReadOnly == passStencilReadOnly);

        // Create the render bundle encoder with the depth stencil format and readonly flags
        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount   = 0;
        bundleDesc.colorFormats       = nullptr;
        bundleDesc.depthStencilFormat = depthStencilFormat;
        bundleDesc.depthReadOnly      = bundleDepthReadOnly   ? WGPU_TRUE : WGPU_FALSE;
        bundleDesc.stencilReadOnly    = bundleStencilReadOnly ? WGPU_TRUE : WGPU_FALSE;

        WGPURenderBundleEncoder bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEnc, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEnc);

        // Create a depth/stencil texture and begin a render pass with the given readonly flags
        WGPUTexture dsTex  = makeDepthStencilTexture(t, depthStencilFormat);
        WGPUTextureView dsView = makeDefaultView(dsTex);

        const bool hasDepth   = isDepthTextureFormat(depthStencilFormat);
        const bool hasStencil = isStencilTextureFormat(depthStencilFormat);

        WGPURenderPassDepthStencilAttachment dsAtt = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        dsAtt.view = dsView;

        // Set depthReadOnly/stencilReadOnly unconditionally, mirroring the upstream which always
        // passes these flags to the descriptor regardless of whether the format has that aspect.
        // Only the load/store ops are conditioned on the format actually having the aspect AND
        // the aspect not being read-only (matching upstream gpu_test.ts createEncoder logic).
        dsAtt.depthReadOnly   = passDepthReadOnly   ? WGPU_TRUE : WGPU_FALSE;
        dsAtt.stencilReadOnly = passStencilReadOnly ? WGPU_TRUE : WGPU_FALSE;

        if (hasDepth && !passDepthReadOnly) {
            dsAtt.depthLoadOp     = WGPULoadOp_Clear;
            dsAtt.depthStoreOp    = WGPUStoreOp_Discard;
            dsAtt.depthClearValue = 0.0f;
        }

        if (hasStencil && !passStencilReadOnly) {
            dsAtt.stencilLoadOp     = WGPULoadOp_Clear;
            dsAtt.stencilStoreOp    = WGPUStoreOp_Discard;
            dsAtt.stencilClearValue = 1;
        }

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount   = 0;
        passDesc.colorAttachments       = nullptr;
        passDesc.depthStencilAttachment = &dsAtt;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !compatible);

        wgpuRenderBundleRelease(bundle);
        wgpuTextureViewRelease(dsView);
    });

// ---------------------------------------------------------------------------
// Test: sample_count_mismatch
// Tests executeBundles cannot be called with render bundles that do not match
// the sampleCount of the render pass.
//
// NOTE: The upstream params contain a bug — the last two entries use
// 'bundleFormat'/'passFormat' keys (instead of 'bundleSamples'/'passSamples'),
// so those params read as undefined, which means bundleSamples===passSamples
// evaluates to compatible=true. This port faithfully reproduces that behavior:
// those cases produce sampleCount=undefined → treated as sampleCount=1
// in the createRenderBundleEncoder call (the INIT macro defaults sampleCount=1),
// and compatible=true.
// ---------------------------------------------------------------------------
CTS_TEST(g, "sample_count_mismatch")
    .desc(
        "Tests executeBundles cannot be called with render bundles that do match the sampleCount of "
        "the render pass.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            // control cases
            ParamRecord{{"bundleSamples", int64_t(1)}, {"passSamples", int64_t(1)}},
            ParamRecord{{"bundleSamples", int64_t(4)}, {"passSamples", int64_t(4)}},
            // These two have a bug in the upstream: bundleFormat/passFormat keys are used
            // instead of bundleSamples/passSamples. Both params will be undefined/missing,
            // which makes compatible = undefined === undefined = true (both default to 1).
            ParamRecord{{"bundleFormat",  int64_t(4)}, {"passFormat",  int64_t(1)}},
            ParamRecord{{"bundleFormat",  int64_t(1)}, {"passFormat",  int64_t(4)}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // When bundleSamples or passSamples param is missing (the buggy upstream entries),
        // we default them to 1 (same as WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT defaults).
        const uint32_t bundleSamples = t.hasParam("bundleSamples")
            ? static_cast<uint32_t>(t.param<int64_t>("bundleSamples"))
            : 1;
        const uint32_t passSamples = t.hasParam("passSamples")
            ? static_cast<uint32_t>(t.param<int64_t>("passSamples"))
            : 1;

        const bool compatible = (bundleSamples == passSamples);

        WGPUTextureFormat colorFmt = WGPUTextureFormat_BGRA8Unorm;

        // Create render bundle encoder with bundleSamples
        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount = 1;
        bundleDesc.colorFormats     = &colorFmt;
        bundleDesc.sampleCount      = bundleSamples;

        WGPURenderBundleEncoder bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEnc, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEnc);

        // Create a render pass with passSamples (multisample texture for sampleCount>1)
        WGPUTexture colorTex = makeColorTexture(t, WGPUTextureFormat_BGRA8Unorm, passSamples);
        WGPUTextureView colorView = makeDefaultView(colorTex);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view    = colorView;
        colorAtt.loadOp  = WGPULoadOp_Load;
        colorAtt.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAtt;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !compatible);

        wgpuRenderBundleRelease(bundle);
        wgpuTextureViewRelease(colorView);
    });

} // namespace
