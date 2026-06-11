// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/coherence.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
// - Upstream uses `.paramsSimple([...])`, ported as `.params(u => u.combineWithParams({...}))`
//   at the case level. Records that carry `extraFlags: 'rmw_variant'` upstream carry the same
//   public param here; non-RMW records omit it, so query identities match upstream exactly.
// - Upstream private param `_testCode` (excluded from upstream query strings) is not carried
//   as a harness param. The test code is deterministically derived inside each fn body from
//   (memType, testType, rmw-variant presence), reproducing the upstream record mapping 1:1.
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

#include <string>

using namespace cts;
using namespace cts::memory_model;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,memory_model,coherence",
    "\n"
    "Tests that all threads see a sequentially consistent view of the order of memory\n"
    "accesses to a single memory location. Uses a parallel testing strategy along with stressing\n"
    "threads to increase coverage of possible bugs.");

// True when the upstream record carried `extraFlags: 'rmw_variant'`.
bool isRmwVariant(const GpuTest& t) {
    return t.hasParam("extraFlags");
}

// ---------------------------------------------------------------------------
// corr
// ---------------------------------------------------------------------------

const char* const kStorageMemoryCorrTestCode = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  let r0 = atomicLoad(&test_locations.value[x_1]);
  let r1 = atomicLoad(&test_locations.value[y_1]);
  atomicStore(&results.value[id_1].r0, r0);
  atomicStore(&results.value[id_1].r1, r1);
)";

const char* const kWorkgroupStorageMemoryCorrTestCode = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  let r0 = atomicLoad(&test_locations.value[x_1]);
  let r1 = atomicLoad(&test_locations.value[y_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char* const kStorageMemoryCorrRMWTestCode = R"(
  atomicExchange(&test_locations.value[x_0], 1u);
  let r0 = atomicLoad(&test_locations.value[x_1]);
  let r1 = atomicAdd(&test_locations.value[y_1], 0u);
  atomicStore(&results.value[id_1].r0, r0);
  atomicStore(&results.value[id_1].r1, r1);
)";

const char* const kWorkgroupStorageMemoryCorrRMWTestCode = R"(
  atomicExchange(&test_locations.value[x_0], 1u);
  let r0 = atomicLoad(&test_locations.value[x_1]);
  let r1 = atomicAdd(&test_locations.value[y_1], 0u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char* const kWorkgroupMemoryCorrTestCode = R"(
  atomicStore(&wg_test_locations[x_0], 1u);
  let r0 = atomicLoad(&wg_test_locations[x_1]);
  let r1 = atomicLoad(&wg_test_locations[y_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char* const kWorkgroupMemoryCorrRMWTestCode = R"(
  atomicExchange(&wg_test_locations[x_0], 1u);
  let r0 = atomicLoad(&wg_test_locations[x_1]);
  let r1 = atomicAdd(&wg_test_locations[y_1], 0u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

CTS_TEST(g, "corr")
    .desc(
        "Ensures two reads on one thread cannot observe an inconsistent view of a write on a "
        "second thread.\n"
        "     The first thread writes the value 1 some location x, and the second thread reads x "
        "twice in a row.\n"
        "     If the first read returns 1 but the second read returns 0, then there has been a "
        "coherence violation.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "inter_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "inter_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_workgroup"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_workgroup"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
        });
    })
    .fn([](GpuTest& t) {
        const char* const resultCode = R"(
      if ((r0 == 0u && r1 == 0u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 1u && r1 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 0u && r1 == 1u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 1u && r1 == 0u)) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const TestType testType = testTypeFromId(t.param<std::string>("testType"));
        const bool rmw = isRmwVariant(t);

        // Reproduce the upstream _testCode mapping.
        const char* testCodeStr = nullptr;
        if (memType == MemoryType::AtomicWorkgroupClass) {
            testCodeStr = rmw ? kWorkgroupMemoryCorrRMWTestCode : kWorkgroupMemoryCorrTestCode;
        } else if (testType == TestType::InterWorkgroup) {
            testCodeStr = rmw ? kStorageMemoryCorrRMWTestCode : kStorageMemoryCorrTestCode;
        } else {
            testCodeStr =
                rmw ? kWorkgroupStorageMemoryCorrRMWTestCode : kWorkgroupStorageMemoryCorrTestCode;
        }

        const std::string testShader = buildTestShader(testCodeStr, memType, testType);
        const std::string resultShader =
            buildResultShader(resultCode, testType, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kCoherenceTestParams, testShader, resultShader);
        memModelTester.run(60, 3);
    });

// ---------------------------------------------------------------------------
// coww
// ---------------------------------------------------------------------------

const char* const kStorageMemoryCowwTestCode = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  atomicStore(&test_locations.value[y_0], 2u);
)";

const char* const kStorageMemoryCowwRMWTestCode = R"(
  atomicExchange(&test_locations.value[x_0], 1u);
  atomicStore(&test_locations.value[y_0], 2u);
)";

