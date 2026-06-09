// Ported from gpuweb/cts src/webgpu/api/validation/encoding/beginComputePass.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// kQueryTypes mirrors upstream kQueryTypes = ['occlusion', 'timestamp'].
// Occlusion is always available; timestamp requires the feature.
static std::vector<Value> kQueryTypeValues() {
    return {std::string("occlusion"), std::string("timestamp")};
}

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,beginComputePass",
    "Tests for validation in beginComputePass and GPUComputePassDescriptor as its optional descriptor.");

// ---------------------------------------------------------------------------
// Helper: tryComputePass
// Mirrors the upstream F.tryComputePass(success, descriptor):
//   create a command encoder, begin a compute pass, end it, then
//   expect a validation error on encoder.finish() iff !success.
// ---------------------------------------------------------------------------
static void tryComputePass(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    WGPUComputePassDescriptor& descriptor)
{
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &descriptor);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    t.expectValidationError([&] {
        t.finishTracked(encoder);
    }, !success);
}

// ---------------------------------------------------------------------------
// Helper: createQuerySetWithState
// Mirrors vtu.createQuerySetWithState(t, querySetState, {type: 'timestamp', count: N}).
// 'valid'   -> create and return a valid query set (caller must release).
// 'invalid' -> create an invalid (error) query set inside an error scope
//              by using an excessively large count (>4096) (caller must release).
// ---------------------------------------------------------------------------
static WGPUQuerySet createQuerySetWithState(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& state,
    WGPUQueryType type,
    uint32_t count)
{
    if (state == "valid") {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = type;
        desc.count = count;
        return wgpuDeviceCreateQuerySet(t.device(), &desc);
    }

    // 'invalid': force a validation error to produce an invalid (error) query set object.
    WGPUQuerySet qs = nullptr;
    t.expectValidationError([&] {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = type;
        desc.count = 4097; // > kMaxQueryCount (4096)
        qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
    }, true);
    return qs;
}

// ---------------------------------------------------------------------------
// Test: timestampWrites,query_set_type
// Test that all entries of timestampWrites must have type 'timestamp'.
// If the query type is not 'timestamp', a validation error should be generated.
// ---------------------------------------------------------------------------
CTS_TEST(g, "timestampWrites,query_set_type")
    .desc(
        "Test that all entries of the timestampWrites must have type 'timestamp'. "
        "If all query types are not 'timestamp' in GPUComputePassDescriptor, "
        "a validation error should be generated.")
    .params([](ParamsBuilder u) {
        return u.combine("queryType", kQueryTypeValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string queryType = t.param<std::string>("queryType");

        // Skip if the device doesn't support the required query type.
        if (queryType == "timestamp") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
                t.skip("timestamp-query feature not available");
            }
        }
        // Occlusion queries are always supported (no feature gate needed).

        const bool isValid = (queryType == "timestamp");

        const WGPUQueryType wgpuQueryType =
            (queryType == "occlusion") ? WGPUQueryType_Occlusion : WGPUQueryType_Timestamp;

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = wgpuQueryType;
        qsDesc.count = 2;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        tw.querySet = querySet;
        tw.beginningOfPassWriteIndex = 0;
        tw.endOfPassWriteIndex = 1;

        WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        descriptor.timestampWrites = &tw;

        tryComputePass(t, isValid, descriptor);

        if (querySet != nullptr) {
            wgpuQuerySetRelease(querySet);
        }
    });

