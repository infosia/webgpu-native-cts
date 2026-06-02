// Ported from gpuweb/cts src/webgpu/api/operation/onSubmittedWorkDone.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,operation,onSubmittedWorkDone",
    "Tests for GPUQueue.onSubmittedWorkDone behavior.");

CTS_TEST(g, "without_work")
    .desc("Await onSubmittedWorkDone once without having submitted any work.")
    .fn([](GpuTest& t) {
        t.onSubmittedWorkDoneSync();
    });

CTS_TEST(g, "with_work")
    .desc("Await onSubmittedWorkDone once after submitting some work.")
    .fn([](GpuTest& t) {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 4;
        desc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        const std::array<uint8_t, 4> zeros = {0, 0, 0, 0};
        t.queueWriteBuffer(buffer, 0, zeros.data(), zeros.size());
        t.onSubmittedWorkDoneSync();
    });

CTS_TEST(g, "many,serial")
    .desc("Await 1000 onSubmittedWorkDone calls in serial.")
    .fn([](GpuTest& t) {
        for (int i = 0; i < 1000; ++i) {
            t.onSubmittedWorkDoneSync();
        }
    });

CTS_TEST(g, "many,parallel")
    .desc("Await 1000 onSubmittedWorkDone calls in parallel.")
    .fn([](GpuTest& t) {
        t.onSubmittedWorkDoneMany(1000, false);
    });

CTS_TEST(g, "many,parallel_order")
    .desc("Issue 200 onSubmittedWorkDone calls and make sure they resolve in the right order.")
    .fn([](GpuTest& t) {
        t.onSubmittedWorkDoneMany(200, true);
    });

} // namespace