const char* const kWorkgroupMemoryCowwTestCode = R"(
  atomicStore(&wg_test_locations[x_0], 1u);
  atomicStore(&wg_test_locations[y_0], 2u);
  workgroupBarrier();
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_0], atomicLoad(&wg_test_locations[x_0]));
)";

const char* const kWorkgroupMemoryCowwRMWTestCode = R"(
  atomicExchange(&wg_test_locations[x_0], 1u);
  atomicStore(&wg_test_locations[y_0], 2u);
  workgroupBarrier();
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_0], atomicLoad(&wg_test_locations[x_0]));
)";

CTS_TEST(g, "coww")
    .desc(
        "Ensures two writes on one thread do not lead to incoherent results. The thread first "
        "writes 1 to\n"
        "     some location x and then writes 2 to the same location. If the value in memory "
        "after the test finishes\n"
        "     is 1, then there has been a coherence violation.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "inter_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "inter_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_workgroup"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_workgroup"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
        });
    })
    .fn([](GpuTest& t) {
        const char* const resultCode = R"(
      if (mem_x_0 == 2u) {
        atomicAdd(&test_results.seq, 1u);
      } else if (mem_x_0 == 1u) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const TestType testType = testTypeFromId(t.param<std::string>("testType"));
        const bool rmw = isRmwVariant(t);

        // Reproduce the upstream _testCode mapping. Note: upstream uses the same storage
        // test code for both inter- and intra-workgroup storage cases of this test.
        const char* testCodeStr = nullptr;
        if (memType == MemoryType::AtomicWorkgroupClass) {
            testCodeStr = rmw ? kWorkgroupMemoryCowwRMWTestCode : kWorkgroupMemoryCowwTestCode;
        } else {
            testCodeStr = rmw ? kStorageMemoryCowwRMWTestCode : kStorageMemoryCowwTestCode;
        }

        const std::string testShader = buildTestShader(testCodeStr, memType, testType);
        const std::string resultShader =
            buildResultShader(resultCode, testType, ResultType::TwoBehavior);
        MemoryModelTestParams params = kCoherenceTestParams;
        params.numBehaviors = 2;
        MemoryModelTester memModelTester(t, params, testShader, resultShader);
        memModelTester.run(60, 1);
    });

// ---------------------------------------------------------------------------
// cowr
// ---------------------------------------------------------------------------

const char* const kStorageMemoryCowrTestCode = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  let r0 = atomicLoad(&test_locations.value[y_0]);
  atomicStore(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[id_0].r0, r0);
)";

const char* const kWorkgroupStorageMemoryCowrTestCode = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  let r0 = atomicLoad(&test_locations.value[y_0]);
  atomicStore(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

const char* const kStorageMemoryCowrRMWTestCode = R"(
  atomicExchange(&test_locations.value[x_0], 1u);
  let r0 = atomicAdd(&test_locations.value[y_0], 0u);
  atomicExchange(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[id_0].r0, r0);
)";

const char* const kWorkgroupStorageMemoryCowrRMWTestCode = R"(
  atomicExchange(&test_locations.value[x_0], 1u);
  let r0 = atomicAdd(&test_locations.value[y_0], 0u);
  atomicExchange(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

const char* const kWorkgroupMemoryCowrTestCode = R"(
  atomicStore(&wg_test_locations[x_0], 1u);
  let r0 = atomicLoad(&wg_test_locations[y_0]);
  atomicStore(&wg_test_locations[x_1], 2u);
  workgroupBarrier();
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1], atomicLoad(&wg_test_locations[x_1]));
)";

