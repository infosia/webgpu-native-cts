// Ported from gpuweb/cts src/webgpu/shader/execution/statement/compound.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations):
// - runStatementTest() is inlined here from upstream compound.spec.ts (it is
//   also exported for use by other statement tests, but this is the only file
//   in the current batch that calls it, so no shared header is created yet).
// - The upstream helper signature takes a TypedArrayBufferView; here the caller
//   passes a raw int32_t pointer + count — simpler and MSVC-portable.
// - The 'decl' test's kTests table is a plain struct array; the .values member
//   uses std::vector<int32_t> initializer syntax.
// - eval_order uses the flow_control/harness.h C++ header-only port.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "../flow_control/harness.h"

using namespace cts;

namespace {

// Helper: build a WGPUStringView from a string_view without allocation.
WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,statement,compound",
    "\nCompound statement execution.\n");

// ---------------------------------------------------------------------------
// runStatementTest — inline port of upstream's runStatementTest() helper.
//
// Builds a compute shader that:
//   - Uses a module-scope private counter 'count'.
//   - Provides a put(value : ty) helper that appends to an output buffer.
//   - Runs wgsl_main as the compute entry point body.
// Then readbacks the output buffer and compares against 'expectedValues'.
//
// The output buffer is zero-initialized (never pre-filled with expected
// values), matching the harness requirement for readback buffers.
// ---------------------------------------------------------------------------
void runStatementTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const char* ty,
    const int32_t* expectedValues,
    size_t numValues,
    const char* wgsl_main)
{
    const std::string wgsl =
        std::string("struct Outputs {\n  data  : array<") +
        ty + ">,\n};\n"
        "var<private> count: u32 = 0;\n"
        "\n"
        "@group(0) @binding(1) var<storage, read_write> outputs : Outputs;\n"
        "\n"
        "fn put(value : " + ty + ") {\n"
        "  outputs.data[count] = value;\n"
        "  count += 1;\n"
        "}\n"
        "\n"
        "@compute @workgroup_size(1)\n"
        "fn main() {\n"
        "  _ = &outputs;\n"
        "  " + wgsl_main + "\n"
        "}\n";

    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // 'auto' layout
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    // Output buffer: 4 * (1 + 1000) bytes, zero-initialized by the WebGPU
    // implementation (rule: readback buffers must not be pre-filled with the
    // expected result).  The shader's outputs.data[] starts at offset 0
    // within the buffer (no leading count word in this variant).
    static const uint32_t kMaxOutputValues = 1000;
    const uint64_t outputBufferSize = static_cast<uint64_t>(4) * (1 + kMaxOutputValues);

    WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputBufferDesc.size = outputBufferSize;
    outputBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputBufferDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry bindGroupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bindGroupEntry.binding = 1;
    bindGroupEntry.buffer = outputBuffer;
    bindGroupEntry.offset = 0;
    bindGroupEntry.size = outputBufferSize;

    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = bgl;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bindGroupEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Run the shader.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    // Check the output buffer against the expected values.
    t.expectGPUBufferValuesEqual(outputBuffer, expectedValues, numValues * sizeof(int32_t));
}

// ---------------------------------------------------------------------------
// kTests — port of the upstream kTests constant object.
// The 'case' param values are the kTests[i].name strings, in order.
// ---------------------------------------------------------------------------

struct StatementTestCase {
    const char* name;
    const char* src;
    std::vector<int32_t> values;
};

const StatementTestCase kTests[] = {
    {
        "uses",
        // Observe values without conflicting declarations.
        "let x = 1;\n"
        "          put(x);\n"
        "          {\n"
        "            put(x);\n"
        "            let x = x+1;  // The declaration in question\n"
        "            put(x);\n"
        "            {\n"
        "              put(x);\n"
        "            }\n"
        "            put(x);\n"
        "          }\n"
        "          put(x);",
        {1, 1, 2, 2, 2, 1},
    },
    {
        "shadowed",
        // Observe values when shadowed.
        "let x = 1;\n"
        "          put(x);\n"
        "          {\n"
        "            put(x);\n"
        "            let x = x+1;  // The declaration in question\n"
        "            put(x);\n"
        "            {\n"
        "              let x = x+1;  // A shadow\n"
        "              put(x);\n"
        "            }\n"
        "            put(x);\n"
        "          }\n"
        "          put(x);",
        {1, 1, 2, 3, 2, 1},
    },
    {
        "gone",
        // The declaration goes out of scope.
        "{\n"
        "            let x = 2;  // The declaration in question\n"
        "            put(x);\n"
        "          }\n"
        "          let x = 1;\n"
        "          put(x);",
        {2, 1},
    },
};

const size_t kNumTests = sizeof(kTests) / sizeof(kTests[0]);

// ---------------------------------------------------------------------------
// Test: decl
// ---------------------------------------------------------------------------

CTS_TEST(g, "decl")
    .desc("Tests the value of a declared value in a compound statment.")
    .params([](ParamsBuilder u) {
        // Build the 'case' param combine list from kTests names, preserving
        // upstream order (uses, shadowed, gone).
        std::vector<Value> caseValues;
        caseValues.reserve(kNumTests);
        for (size_t i = 0; i < kNumTests; ++i) {
            caseValues.emplace_back(std::string(kTests[i].name));
        }
        return u.combine("case", std::move(caseValues));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string caseName = t.param<std::string>("case");
        const StatementTestCase* tc = nullptr;
        for (size_t i = 0; i < kNumTests; ++i) {
            if (std::string(kTests[i].name) == caseName) {
                tc = &kTests[i];
                break;
            }
        }
        if (tc == nullptr) {
            t.fail("unknown case: " + caseName);
            return;
        }
        runStatementTest(t, "i32", tc->values.data(), tc->values.size(), tc->src);
    });

// ---------------------------------------------------------------------------
// Test: eval_order
//
// Tests evaluation order of compound assignment: lhs is evaluated before rhs.
// The WGSL:
//   arr[0] = 41;
//   arr[idx()] += foo();     // idx() fires first (LHS), then foo() (RHS)
//   if (arr[0] == 42) { ... }
//
// Execution order: main-0 → idx-1 → foo-2 → main-3 → if-true-4.
// foo() sets arr[0]=10 and returns 1.  Since the LHS read (arr[0]=41) happens
// before the RHS mutation, the compound result is 41+1 = 42.
// ---------------------------------------------------------------------------

CTS_TEST(g, "eval_order")
    .desc("Tests evaluation order of compound assignment, lhs is evaluated before rhs")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            // Entrypoint: 4 checkpoints (events 0, 3, 4, and not-reached in else).
            wgsl.entrypoint = wgslTemplate(
                "arr[0] = 41;\n"
                "%%\n"
                "arr[idx()] += foo();\n"
                "%%\n"
                "if (arr[0] == 42) {\n"
                "  %%\n"
                "} else {\n"
                "  %%\n"
                "}\n",
                {f.expect_order(0), f.expect_order(3), f.expect_order(4), f.expect_not_reached()});
            // Extra module-scope code: arr (private), idx(), foo().
            // Two checkpoints: idx fires at event 1, foo fires at event 2.
            wgsl.extra = wgslTemplate(
                "var<private> arr : array<u32, 1>;\n"
                "fn idx() -> u32 {\n"
                "  %%\n"
                "  return 0;\n"
                "}\n"
                "fn foo() -> u32 {\n"
                "  %%\n"
                "  arr[0] = 10;\n"
                "  return 1;\n"
                "}\n",
                {f.expect_order(1), f.expect_order(2)});
            return wgsl;
        });
    });

} // namespace
