// Ported from gpuweb/cts src/webgpu/api/validation/buffer/mapping.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,buffer,mapping",
    "Validation tests for GPUBuffer.mapAsync.");

WGPUBuffer createMappableBuffer(GpuTest& t, WGPUMapMode mode, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = mode == WGPUMapMode_Read ? WGPUBufferUsage_MapRead : WGPUBufferUsage_MapWrite;
    return t.createBufferTracked(desc);
}

CTS_TEST(g, "mapAsync,usage")
    .desc("Test the usage validation for mapAsync.")
    .params([](ParamsBuilder u) {
        std::vector<Value> usages;
        usages.reserve(kBufferUsages.size());
        for (WGPUBufferUsage usage : kBufferUsages) {
            usages.emplace_back(static_cast<int64_t>(usage));
        }
        return u.beginSubcases()
            .combineWithParams({
                ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Read)},
                            {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapRead)}},
                ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Write)},
                            {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapWrite)}},
                ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_None)}, {"validUsage", 0}},
            })
            .combine("usage", std::move(usages));
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const WGPUBufferUsage validUsage = t.param<WGPUBufferUsage>("validUsage");
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("usage");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = usage;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        const bool expectSuccess = mapMode != WGPUMapMode_None && usage == validUsage;
        t.expectMapAsync(buffer, mapMode, expectSuccess);
        if (expectSuccess) {
            wgpuBufferUnmap(buffer);
        }
    });

CTS_TEST(g, "mapAsync,invalidBuffer")
    .desc("Test that mapAsync is an error when called on an invalid buffer.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", {
            static_cast<int64_t>(WGPUMapMode_Read),
            static_cast<int64_t>(WGPUMapMode_Write),
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = t.getErrorBuffer();
        t.expectMapAsync(buffer, mapMode, false);
    });

CTS_TEST(g, "mapAsync,state,destroyed")
    .desc("Test that mapAsync is an error when called on a destroyed buffer.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", {
            static_cast<int64_t>(WGPUMapMode_Read),
            static_cast<int64_t>(WGPUMapMode_Write),
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);

        wgpuBufferDestroy(buffer);
        t.expectMapAsync(buffer, mapMode, false);
    });

CTS_TEST(g, "mapAsync,state,mappedAtCreation")
    .desc("Test that mapAsync is an error on a mapped-at-creation buffer, but succeeds after unmapping it.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Read)},
                        {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapRead)}},
            ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Write)},
                        {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapWrite)}},
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const WGPUBufferUsage validUsage = t.param<WGPUBufferUsage>("validUsage");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = validUsage;
        desc.mappedAtCreation = WGPU_TRUE;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        t.expectMapAsync(buffer, mapMode, false);
        wgpuBufferUnmap(buffer);
        t.expectMapAsync(buffer, mapMode, true);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "mapAsync,state,mapped")
    .desc("Test that mapAsync is an error on a mapped buffer, but succeeds after unmapping it.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", {
            static_cast<int64_t>(WGPUMapMode_Read),
            static_cast<int64_t>(WGPUMapMode_Write),
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);

        t.expectMapAsync(buffer, mapMode, true);
        t.expectMapAsync(buffer, mapMode, false);
        wgpuBufferUnmap(buffer);
        t.expectMapAsync(buffer, mapMode, true);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "mapAsync,state,mappingPending")
    .desc("Test that mapAsync is rejected when called on a buffer that is being mapped.")
    .unimplemented("JS microtask/pending-map timing");

} // namespace
