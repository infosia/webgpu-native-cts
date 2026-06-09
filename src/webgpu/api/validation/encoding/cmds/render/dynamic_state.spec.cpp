// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render/dynamic_state.spec.ts

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a texture of the given width/height/layers as RGBA8Unorm render attachment.
static WGPUTexture createRenderAttachmentTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t width,
    uint32_t height,
    uint32_t depthOrArrayLayers = 1)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size           = WGPUExtent3D{width, height, depthOrArrayLayers};
    desc.mipLevelCount  = 1;
    desc.sampleCount    = 1;
    desc.dimension      = WGPUTextureDimension_2D;
    desc.format         = WGPUTextureFormat_RGBA8Unorm;
    desc.usage          = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// Begin a minimal single-color-attachment render pass (loadOp=load, storeOp=store).
static WGPURenderPassEncoder beginRenderPass(
    WGPUCommandEncoder cmdEnc,
    WGPUTextureView    view)
{
    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view    = view;
    colorAttach.loadOp  = WGPULoadOp_Load;
    colorAttach.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;
    return wgpuCommandEncoderBeginRenderPass(cmdEnc, &passDesc);
}

// nextAfterF32(val, 'positive'/'negative', 'no-flush'):
// Returns the next representable float32 value in the given direction.
// Mirrors the upstream nextAfterF32 with mode='no-flush'.
static float nextAfterF32(float val, bool positive) {
    return std::nextafter(val, positive ? std::numeric_limits<float>::infinity()
                                        : -std::numeric_limits<float>::infinity());
}

// testViewportCall: mirrors F.testViewportCall in the upstream test.
// Creates a 1x1 (or given size) render attachment, opens a render pass,
// calls setViewport, ends the pass, then expects validation error on finish()
// iff !success.
static void testViewportCall(
    AllFeaturesMaxLimitsGpuTest& t,
    bool    success,
    float   x,
    float   y,
    float   w,
    float   h,
    float   minDepth,
    float   maxDepth,
    uint32_t attachmentWidth  = 1,
    uint32_t attachmentHeight = 1)
{
    WGPUTexture attachment = createRenderAttachmentTexture(t, attachmentWidth, attachmentHeight);

    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(attachment, vDesc);

    WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginRenderPass(cmdEnc, view);
    wgpuRenderPassEncoderSetViewport(pass, x, y, w, h, minDepth, maxDepth);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    t.expectValidationError([&] {
        t.finishTracked(cmdEnc);
    }, !success);
}

// testScissorCall: mirrors F.testScissorCall in the upstream test.
// success=true  → finish() must succeed.
// success=false → finish() must produce a validation error.
// success='type-error' → in C++ there is no TypeError; negative values cast to
//   uint32_t produce a very large number that will fail validation at finish().
//   Treat it the same as success=false (expect validation error at finish()).
static void testScissorCall(
    AllFeaturesMaxLimitsGpuTest& t,
    bool    success,          // false means expect validation error
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h,
    uint32_t attachmentWidth  = 1,
    uint32_t attachmentHeight = 1)
{
    WGPUTexture attachment = createRenderAttachmentTexture(t, attachmentWidth, attachmentHeight);

    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(attachment, vDesc);

    WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginRenderPass(cmdEnc, view);
    wgpuRenderPassEncoderSetScissorRect(pass, x, y, w, h);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    t.expectValidationError([&] {
        t.finishTracked(cmdEnc);
    }, !success);
}

// createDummyRenderPassEncoder: mirrors F.createDummyRenderPassEncoder.
// Opens a 1x1 render pass and returns the encoder + pass for further commands.
struct DummyRenderPassEncoders {
    WGPUCommandEncoder    encoder = nullptr;
    WGPURenderPassEncoder pass    = nullptr;
};

static DummyRenderPassEncoders createDummyRenderPassEncoder(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTexture attachment = createRenderAttachmentTexture(t, 1, 1);

    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(attachment, vDesc);

    WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginRenderPass(cmdEnc, view);
    return DummyRenderPassEncoders{cmdEnc, pass};
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render,dynamic_state",
    "API validation tests for dynamic state commands (setViewport/ScissorRect/BlendColor...).");

