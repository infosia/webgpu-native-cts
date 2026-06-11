// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/weak.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Port notes (intentional deviations, documented for the reviewer):
// - Upstream uses paramsSimple([{ memType, _testCode }, ...]). The
//   underscore-prefixed `_testCode` param is private (excluded from upstream
//   query identity), so this port carries only the public `memType` param
//   (values "atomic_workgroup", "atomic_storage", same order as upstream) and
//   selects the matching test-shader code in the test body. Query identity is
//   unchanged: webgpu:shader,execution,memory_model,weak:<test>:memType="...".
// - The in-file upstream `memoryModelTestParams` const is provided by the
//   shared header as cts::memory_model::kWeakTestParams (field-for-field
//   identical; verified against upstream weak.spec.ts).
// - Upstream pass criteria are preserved exactly: each test runs 40 iterations
//   and fails only if the weak-behavior counter (index 3) is ever non-zero.
// - Upstream's plain GPUTest fixture just uses the fixture-provided device. The C API
//   allows only one device per adapter, and the harness's shared adapter is consumed by
//   the cached all-features/max-limits device in suite runs, so the plain GpuTest fixture
//   (which would request a second device) cannot be used. AllFeaturesMaxLimitsGpuTest's
//   tracked device is used instead; behavior is identical because this file uses no
//   optional features and the only limit consumed is
//   workgroupXSize = min(256, maxComputeWorkgroupSizeX) = 256 on any conforming device.

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

#include "webgpu/shader/execution/memory_model/memory_model_setup.h"

using namespace cts;
using namespace cts::memory_model;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,memory_model,weak",
    "\n"
    "Tests for properties of the WebGPU memory model involving two memory locations.\n"
    "Specifically, the acquire/release ordering provided by WebGPU's barriers can be used to disallow\n"
    "weak behaviors in several classic memory model litmus tests.");

// message_passing -----------------------------------------------------------

const char kWorkgroupMemoryMessagePassingTestCode[] = R"(
  atomicStore(&wg_test_locations[x_0], 1u);
  workgroupBarrier();
  atomicStore(&wg_test_locations[y_0], 1u);
  let r0 = atomicLoad(&wg_test_locations[y_1]);
  workgroupBarrier();
  let r1 = atomicLoad(&wg_test_locations[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char kStorageMemoryMessagePassingTestCode[] = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  storageBarrier();
  atomicStore(&test_locations.value[y_0], 1u);
  let r0 = atomicLoad(&test_locations.value[y_1]);
  storageBarrier();
  let r1 = atomicLoad(&test_locations.value[x_1]);
  atomicStore(&results.value[shuffled_workgroup * u32(workgroupXSize) + id_1].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * u32(workgroupXSize) + id_1].r1, r1);
)";

const char kMessagePassingResultCode[] = R"(
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

CTS_TEST(g, "message_passing")
    .desc(
        "Checks whether two reads on one thread can observe two writes in another thread in a way\n"
        "    that is inconsistent with sequential consistency. In the message passing litmus test, one\n"
        "    thread writes the value 1 to some location x and then 1 to some location y. The second thread\n"
        "    reads y and then x. If the second thread reads y == 1 and x == 0, then sequential consistency\n"
        "    has not been respected. The acquire/release semantics of WebGPU's barrier functions should disallow\n"
        "    this behavior within a workgroup.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combine("memType", {memoryTypeId(MemoryType::AtomicWorkgroupClass),
                                     memoryTypeId(MemoryType::AtomicStorageClass)});
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const char* testCode = memType == MemoryType::AtomicWorkgroupClass
                                   ? kWorkgroupMemoryMessagePassingTestCode
                                   : kStorageMemoryMessagePassingTestCode;
        std::string testShader = buildTestShader(testCode, memType, TestType::IntraWorkgroup);
        std::string messagePassingResultShader = buildResultShader(
            kMessagePassingResultCode, TestType::IntraWorkgroup, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kWeakTestParams, testShader,
                                         messagePassingResultShader);
        memModelTester.run(40, 3);
    });

