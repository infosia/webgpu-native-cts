// Ported from gpuweb/cts src/webgpu/api/validation/encoding/queries/resolveQuerySet.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// kQueryCount mirrors upstream kQueryCount = 2.
constexpr uint32_t kQueryCount = 2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a WGPUQuerySet in the given resource state.
// 'valid'     -> create normally (count = kQueryCount, type = occlusion)
// 'invalid'   -> force an error query set (count > 4096 triggers validation error)
// 'destroyed' -> create normally then destroy
// Caller must release the returned WGPUQuerySet.
static WGPUQuerySet createQuerySetWithState(
    AllFeaturesMaxLimitsGpuTest& t,
    ResourceState state)
{
    if (state == ResourceState::Valid) {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = WGPUQueryType_Occlusion;
        desc.count = kQueryCount;
        return wgpuDeviceCreateQuerySet(t.device(), &desc);
    }
    if (state == ResourceState::Destroyed) {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = WGPUQueryType_Occlusion;
        desc.count = kQueryCount;
        WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
        wgpuQuerySetDestroy(qs);
        return qs;
    }
    // Invalid: use an out-of-range count to produce an error object.
    WGPUQuerySet qs = nullptr;
    t.expectValidationError([&] {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = WGPUQueryType_Occlusion;
        desc.count = 4097; // > kMaxQueryCount (4096)
        qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
    }, true);
    return qs;
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,queries,resolveQuerySet",
    "Validation tests for resolveQuerySet.");

// ---------------------------------------------------------------------------
// queryset_and_destination_buffer_state
//
// Tests that resolve query set must be with valid query set and destination buffer.
// - {invalid, destroyed} GPUQuerySet results in validation error.
// - {invalid, destroyed} destination buffer results in validation error.
//
// EAGER ERROR NOTE:
//   In the native C API, wgpuCommandEncoderResolveQuerySet with an invalid
//   (error) queryset or buffer fires the validation error eagerly at that call,
//   tainting the encoder. finish() then also errors because the encoder is
//   tainted. For 'destroyed' resources the error surfaces at submit time.
//   This matches the JS 'invalid → finish error, destroyed → submit error'
//   semantics of validateFinishAndSubmit(shouldBeValid, shouldSubmitSuccess).
// ---------------------------------------------------------------------------
CTS_TEST(g, "queryset_and_destination_buffer_state")
    .desc(
        "Tests that resolve query set must be with valid query set and destination buffer.\n"
        "- {invalid, destroyed} GPUQuerySet results in validation error.\n"
        "- {invalid, destroyed} destination buffer results in validation error.")
    .params([](ParamsBuilder u) {
        return u
            .combine("querySetState", resourceStateValues())
            .combine("destinationState", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState querySetState    = parseResourceState(t.param<std::string>("querySetState"));
        const ResourceState destinationState = parseResourceState(t.param<std::string>("destinationState"));

        // shouldBeValid: finish succeeds iff neither resource is invalid (error object).
        const bool shouldBeValid =
            querySetState    != ResourceState::Invalid &&
            destinationState != ResourceState::Invalid;

        // shouldSubmitSuccess: submit succeeds iff both resources were fully valid (not destroyed).
        const bool shouldSubmitSuccess =
            querySetState    == ResourceState::Valid &&
            destinationState == ResourceState::Valid;

        WGPUQuerySet querySet = createQuerySetWithState(t, querySetState);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = static_cast<uint64_t>(kQueryCount) * 8;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer destination = t.createBufferWithState(destinationState, bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        // For invalid resources the error fires eagerly at this call.
        if (!shouldBeValid) {
            t.expectValidationError([&] {
                wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, destination, 0);
            }, true);
            // finish() will also error because the encoder is tainted.
            t.expectValidationError([&] {
                t.finishTracked(encoder);
            }, true);
        } else {
            wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, destination, 0);
            WGPUCommandBuffer cb = t.finishTracked(encoder);
            // submit errors iff a resource was destroyed.
            t.expectValidationError([&] {
                wgpuQueueSubmit(t.queue(), 1, &cb);
            }, !shouldSubmitSuccess);
        }

        wgpuQuerySetRelease(querySet);
    });

// ---------------------------------------------------------------------------
// first_query_and_query_count
//
// Tests that resolve query set with invalid firstQuery and queryCount:
// - firstQuery and/or queryCount out of range.
//
// Error is a finish-time error (recorded in encoder state).
// ---------------------------------------------------------------------------
CTS_TEST(g, "first_query_and_query_count")
    .desc(
        "Tests that resolve query set with invalid firstQuery and queryCount:\n"
        "- firstQuery and/or queryCount out of range")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            // control case: firstQuery=0, queryCount=2 (= kQueryCount) — valid
            ParamRecord{{"firstQuery", int64_t(0)}, {"queryCount", int64_t(kQueryCount)}},
            // out of range: firstQuery=0, queryCount=3 (> kQueryCount)
            ParamRecord{{"firstQuery", int64_t(0)}, {"queryCount", int64_t(kQueryCount + 1)}},
            // out of range: firstQuery=1, queryCount=2 (1+2 > kQueryCount)
            ParamRecord{{"firstQuery", int64_t(1)}, {"queryCount", int64_t(kQueryCount)}},
            // out of range: firstQuery=2 (= kQueryCount), queryCount=1
            ParamRecord{{"firstQuery", int64_t(kQueryCount)}, {"queryCount", int64_t(1)}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t firstQuery  = static_cast<uint32_t>(t.param<int64_t>("firstQuery"));
        const uint32_t queryCount  = static_cast<uint32_t>(t.param<int64_t>("queryCount"));
        const bool     shouldError = (firstQuery + queryCount > kQueryCount);

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = kQueryCount;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = static_cast<uint64_t>(kQueryCount) * 8;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer destination = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, firstQuery, queryCount, destination, 0);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, shouldError);

        wgpuQuerySetRelease(querySet);
    });

