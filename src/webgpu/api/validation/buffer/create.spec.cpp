// Ported from gpuweb/cts src/webgpu/api/validation/buffer/create.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,buffer,create",
    "Tests for validation in createBuffer.");

CTS_TEST(g, "size")
    .desc("Test buffer size alignment is validated to be a multiple of 4 if mappedAtCreation is true.")
    .unimplemented("RangeError behavior has no direct C-API analog in this slice");

CTS_TEST(g, "limit")
    .desc("Test buffer size is validated against maxBufferSize.")
    .params([](ParamsBuilder u) {
        return u.combine("sizeAddition", {-1, 0, 1});
    })
    .fn([](GpuTest& t) {
        const int sizeAddition = t.param<int>("sizeAddition");
        const WGPULimits limits = t.getLimits();
        const uint64_t size = static_cast<uint64_t>(static_cast<int64_t>(limits.maxBufferSize) + sizeAddition);
        const bool isValid = size <= limits.maxBufferSize;

        t.expectValidationError([&] {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = size;
            desc.usage = WGPUBufferUsage_CopySrc;
            t.createBufferTracked(desc);
        }, !isValid);
    });

CTS_TEST(g, "usage")
    .desc("Test combinations of zero to two usage flags are validated to be valid.")
    .unimplemented("requires ParamsBuilder::filter, deferred");

CTS_TEST(g, "new_usages")
    .desc("Valid usages not present in GPUBufferUsage should not be accepted by createBuffer().")
    .params([](ParamsBuilder u) {
        std::vector<Value> usages;
        usages.reserve(kBufferUsages.size());
        for (WGPUBufferUsage usage : kBufferUsages) {
            usages.emplace_back(static_cast<int64_t>(usage));
        }
        return u.beginSubcases().combine("usage", std::move(usages));
    })
    .fn([](GpuTest& t) {
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("usage");
        t.expectValidationError([&] {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = 16;
            desc.usage = usage;
            t.createBufferTracked(desc);
        }, false);
    });

CTS_TEST(g, "createBuffer_invalid_and_oom")
    .desc("Validation should be more severe than OOM for invalid mappable buffers.")
    .unimplemented("requires combineWithParams and OOM scope, deferred");

} // namespace
