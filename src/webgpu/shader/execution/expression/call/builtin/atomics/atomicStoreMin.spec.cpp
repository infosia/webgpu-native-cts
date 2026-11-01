// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicStoreMin.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port note: WGPUFeatureName_AtomicVec2uMinMax is not in wgpu-native's webgpu.h, so the test skips
// there, as upstream does on a device without the feature.

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "common/webgpu/backend.h"
#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicStoreMin",
    "\nAtomically stores the min value v in the atomic object pointed to by atomic_ptr.\n");

bool hasAtomicVec2uMinMax(GpuTest& t) {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    return wgpuDeviceHasFeature(t.device(), WGPUFeatureName_AtomicVec2uMinMax) != WGPU_FALSE;
#else
    (void)t;
    return false;
#endif
}

CTS_TEST(testGroup, "store_min_basic")
    .desc(R"(
AS is storage
T is vec2u

fn atomicStoreMin(atomic_ptr: ptr<AS, atomic<T>, read_write>, v: T)

Tests that multiple invocations of atomicStoreMin to the same location returns
the composite min of all values written.
)")
    .params([](ParamsBuilder u) {
        return u.combine("workgroupSize", onlyWorkgroupSizeValues())
            .combine("dispatchSize", dispatchSizeValues())
            .combine("rndMultiplyX", {0, 1, 3163, 176284061, 268435456})
            .combine("rndMultiplyY", {0, 1, 41609, 138545483, 268435456});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!hasAtomicVec2uMinMax(t)) {
            t.skip("Missing atomic-vec2u-min-max feature");
        }
        const uint32_t workgroupSize = static_cast<uint32_t>(t.param<int64_t>("workgroupSize"));
        const uint32_t dispatchSize = static_cast<uint32_t>(t.param<int64_t>("dispatchSize"));
        const uint32_t rndMultiplyX = static_cast<uint32_t>(t.param<int64_t>("rndMultiplyX"));
        const uint32_t rndMultiplyY = static_cast<uint32_t>(t.param<int64_t>("rndMultiplyY"));

        const std::string wgsl = R"(
      enable atomic_vec2u_min_max;
      @group(0) @binding(0)
      var<storage, read_write> output : atomic<vec2u>;

      @compute @workgroup_size()" +
                                 std::to_string(workgroupSize) + R"()
      fn main(
          @builtin(global_invocation_id) global_invocation_id : vec3<u32>,
          ) {
          let reversal = vec2u()" +
                                 std::to_string(workgroupSize) + R"( - global_invocation_id[0],
                               )" +
                                 std::to_string(dispatchSize) + R"( - global_invocation_id[1]);
        let id = reversal * vec2u(  )" +
                                 std::to_string(rndMultiplyX) + " , " + std::to_string(rndMultiplyY) + R"();

        // All invocations store to the same location
        atomicStoreMin(&output, id);
      }
    )";

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);

        // For a single vec2u atomic we need 2 u32s.
        // Initialize with max u32 value.
        const uint32_t kU32Max = std::numeric_limits<uint32_t>::max();
        const uint32_t data[2] = {kU32Max, kU32Max};
        WGPUBuffer outputBuffer =
            t.makeBufferWithContents(data, sizeof(data), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

        WGPUBindGroup bindGroup = makeAutoBindGroup(t, pipeline, {{outputBuffer, sizeof(data)}});

        // Run the shader.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        // Dispatch will be Y for simplicity.
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, dispatchSize, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        // Read back the buffer
        const std::vector<uint32_t> result = readBackU32(t, outputBuffer, 2);

        uint32_t expectedMinX = kU32Max;
        uint32_t expectedMinY = kU32Max;

        // Simulate shader execution to find the max
        for (uint32_t x = 0; x < workgroupSize; ++x) {
            for (uint32_t y = 0; y < dispatchSize; ++y) {
                const uint32_t xReversal = workgroupSize - x;
                const uint32_t yReversal = dispatchSize - y;
                const uint32_t idX = rndMultiplyX * xReversal;
                const uint32_t idY = rndMultiplyY * yReversal;

                if (idY < expectedMinY || (idY == expectedMinY && idX < expectedMinX)) {
                    expectedMinX = idX;
                    expectedMinY = idY;
                }
            }
        }

        if (result[0] != expectedMinX || result[1] != expectedMinY) {
            t.fail("Unexpected value in output: [" + std::to_string(result[0]) + ", " +
                   std::to_string(result[1]) + "], expected: [" + std::to_string(expectedMinX) + ", " +
                   std::to_string(expectedMinY) + "]");
        }
    });

} // namespace
