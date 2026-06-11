// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/barrier.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// Notes:
// - Upstream `.beforeAllSubcases(t => t.skipIf(...))` guards are ported as
//   runtime skips at the top of each test body (these tests have no subcases,
//   so the skip granularity is identical).
// - Upstream gates the texture path on
//   `t.hasLanguageFeature('readonly_and_readwrite_storage_textures')`. The C
//   API's WGSL language-feature query (wgpuInstanceHasWGSLLanguageFeature) is
//   not reachable from test bodies in this harness (same precedent as
//   api/validation/render_pipeline/resource_compatibility.spec.cpp and
//   shader/execution/memory_layout.spec.cpp); all supported backends implement
//   read-write storage textures for r32uint, so the guard is omitted.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/memory_model/memory_model_setup.h"

using namespace cts;
using namespace cts::memory_model;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,memory_model,barrier",
    "Tests for non-atomic memory synchronization within a workgroup in the presence of a "
    "WebGPU barrier");

// A reasonable parameter set, determined heuristically.
// Upstream `memoryModelTestParams` for barrier.spec.ts == kBarrierTestParams.
constexpr MemoryModelTestParams kMemoryModelTestParams = kBarrierTestParams;

// The three kinds of non-atomic accesses tested.
//  rw: read -> barrier -> write
//  wr: write -> barrier -> read
//  ww: write -> barrier -> write

// Test the non-atomic memory types.
constexpr std::array<MemoryType, 3> kMemTypes = {
    MemoryType::NonAtomicStorageClass,
    MemoryType::NonAtomicWorkgroupClass,
    MemoryType::NonAtomicTextureClass,
};

std::vector<Value> accessValueTypeValues() {
    std::vector<Value> values;
    values.reserve(kAccessValueTypes.size());
    for (AccessValueType type : kAccessValueTypes) {
        values.emplace_back(std::string(accessValueTypeId(type)));
    }
    return values;
}

std::vector<Value> memTypeValues() {
    std::vector<Value> values;
    values.reserve(kMemTypes.size());
    for (MemoryType type : kMemTypes) {
        values.emplace_back(std::string(memoryTypeId(type)));
    }
    return values;
}

constexpr const char kStorageMemoryBarrierStoreLoadTestCode[] = R"(
  test_locations.value[x_0] = 1;
  storageBarrier();
  let r0 = u32(test_locations.value[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
)";

constexpr const char kTextureMemoryBarrierStoreLoadTestCode[] = R"(
  textureStore(texture_locations, indexToCoord(x_0), vec4u(1));
  textureBarrier();
  let r0 = textureLoad(texture_locations, indexToCoord(x_1)).x;
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
)";

constexpr const char kWorkgroupMemoryBarrierStoreLoadTestCode[] = R"(
  wg_test_locations[x_0] = 1;
  workgroupBarrier();
  let r0 = u32(wg_test_locations[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
)";

constexpr const char kWorkgroupUniformLoadMemoryBarrierStoreLoadTestCode[] = R"(
  wg_test_locations[x_0] = 1;
  _ = workgroupUniformLoad(&placeholder_wg_var);
  let r0 = u32(wg_test_locations[x_1]);
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_1].r0, r0);
)";

constexpr const char kStorageMemoryBarrierLoadStoreTestCode[] = R"(
  let r0 = u32(test_locations.value[x_0]);
  storageBarrier();
  test_locations.value[x_1] = 1;
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

constexpr const char kTextureMemoryBarrierLoadStoreTestCode[] = R"(
  let r0 = textureLoad(texture_locations, indexToCoord(x_0)).x;
  textureBarrier();
  textureStore(texture_locations, indexToCoord(x_1), vec4u(1));
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

constexpr const char kWorkgroupMemoryBarrierLoadStoreTestCode[] = R"(
  let r0 = u32(wg_test_locations[x_0]);
  workgroupBarrier();
  wg_test_locations[x_1] = 1;
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

