// Ported from gpuweb/cts src/webgpu/api/operation/rendering/robust_access_index.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

// The upstream file contains only a TODO stub (no tests defined).
// Upstream description:
//   "TODO: Test that drawIndexedIndirect accesses the index buffer robustly."

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// Upstream: export const g = makeTestGroup(AllFeaturesMaxLimitsGPUTest);
// No tests are defined upstream — the file is a pure unimplemented stub.
TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,robust_access_index",
    "\nTODO: Test that drawIndexedIndirect accesses the index buffer robustly.\n");

// Upstream defines no g.test(...) entries; nothing to port.

} // namespace