// ---------------------------------------------------------------------------
// setViewport,width_height_nonnegative
// Test that the width and height parameters of setViewport must be non-negative.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setViewport,width_height_nonnegative")
    .desc(
        "Test that the width and height parameters of setViewport must be non-negative.")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combineWithParams({
                // Control case: everything to 0 is ok, covers the empty viewport case.
                ParamRecord{{"x", double(0)}, {"y", double(0)}, {"w", double(0)},  {"h", double(0)}},
                // Negative width/height is invalid
                ParamRecord{{"x", double(0)}, {"y", double(0)}, {"w", double(-1)}, {"h", double(0)}},
                ParamRecord{{"x", double(0)}, {"y", double(0)}, {"w", double(0)},  {"h", double(-1)}},
                // Negative width/height is invalid even if resulting bounds are positive
                ParamRecord{{"x", double(1)}, {"y", double(0)}, {"w", double(-1)}, {"h", double(0)}},
                ParamRecord{{"x", double(0)}, {"y", double(1)}, {"w", double(0)},  {"h", double(-1)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const float x = static_cast<float>(t.param<double>("x"));
        const float y = static_cast<float>(t.param<double>("y"));
        const float w = static_cast<float>(t.param<double>("w"));
        const float h = static_cast<float>(t.param<double>("h"));
        const bool success = (w >= 0.0f) && (h >= 0.0f);
        testViewportCall(t, success, x, y, w, h, 0.0f, 1.0f);
    });

// ---------------------------------------------------------------------------
// setViewport,exceeds_attachment_size
// Test that the viewport can exceed the attachment size.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setViewport,exceeds_attachment_size")
    .desc("Test that the viewport can exceed the attachment size")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"attachmentWidth", int64_t(3)},    {"attachmentHeight", int64_t(3)}},
                ParamRecord{{"attachmentWidth", int64_t(1024)}, {"attachmentHeight", int64_t(1024)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t attachmentWidth  = static_cast<uint32_t>(t.param<int64_t>("attachmentWidth"));
        const uint32_t attachmentHeight = static_cast<uint32_t>(t.param<int64_t>("attachmentHeight"));
        testViewportCall(
            t,
            /*success=*/true,
            0.0f, 0.0f,
            static_cast<float>(attachmentWidth + 1),
            static_cast<float>(attachmentHeight + 1),
            0.0f, 1.0f,
            attachmentWidth, attachmentHeight);
    });