// store ----------------------------------------------------------------------

const char kWorkgroupMemoryStoreTestCode[] = R"(
  atomicStore(&wg_test_locations[x_0], 2u);
  workgroupBarrier();
  atomicStore(&wg_test_locations[y_0], 1u);
  let r0 = atomicLoad(&wg_test_locations[y_1]);
  workgroupBarrier();
  atomicStore(&wg_test_locations[x_1], 1u);
  workgroupBarrier();
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1], atomicLoad(&wg_test_locations[x_1]));
)";

const char kStorageMemoryStoreTestCode[] = R"(
  atomicStore(&test_locations.value[x_0], 2u);
  storageBarrier();
  atomicStore(&test_locations.value[y_0], 1u);
  let r0 = atomicLoad(&test_locations.value[y_1]);
  storageBarrier();
  atomicStore(&test_locations.value[x_1], 1u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
)";

const char kStoreResultCode[] = R"(
      if ((r0 == 1u && mem_x_0 == 1u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 0u && mem_x_0 == 2u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 0u && mem_x_0 == 1u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 1u && mem_x_0 == 2u)) {
        atomicAdd(&test_results.weak, 1u);
      }
      )";

CTS_TEST(g, "store")
    .desc(
        "In the store litmus test, one thread writes 2 to some memory location x and then 1 to some memory location\n"
        "     y. A second thread reads the value of y and then writes 1 to x. If the read on the second thread returns 1,\n"
        "     but the value of x in memory after the test ends is 2, then there has been a re-ordering which is not allowed\n"
        "     when using WebGPU's barriers.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combine("memType", {memoryTypeId(MemoryType::AtomicWorkgroupClass),
                                     memoryTypeId(MemoryType::AtomicStorageClass)});
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const char* testCode = memType == MemoryType::AtomicWorkgroupClass
                                   ? kWorkgroupMemoryStoreTestCode
                                   : kStorageMemoryStoreTestCode;
        std::string testShader = buildTestShader(testCode, memType, TestType::IntraWorkgroup);
        std::string messagePassingResultShader = buildResultShader(
            kStoreResultCode, TestType::IntraWorkgroup, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kWeakTestParams, testShader,
                                         messagePassingResultShader);
        memModelTester.run(40, 3);
    });

// load_buffer ----------------------------------------------------------------

const char kWorkgroupMemoryLoadBufferTestCode[] = R"(
  let r0 = atomicLoad(&wg_test_locations[y_0]);
  workgroupBarrier();
  atomicStore(&wg_test_locations[x_0], 1u);
  let r1 = atomicLoad(&wg_test_locations[x_1]);
  workgroupBarrier();
  atomicStore(&wg_test_locations[y_1], 1u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char kStorageMemoryLoadBufferTestCode[] = R"(
  let r0 = atomicLoad(&test_locations.value[y_0]);
  storageBarrier();
  atomicStore(&test_locations.value[x_0], 1u);
  let r1 = atomicLoad(&test_locations.value[x_1]);
  storageBarrier();
  atomicStore(&test_locations.value[y_1], 1u);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char kLoadBufferResultCode[] = R"(
      if ((r0 == 1u && r1 == 0u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 0u && r1 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 0u && r1 == 0u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 1u && r1 == 1u)) {
        atomicAdd(&test_results.weak, 1u);
      }
      )";

CTS_TEST(g, "load_buffer")
    .desc(
        "In the load buffer litmus test, one thread reads from memory location y and then writes 1 to memory location x.\n"
        "     A second thread reads from x and then writes 1 to y. If both threads read the value 0, then the loads have been\n"
        "     buffered or re-ordered, which is not allowed when used in conjunction with WebGPU's barriers.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combine("memType", {memoryTypeId(MemoryType::AtomicWorkgroupClass),
                                     memoryTypeId(MemoryType::AtomicStorageClass)});
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const char* testCode = memType == MemoryType::AtomicWorkgroupClass
                                   ? kWorkgroupMemoryLoadBufferTestCode
                                   : kStorageMemoryLoadBufferTestCode;
        std::string testShader = buildTestShader(testCode, memType, TestType::IntraWorkgroup);
        std::string messagePassingResultShader = buildResultShader(
            kLoadBufferResultCode, TestType::IntraWorkgroup, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kWeakTestParams, testShader,
                                         messagePassingResultShader);
        memModelTester.run(40, 3);
    });

