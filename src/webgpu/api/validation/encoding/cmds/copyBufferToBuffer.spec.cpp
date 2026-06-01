// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/copyBufferToBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,encoding,cmds,copyBufferToBuffer",
    "copyBufferToBuffer tests.");

std::vector<Value> bufferUsageValues() {
    std::vector<Value> usages;
    usages.reserve(kBufferUsages.size());
    for (WGPUBufferUsage usage : kBufferUsages) {
        usages.emplace_back(static_cast<int64_t>(usage));
    }
    return usages;
}

WGPUBuffer createCopyBuffer(GpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

std::vector<Value> resourceStateValues() {
    std::vector<Value> values;
    values.reserve(kResourceStates.size());
    for (ResourceState state : kResourceStates) {
        values.emplace_back(static_cast<int64_t>(state));
    }
    return values;
}

enum class CommandExpectation {
    Success,
    FinishError,
    SubmitError,
};

void testCopyBufferToBuffer(
    GpuTest& t,
    WGPUBuffer src,
    uint64_t srcOffset,
    WGPUBuffer dst,
    uint64_t dstOffset,
    uint64_t copySize,
    CommandExpectation expectation) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyBufferToBuffer(encoder, src, srcOffset, dst, dstOffset, copySize);

    if (expectation == CommandExpectation::FinishError) {
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, true);
        return;
    }

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    }, expectation == CommandExpectation::SubmitError);
}

void testCopyBufferToBuffer(
    GpuTest& t,
    WGPUBuffer src,
    uint64_t srcOffset,
    WGPUBuffer dst,
    uint64_t dstOffset,
    uint64_t copySize,
    bool isSuccess) {
    testCopyBufferToBuffer(
        t,
        src,
        srcOffset,
        dst,
        dstOffset,
        copySize,
        isSuccess ? CommandExpectation::Success : CommandExpectation::FinishError);
}

CTS_TEST(g, "buffer_state")
    .desc("Test that copying an invalid or destroyed buffer fails.")
    .params([](ParamsBuilder u) {
        return u.combine("srcBufferState", resourceStateValues()).combine("dstBufferState", resourceStateValues());
    })
    .fn([](GpuTest& t) {
        const ResourceState srcState = static_cast<ResourceState>(t.param<int64_t>("srcBufferState"));
        const ResourceState dstState = static_cast<ResourceState>(t.param<int64_t>("dstBufferState"));

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer src = t.createBufferWithState(srcState, desc);
        WGPUBuffer dst = t.createBufferWithState(dstState, desc);

        CommandExpectation expectation = CommandExpectation::SubmitError;
        if (srcState == ResourceState::Invalid || dstState == ResourceState::Invalid) {
            expectation = CommandExpectation::FinishError;
        } else if (srcState == ResourceState::Valid && dstState == ResourceState::Valid) {
            expectation = CommandExpectation::Success;
        }

        testCopyBufferToBuffer(t, src, 0, dst, 0, 8, expectation);
    });

CTS_TEST(g, "buffer,device_mismatch")
    .desc("Tests copyBufferToBuffer cannot be called with src buffer or dst buffer created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"srcMismatched", false}, {"dstMismatched", false}},
            ParamRecord{{"srcMismatched", true}, {"dstMismatched", false}},
            ParamRecord{{"srcMismatched", false}, {"dstMismatched", true}},
        });
    })
    .fn([](GpuTest& t) {
        const bool srcMismatched = t.param<bool>("srcMismatched");
        const bool dstMismatched = t.param<bool>("dstMismatched");

        WGPUBufferDescriptor srcDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        srcDesc.size = 16;
        srcDesc.usage = WGPUBufferUsage_CopySrc;
        WGPUBuffer src = srcMismatched
            ? t.createBufferOnMismatchedDevice(srcDesc)
            : t.createBufferTracked(srcDesc);

        WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        dstDesc.size = 16;
        dstDesc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer dst = dstMismatched
            ? t.createBufferOnMismatchedDevice(dstDesc)
            : t.createBufferTracked(dstDesc);

        testCopyBufferToBuffer(t, src, 0, dst, 0, 8, !(srcMismatched || dstMismatched));
    });

CTS_TEST(g, "buffer_usage")
    .desc("Test that source requires COPY_SRC usage and destination requires COPY_DST usage.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("srcUsage", bufferUsageValues())
            .combine("dstUsage", bufferUsageValues());
    })
    .fn([](GpuTest& t) {
        const WGPUBufferUsage srcUsage = t.param<WGPUBufferUsage>("srcUsage");
        const WGPUBufferUsage dstUsage = t.param<WGPUBufferUsage>("dstUsage");
        WGPUBuffer src = createCopyBuffer(t, 16, srcUsage);
        WGPUBuffer dst = createCopyBuffer(t, 16, dstUsage);
        const bool isSuccess = srcUsage == WGPUBufferUsage_CopySrc && dstUsage == WGPUBufferUsage_CopyDst;
        testCopyBufferToBuffer(t, src, 0, dst, 0, 8, isSuccess);
    });

