// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/storageBarrier.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Both upstream g.test()s are .unimplemented() stubs: the actual barrier
// behavior is exercised by the memory_model tests. The port preserves the two
// test entries (and the 'stage' params grid) as unimplemented to match the
// upstream listing.

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,storageBarrier",
    "'storageBarrier' affects memory and atomic operations in the storage address space.");

CTS_TEST(testGroup, "stage")
    .desc("All synchronization functions must only be used in the compute shader stage.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {Value(std::string("vertex")), Value(std::string("fragment")),
                                   Value(std::string("compute"))});
    })
    .unimplemented();

CTS_TEST(testGroup, "barrier").desc("fn storageBarrier()").unimplemented();

} // namespace
