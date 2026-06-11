// Ported from gpuweb/cts src/webgpu/shader/execution/statement/increment_decrement.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
// - Upstream's `runStatementTest` is inlined as a static helper
//   `runStatementTest` accepting the fixture by reference.
// - TypedArrayBufferView is replaced by raw C arrays passed with element count.
// - kValue.i32.positive.max  = 2147483647  (0x7fffffff)
//   kValue.i32.negative.min  = -2147483648 (wrap-around value; stored in the
//   shader literal as the decimal constant -2147483648; note WGSL i32 literals
//   can hold i32.negative.min directly via wrapping semantics in overflow tests)
//   kValue.u32.max            = 4294967295  (0xffffffff)
// - The output buffer is zero-filled by WebGPU (STORAGE buffer with no initial
//   data); this matches the upstream pattern — never pre-fill with expected
//   values (F-078 lesson).  The expectation check reads the first N*4 bytes.

#include <array>
#include <climits>
#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> grp = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,statement,increment_decrement",
    "\nIncrement and decrement statement tests.\n");

// ---------------------------------------------------------------------------
// i32/u32 boundary constants (inlined from upstream kValue).
// ---------------------------------------------------------------------------
static constexpr int32_t  kI32Max  = INT32_MAX;   // 2147483647
static constexpr int32_t  kI32Min  = INT32_MIN;   // -2147483648 (avoids MSVC overflow warning from -(2147483648))
static constexpr uint32_t kU32Max  = UINT32_MAX;  // 4294967295

