// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/clearBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,encoding,cmds,clearBuffer",
    "API validation tests for clearBuffer.");

uint64_t paramOrUndefined(GpuTest& t, std::string_view key, uint64_t undefinedValue) {
    if (!t.hasParam(key) || t.paramIsUndefined(key)) {
        return undefinedValue;
    }
    return t.param<uint64_t>(key);
}

WGPUBuffer createClearBuffer(GpuTest& t, uint64_t size, WGPUBufferUsage usage = WGPUBufferUsage_CopyDst) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

void testClearBuffer(GpuTest& t, WGPUBuffer buffer, uint64_t offset, uint64_t size, bool isSuccess) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderClearBuffer(encoder, buffer, offset, size);

    t.expectValidationError([&] {
        t.finishTracked(encoder);
    }, !isSuccess);
}

CTS_TEST(g, "buffer_state")
    .desc("Test that clearing an invalid or destroyed buffer fails.")
    .unimplemented("needs createBufferWithState and queue.submit");

CTS_TEST(g, "buffer,device_mismatch")
    .desc("Tests clearBuffer cannot be called with buffer created from another device.")
    .unimplemented("needs a second device");

CTS_TEST(g, "default_args")
    .desc("Test that calling clearBuffer with a default offset and size is valid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"offset", Value::undef()}, {"size", Value::undef()}},
            ParamRecord{{"offset", 4}, {"size", Value::undef()}},
            ParamRecord{{"offset", Value::undef()}, {"size", 8}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createClearBuffer(t, 16);
        testClearBuffer(
            t,
            buffer,
            paramOrUndefined(t, "offset", 0),
            paramOrUndefined(t, "size", WGPU_WHOLE_SIZE),
            true);
    });

CTS_TEST(g, "buffer_usage")
    .desc("Test that only buffers with COPY_DST usage are valid to use with clearBuffer.")
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
        WGPUBuffer buffer = createClearBuffer(t, 16, usage);
        testClearBuffer(t, buffer, 0, 16, usage == WGPUBufferUsage_CopyDst);
    });

CTS_TEST(g, "size_alignment")
    .desc("Test that the clear size must be 4 byte aligned.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"size", 0}, {"_isSuccess", true}},
            ParamRecord{{"size", 2}, {"_isSuccess", false}},
            ParamRecord{{"size", 4}, {"_isSuccess", true}},
            ParamRecord{{"size", 5}, {"_isSuccess", false}},
            ParamRecord{{"size", 8}, {"_isSuccess", true}},
            ParamRecord{{"size", 20}, {"_isSuccess", false}},
            ParamRecord{{"size", Value::undef()}, {"_isSuccess", true}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createClearBuffer(t, 16);
        testClearBuffer(
            t,
            buffer,
            0,
            paramOrUndefined(t, "size", WGPU_WHOLE_SIZE),
            t.param<bool>("_isSuccess"));
    });

CTS_TEST(g, "offset_alignment")
    .desc("Test that the clear offsets must be 4 byte aligned.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"offset", 0}, {"_isSuccess", true}},
            ParamRecord{{"offset", 2}, {"_isSuccess", false}},
            ParamRecord{{"offset", 4}, {"_isSuccess", true}},
            ParamRecord{{"offset", 5}, {"_isSuccess", false}},
            ParamRecord{{"offset", 8}, {"_isSuccess", true}},
            ParamRecord{{"offset", 20}, {"_isSuccess", false}},
            ParamRecord{{"offset", Value::undef()}, {"_isSuccess", true}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createClearBuffer(t, 16);
        testClearBuffer(
            t,
            buffer,
            paramOrUndefined(t, "offset", 0),
            8,
            t.param<bool>("_isSuccess"));
    });

CTS_TEST(g, "overflow")
    .desc("Test that clears which may cause arithmetic overflows are invalid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"offset", 0}, {"size", kMaxSafeMultipleOf8}},
            ParamRecord{{"offset", 16}, {"size", kMaxSafeMultipleOf8}},
            ParamRecord{{"offset", kMaxSafeMultipleOf8}, {"size", 16}},
            ParamRecord{{"offset", kMaxSafeMultipleOf8}, {"size", kMaxSafeMultipleOf8}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createClearBuffer(t, 16);
        testClearBuffer(t, buffer, t.param<uint64_t>("offset"), t.param<uint64_t>("size"), false);
    });

CTS_TEST(g, "out_of_bounds")
    .desc("Test that clears which exceed the buffer bounds are invalid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"offset", 0}, {"size", 32}, {"_isSuccess", true}},
            ParamRecord{{"offset", 0}, {"size", 36}},
            ParamRecord{{"offset", 32}, {"size", 0}, {"_isSuccess", true}},
            ParamRecord{{"offset", 32}, {"size", 4}},
            ParamRecord{{"offset", 36}, {"size", 4}},
            ParamRecord{{"offset", 36}, {"size", 0}},
            ParamRecord{{"offset", 20}, {"size", 16}},
            ParamRecord{{"offset", 20}, {"size", 12}, {"_isSuccess", true}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createClearBuffer(t, 32);
        const bool isSuccess = t.hasParam("_isSuccess") && t.param<bool>("_isSuccess");
        testClearBuffer(t, buffer, t.param<uint64_t>("offset"), t.param<uint64_t>("size"), isSuccess);
    });

} // namespace