// read -----------------------------------------------------------------------

const char kWorkgroupMemoryReadTestCode[] = R"(
  atomicStore(&wg_test_locations[x_0], 1u);
  workgroupBarrier();
  atomicExchange(&wg_test_locations[y_0], 1u);
  atomicExchange(&wg_test_locations[y_1], 2u);
  workgroupBarrier();
  let r0 = atomicLoad(&wg_test_locations[x_1]);
  workgroupBarrier();
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + y_1], atomicLoad(&wg_test_locations[y_1]));
)";

const char kStorageMemoryReadTestCode[] = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  storageBarrier();
  atomicExchange(&test_locations.value[y_0], 1u);
  atomicExchange(&test_locations.value[y_1], 2u);
  storageBarrier();
  let r0 = atomicLoad(&test_locations.value[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
)";

const char kReadResultCode[] = R"(
      if ((r0 == 1u && mem_y_0 == 2u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 0u && mem_y_0 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 1u && mem_y_0 == 1u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 0u && mem_y_0 == 2u)) {
        atomicAdd(&test_results.weak, 1u);
      }
      )";

CTS_TEST(g, "read")
    .desc(
        "In the read litmus test, one thread writes 1 to memory location x and then 1 to memory location y. A second thread\n"
        "     first writes 2 to y and then reads from x. If the value read by the second thread is 0 but the value in memory of y\n"
        "     after the test completes is 2, then there has been some re-ordering of instructions disallowed when using WebGPU's\n"
        "     barrier. Additionally, both writes to y are RMWs, so that the barrier forces the correct acquire/release memory ordering\n"
        "     synchronization.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combine("memType", {memoryTypeId(MemoryType::AtomicWorkgroupClass),
                                     memoryTypeId(MemoryType::AtomicStorageClass)});
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const char* testCode = memType == MemoryType::AtomicWorkgroupClass
                                   ? kWorkgroupMemoryReadTestCode
                                   : kStorageMemoryReadTestCode;
        std::string testShader = buildTestShader(testCode, memType, TestType::IntraWorkgroup);
        std::string messagePassingResultShader = buildResultShader(
            kReadResultCode, TestType::IntraWorkgroup, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kWeakTestParams, testShader,
                                         messagePassingResultShader);
        memModelTester.run(40, 3);
    });

// store_buffer ---------------------------------------------------------------