CTS_TEST(g, "copy_size_alignment")
    .desc("Test that copySize must be 4 byte aligned.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"copySize", 0}, {"_isSuccess", true}},
            ParamRecord{{"copySize", 2}, {"_isSuccess", false}},
            ParamRecord{{"copySize", 4}, {"_isSuccess", true}},
            ParamRecord{{"copySize", 5}, {"_isSuccess", false}},
            ParamRecord{{"copySize", 8}, {"_isSuccess", true}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer src = createCopyBuffer(t, 16, WGPUBufferUsage_CopySrc);
        WGPUBuffer dst = createCopyBuffer(t, 16, WGPUBufferUsage_CopyDst);
        testCopyBufferToBuffer(t, src, 0, dst, 0, t.param<uint64_t>("copySize"), t.param<bool>("_isSuccess"));
    });

CTS_TEST(g, "copy_offset_alignment")
    .desc("Test that copy offsets must be 4 byte aligned.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 0}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 2}, {"dstOffset", 0}, {"_isSuccess", false}},
            ParamRecord{{"srcOffset", 4}, {"dstOffset", 0}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 5}, {"dstOffset", 0}, {"_isSuccess", false}},
            ParamRecord{{"srcOffset", 8}, {"dstOffset", 0}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 2}, {"_isSuccess", false}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 4}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 5}, {"_isSuccess", false}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 8}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 4}, {"dstOffset", 4}, {"_isSuccess", true}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer src = createCopyBuffer(t, 16, WGPUBufferUsage_CopySrc);
        WGPUBuffer dst = createCopyBuffer(t, 16, WGPUBufferUsage_CopyDst);
        testCopyBufferToBuffer(
            t,
            src,
            t.param<uint64_t>("srcOffset"),
            dst,
            t.param<uint64_t>("dstOffset"),
            8,
            t.param<bool>("_isSuccess"));
    });

CTS_TEST(g, "copy_overflow")
    .desc("Test that copies which may cause arithmetic overflows are invalid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 0}, {"copySize", kMaxSafeMultipleOf8}},
            ParamRecord{{"srcOffset", 16}, {"dstOffset", 0}, {"copySize", kMaxSafeMultipleOf8}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 16}, {"copySize", kMaxSafeMultipleOf8}},
            ParamRecord{{"srcOffset", kMaxSafeMultipleOf8}, {"dstOffset", 0}, {"copySize", 16}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", kMaxSafeMultipleOf8}, {"copySize", 16}},
            ParamRecord{{"srcOffset", kMaxSafeMultipleOf8}, {"dstOffset", 0}, {"copySize", kMaxSafeMultipleOf8}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", kMaxSafeMultipleOf8}, {"copySize", kMaxSafeMultipleOf8}},
            ParamRecord{{"srcOffset", kMaxSafeMultipleOf8}, {"dstOffset", kMaxSafeMultipleOf8}, {"copySize", kMaxSafeMultipleOf8}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer src = createCopyBuffer(t, 16, WGPUBufferUsage_CopySrc);
        WGPUBuffer dst = createCopyBuffer(t, 16, WGPUBufferUsage_CopyDst);
        testCopyBufferToBuffer(
            t,
            src,
            t.param<uint64_t>("srcOffset"),
            dst,
            t.param<uint64_t>("dstOffset"),
            t.param<uint64_t>("copySize"),
            false);
    });

CTS_TEST(g, "copy_out_of_bounds")
    .desc("Test that copies which exceed the buffer bounds are invalid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 0}, {"copySize", 32}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 0}, {"copySize", 36}},
            ParamRecord{{"srcOffset", 36}, {"dstOffset", 0}, {"copySize", 4}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 36}, {"copySize", 4}},
            ParamRecord{{"srcOffset", 36}, {"dstOffset", 0}, {"copySize", 0}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 36}, {"copySize", 0}},
            ParamRecord{{"srcOffset", 20}, {"dstOffset", 0}, {"copySize", 16}},
            ParamRecord{{"srcOffset", 20}, {"dstOffset", 0}, {"copySize", 12}, {"_isSuccess", true}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 20}, {"copySize", 16}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 20}, {"copySize", 12}, {"_isSuccess", true}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer src = createCopyBuffer(t, 32, WGPUBufferUsage_CopySrc);
        WGPUBuffer dst = createCopyBuffer(t, 32, WGPUBufferUsage_CopyDst);
        const bool isSuccess = t.hasParam("_isSuccess") && t.param<bool>("_isSuccess");
        testCopyBufferToBuffer(
            t,
            src,
            t.param<uint64_t>("srcOffset"),
            dst,
            t.param<uint64_t>("dstOffset"),
            t.param<uint64_t>("copySize"),
            isSuccess);
    });

CTS_TEST(g, "copy_within_same_buffer")
    .desc("Test that copying within the same buffer is invalid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 8}, {"copySize", 4}},
            ParamRecord{{"srcOffset", 8}, {"dstOffset", 0}, {"copySize", 4}},
            ParamRecord{{"srcOffset", 0}, {"dstOffset", 4}, {"copySize", 8}},
            ParamRecord{{"srcOffset", 4}, {"dstOffset", 0}, {"copySize", 8}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createCopyBuffer(t, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        testCopyBufferToBuffer(
            t,
            buffer,
            t.param<uint64_t>("srcOffset"),
            buffer,
            t.param<uint64_t>("dstOffset"),
            t.param<uint64_t>("copySize"),
            false);
    });

} // namespace
