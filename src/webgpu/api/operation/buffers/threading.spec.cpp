// Ported from gpuweb/cts src/webgpu/api/operation/buffers/threading.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 webgpu-native-cts contributors, BSD-3-Clause.
//
// Both upstream tests are .unimplemented() — they test multi-thread / Worker
// semantics (serializing a GPUBuffer to another thread, destroying on one
// thread while another holds it) that have no analog in the single-threaded
// C API harness.  Ported as unimplemented stubs so the test names appear in
// --list output and match upstream query identities.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,buffers,threading",
    R"(
Tests for valid operations with various client-side thread-shared state of GPUBuffers.

States to test:
- mapping pending
- mapped
- mapped at creation
- mapped at creation, then unmapped
- mapped at creation, then unmapped, then re-mapped
- destroyed

TODO: Look for more things to test.
)");

CTS_TEST(g, "serialize")
    .desc(
        "Copy a GPUBuffer to another thread while it is in various states on"
        " {the sending thread, yet another thread}.")
    .unimplemented(
        "N/A: multi-thread / Worker semantics have no C API analog");

CTS_TEST(g, "destroyed")
    .desc("Destroy on one thread while in various states in another thread.")
    .unimplemented(
        "N/A: multi-thread / Worker semantics have no C API analog");

} // namespace