// ---------------------------------------------------------------------------
// destination_buffer_usage
//
// Tests that resolve query set with invalid destinationBuffer:
// - Buffer usage {with, without} QUERY_RESOLVE.
//
// Error is a finish-time error.
// ---------------------------------------------------------------------------
CTS_TEST(g, "destination_buffer_usage")
    .desc(
        "Tests that resolve query set with invalid destinationBuffer:\n"
        "- Buffer usage {with, without} QUERY_RESOLVE")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("bufferUsage", {
            // missing QUERY_RESOLVE
            static_cast<int64_t>(WGPUBufferUsage_Storage),
            // control case: has QUERY_RESOLVE
            static_cast<int64_t>(WGPUBufferUsage_QueryResolve),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferUsage bufferUsage = t.param<WGPUBufferUsage>("bufferUsage");

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = kQueryCount;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = static_cast<uint64_t>(kQueryCount) * 8;
        bufDesc.usage = bufferUsage;
        WGPUBuffer destination = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, kQueryCount, destination, 0);

        const bool shouldError = (bufferUsage != WGPUBufferUsage_QueryResolve);
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, shouldError);

        wgpuQuerySetRelease(querySet);
    });

// ---------------------------------------------------------------------------
// destination_offset_alignment
//
// Tests that resolve query set with invalid destinationOffset:
// - destinationOffset is not a multiple of 256.
//
// Error is a finish-time error.
// ---------------------------------------------------------------------------
CTS_TEST(g, "destination_offset_alignment")
    .desc(
        "Tests that resolve query set with invalid destinationOffset:\n"
        "- destinationOffset is not a multiple of 256")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("destinationOffset", {
            int64_t(0),
            int64_t(128),
            int64_t(256),
            int64_t(384),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t destinationOffset = t.param<uint64_t>("destinationOffset");

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = kQueryCount;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 512;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer destination = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, kQueryCount, destination, destinationOffset);

        const bool shouldError = (destinationOffset % 256 != 0);
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, shouldError);

        wgpuQuerySetRelease(querySet);
    });

// ---------------------------------------------------------------------------
// resolve_buffer_oob
//
// Tests that resolve query set with the size oob:
// - The size of destinationBuffer - destinationOffset < queryCount * 8.
//
// Error is a finish-time error.
// ---------------------------------------------------------------------------
CTS_TEST(g, "resolve_buffer_oob")
    .desc(
        "Tests that resolve query set with the size oob:\n"
        "- The size of destinationBuffer - destinationOffset < queryCount * 8")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"queryCount", int64_t(2)}, {"bufferSize", int64_t(16)},  {"destinationOffset", int64_t(0)},   {"_success", true}},
            ParamRecord{{"queryCount", int64_t(3)}, {"bufferSize", int64_t(16)},  {"destinationOffset", int64_t(0)},   {"_success", false}},
            ParamRecord{{"queryCount", int64_t(2)}, {"bufferSize", int64_t(16)},  {"destinationOffset", int64_t(256)}, {"_success", false}},
            ParamRecord{{"queryCount", int64_t(2)}, {"bufferSize", int64_t(272)}, {"destinationOffset", int64_t(256)}, {"_success", true}},
            ParamRecord{{"queryCount", int64_t(2)}, {"bufferSize", int64_t(264)}, {"destinationOffset", int64_t(256)}, {"_success", false}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t queryCount         = static_cast<uint32_t>(t.param<int64_t>("queryCount"));
        const uint64_t bufferSize         = t.param<uint64_t>("bufferSize");
        const uint64_t destinationOffset  = t.param<uint64_t>("destinationOffset");
        const bool     success            = t.param<bool>("_success");

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = queryCount;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = bufferSize;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer destination = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, queryCount, destination, destinationOffset);

        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !success);

        wgpuQuerySetRelease(querySet);
    });

// ---------------------------------------------------------------------------
// query_set_buffer,device_mismatch
//
// Tests resolveQuerySet cannot be called with a query set or destination
// buffer created from another device.
//
// Error is a finish-time error (device mismatch is caught at finish).
// ---------------------------------------------------------------------------
CTS_TEST(g, "query_set_buffer,device_mismatch")
    .desc(
        "Tests resolveQuerySet cannot be called with a query set or destination "
        "buffer created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            // control case: both from main device
            ParamRecord{{"querySetMismatched", false}, {"bufferMismatched", false}},
            // query set from mismatched device
            ParamRecord{{"querySetMismatched", true},  {"bufferMismatched", false}},
            // buffer from mismatched device
            ParamRecord{{"querySetMismatched", false}, {"bufferMismatched", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool querySetMismatched = t.param<bool>("querySetMismatched");
        const bool bufferMismatched   = t.param<bool>("bufferMismatched");

        constexpr uint32_t kLocalQueryCount = 1;

        // Create query set on the appropriate device.
        WGPUDevice querySetDevice = querySetMismatched ? t.mismatchedDevice() : t.device();
        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type  = WGPUQueryType_Occlusion;
        qsDesc.count = kLocalQueryCount;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(querySetDevice, &qsDesc);

        // Create buffer on the appropriate device.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = static_cast<uint64_t>(kLocalQueryCount) * 8;
        bufDesc.usage = WGPUBufferUsage_QueryResolve;
        WGPUBuffer buffer = bufferMismatched
            ? t.createBufferOnMismatchedDevice(bufDesc)
            : t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, kLocalQueryCount, buffer, 0);

        const bool shouldError = querySetMismatched || bufferMismatched;
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, shouldError);

        wgpuQuerySetRelease(querySet);
    });

} // namespace