constexpr const char kWorkgroupUniformLoadMemoryBarrierLoadStoreTestCode[] = R"(
  let r0 = u32(wg_test_locations[x_0]);
  _ = workgroupUniformLoad(&placeholder_wg_var);
  wg_test_locations[x_1] = 1;
  atomicStore(&results.value[shuffled_workgroup * workgroupXSize + id_0].r0, r0);
)";

constexpr const char kStorageMemoryBarrierStoreStoreTestCode[] = R"(
  test_locations.value[x_0] = 1;
  storageBarrier();
  test_locations.value[x_1] = 2;
)";

constexpr const char kTextureMemoryBarrierStoreStoreTestCode[] = R"(
  textureStore(texture_locations, indexToCoord(x_0), vec4u(1));
  textureBarrier();
  textureStore(texture_locations, indexToCoord(x_1), vec4u(2));
  textureBarrier();
  test_locations.value[x_1] = textureLoad(texture_locations, indexToCoord(x_1)).x;
)";

constexpr const char kWorkgroupMemoryBarrierStoreStoreTestCode[] = R"(
  wg_test_locations[x_0] = 1;
  workgroupBarrier();
  wg_test_locations[x_1] = 2;
  workgroupBarrier();
  test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1] = wg_test_locations[x_1];
)";

constexpr const char kWorkgroupUniformLoadMemoryBarrierStoreStoreTestCode[] = R"(
  wg_test_locations[x_0] = 1;
  _ = workgroupUniformLoad(&placeholder_wg_var);
  wg_test_locations[x_1] = 2;
  _ = workgroupUniformLoad(&placeholder_wg_var);
  test_locations.value[shuffled_workgroup * workgroupXSize * stress_params.mem_stride * 2u + x_1] = wg_test_locations[x_1];
)";

std::string getTestCode(MemoryType memType, const std::string& accessPair, bool normalBarrier) {
    if (accessPair == "rw") {
        switch (memType) {
            case MemoryType::NonAtomicStorageClass:
                return kStorageMemoryBarrierLoadStoreTestCode;
            case MemoryType::NonAtomicTextureClass:
                return kTextureMemoryBarrierLoadStoreTestCode;
            default:
                return normalBarrier ? kWorkgroupMemoryBarrierLoadStoreTestCode
                                     : kWorkgroupUniformLoadMemoryBarrierLoadStoreTestCode;
        }
    }
    if (accessPair == "wr") {
        switch (memType) {
            case MemoryType::NonAtomicStorageClass:
                return kStorageMemoryBarrierStoreLoadTestCode;
            case MemoryType::NonAtomicTextureClass:
                return kTextureMemoryBarrierStoreLoadTestCode;
            default:
                return normalBarrier ? kWorkgroupMemoryBarrierStoreLoadTestCode
                                     : kWorkgroupUniformLoadMemoryBarrierStoreLoadTestCode;
        }
    }
    if (accessPair == "ww") {
        switch (memType) {
            case MemoryType::NonAtomicStorageClass:
                return kStorageMemoryBarrierStoreStoreTestCode;
            case MemoryType::NonAtomicTextureClass:
                return kTextureMemoryBarrierStoreStoreTestCode;
            default:
                return normalBarrier ? kWorkgroupMemoryBarrierStoreStoreTestCode
                                     : kWorkgroupUniformLoadMemoryBarrierStoreStoreTestCode;
        }
    }
    std::abort();
}

// Shared runtime guards, mirroring upstream beforeAllSubcases + in-body skips.
void applyCommonSkips(AllFeaturesMaxLimitsGpuTest& t,
                      const std::string& accessValueType,
                      MemoryType memType,
                      bool normalBarrier) {
    if (!normalBarrier && memType != MemoryType::NonAtomicWorkgroupClass) {
        t.skip("workgroupUniformLoad does not have storage memory semantics");
    }
    if (memType == MemoryType::NonAtomicTextureClass && accessValueType == "f16") {
        t.skip("textures do not support f16 access");
    }
    if (accessValueType == "f16" && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("device does not support the shader-f16 feature");
    }
    // Upstream also skips the texture path when the
    // 'readonly_and_readwrite_storage_textures' WGSL language feature is
    // missing; see the header note for why that guard is omitted here.
}