// ---------------------------------------------------------------------------
// runStatementTest: port of upstream's helper.
//
// Parameters
//   t         – the GPU test fixture
//   fmt       – WGSL element type string (e.g. "i32", "u32")
//   expected  – pointer to expected output words (typed as uint32_t for
//               uniform read; reinterpreted per fmt at check time)
//   count     – number of elements in `expected`
//   wgsl_main – WGSL statements that execute inside main() and call push_output
//   global_decl – optional extra module-scope WGSL code (default empty)
//
// The output buffer layout exactly mirrors upstream:
//   binding 1  → Outputs { data: array<fmt> }  (runtime-sized array)
//   size       = 4 * (1 + 1000) = 4004 bytes
// `count` is a private var; the buffer starts at data[0].
// ---------------------------------------------------------------------------
static void runStatementTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const char* fmt,
    const uint32_t* expected,
    size_t count,
    const char* wgsl_main,
    const char* global_decl = "")
{
    // Build the WGSL shader.
    const std::string wgsl =
        std::string("struct Outputs {\n") +
        "  data  : array<" + fmt + ">,\n"
        "};\n"
        "var<private> count: u32 = 0;\n"
        "\n"
        "@group(0) @binding(1) var<storage, read_write> outputs : Outputs;\n"
        "\n"
        "fn push_output(value : " + fmt + ") {\n"
        "  outputs.data[count] = value;\n"
        "  count += 1;\n"
        "}\n"
        "\n" +
        global_decl +
        "\n"
        "@compute @workgroup_size(1)\n"
        "fn main() {\n"
        "  _ = &outputs;\n" +
        wgsl_main +
        "}\n";

    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    // Create pipeline with auto layout.
    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr;  // auto
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = WGPUStringView{"main", 4};
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    // Output buffer: 4 * (1 + 1000) bytes, zero-filled by WebGPU.
    const uint64_t outputBufferSize = 4u * (1u + 1000u);
    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = outputBufferSize;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    // Bind group: binding 1 → outputBuffer.
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 1;
    bgEntry.buffer  = outputBuffer;
    bgEntry.offset  = 0;
    bgEntry.size    = outputBufferSize;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Dispatch.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    // Check output: first `count` elements at the start of the buffer.
    t.expectGPUBufferValuesEqual(outputBuffer, expected, count * sizeof(uint32_t));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

CTS_TEST(grp, "scalar_i32_increment")
    .desc("Tests increment of scalar i32 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-9, 11, kI32Min+1, kI32Max, 1]
        const std::array<int32_t, 5> expected = {-9, 11, kI32Min + 1, kI32Max, 1};
        const std::string wgsl_main =
            std::string("  var a: i32 = -10;\n") +
            "  var b: i32 = 10;\n"
            "  var c: i32 = " + std::to_string(kI32Min) + ";\n"
            "  var d: i32 = " + std::to_string(kI32Max - 1) + ";\n"
            "  var e: i32 = 0;\n"
            "\n"
            "  a++;\n"
            "  b++;\n"
            "  c++;\n"
            "  d++;\n"
            "  e++;\n"
            "\n"
            "  push_output(a);\n"
            "  push_output(b);\n"
            "  push_output(c);\n"
            "  push_output(d);\n"
            "  push_output(e);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_i32_increment_overflow")
    .desc("Tests increment of scalar i32 values which overflows")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [kI32Min]
        const std::array<int32_t, 1> expected = {kI32Min};
        const std::string wgsl_main =
            std::string("  var a: i32 = ") + std::to_string(kI32Max) + ";\n"
            "  a++;\n"
            "  push_output(a);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_u32_increment")
    .desc("Tests increment of scalar u32 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [1, 11, kU32Max]
        const std::array<uint32_t, 3> expected = {1u, 11u, kU32Max};
        const std::string wgsl_main =
            std::string("  var a: u32 = 0;\n") +
            "  var b: u32 = 10;\n"
            "  var c: u32 = " + std::to_string(kU32Max - 1u) + "u;\n"
            "\n"
            "  a++;\n"
            "  b++;\n"
            "  c++;\n"
            "\n"
            "  push_output(a);\n"
            "  push_output(b);\n"
            "  push_output(c);\n";
        runStatementTest(t, "u32",
            expected.data(),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_u32_increment_overflow")
    .desc("Tests increment of scalar u32 values which overflows")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [0]
        const std::array<uint32_t, 1> expected = {0u};
        const std::string wgsl_main =
            std::string("  var a: u32 = ") + std::to_string(kU32Max) + "u;\n"
            "  a++;\n"
            "  push_output(a);\n";
        runStatementTest(t, "u32",
            expected.data(),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_i32_decrement")
    .desc("Tests decrement of scalar i32 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-11, 9, kI32Min, kI32Max-1, -1]
        const std::array<int32_t, 5> expected = {-11, 9, kI32Min, kI32Max - 1, -1};
        const std::string wgsl_main =
            std::string("  var a: i32 = -10;\n") +
            "  var b: i32 = 10;\n"
            "  var c: i32 = " + std::to_string(kI32Min + 1) + ";\n"
            "  var d: i32 = " + std::to_string(kI32Max) + ";\n"
            "  var e: i32 = 0;\n"
            "\n"
            "  a--;\n"
            "  b--;\n"
            "  c--;\n"
            "  d--;\n"
            "  e--;\n"
            "\n"
            "  push_output(a);\n"
            "  push_output(b);\n"
            "  push_output(c);\n"
            "  push_output(d);\n"
            "  push_output(e);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_i32_decrement_underflow")
    .desc("Tests decrement of scalar i32 values which underflow")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [kI32Max]
        const std::array<int32_t, 1> expected = {kI32Max};
        const std::string wgsl_main =
            std::string("  var a: i32 = ") + std::to_string(kI32Min) + ";\n"
            "  a--;\n"
            "  push_output(a);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_u32_decrement")
    .desc("Tests decrement of scalar u32 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [0, 9, kU32Max-1]
        const std::array<uint32_t, 3> expected = {0u, 9u, kU32Max - 1u};
        const std::string wgsl_main =
            std::string("  var a: u32 = 1;\n") +
            "  var b: u32 = 10;\n"
            "  var c: u32 = " + std::to_string(kU32Max) + "u;\n"
            "\n"
            "  a--;\n"
            "  b--;\n"
            "  c--;\n"
            "\n"
            "  push_output(a);\n"
            "  push_output(b);\n"
            "  push_output(c);\n";
        runStatementTest(t, "u32",
            expected.data(),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "scalar_u32_decrement_underflow")
    .desc("Tests decrement of scalar u32 values which underflow")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [kU32Max]
        const std::array<uint32_t, 1> expected = {kU32Max};
        const std::string wgsl_main =
            std::string("  var a: u32 = 0u;\n") +
            "  a--;\n"
            "  push_output(a);\n";
        runStatementTest(t, "u32",
            expected.data(),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "vec2_element_increment")
    .desc("Tests increment of ve2 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-9, 11]
        const std::array<int32_t, 2> expected = {-9, 11};
        const char* wgsl_main =
            "  var a = vec2(-10, 10);\n"
            "\n"
            "  a.x++;\n"
            "  a.g++;\n"
            "\n"
            "  push_output(a.x);\n"
            "  push_output(a.y);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main);
    });

CTS_TEST(grp, "vec3_element_increment")
    .desc("Tests increment of vec3 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-9, 11, kI32Min+1]
        const std::array<int32_t, 3> expected = {-9, 11, kI32Min + 1};
        const std::string wgsl_main =
            std::string("  var a = vec3(-10, 10, ") + std::to_string(kI32Min) + ");\n"
            "\n"
            "  a.x++;\n"
            "  a.g++;\n"
            "  a.z++;\n"
            "\n"
            "  push_output(a.x);\n"
            "  push_output(a.y);\n"
            "  push_output(a.z);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "vec4_element_increment")
    .desc("Tests increment of vec4 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-9, 11, kI32Min+1, kI32Max]
        const std::array<int32_t, 4> expected = {-9, 11, kI32Min + 1, kI32Max};
        const std::string wgsl_main =
            std::string("  var a: vec4<i32> = vec4(-10, 10, ") +
            std::to_string(kI32Min) + ", " + std::to_string(kI32Max - 1) + ");\n"
            "\n"
            "  a.x++;\n"
            "  a.g++;\n"
            "  a.z++;\n"
            "  a.a++;\n"
            "\n"
            "  push_output(a.x);\n"
            "  push_output(a.y);\n"
            "  push_output(a.z);\n"
            "  push_output(a.w);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "vec2_element_decrement")
    .desc("Tests decrement of vec2 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-11, 9]
        const std::array<int32_t, 2> expected = {-11, 9};
        const char* wgsl_main =
            "  var a = vec2(-10, 10);\n"
            "\n"
            "  a.x--;\n"
            "  a.g--;\n"
            "\n"
            "  push_output(a.x);\n"
            "  push_output(a.y);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main);
    });

CTS_TEST(grp, "vec3_element_decrement")
    .desc("Tests decrement of vec3 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-11, 9, kI32Min]
        const std::array<int32_t, 3> expected = {-11, 9, kI32Min};
        const std::string wgsl_main =
            std::string("  var a = vec3(-10, 10, ") + std::to_string(kI32Min + 1) + ");\n"
            "\n"
            "  a.x--;\n"
            "  a.g--;\n"
            "  a.z--;\n"
            "\n"
            "  push_output(a.x);\n"
            "  push_output(a.y);\n"
            "  push_output(a.z);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "vec4_element_decrement")
    .desc("Tests decrement of vec4 values")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [-11, 9, kI32Min, kI32Max-1]
        const std::array<int32_t, 4> expected = {-11, 9, kI32Min, kI32Max - 1};
        const std::string wgsl_main =
            std::string("  var a: vec4<i32> = vec4(-10, 10, ") +
            std::to_string(kI32Min + 1) + ", " + std::to_string(kI32Max) + ");\n"
            "\n"
            "  a.x--;\n"
            "  a.g--;\n"
            "  a.z--;\n"
            "  a.a--;\n"
            "\n"
            "  push_output(a.x);\n"
            "  push_output(a.y);\n"
            "  push_output(a.z);\n"
            "  push_output(a.w);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main.c_str());
    });

CTS_TEST(grp, "frexp_exp_increment")
    .desc("Tests increment can be used on a frexp field")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [2]
        const std::array<int32_t, 1> expected = {2};
        const char* wgsl_main =
            "  var a = frexp(1.23);\n"
            "  a.exp++;\n"
            "  push_output(a.exp);\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main);
    });

