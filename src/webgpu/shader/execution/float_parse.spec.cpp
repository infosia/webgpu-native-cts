// Ported from gpuweb/cts src/webgpu/shader/execution/float_parse.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 webgpu-native-cts contributors, BSD-3-Clause.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------
TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,float_parse",
    "Execution Tests for float parsing cases");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// runShaderTest (inlined from upstream)
//
// Creates a compute pipeline from the given WGSL, allocates an output buffer
// pre-filled with the 0xdeadbeef sentinel (matching upstream's iterRange fill),
// dispatches one workgroup, and verifies the buffer equals `expected`.
//
// The sentinel fill matches upstream: the TypedArray is constructed via
//   new Float32Array([...iterRange(expected.length, _i => 0xdeadbeef)])
// which reinterprets 0xdeadbeef (uint32) as a float by writing the integer
// value 0xdeadbeef directly into a Float32Array slot — i.e. the JS Number
// 4277009391.0 is stored as the nearest float32, which is 0xCF800000
// (NaN-like bit pattern).  We use makeBufferWithContents with the same
// sentinel float bits so that the shader must actually write to the buffer.
// ---------------------------------------------------------------------------
static void runShaderTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl,
    const float* expected,
    size_t expectedCount)
{
    // Build the sentinel-filled initial buffer contents.
    // JS: new Float32Array([...iterRange(n, _i => 0xdeadbeef)])
    // This stores the float value nearest to 0xDEADBEEF (= 4277009391) in each slot.
    // On a conforming IEEE 754 system the nearest float32 to 4277009391.0 has
    // bit pattern 0x4F7AB740, but what the upstream actually does is assign the
    // JS Number literal 0xdeadbeef (integer) to each Float32Array slot, which
    // converts through Number->float32.  We replicate by storing 0xdeadbeef as
    // a uint32 into the byte representation of a float.
    // NOTE: The spec only requires that the sentinel is a distinct non-zero value
    // so that a non-writing shader would be detectable; we store the same uint32
    // bit pattern for fidelity.
    const uint32_t kSentinel = 0xDEADBEEFu;
    std::vector<uint32_t> sentinelData(expectedCount, kSentinel);

    WGPUBuffer outputBuffer = t.makeBufferWithContents(
        sentinelData.data(),
        sentinelData.size() * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

    // Create compute pipeline with auto layout.
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.compute.module     = shaderModule;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    // Build bind group: binding 0 → outputBuffer.
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer  = outputBuffer;
    entry.offset  = 0;
    entry.size    = static_cast<uint64_t>(expectedCount * sizeof(float));

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Dispatch one workgroup.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    // Compare output buffer to expected bytes.
    t.expectGPUBufferValuesEqual(outputBuffer, expected, expectedCount * sizeof(float));
}

// ---------------------------------------------------------------------------
// kTestFloats — matches upstream kTestFloats exactly.
//
// Each entry has:
//   src    — the WGSL float literal string
//   result — the expected f32 value (as seen after JS `new Float32Array([v])`)
//
// NOTE on result values: upstream stores `data.result` directly into a
// `Float32Array`, which converts the JS number to float32.  All "result"
// values here are either 0.0 or very small numbers below the f32 subnormal
// minimum (~1.4e-45), so they all map to 0.0f in float32.  The struct keeps
// the upstream naming and double value for documentation purposes.
// ---------------------------------------------------------------------------

struct FloatTestEntry {
    const char* key;
    const char* src;
    double resultDouble; // upstream result (for documentation)
    float  result;       // float32 value after JS Float32Array coercion
};

// Upstream digit strings used in src literals.
// 50 zeros per block; 7 blocks = 350 zeros after the decimal point.
#define Z50 "00000000000000000000000000000000000000000000000000"

static const FloatTestEntry kTestFloats[] = {
    {
        "small_pos_zero_exp",
        // 0.{350 zeros}1e+0 → value ~1e-351 → rounds to 0.0 in f32
        "0." Z50 Z50 Z50 Z50 Z50 Z50 Z50 "1e+0",
        0.0,
        0.0f,
    },
    {
        "small_pos_non_zero_exp",
        // 0.{350 zeros}1e+10 → value ~1e-341 → rounds to 0.0 in f32
        "0." Z50 Z50 Z50 Z50 Z50 Z50 Z50 "1e+10",
        0.0,
        0.0f,
    },
    {
        "pos_exp_neg_result",
        // 0.{350 zeros}1e+300 → value = 10^(-351) * 10^300 = 10^(-51)
        // JS result: 1e-51; Float32Array([1e-51]) → 0.0 (below f32 subnormal min ~1.4e-45)
        "0." Z50 Z50 Z50 Z50 Z50 Z50 Z50 "1e+300",
        1e-51,
        0.0f,  // 1e-51 underflows to 0.0 in float32
    },
    {
        "no_exp",
        // 0.{350 zeros}1 → value ~1e-351 → rounds to 0.0 in f32
        "0." Z50 Z50 Z50 Z50 Z50 Z50 Z50 "1",
        0.0,
        0.0f,
    },
    {
        "large_number_small_exp",
        // 1{100 zeros}.0e-350 → value = 10^100 * 10^(-350) = 10^(-250)
        // JS result: 1e-251; Float32Array([1e-251]) → 0.0 (below f32 subnormal min)
        // NOTE: The upstream lists `1 + 50 zeros + 50 zeros` = 1 followed by 100 zeros,
        //       giving 10^100, and then `.0e-350` gives 10^100 * 10^(-350) = 10^(-250).
        //       The upstream result is 1e-251, which may reflect a JS numeric rounding
        //       quirk; in either case Float32Array coercion of both 1e-250 and 1e-251
        //       yields 0.0f, so the expected GPU output is identical.
        "1" Z50 Z50 ".0e-350",
        1e-251,
        0.0f,  // 1e-251 (or 1e-250) underflows to 0.0 in float32
    },
};

#undef Z50

// ---------------------------------------------------------------------------
// Test: valid
// ---------------------------------------------------------------------------
CTS_TEST(g, "valid")
    .desc("Test that floats are parsed correctly")
    .params([](ParamsBuilder u) {
        return u.combine("value", {
            Value(std::string("small_pos_zero_exp")),
            Value(std::string("small_pos_non_zero_exp")),
            Value(std::string("pos_exp_neg_result")),
            Value(std::string("no_exp")),
            Value(std::string("large_number_small_exp")),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string key = t.param<std::string>("value");

        // Find the matching entry.
        const FloatTestEntry* entry = nullptr;
        for (const auto& e : kTestFloats) {
            if (key == e.key) {
                entry = &e;
                break;
            }
        }
        if (entry == nullptr) {
            t.fail("unknown test key: " + key);
        }

        // Build the WGSL shader that stores the literal into a storage buffer.
        const std::string wgsl =
            "struct S {\n"
            "  val: f32,\n"
            "}\n"
            "@group(0) @binding(0) var<storage, read_write> buffer : S;\n"
            "\n"
            "@compute @workgroup_size(1)\n"
            "fn main() {\n"
            "  buffer = S(" + std::string(entry->src) + ");\n"
            "}\n";

        runShaderTest(t, wgsl, &entry->result, 1);
    });

} // namespace
