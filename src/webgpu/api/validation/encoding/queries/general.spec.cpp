// Ported from gpuweb/cts src/webgpu/api/validation/encoding/queries/general.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// createQuerySetWithType: mirrors the upstream common.ts createQuerySetWithType().
// Creates a WGPUQuerySet with the given type and count on the test device.
// Caller must release the returned WGPUQuerySet.
static WGPUQuerySet createQuerySetWithType(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUQueryType type,
    uint32_t count)
{
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type  = type;
    desc.count = count;
    return wgpuDeviceCreateQuerySet(t.device(), &desc);
}

// createQuerySetWithState: mirrors vtu.createQuerySetWithState(t, state) with
// default descriptor {type: occlusion, count: 2}.
// Caller must release the returned WGPUQuerySet.
static WGPUQuerySet createQuerySetWithState(
    AllFeaturesMaxLimitsGpuTest& t,
    ResourceState state)
{
    if (state == ResourceState::Valid) {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type  = WGPUQueryType_Occlusion;
        desc.count = 2;
        return wgpuDeviceCreateQuerySet(t.device(), &desc);
    }
    if (state == ResourceState::Destroyed) {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type  = WGPUQueryType_Occlusion;
        desc.count = 2;
        WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
        wgpuQuerySetDestroy(qs);
        return qs;
    }
    // Invalid: create with out-of-range count (> kMaxQueryCount = 4096).
    WGPUQuerySet qs = nullptr;
    t.expectValidationError([&] {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type  = WGPUQueryType_Occlusion;
        desc.count = 4097;
        qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
    }, true);
    return qs;
}

// beginRenderPassWithQuerySet: mirrors upstream common.ts beginRenderPassWithQuerySet().
// Creates a 16x16 RGBA8Unorm render-attachment texture and begins a render pass
// whose occlusionQuerySet is the supplied query set (may be nullptr).
// Returns the render pass encoder; the colour texture view is stored via t.createViewTracked.
static WGPURenderPassEncoder beginRenderPassWithQuerySet(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUCommandEncoder encoder,
    WGPUQuerySet querySet)
{
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size   = WGPUExtent3D{16, 16, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage  = WGPUTextureUsage_RenderAttachment;
    WGPUTexture colorTex = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTex, vDesc);

    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view       = colorView;
    colorAttach.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
    colorAttach.loadOp     = WGPULoadOp_Clear;
    colorAttach.storeOp    = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;
    passDesc.occlusionQuerySet    = querySet;

    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

// validateFinishAndSubmitGivenState: mirrors
//   CommandBufferMaker.validateFinishAndSubmitGivenState(state).
// - Invalid   → expectValidationError at finish()
// - Destroyed → expectValidationError at submit()
// - Valid     → both succeed
static void validateFinishAndSubmitGivenState(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUCommandEncoder encoder,
    ResourceState state)
{
    const bool shouldFinishSucceed = (state != ResourceState::Invalid);
    const bool shouldSubmitSucceed = (state == ResourceState::Valid);

    if (!shouldFinishSucceed) {
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = t.finishTracked(encoder);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, !shouldSubmitSucceed);
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,queries,general",
    "Validation for encoding queries.");

// ---------------------------------------------------------------------------
// occlusion_query,query_type
//
// Tests that set occlusion query set with all types in render pass descriptor:
// - type {occlusion (control case), timestamp}
// - {undefined} for occlusion query set in render pass descriptor
//
// ERROR MODEL: beginOcclusionQuery/endOcclusionQuery are encoder commands —
// validation errors are deferred to finish().  Wrap expectValidationError
// around finishTracked(), not around the command calls.
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query,query_type")
    .desc(
        "Tests that set occlusion query set with all types in render pass descriptor:\n"
        "- type {occlusion (control case), timestamp}\n"
        "- {undefined} for occlusion query set in render pass descriptor")
    .params([](ParamsBuilder u) {
        // kQueryTypes = ['occlusion', 'timestamp']; also include undefined (no query set).
        return u.combine("type", {
            Value::undef(),
            std::string("occlusion"),
            std::string("timestamp"),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isUndef    = t.paramIsUndefined("type");
        const std::string typeStr = isUndef ? "" : t.param<std::string>("type");

        // Skip timestamp variant if timestamp-query feature is not available.
        if (typeStr == "timestamp") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
                t.skip("timestamp-query feature not available");
            }
        }

        // Build the query set (nullptr when type is undefined).
        WGPUQuerySet querySet = nullptr;
        WGPUQueryType queryType = WGPUQueryType_Occlusion;
        if (!isUndef) {
            if (typeStr == "timestamp") {
                queryType = WGPUQueryType_Timestamp;
            }
            querySet = createQuerySetWithType(t, queryType, 1);
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPassWithQuerySet(t, encoder, querySet);

        // beginOcclusionQuery / endOcclusionQuery are encoder commands:
        // errors are deferred to finish().
        wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Valid only when type == 'occlusion'.
        const bool shouldSucceed = (!isUndef && typeStr == "occlusion");
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !shouldSucceed);

        if (querySet != nullptr) {
            wgpuQuerySetRelease(querySet);
        }
    });

// ---------------------------------------------------------------------------
// occlusion_query,invalid_query_set
//
// Tests that begin occlusion query with an invalid query set that failed
// during creation results in a validation error.
//
// ERROR MODEL: encoder commands (beginOcclusionQuery/endOcclusionQuery) are
// deferred.  Use validateFinishAndSubmitGivenState to distinguish invalid
// (finish error) from destroyed (submit error).
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query,invalid_query_set")
    .desc(
        "Tests that begin occlusion query with a invalid query set that failed during creation.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("querySetState", {
            std::string("valid"),
            std::string("invalid"),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state =
            parseResourceState(t.param<std::string>("querySetState"));

        WGPUQuerySet occlusionQuerySet = createQuerySetWithState(t, state);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPassWithQuerySet(t, encoder, occlusionQuerySet);

        wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        validateFinishAndSubmitGivenState(t, encoder, state);

        wgpuQuerySetRelease(occlusionQuerySet);
    });

// ---------------------------------------------------------------------------
// occlusion_query,query_index
//
// Tests that begin occlusion query with query index:
// - queryIndex {in, out of} range for GPUQuerySet
//
// ERROR MODEL: encoder command — deferred to finish().
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query,query_index")
    .desc(
        "Tests that begin occlusion query with query index:\n"
        "- queryIndex {in, out of} range for GPUQuerySet")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("queryIndex", {
            int64_t(0),  // in range  (valid)
            int64_t(2),  // out of range (count = 2, valid indices are 0..1)
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t queryIndex = static_cast<uint32_t>(t.param<int64_t>("queryIndex"));

        WGPUQuerySet occlusionQuerySet = createQuerySetWithType(t, WGPUQueryType_Occlusion, 2);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPassWithQuerySet(t, encoder, occlusionQuerySet);

        wgpuRenderPassEncoderBeginOcclusionQuery(pass, queryIndex);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // queryIndex < 2 is valid; queryIndex == 2 is out of range.
        const bool shouldSucceed = (queryIndex < 2);
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !shouldSucceed);

        wgpuQuerySetRelease(occlusionQuerySet);
    });

} // namespace
