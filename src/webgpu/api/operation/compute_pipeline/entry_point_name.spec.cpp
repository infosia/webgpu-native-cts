// Ported from gpuweb/cts src/webgpu/api/operation/compute_pipeline/entry_point_name.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 webgpu-native-cts contributors, BSD-3-Clause.
//
// Upstream file has no g.test() definitions — all tests are TODO items in the
// description.  Registered as an empty group so the path appears in --list output
// and matches the upstream query namespace.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,compute_pipeline,entry_point_name",
    R"(TODO:
- Test some weird but valid values for entry point name (both module and pipeline creation
  should succeed).
- Test using each of many entry points in the module (should succeed).
- Test using an entry point with the wrong stage (should fail).
)");

// No tests: the upstream file contains only a description with TODO items and
// no g.test() calls.  This stub satisfies the query-namespace requirement.

} // namespace