// ---------------------------------------------------------------------------
// Test: timestampWrites,invalid_query_set
// Tests that a timestampWrite with an invalid query set generates a validation error.
// ---------------------------------------------------------------------------
CTS_TEST(g, "timestampWrites,invalid_query_set")
    .desc("Tests that timestampWrite that has an invalid query set generates a validation error.")
    .params([](ParamsBuilder u) {
        return u.combine("querySetState", {std::string("valid"), std::string("invalid")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }

        const std::string querySetState = t.param<std::string>("querySetState");

        WGPUQuerySet querySet = createQuerySetWithState(t, querySetState, WGPUQueryType_Timestamp, 1);

        WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        tw.querySet = querySet;
        tw.beginningOfPassWriteIndex = 0;
        // endOfPassWriteIndex left as WGPU_QUERY_SET_INDEX_UNDEFINED (from INIT)

        WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        descriptor.timestampWrites = &tw;

        tryComputePass(t, querySetState == "valid", descriptor);

        if (querySet != nullptr) {
            wgpuQuerySetRelease(querySet);
        }
    });

// ---------------------------------------------------------------------------
// Test: timestampWrites,query_index
// Test that querySet.count should be greater than timestampWrite.queryIndex,
// and that the query indexes are unique.
// ---------------------------------------------------------------------------
CTS_TEST(g, "timestampWrites,query_index")
    .desc(
        "Test that querySet.count should be greater than timestampWrite.queryIndex, "
        "and that the query indexes are unique.")
    .params([](ParamsBuilder u) {
        // Upstream uses paramsSubcasesOnly, which in C++ maps to beginSubcases before combines.
        // The valid index values are: undefined (WGPU_QUERY_SET_INDEX_UNDEFINED), 0, 1, 2, 3.
        // We encode 'undefined' as -1 (sentinel), actual values as non-negative ints.
        return u.beginSubcases()
            .combine("beginningOfPassWriteIndex", {Value(-1), Value(0), Value(1), Value(2), Value(3)})
            .combine("endOfPassWriteIndex", {Value(-1), Value(0), Value(1), Value(2), Value(3)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }

        const int beginningRaw = t.param<int>("beginningOfPassWriteIndex");
        const int endRaw       = t.param<int>("endOfPassWriteIndex");

        // -1 sentinel maps to WGPU_QUERY_SET_INDEX_UNDEFINED.
        const uint32_t beginningOfPassWriteIndex =
            (beginningRaw < 0) ? WGPU_QUERY_SET_INDEX_UNDEFINED
                               : static_cast<uint32_t>(beginningRaw);
        const uint32_t endOfPassWriteIndex =
            (endRaw < 0) ? WGPU_QUERY_SET_INDEX_UNDEFINED
                         : static_cast<uint32_t>(endRaw);

        constexpr uint32_t querySetCount = 2;

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = WGPUQueryType_Timestamp;
        qsDesc.count = querySetCount;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

        // Upstream isValid logic (undefined == WGPU_QUERY_SET_INDEX_UNDEFINED):
        //   beginningOfPassWriteIndex !== endOfPassWriteIndex &&
        //   (beginningOfPassWriteIndex === undefined || beginningOfPassWriteIndex < querySetCount) &&
        //   (endOfPassWriteIndex === undefined || endOfPassWriteIndex < querySetCount)
        const bool beginIsUndef = (beginningOfPassWriteIndex == WGPU_QUERY_SET_INDEX_UNDEFINED);
        const bool endIsUndef   = (endOfPassWriteIndex == WGPU_QUERY_SET_INDEX_UNDEFINED);

        const bool isValid =
            (beginningOfPassWriteIndex != endOfPassWriteIndex) &&
            (beginIsUndef || beginningOfPassWriteIndex < querySetCount) &&
            (endIsUndef   || endOfPassWriteIndex < querySetCount);

        WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        tw.querySet                   = querySet;
        tw.beginningOfPassWriteIndex  = beginningOfPassWriteIndex;
        tw.endOfPassWriteIndex        = endOfPassWriteIndex;

        WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        descriptor.timestampWrites = &tw;

        tryComputePass(t, isValid, descriptor);

        if (querySet != nullptr) {
            wgpuQuerySetRelease(querySet);
        }
    });

// ---------------------------------------------------------------------------
// Test: timestamp_query_set,device_mismatch
// Tests that beginComputePass cannot be called with a timestamp query set
// created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "timestamp_query_set,device_mismatch")
    .desc(
        "Tests beginComputePass cannot be called with a timestamp query set "
        "created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available");
        }

        const bool mismatched = t.param<bool>("mismatched");

        // Use mismatchedDevice or main device depending on test case.
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        // The query set is created on sourceDevice; that device must have the
        // timestamp-query feature, otherwise creating the query set raises an
        // uncaptured error rather than the validation error the test is checking.
        if (!wgpuDeviceHasFeature(sourceDevice, WGPUFeatureName_TimestampQuery)) {
            t.skip("timestamp-query feature not available on source device");
        }

        WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qsDesc.type = WGPUQueryType_Timestamp;
        qsDesc.count = 1;
        WGPUQuerySet timestampQuerySet = wgpuDeviceCreateQuerySet(sourceDevice, &qsDesc);

        WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        tw.querySet                  = timestampQuerySet;
        tw.beginningOfPassWriteIndex = 0;
        // endOfPassWriteIndex left as WGPU_QUERY_SET_INDEX_UNDEFINED (from INIT)

        WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        descriptor.timestampWrites = &tw;

        tryComputePass(t, !mismatched, descriptor);

        if (timestampQuerySet != nullptr) {
            wgpuQuerySetRelease(timestampQuerySet);
        }
    });

} // namespace
