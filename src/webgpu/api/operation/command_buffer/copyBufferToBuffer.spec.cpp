// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/copyBufferToBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,operation,command_buffer,copyBufferToBuffer",
    "copyBufferToBuffer operation tests.");

bool paramIsUndefined(const ParamRecord& params, std::string_view key) {
    const Value* value = findParam(params, key);
    return value != nullptr && std::holds_alternative<Value::Undefined>(value->data());
}

uint64_t paramOr(const ParamRecord& params, std::string_view key, uint64_t fallback) {
    if (paramIsUndefined(params, key)) {
        return fallback;
    }
    return valueAs<uint64_t>(*findParam(params, key));
}

bool singleUndefinedOffset(const ParamRecord& params) {
    return paramIsUndefined(params, "srcOffset") != paramIsUndefined(params, "dstOffset");
}

bool needsNewSignature(const ParamRecord& params) {
    return paramIsUndefined(params, "srcOffset")
        || paramIsUndefined(params, "dstOffset")
        || paramIsUndefined(params, "copySize");
}

uint64_t paramOr(GpuTest& t, std::string_view key, uint64_t fallback) {
    if (t.paramIsUndefined(key)) {
        return fallback;
    }
    return t.param<uint64_t>(key);
}

void submitCommandBuffer(GpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUBuffer createBuffer(GpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

CTS_TEST(g, "single")
    .desc("Validate one copyBufferToBuffer operation by verifying the destination buffer with MapRead.")
    .params([](ParamsBuilder u) {
        return u.combine("newSig", {false, true})
            .beginSubcases()
            .combine("srcOffset", {0, 4, 8, 16, Value::undef()})
            .combine("dstOffset", {0, 4, 8, 16, Value::undef()})
            .filter([](const ParamRecord& params) {
                return !singleUndefinedOffset(params);
            })
            .combine("copySize", {0, 4, 8, 16, Value::undef()})
            .expand("srcBufferSize", [](const ParamRecord& params) {
                const uint64_t srcOffset = paramOr(params, "srcOffset", 0);
                const uint64_t copySize = paramOr(params, "copySize", 0);
                return std::vector<Value>{
                    Value(srcOffset + copySize),
                    Value(srcOffset + copySize + 8),
                };
            })
            .expand("dstBufferSize", [](const ParamRecord& params) {
                const uint64_t dstOffset = paramOr(params, "dstOffset", 0);
                const uint64_t copySize = paramOr(params, "copySize", 0);
                return std::vector<Value>{
                    Value(dstOffset + copySize),
                    Value(dstOffset + copySize + 8),
                };
            })
            .filter([](const ParamRecord& params) {
                return valueAs<bool>(*findParam(params, "newSig")) == needsNewSignature(params);
            });
    })
    .fn([](GpuTest& t) {
        const uint64_t srcBufferSize = t.param<uint64_t>("srcBufferSize");
        const uint64_t dstBufferSize = t.param<uint64_t>("dstBufferSize");

        std::vector<uint8_t> srcData(static_cast<size_t>(srcBufferSize));
        for (size_t i = 0; i < srcData.size(); ++i) {
            srcData[i] = static_cast<uint8_t>(i + 1);
        }

        WGPUBuffer src = t.makeBufferWithContents(srcData.data(), srcData.size(), WGPUBufferUsage_CopySrc);
        WGPUBuffer dst = createBuffer(t, dstBufferSize, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        const uint64_t srcOffset = paramOr(t, "srcOffset", 0);
        const uint64_t dstOffset = paramOr(t, "dstOffset", 0);
        const uint64_t copySize = t.paramIsUndefined("copySize")
            ? srcBufferSize - srcOffset
            : t.param<uint64_t>("copySize");

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyBufferToBuffer(encoder, src, srcOffset, dst, dstOffset, copySize);

        const bool isValid = dstBufferSize - dstOffset >= copySize;
        if (isValid) {
            t.expectValidationError([&] {
                submitCommandBuffer(t, encoder);
            }, false);
        } else {
            t.expectValidationError([&] {
                t.finishTracked(encoder);
            }, true);
        }

        std::vector<uint8_t> expectedDstData(static_cast<size_t>(dstBufferSize));
        for (uint64_t i = 0; i < copySize; ++i) {
            const uint64_t dstIndex = dstOffset + i;
            if (dstIndex < dstBufferSize) {
                expectedDstData[static_cast<size_t>(dstIndex)] = srcData[static_cast<size_t>(srcOffset + i)];
            }
        }

        t.expectGPUBufferValuesEqual(dst, expectedDstData.data(), expectedDstData.size());
    });

CTS_TEST(g, "state_transitions")
    .desc("Test that barriers happen between copy commands by cross-copying two buffers.")
    .fn([](GpuTest& t) {
        const std::vector<uint8_t> srcData = {1, 2, 3, 4, 5, 6, 7, 8};
        const std::vector<uint8_t> dstData = {10, 20, 30, 40, 50, 60, 70, 80};

        WGPUBuffer src = t.makeBufferWithContents(
            srcData.data(),
            srcData.size(),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        WGPUBuffer dst = t.makeBufferWithContents(
            dstData.data(),
            dstData.size(),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyBufferToBuffer(encoder, src, 0, dst, 4, 4);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, dst, 0, src, 4, 4);
        submitCommandBuffer(t, encoder);

        const std::vector<uint8_t> expectedSrcData = {1, 2, 3, 4, 10, 20, 30, 40};
        const std::vector<uint8_t> expectedDstData = {10, 20, 30, 40, 1, 2, 3, 4};
        t.expectGPUBufferValuesEqual(src, expectedSrcData.data(), expectedSrcData.size());
        t.expectGPUBufferValuesEqual(dst, expectedDstData.data(), expectedDstData.size());
    });

CTS_TEST(g, "copy_order")
    .desc("Test that copy commands in one command buffer execute in order.")
    .fn([](GpuTest& t) {
        const std::vector<uint32_t> srcData = {1, 2, 3, 4, 5, 6, 7, 8};

        WGPUBuffer src = t.makeBufferWithContents(
            srcData.data(),
            srcData.size() * sizeof(uint32_t),
            WGPUBufferUsage_CopySrc);
        WGPUBuffer dst = createBuffer(
            t,
            srcData.size() * sizeof(uint32_t),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyBufferToBuffer(encoder, src, 0, dst, 0, 16);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, src, 16, dst, 8, 16);
        submitCommandBuffer(t, encoder);

        const std::vector<uint32_t> expectedDstData = {1, 2, 5, 6, 7, 8, 0, 0};
        t.expectGPUBufferValuesEqual(
            dst,
            expectedDstData.data(),
            expectedDstData.size() * sizeof(uint32_t));
    });

} // namespace
