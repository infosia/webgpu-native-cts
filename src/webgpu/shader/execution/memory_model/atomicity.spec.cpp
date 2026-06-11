// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/atomicity.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
// - Upstream uses `.paramsSimple([...])` which has no direct C++ harness equivalent.
//   Ported as `.params(u => u.combineWithParams({...}))` at the case level.
// - Upstream private param `_testCode` (excluded from upstream query strings) is not
//   carried as a harness param. Instead the test code is deterministically derived
//   from `memType` inside the fn body (same rule: storage class -> storageMemoryTestCode,
//   workgroup class -> workgroupMemoryTestCode). The resulting public query identity
//   (memType + testType) matches upstream exactly.
// - Upstream's plain GPUTest fixture just uses the fixture-provided device. The C API
//   allows only one device per adapter, and the harness's shared adapter is consumed by
//   the cached all-features/max-limits device in suite runs, so the plain GpuTest fixture
//   (which would request a second device) cannot be used. AllFeaturesMaxLimitsGpuTest's
//   tracked device is used instead; behavior is identical because this file uses no
//   optional features and the only limit consumed is
//   workgroupXSize = min(256, maxComputeWorkgroupSizeX) = 256 on any conforming device.

#include "webgpu/shader/execution/memory_model/memory_model_setup.h"

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;
using namespace cts::memory_model;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,memory_model,atomicity",
    "Tests for the atomicity of atomic read-modify-write instructions.");

// Test code for tests that operate on storage memory.
static const char* kStorageMemoryTestCode =
    "\n  let r0 = atomicAdd(&test_locations.value[x_0], 0u);\n"
    "  atomicStore(&test_locations.value[x_1], 2u);\n"
    "  atomicStore(&results.value[id_0].r0, r0);\n";

// Test code for tests that operate on workgroup memory.
static const char* kWorkgroupMemoryTestCode =
    "\n  let r0 = atomicAdd(&wg_test_locations[x_0], 0u);\n"
    "  atomicStore(&wg_test_locations[x_1], 2u);\n"
    "  workgroupBarrier();\n"
    "  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);\n"
    "  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1],"
    " atomicLoad(&wg_test_locations[x_1]));\n";

// Result code for the atomicity test.
static const char* kResultCode =
    "\n  if ((r0 == 0u && mem_x_0 == 2u)) {\n"
    "    atomicAdd(&test_results.seq0, 1u);\n"
    "  } else if ((r0 == 2u && mem_x_0 == 1u)) {\n"
    "    atomicAdd(&test_results.seq1, 1u);\n"
    "  } else if ((r0 == 0u && mem_x_0 == 1u)) {\n"
    "    atomicAdd(&test_results.weak, 1u);\n"
    "  }\n";

CTS_TEST(g, "atomicity")
    .desc(
        "Checks whether a store on one thread can interrupt an atomic RMW on a second thread. "
        "If the read returned by\n"
        "    the RMW instruction is the initial value of memory (0), but the final value in "
        "memory is 1, then the atomic write\n"
        "    in the second thread occurred in between the read and the write of the RMW.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "inter_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_workgroup"}, {"testType", "intra_workgroup"}},
        });
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const TestType testType = testTypeFromId(t.param<std::string>("testType"));

        // Derive the test code from the memory type, matching the upstream _testCode mapping.
        const char* testCodeStr =
            (memType == MemoryType::AtomicWorkgroupClass)
                ? kWorkgroupMemoryTestCode
                : kStorageMemoryTestCode;

        const std::string testShader = buildTestShader(testCodeStr, memType, testType);
        const std::string resultShader = buildResultShader(kResultCode, testType, ResultType::FourBehavior);
        MemoryModelTester tester(t, kAtomicityTestParams, testShader, resultShader);
        tester.run(10, 3);
    });

} // namespace
