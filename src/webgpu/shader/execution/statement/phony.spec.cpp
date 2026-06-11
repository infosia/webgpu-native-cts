// Ported from gpuweb/cts src/webgpu/shader/execution/statement/phony.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
// - The upstream `keysOf(kTests)` call iterates JS object keys in insertion
//   order. The C++ port replicates the same order using an explicit array of
//   names + a struct table — the case param values ("literal", "call", etc.)
//   and their ordering are identical to upstream.
// - `TypedArrayBufferView` is Int32Array (i32) for every case; the C++ port
//   uses int32_t values directly.
// - `expectGPUBufferValuesEqual` is called with the i32 values array;
//   upstream's helper maps to t.expectGPUBufferValuesEqual in the harness.
// - The output buffer starts at `data[0]` which is 0 (zero-initialized by
//   WebGPU); the WGSL main reads `x = outputs.data[0]` but never uses x in
//   these cases, so the initial value does not affect correctness. The buffer
//   is NOT pre-filled with expected values (readback-buffer zero-init rule).

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,statement,phony",
    "Phony assignment execution tests");

// ---------------------------------------------------------------------------
// Inline port of runStatementTest() from phony.spec.ts.
//
// Builds and runs a compute shader that uses a `put(value)` helper to write
// values into a storage buffer, then checks that the buffer contains
// the expected i32 values at positions [0..N-1].
//
// The WGSL template is a faithful copy of the upstream helper:
//   - binding 1: the storage output buffer (array<i32>)
//   - var<private> count: u32 — tracks how many values have been written
//   - fn put(value: i32) -> i32 — writes and increments count
//   - @compute @workgroup_size(1) fn main() — reads data[0] into x, then
//     executes wgsl_main
// ---------------------------------------------------------------------------
static void runStatementTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl_main,
    const std::vector<int32_t>& expectedValues)
{
    // Build the WGSL source. Using std::string concatenation; the upstream
    // scalar type is hardcoded as "i32" for all phony cases.
    const std::string wgsl =
        std::string(
            "struct Outputs {\n"
            "  data  : array<i32>,\n"
            "};\n"
            "var<private> count: u32 = 0;\n"
            "\n"
            "@group(0) @binding(1) var<storage, read_write> outputs : Outputs;\n"
            "\n"
            "fn put(value : i32) -> i32 {\n"
            "  outputs.data[count] = value;\n"
            "  count += 1;\n"
            "  return value;\n"
            "}\n"
            "\n"
            "@compute @workgroup_size(1)\n"
            "fn main() {\n"
            "  let x = outputs.data[0];\n"
            "  ") +
        wgsl_main +
        "\n}\n";

    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // auto layout
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = WGPUStringView{
        "main",
        std::string_view("main").size()};
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    // maxOutputValues = 1000; buffer size = 4 * (1 + 1000) bytes (i32).
    const uint64_t maxOutputValues = 1000;
    const uint64_t outputBufferSize = 4u * (1u + maxOutputValues);

    WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputBufferDesc.size = outputBufferSize;
    outputBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    // Zero-initialized by WebGPU (no mappedAtCreation, no pre-fill).
    WGPUBuffer outputBuffer = t.createBufferTracked(outputBufferDesc);

    // The pipeline uses auto layout; retrieve bind group layout at index 0.
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 1;
    entry.buffer = outputBuffer;
    entry.offset = 0;
    entry.size = outputBufferSize;

    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = bgl;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    // Release the layout getter result (not a create*Tracked handle).
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    // Check the prefix of the output buffer against the expected values.
    const size_t checkBytes = expectedValues.size() * sizeof(int32_t);
    t.expectGPUBufferValuesEqual(
        outputBuffer,
        expectedValues.data(),
        checkBytes,
        /*srcByteOffset=*/0);
}

// ---------------------------------------------------------------------------
// kTests — faithful port of the upstream kTests object (same names, same
// order, same src/values).  Checked against Int32Array(values) in upstream.
// ---------------------------------------------------------------------------

struct TestCase {
    const char* name;
    const char* src;
    std::vector<int32_t> values;
};

// The cases are defined in insertion order to mirror upstream keysOf() order.
static const std::array<TestCase, 6> kTests{{
    {
        "literal",
        "_ = true;",
        {0},
    },
    {
        "call",
        // RHS evaluated once.
        "_ = put(42i);",
        {42, 0},
    },
    {
        "call_in_subexpr",
        "_ = put(42i) + 1;",
        {42, 0},
    },
    {
        "nested_call",
        "_ = put(put(42)+1);",
        {42, 43, 0},
    },
    {
        "unreached",
        "if (false) { _ = put(1); }",
        {0},
    },
    {
        "loop",
        "for (var i=5; i<7; i++) { _ = put(i); }",
        {5, 6, 0},
    },
}};

// Build the list of case names for the combine param (mirrors keysOf(kTests)).
static std::vector<Value> kTestCaseNames() {
    std::vector<Value> names;
    names.reserve(kTests.size());
    for (const TestCase& tc : kTests) {
        names.emplace_back(std::string(tc.name));
    }
    return names;
}

// ---------------------------------------------------------------------------
// Test: executes
// ---------------------------------------------------------------------------

CTS_TEST(g, "executes")
    .desc("Tests the RHS is evaluated once when the statement is executed.")
    .params([](ParamsBuilder u) {
        return u.combine("case", kTestCaseNames());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string caseName = t.param<std::string>("case");

        // Find the matching test case.
        const TestCase* tc = nullptr;
        for (const TestCase& candidate : kTests) {
            if (candidate.name == caseName) {
                tc = &candidate;
                break;
            }
        }
        if (tc == nullptr) {
            t.fail("internal error: unknown case param: " + caseName);
        }

        runStatementTest(t, tc->src, tc->values);
    });

} // namespace