// Runs one barrier litmus configuration: builds the test/result shaders for
// the parameterized memory type and access pair, then runs the stress harness.
void runBarrierTest(AllFeaturesMaxLimitsGpuTest& t,
                    const std::string& resultCode,
                    uint32_t iterations) {
    const std::string accessValueType = t.param<std::string>("accessValueType");
    const MemoryType memType = memoryTypeFromId(t.param<std::string>("memType"));
    const std::string accessPair = t.param<std::string>("accessPair");
    const bool normalBarrier = t.param<bool>("normalBarrier");

    applyCommonSkips(t, accessValueType, memType, normalBarrier);

    std::string testShader =
        buildTestShader(getTestCode(memType, accessPair, normalBarrier), memType,
                        TestType::IntraWorkgroup);
    if (!normalBarrier) {
        testShader += "\nvar<workgroup> placeholder_wg_var : u32;\n";
    }
    const std::string resultShader =
        buildResultShader(resultCode, TestType::IntraWorkgroup, ResultType::TwoBehavior);
    MemoryModelTester memModelTester(
        t, kMemoryModelTestParams, testShader, resultShader,
        accessValueTypeFromId(accessValueType),
        /*useTexture=*/memType == MemoryType::NonAtomicTextureClass);
    memModelTester.run(iterations, /*weakIndex=*/1);
}

CTS_TEST(g, "workgroup_barrier_store_load")
    .desc(
        "Checks whether the workgroup barrier properly synchronizes a non-atomic write and read "
        "on separate threads in the same workgroup. Within a workgroup, the barrier should force "
        "an invocation after the barrier to read a write from an invocation before the barrier.")
    .params([](ParamsBuilder u) {
        return u.combine("accessValueType", accessValueTypeValues())
            .combine("memType", memTypeValues())
            .combine("accessPair", {"wr"})
            .combine("normalBarrier", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string resultCode = R"(
      if (r0 == 1u) {
        atomicAdd(&test_results.seq, 1u);
      } else if (r0 == 0u) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        runBarrierTest(t, resultCode, /*iterations=*/15);
    });

CTS_TEST(g, "workgroup_barrier_load_store")
    .desc(
        "Checks whether the workgroup barrier properly synchronizes a non-atomic write and read "
        "on separate threads in the same workgroup. Within a workgroup, the barrier should force "
        "an invocation before the barrier to not read the write from an invocation after the "
        "barrier.")
    .params([](ParamsBuilder u) {
        return u.combine("accessValueType", accessValueTypeValues())
            .combine("memType", memTypeValues())
            .combine("accessPair", {"rw"})
            .combine("normalBarrier", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string resultCode = R"(
      if (r0 == 0u) {
        atomicAdd(&test_results.seq, 1u);
      } else if (r0 == 1u) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        runBarrierTest(t, resultCode, /*iterations=*/12);
    });

CTS_TEST(g, "workgroup_barrier_store_store")
    .desc(
        "Checks whether the workgroup barrier properly synchronizes non-atomic writes on "
        "separate threads in the same workgroup. Within a workgroup, the barrier should force "
        "the value in memory to be the result of the write after the barrier, not the write "
        "before.")
    .params([](ParamsBuilder u) {
        return u.combine("accessValueType", accessValueTypeValues())
            .combine("memType", memTypeValues())
            .combine("accessPair", {"ww"})
            .combine("normalBarrier", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string resultCode = R"(
      if (mem_x_0 == 2u) {
        atomicAdd(&test_results.seq, 1u);
      } else if (mem_x_0 == 1u) {
        atomicAdd(&test_results.weak, 1u);
      }
    )";
        runBarrierTest(t, resultCode, /*iterations=*/10);
    });

} // namespace