CTS_TEST(grp, "single_eval_increment")
    .desc("Tests the left-hand-side reference of an increment is computed only once.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [999, 0, 1, 2, 11, 21, 31]
        const std::array<int32_t, 7> expected = {999, 0, 1, 2, 11, 21, 31};
        const char* wgsl_main =
            "  var a: array<i32,3> = array(10, 20, 30);\n"
            "\n"
            "  push_output(999);\n"
            "  a[bump()]++;\n"
            "  a[bump()]++;\n"
            "  a[bump()]++;\n"
            "  push_output(a[0]);\n"
            "  push_output(a[1]);\n"
            "  push_output(a[2]);\n";
        const char* global_decl =
            "var<private> index_counter: i32 = 0;\n"
            "fn bump() -> i32 {\n"
            "  let result = index_counter;\n"
            "  push_output(result);\n"
            "  index_counter = index_counter + 1;\n"
            "  return result;\n"
            "}\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main,
            global_decl);
    });

CTS_TEST(grp, "single_eval_decrement")
    .desc("Tests the left-hand-side reference of a decrement is computed only once.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Expected: [999, 0, 1, 2, 9, 19, 29]
        const std::array<int32_t, 7> expected = {999, 0, 1, 2, 9, 19, 29};
        const char* wgsl_main =
            "  var a: array<i32,3> = array(10, 20, 30);\n"
            "\n"
            "  push_output(999);\n"
            "  a[bump()]--;\n"
            "  a[bump()]--;\n"
            "  a[bump()]--;\n"
            "  push_output(a[0]);\n"
            "  push_output(a[1]);\n"
            "  push_output(a[2]);\n";
        const char* global_decl =
            "var<private> index_counter: i32 = 0;\n"
            "fn bump() -> i32 {\n"
            "  let result = index_counter;\n"
            "  push_output(result);\n"
            "  index_counter = index_counter + 1;\n"
            "  return result;\n"
            "}\n";
        runStatementTest(t, "i32",
            reinterpret_cast<const uint32_t*>(expected.data()),
            expected.size(),
            wgsl_main,
            global_decl);
    });

} // namespace
