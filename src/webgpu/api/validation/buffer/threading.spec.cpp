// Ported from gpuweb/cts src/webgpu/api/validation/buffer/threading.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// The upstream source is an all-TODO stub with no named test cases:
//   - Try to map on one thread while {pending,mapped,mappedAtCreation,...} on another thread.
//   - Invalid to postMessage a mapped range's ArrayBuffer or ArrayBufferView.
//   - Copy GPUBuffer to another thread while {pending,mapped,mappedAtCreation} on {same,diff} thread.
//
// None of these are implementable against the C WebGPU API: the WebGPU C API has no
// concept of workers / postMessage / SharedArrayBuffer cross-thread ownership; all
// threading and ownership transfer semantics are JS/browser-specific and have no
// equivalent surface in webgpu.h.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,buffer,threading",
    "TODO: threading validation tests for GPUBuffer.");

// No test cases: the upstream spec is entirely TODO with no named tests.
// All described scenarios involve JS worker threads, postMessage, and
// SharedArrayBuffer — concepts with no equivalent in the C WebGPU API.

} // namespace