const char kWorkgroupMemoryStoreBufferTestCode[] = R"(
  atomicStore(&wg_test_locations[x_0], 1u);
  workgroupBarrier();
  let r0 = atomicAdd(&wg_test_locations[y_0], 0u);
  atomicExchange(&wg_test_locations[y_1], 1u);
  workgroupBarrier();
  let r1 = atomicLoad(&wg_test_locations[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char kStorageMemoryStoreBufferTestCode[] = R"(
  atomicStore(&test_locations.value[x_0], 1u);
  storageBarrier();
  let r0 = atomicAdd(&test_locations.value[y_0], 0u);
  atomicExchange(&test_locations.value[y_1], 1u);
  storageBarrier();
  let r1 = atomicLoad(&test_locations.value[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r1, r1);
)";

const char kStoreBufferResultCode[] = R"(
      if ((r0 == 1u && r1 == 0u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((r0 == 0u && r1 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((r0 == 1u && r1 == 1u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((r0 == 0u && r1 == 0u)) {
        atomicAdd(&test_results.weak, 1u);
      }
      )";

CTS_TEST(g, "store_buffer")
    .desc(
        "In the store buffer litmus test, one thread writes 1 to memory location x and then reads from memory location\n"
        "     y. A second thread writes 1 to y and then reads from x. If both reads return 0, then stores have been buffered\n"
        "     or some other re-ordering has occurred that is disallowed by WebGPU's barriers. Additionally, both the read\n"
        "     and store to y are RMWs to achieve the necessary synchronization across threads.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combine("memType", {memoryTypeId(MemoryType::AtomicWorkgroupClass),
                                     memoryTypeId(MemoryType::AtomicStorageClass)});
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const char* testCode = memType == MemoryType::AtomicWorkgroupClass
                                   ? kWorkgroupMemoryStoreBufferTestCode
                                   : kStorageMemoryStoreBufferTestCode;
        std::string testShader = buildTestShader(testCode, memType, TestType::IntraWorkgroup);
        std::string messagePassingResultShader = buildResultShader(
            kStoreBufferResultCode, TestType::IntraWorkgroup, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kWeakTestParams, testShader,
                                         messagePassingResultShader);
        memModelTester.run(40, 3);
    });

// 2_plus_2_write -------------------------------------------------------------

const char kWorkgroupMemory2P2WTestCode[] = R"(
  atomicStore(&wg_test_locations[x_0], 2u);
  workgroupBarrier();
  atomicExchange(&wg_test_locations[y_0], 1u);
  atomicExchange(&wg_test_locations[y_1], 2u);
  workgroupBarrier();
  atomicStore(&wg_test_locations[x_1], 1u);
  workgroupBarrier();
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1], atomicLoad(&wg_test_locations[x_1]));
  atomicStore(&test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + y_1], atomicLoad(&wg_test_locations[y_1]));
)";

const char kStorageMemory2P2WTestCode[] = R"(
  atomicStore(&test_locations.value[x_0], 2u);
  storageBarrier();
  atomicExchange(&test_locations.value[y_0], 1u);
  atomicExchange(&test_locations.value[y_1], 2u);
  storageBarrier();
  atomicStore(&test_locations.value[x_1], 1u);
)";

const char k2P2WResultCode[] = R"(
      if ((mem_x_0 == 1u && mem_y_0 == 2u)) {
        atomicAdd(&test_results.seq0, 1u);
      } else if ((mem_x_0 == 2u && mem_y_0 == 1u)) {
        atomicAdd(&test_results.seq1, 1u);
      } else if ((mem_x_0 == 1u && mem_y_0 == 1u)) {
        atomicAdd(&test_results.interleaved, 1u);
      } else if ((mem_x_0 == 2u && mem_y_0 == 2u)) {
        atomicAdd(&test_results.weak, 1u);
      }
      )";

CTS_TEST(g, "2_plus_2_write")
    .desc(
        "In the 2+2 write litmus test, one thread stores 2 to memory location x and then 1 to memory location y.\n"
        "     A second thread stores 2 to y and then 1 to x. If at the end of the test both memory locations are set to 2,\n"
        "     then some disallowed re-ordering has occurred. Both writes to y are RMWs to achieve the required synchronization.\n"
        "    ")
    .params([](ParamsBuilder u) {
        return u.combine("memType", {memoryTypeId(MemoryType::AtomicWorkgroupClass),
                                     memoryTypeId(MemoryType::AtomicStorageClass)});
    })
    .fn([](GpuTest& t) {
        const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
        const char* testCode = memType == MemoryType::AtomicWorkgroupClass
                                   ? kWorkgroupMemory2P2WTestCode
                                   : kStorageMemory2P2WTestCode;
        std::string testShader = buildTestShader(testCode, memType, TestType::IntraWorkgroup);
        std::string messagePassingResultShader = buildResultShader(
            k2P2WResultCode, TestType::IntraWorkgroup, ResultType::FourBehavior);
        MemoryModelTester memModelTester(t, kWeakTestParams, testShader,
                                         messagePassingResultShader);
        memModelTester.run(40, 3);
    });

} // namespace
