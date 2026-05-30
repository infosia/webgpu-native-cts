// Ported from gpuweb/cts src/webgpu/api/validation/buffer/destroy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,buffer,destroy",
    "Validation tests for GPUBuffer.destroy.");

CTS_TEST(g, "all_usages")
    .desc("Test destroying buffers of every usage type.")
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

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 4;
        desc.usage = usage;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        wgpuBufferDestroy(buffer);
    });

CTS_TEST(g, "error_buffer")
    .desc("Test that error buffers may be destroyed without generating validation errors.")
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = t.getErrorBuffer();
        wgpuBufferDestroy(buffer);
    });

CTS_TEST(g, "twice")
    .desc("Test that destroying a buffer more than once is allowed.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mappedAtCreation", {false, true})
            .combineWithParams({
                ParamRecord{{"size", 4}, {"usage", static_cast<int64_t>(WGPUBufferUsage_CopySrc)}},
                ParamRecord{{"size", 4}, {"usage", static_cast<int64_t>(WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc)}},
                ParamRecord{{"size", 4}, {"usage", static_cast<int64_t>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead)}},
            });
    })
    .fn([](GpuTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("usage");
        const bool mappedAtCreation = t.param<bool>("mappedAtCreation");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = size;
        desc.usage = usage;
        desc.mappedAtCreation = mappedAtCreation ? WGPU_TRUE : WGPU_FALSE;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        wgpuBufferDestroy(buffer);
        wgpuBufferDestroy(buffer);
    });

CTS_TEST(g, "while_mapped")
    .desc("Test destroying buffers while mapped or after being unmapped.")
    .unimplemented("needs mapAsync/unmap synchronization wrappers");

} // namespace