const char* const kWorkgroupMemoryCowrRMWTestCode = R"(
  atomicExchange(&wg_test_locations[x_0], 1u);
  let r0 = atomicAdd(&wg_test_locations[y_0], 0u);
  atomicExchange(&wg_test_locations[x_1], 2u);
  workgroupBarrier();
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1], atomicLoad(&wg_test_locations[x_1]));
)";

CTS_TEST(g, "cowr")
    .desc(
        "The first thread first writes 1 to some location x and then reads x. The second thread "
        "writes 2 to x.\n"
        "     If the first thread reads the value 2 and the value in memory at the end of the "
        "test is 1, then the read\n"
        "     and write on the first thread have been reordered, a coherence violation.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "inter_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "inter_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_workgroup"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_workgroup"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
        });
    })
    .fn([](GpuTest& t) {
        const char* const resultCode = R"(
      if ((r0 == 1u && mem_x_0 == 2u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 1u && mem_x_0 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 2u && mem_x_0 == 2u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 2u && mem_x_0 == 1u)) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const TestType testType = testTypeFromId(t.param<std::string>("testType"));
        const bool rmw = isRmwVariant(t);

        // Reproduce the upstream _testCode mapping.
        const char* testCodeStr = nullptr;
        if (memType == MemoryType::AtomicWorkgroupClass) {
            testCodeStr = rmw ? kWorkgroupMemoryCowrRMWTestCode : kWorkgroupMemoryCowrTestCode;
        } else if (testType == TestType::InterWorkgroup) {
            testCodeStr = rmw ? kStorageMemoryCowrRMWTestCode : kStorageMemoryCowrTestCode;
        } else {
            testCodeStr =
                rmw ? kWorkgroupStorageMemoryCowrRMWTestCode : kWorkgroupStorageMemoryCowrTestCode;
        }

        const std::string testShader = buildTestShader(testCodeStr, memType, testType);
        const std::string resultShader =
            buildResultShader(resultCode, testType, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kCoherenceTestParams, testShader, resultShader);
        memModelTester.run(60, 3);
    });

// ---------------------------------------------------------------------------
// corw1
// ---------------------------------------------------------------------------

const char* const kStorageMemoryCorw1TestCode = R"(
  let r0 = atomicLoad(&test_locations.value[x_0]);
  atomicStore(&test_locations.value[x_0], 1u);
  workgroupBarrier();
  atomicStore(&results.value[id_0].r0, r0);
)";

const char* const kWorkgroupStorageMemoryCorw1TestCode = R"(
  let r0 = atomicLoad(&test_locations.value[x_0]);
  atomicStore(&test_locations.value[y_0], 1u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

const char* const kWorkgroupMemoryCorw1TestCode = R"(
  let r0 = atomicLoad(&wg_test_locations[x_0]);
  atomicStore(&wg_test_locations[y_0], 1u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

CTS_TEST(g, "corw1")
    .desc(
        "One thread first reads from a memory location x and then writes 1 to x. If the read "
        "observes the subsequent\n"
        "     write, there has been a coherence violation.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "inter_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_workgroup"}, {"testType", "intra_workgroup"}},
        });
    })
    .fn([](GpuTest& t) {
        const char* const resultCode = R"(
      if (r0 == 0u) {
        atomicAdd(&test_results.seq, 1u);
      } else if (r0 == 1u) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const TestType testType = testTypeFromId(t.param<std::string>("testType"));

        // Reproduce the upstream _testCode mapping.
        const char* testCodeStr = nullptr;
        if (memType == MemoryType::AtomicWorkgroupClass) {
            testCodeStr = kWorkgroupMemoryCorw1TestCode;
        } else if (testType == TestType::InterWorkgroup) {
            testCodeStr = kStorageMemoryCorw1TestCode;
        } else {
            testCodeStr = kWorkgroupStorageMemoryCorw1TestCode;
        }

        const std::string testShader = buildTestShader(testCodeStr, memType, testType);
        const std::string resultShader =
            buildResultShader(resultCode, testType, ResultType::TwoBehavior);
        MemoryModelTestParams params = kCoherenceTestParams;
        params.numBehaviors = 2;
        MemoryModelTester memModelTester(t, params, testShader, resultShader);
        memModelTester.run(60, 1);
    });

