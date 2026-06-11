// Ported from gpuweb/cts src/webgpu/api/operation/pipeline/default_layout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   All three upstream tests are marked .unimplemented() in the source.
//   They are ported here as .unimplemented() stubs to preserve query identity.
//   File status: partial.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,pipeline,default_layout",
    "\nTests for default pipeline layouts.\n");

// Upstream: g.test('getBindGroupLayout_js_object').unimplemented()
CTS_TEST(g, "getBindGroupLayout_js_object")
    .desc(
        "Test that getBindGroupLayout returns [TODO: the same or a different, needs spec] object"
        " each time.")
    .unimplemented();

// Upstream: g.test('incompatible_with_explicit').unimplemented()
CTS_TEST(g, "incompatible_with_explicit")
    .desc("Test that default bind group layouts are never compatible with explicitly created ones.")
    .unimplemented();

// Upstream: g.test('layout').unimplemented()
CTS_TEST(g, "layout")
    .desc(
        "Test that bind group layouts of the default pipeline layout are correct by passing various"
        " shaders and then checking their computed bind group layouts are compatible with particular"
        " bind groups.")
    .unimplemented();

} // namespace