// ---------------------------------------------------------------------------
// setViewport,xy_rect_contained_in_bounds
// Test that the rectangle defined by x, y, width, height must be contained in
// the maximum viewport bounds and that the viewport size cannot exceed the max.
//
// od / sd can be integer offsets or 'negative'/'positive' (encoded as special
// double sentinels: -2.0 for 'negative', +2.0 for 'positive').
// ---------------------------------------------------------------------------
CTS_TEST(g, "setViewport,xy_rect_contained_in_bounds")
    .desc(
        "Test that the rectangle defined by x, y, width, height must be contained in "
        "the maximum viewport bounds and that the viewport size cannot exceed the maximum.")
    .params([](ParamsBuilder u) {
        // od/sd values: integers encoded as int64; special tokens 'negative'=-2 / 'positive'=+2
        // (we use string to keep parity with the TypeScript enum)
        return u
            .combine("dimension", {int64_t(0), int64_t(1)})
            .beginSubcases()
            .combineWithParams({
                // Control case: max viewport is valid.
                ParamRecord{{"om", int64_t(0)},  {"od", std::string("zero")},     {"sd", std::string("zero")},     {"_success", true}},
                // Other valid cases
                ParamRecord{{"om", int64_t(-1)}, {"od", std::string("zero")},     {"sd", std::string("zero")},     {"_success", true}},
                ParamRecord{{"om", int64_t(-2)}, {"od", std::string("zero")},     {"sd", std::string("zero")},     {"_success", true}},
                ParamRecord{{"om", int64_t(1)},  {"od", std::string("minus_one")},{"sd", std::string("zero")},     {"_success", true}},
                ParamRecord{{"om", int64_t(0)},  {"od", std::string("minus_one")},{"sd", std::string("zero")},     {"_success", true}},
                ParamRecord{{"om", int64_t(0)},  {"od", std::string("plus_one")}, {"sd", std::string("zero")},     {"_success", true}},
                ParamRecord{{"om", int64_t(1)},  {"od", std::string("zero")},     {"sd", std::string("minus_one")},{"_success", true}},
                // Cases that go outside the allowed bounds
                ParamRecord{{"om", int64_t(-2)}, {"od", std::string("minus_one")},{"sd", std::string("zero")},     {"_success", false}},
                ParamRecord{{"om", int64_t(1)},  {"od", std::string("zero")},     {"sd", std::string("zero")},     {"_success", false}},
                ParamRecord{{"om", int64_t(1)},  {"od", std::string("plus_one")}, {"sd", std::string("minus_one")},{"_success", false}},
                ParamRecord{{"om", int64_t(1)},  {"od", std::string("negative")}, {"sd", std::string("zero")},     {"_success", false}},
                // Cases that exceed the max viewport size
                ParamRecord{{"om", int64_t(0)},  {"od", std::string("zero")},     {"sd", std::string("plus_one")}, {"_success", false}},
                ParamRecord{{"om", int64_t(0)},  {"od", std::string("zero")},     {"sd", std::string("positive")}, {"_success", false}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int64_t     dimension = t.param<int64_t>("dimension");
        const int64_t     om        = t.param<int64_t>("om");
        const std::string od        = t.param<std::string>("od");
        const std::string sd        = t.param<std::string>("sd");
        const bool        success   = t.param<bool>("_success");

        const WGPULimits limits         = t.getLimits();
        const float      maxViewportSize = static_cast<float>(limits.maxTextureDimension2D);

        // xy[0] = x, xy[1] = y
        float xy[2] = { 0.0f, 0.0f };
        // wh[0] = w, wh[1] = h
        float wh[2] = { maxViewportSize, maxViewportSize };

        // Apply origin offset multiplier
        xy[dimension] = maxViewportSize * static_cast<float>(om);

        // Apply origin delta (od)
        if (od == "negative") {
            xy[dimension] = nextAfterF32(xy[dimension], /*positive=*/false);
        } else if (od == "positive") {
            xy[dimension] = nextAfterF32(xy[dimension], /*positive=*/true);
        } else if (od == "minus_one") {
            xy[dimension] += -1.0f;
        } else if (od == "plus_one") {
            xy[dimension] += 1.0f;
        }
        // else "zero" → no-op

        // Apply size delta (sd)
        if (sd == "negative") {
            wh[dimension] = nextAfterF32(wh[dimension], /*positive=*/false);
        } else if (sd == "positive") {
            wh[dimension] = nextAfterF32(wh[dimension], /*positive=*/true);
        } else if (sd == "minus_one") {
            wh[dimension] += -1.0f;
        } else if (sd == "plus_one") {
            wh[dimension] += 1.0f;
        }
        // else "zero" → no-op

        testViewportCall(t, success, xy[0], xy[1], wh[0], wh[1], 0.0f, 1.0f);
    });

// ---------------------------------------------------------------------------
// setViewport,depth_rangeAndOrder
// Test that 0 <= minDepth <= maxDepth <= 1.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setViewport,depth_rangeAndOrder")
    .desc("Test that 0 <= minDepth <= maxDepth <= 1")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combineWithParams({
                // Success cases
                ParamRecord{{"minDepth", double(0.0)},  {"maxDepth", double(1.0)}},
                ParamRecord{{"minDepth", double(-0.0)}, {"maxDepth", double(-0.0)}},
                ParamRecord{{"minDepth", double(1.0)},  {"maxDepth", double(1.0)}},
                ParamRecord{{"minDepth", double(0.3)},  {"maxDepth", double(0.7)}},
                ParamRecord{{"minDepth", double(0.7)},  {"maxDepth", double(0.7)}},
                ParamRecord{{"minDepth", double(0.3)},  {"maxDepth", double(0.3)}},
                // Invalid cases
                ParamRecord{{"minDepth", double(-0.1)}, {"maxDepth", double(1.0)}},
                ParamRecord{{"minDepth", double(0.0)},  {"maxDepth", double(1.1)}},
                ParamRecord{{"minDepth", double(0.5)},  {"maxDepth", double(0.49999)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const float minDepth = static_cast<float>(t.param<double>("minDepth"));
        const float maxDepth = static_cast<float>(t.param<double>("maxDepth"));
        const bool success =
            (0.0f <= minDepth) && (minDepth <= 1.0f) &&
            (0.0f <= maxDepth) && (maxDepth <= 1.0f) &&
            (minDepth <= maxDepth);
        testViewportCall(t, success, 0.0f, 0.0f, 1.0f, 1.0f, minDepth, maxDepth);
    });

// ---------------------------------------------------------------------------
// setScissorRect,x_y_width_height_nonnegative
// Test that the parameters of setScissorRect must be non-negative.
// In the WebGPU JS API, negative values cause a TypeError before validation.
// In C (uint32_t API), negative integer values cast to uint32_t produce very
// large numbers that will fail validation at finish() just like success=false.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setScissorRect,x_y_width_height_nonnegative")
    .desc(
        "Test that the parameters of setScissorRect to define the box must be non-negative "
        "or a TypeError is thrown.\n"
        "Note: in the C API, setScissorRect takes uint32_t; negative values from JS are "
        "modelled as static_cast<uint32_t>(-1) = 0xFFFFFFFF which will fail validation "
        "(large coords exceed attachment bounds). We expect a validation error at finish() "
        "for the same cases that produce a TypeError in JS.")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combineWithParams({
                // Control case: all-zero is ok (empty scissor).
                ParamRecord{{"x", int64_t(0)},  {"y", int64_t(0)},  {"w", int64_t(0)},  {"h", int64_t(0)}},
                // Test -1 for each dimension (encoded as int64 -1 → cast to uint32_t at call site)
                ParamRecord{{"x", int64_t(-1)}, {"y", int64_t(0)},  {"w", int64_t(0)},  {"h", int64_t(0)}},
                ParamRecord{{"x", int64_t(0)},  {"y", int64_t(-1)}, {"w", int64_t(0)},  {"h", int64_t(0)}},
                ParamRecord{{"x", int64_t(0)},  {"y", int64_t(0)},  {"w", int64_t(-1)}, {"h", int64_t(0)}},
                ParamRecord{{"x", int64_t(0)},  {"y", int64_t(0)},  {"w", int64_t(0)},  {"h", int64_t(-1)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int64_t xi = t.param<int64_t>("x");
        const int64_t yi = t.param<int64_t>("y");
        const int64_t wi = t.param<int64_t>("w");
        const int64_t hi = t.param<int64_t>("h");

        // In the JS test, negative values throw TypeError (treated as failure).
        // In C, we cast to uint32_t: -1 → 0xFFFFFFFF (will overflow attachment bounds).
        const uint32_t x = static_cast<uint32_t>(xi);
        const uint32_t y = static_cast<uint32_t>(yi);
        const uint32_t w = static_cast<uint32_t>(wi);
        const uint32_t h = static_cast<uint32_t>(hi);

        // Valid only if all original values are >= 0.
        const bool success = (xi >= 0) && (yi >= 0) && (wi >= 0) && (hi >= 0);
        testScissorCall(t, success, x, y, w, h);
    });

// ---------------------------------------------------------------------------
// setScissorRect,xy_rect_contained_in_attachment
// Test that the rectangle defined by x, y, width, height must be contained
// in the attachments.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setScissorRect,xy_rect_contained_in_attachment")
    .desc(
        "Test that the rectangle defined by x, y, width, height must be contained in the attachments")
    .params([](ParamsBuilder u) {
        return u
            .combineWithParams({
                ParamRecord{{"attachmentWidth", int64_t(3)},    {"attachmentHeight", int64_t(5)}},
                ParamRecord{{"attachmentWidth", int64_t(5)},    {"attachmentHeight", int64_t(3)}},
                ParamRecord{{"attachmentWidth", int64_t(1024)}, {"attachmentHeight", int64_t(1)}},
                ParamRecord{{"attachmentWidth", int64_t(1)},    {"attachmentHeight", int64_t(1024)}},
            })
            .beginSubcases()
            .combineWithParams({
                // Control case: a full scissor is valid.
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(0)}, {"dw", int64_t(0)},  {"dh", int64_t(0)}},
                // Other valid cases with a partial scissor.
                ParamRecord{{"dx", int64_t(1)}, {"dy", int64_t(0)}, {"dw", int64_t(-1)}, {"dh", int64_t(0)}},
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(1)}, {"dw", int64_t(0)},  {"dh", int64_t(-1)}},
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(0)}, {"dw", int64_t(-1)}, {"dh", int64_t(0)}},
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(0)}, {"dw", int64_t(0)},  {"dh", int64_t(-1)}},
                // Small values that cause the scissor to go outside the attachment.
                ParamRecord{{"dx", int64_t(1)}, {"dy", int64_t(0)}, {"dw", int64_t(0)},  {"dh", int64_t(0)}},
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(1)}, {"dw", int64_t(0)},  {"dh", int64_t(0)}},
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(0)}, {"dw", int64_t(1)},  {"dh", int64_t(0)}},
                ParamRecord{{"dx", int64_t(0)}, {"dy", int64_t(0)}, {"dw", int64_t(0)},  {"dh", int64_t(1)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t attachmentWidth  = static_cast<uint32_t>(t.param<int64_t>("attachmentWidth"));
        const uint32_t attachmentHeight = static_cast<uint32_t>(t.param<int64_t>("attachmentHeight"));
        const int64_t  dx = t.param<int64_t>("dx");
        const int64_t  dy = t.param<int64_t>("dy");
        const int64_t  dw = t.param<int64_t>("dw");
        const int64_t  dh = t.param<int64_t>("dh");

        // Upstream: x = dx, y = dy, w = attachmentWidth + dw, h = attachmentWidth + dh
        // (note: upstream uses attachmentWidth for both w and h deltas — faithful port)
        const int64_t x = dx;
        const int64_t y = dy;
        const int64_t w = static_cast<int64_t>(attachmentWidth)  + dw;
        const int64_t h = static_cast<int64_t>(attachmentWidth)  + dh;  // intentional: upstream uses attachmentWidth

        const bool success =
            (x + w <= static_cast<int64_t>(attachmentWidth)) &&
            (y + h <= static_cast<int64_t>(attachmentHeight));

        testScissorCall(
            t,
            success,
            static_cast<uint32_t>(x),
            static_cast<uint32_t>(y),
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            attachmentWidth,
            attachmentHeight);
    });