// ---------------------------------------------------------------------------
// corw2
// ---------------------------------------------------------------------------

const char* const kStorageMemoryCorw2TestCode = R"(
  let r0 = atomicLoad(&test_locations.value[x_0]);
  atomicStore(&test_locations.value[y_0], 1u);
  atomicStore(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[id_0].r0, r0);
)";

const char* const kWorkgroupStorageMemoryCorw2TestCode = R"(
  let r0 = atomicLoad(&test_locations.value[x_0]);
  atomicStore(&test_locations.value[y_0], 1u);
  atomicStore(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

const char* const kStorageMemoryCorw2RMWTestCode = R"(
  let r0 = atomicLoad(&test_locations.value[x_0]);
  atomicStore(&test_locations.value[y_0], 1u);
  atomicExchange(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[id_0].r0, r0);
)";

const char* const kWorkgroupStorageMemoryCorw2RMWTestCode = R"(
  let r0 = atomicLoad(&test_locations.value[x_0]);
  atomicStore(&test_locations.value[y_0], 1u);
  atomicExchange(&test_locations.value[x_1], 2u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

const char* const kWorkgroupMemoryCorw2TestCode = R"(
  let r0 = atomicLoad(&wg_test_locations[x_0]);
  atomicStore(&wg_test_locations[y_0], 1u);
  atomicStore(&wg_test_locations[x_1], 2u);
  workgroupBarrier();
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1], atomicLoad(&wg_test_locations[x_1]));
)";

const char* const kWorkgroupMemoryCorw2RMWTestCode = R"(
  let r0 = atomicLoad(&wg_test_locations[x_0]);
  atomicStore(&wg_test_locations[y_0], 1u);
  atomicExchange(&wg_test_locations[x_1], 2u);
  workgroupBarrier();
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1], atomicLoad(&wg_test_locations[x_1]));
)";

CTS_TEST(g, "corw2")
    .desc(
        "The first thread reads from some memory location x, and then writes 1 to x. The second "
        "thread\n"
        "     writes 2 to x. If the first thread reads the value 2, but the value in memory after "
        "the test\n"
        "     completes is 1, then the instructions on the first thread have been re-ordered, "
        "leading to a\n"
        "     coherence violation.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "inter_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "inter_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_storage"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_storage"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
            ParamRecord{{"memType", "atomic_workgroup"}, {"testType", "intra_workgroup"}},
            ParamRecord{{"memType", "atomic_workgroup"},
                        {"testType", "intra_workgroup"},
                        {"extraFlags", "rmw_variant"}},
        });
    })
    .fn([](GpuTest& t) {
        const char* const resultCode = R"(
      if ((r0 == 0u && mem_x_0 == 2u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 2u && mem_x_0 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 0u && mem_x_0 == 1u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 2u && mem_x_0 == 2u)) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const TestType testType = testTypeFromId(t.param<std::string>("testType"));
        const bool rmw = isRmwVariant(t);

        // Reproduce the upstream _testCode mapping.
        const char* testCodeStr = nullptr;
        if (memType == MemoryType::AtomicWorkgroupClass) {
            testCodeStr = rmw ? kWorkgroupMemoryCorw2RMWTestCode : kWorkgroupMemoryCorw2TestCode;
        } else if (testType == TestType::InterWorkgroup) {
            testCodeStr = rmw ? kStorageMemoryCorw2RMWTestCode : kStorageMemoryCorw2TestCode;
        } else {
            testCodeStr = rmw ? kWorkgroupStorageMemoryCorw2RMWTestCode
                              : kWorkgroupStorageMemoryCorw2TestCode;
        }

        const std::string testShader = buildTestShader(testCodeStr, memType, testType);
        const std::string resultShader =
            buildResultShader(resultCode, testType, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kCoherenceTestParams, testShader, resultShader);
        memModelTester.run(60, 3);
    });

} // namespace
