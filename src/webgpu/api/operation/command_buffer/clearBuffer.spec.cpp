// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/clearBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,operation,command_buffer,clearBuffer",
    "API operations tests for clearBuffer.");

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

CTS_TEST(g, "clear")
    .desc("Validate clearBuffer by clearing a range and verifying the whole buffer with MapRead.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("offset", {0, 4, 8, 16, Value::undef()})
            .combine("size", {0, 4, 8, 16, Value::undef()})
            .expand("bufferSize", [](const ParamRecord& params) {
                const uint64_t offset = paramOr(params, "offset", 0);
                const uint64_t size = paramOr(params, "size", 16);
                return std::vector<Value>{
                    Value(offset + size),
                    Value(offset + size + 8),
                };
            });
    })
    .fn([](GpuTest& t) {
        const uint64_t bufferSize = t.param<uint64_t>("bufferSize");
        std::vector<uint8_t> expected(static_cast<size_t>(bufferSize));
        for (size_t i = 0; i < expected.size(); ++i) {
            expected[i] = static_cast<uint8_t>(i + 1);
        }

        WGPUBuffer buffer = t.makeBufferWithContents(
            expected.data(),
            expected.size(),
            WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

        const uint64_t clearOffset = paramOr(t, "offset", 0);
        const uint64_t clearSize = t.paramIsUndefined("size") ? WGPU_WHOLE_SIZE : t.param<uint64_t>("size");

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderClearBuffer(encoder, buffer, clearOffset, clearSize);
        submitCommandBuffer(t, encoder);

        const uint64_t expectSize = t.paramIsUndefined("size")
            ? bufferSize - clearOffset
            : t.param<uint64_t>("size");
        for (uint64_t i = 0; i < expectSize; ++i) {
            expected[static_cast<size_t>(clearOffset + i)] = 0;
        }

        t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size());
    });

} // namespace