// ---------------------------------------------------------------------------
// setBlendConstant
// Test that almost any color value is valid for setBlendConstant.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setBlendConstant")
    .desc("Test that almost any color value is valid for setBlendConstant")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"r", double(1.0)},  {"g", double(1.0)},  {"b", double(1.0)},  {"a", double(1.0)}},
                ParamRecord{{"r", double(-1.0)}, {"g", double(-1.0)}, {"b", double(-1.0)}, {"a", double(-1.0)}},
                // Number.MAX_SAFE_INTEGER, Number.MIN_SAFE_INTEGER, -0, 100000
                ParamRecord{{"r", double(9007199254740991.0)}, {"g", double(-9007199254740991.0)}, {"b", double(-0.0)}, {"a", double(100000.0)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const double r = t.param<double>("r");
        const double g = t.param<double>("g");
        const double b = t.param<double>("b");
        const double a = t.param<double>("a");

        DummyRenderPassEncoders encoders = createDummyRenderPassEncoder(t);
        WGPUColor color = WGPUColor{r, g, b, a};
        wgpuRenderPassEncoderSetBlendConstant(encoders.pass, &color);
        wgpuRenderPassEncoderEnd(encoders.pass);
        wgpuRenderPassEncoderRelease(encoders.pass);
        // setBlendConstant is always valid; finish() must succeed.
        t.finishTracked(encoders.encoder);
    });

// ---------------------------------------------------------------------------
// setStencilReference
// Test that almost any stencil reference value is valid for setStencilReference.
// ---------------------------------------------------------------------------
CTS_TEST(g, "setStencilReference")
    .desc("Test that almost any stencil reference value is valid for setStencilReference")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"value", int64_t(1)}},
                ParamRecord{{"value", int64_t(0)}},
                ParamRecord{{"value", int64_t(1000)}},
                ParamRecord{{"value", int64_t(0xffffffff)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t value = static_cast<uint32_t>(t.param<int64_t>("value"));

        DummyRenderPassEncoders encoders = createDummyRenderPassEncoder(t);
        wgpuRenderPassEncoderSetStencilReference(encoders.pass, value);
        wgpuRenderPassEncoderEnd(encoders.pass);
        wgpuRenderPassEncoderRelease(encoders.pass);
        // setStencilReference is always valid; finish() must succeed.
        t.finishTracked(encoders.encoder);
    });

} // namespace
