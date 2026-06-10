// Ported from gpuweb/cts src/webgpu/shader/execution/shadow.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,shadow",
    "\nExecution Tests for shadowing\n");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// runShaderTest
//
// Ported from the upstream runShaderTest helper.
// Creates a compute pipeline from |wgsl|, allocates an output storage buffer
// pre-filled with 0xdeadbeef (sentinel — never pre-filled with expected
// values), dispatches one workgroup, then verifies the buffer matches
// |expected| exactly.
//
// The sentinel fill follows upstream: the buffer is intentionally NOT
// zero-filled so that fields the shader forgets to write remain visibly wrong
// as 0xdeadbeef rather than silently matching a zero expectation.
// ---------------------------------------------------------------------------
static void runShaderTest(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view wgsl,
    const uint32_t* expected,
    size_t expectedCount)
{
    // Build sentinel fill: all slots = 0xdeadbeef.
    std::vector<uint32_t> sentinel(expectedCount, 0xdeadbeefu);

    WGPUBuffer outputBuffer = t.makeBufferWithContents(
        sentinel.data(),
        sentinel.size() * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

    // Create compute pipeline with auto layout.
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.compute.module     = shaderModule;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    // Bind group: binding 0 -> outputBuffer.
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer  = outputBuffer;
    entry.offset  = 0;
    entry.size    = static_cast<uint64_t>(expectedCount * sizeof(uint32_t));

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Run the shader.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    // Check output equals expected.
    t.expectGPUBufferValuesEqual(
        outputBuffer,
        expected,
        expectedCount * sizeof(uint32_t));
}

// ---------------------------------------------------------------------------
// declaration
// ---------------------------------------------------------------------------
CTS_TEST(g, "declaration")
    .desc("Test that shadowing is handled correctly")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_var_start: u32,
        my_var_block_shadow: u32,
        my_var_unshadow: u32,
        my_var_param_shadow: u32,
        my_var_param_reshadow: u32,
        my_var_after_func: u32,

        my_const_start: u32,
        my_const_block_shadow: u32,
        my_const_unshadow: u32,
        my_const_param_shadow: u32,
        my_const_param_reshadow: u32,
        my_const_after_func: u32,

        my_let_block_shadow: u32,
        my_let_param_reshadow: u32,
        my_let_after_func: u32,

        my_func_param_shadow: u32,
        my_func_shadow: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      var<private> my_var: u32  = 1;
      const my_const: u32 = 100;

      @compute @workgroup_size(1)
      fn main() {
        let my_let = 200u;

        buffer.my_var_start = my_var;  // 1
        buffer.my_const_start = my_const;  // 100

        {
            var my_var: u32 = 10;
            const my_const: u32 = 110;

            buffer.my_var_block_shadow = my_var;  // 10
            buffer.my_const_block_shadow = my_const;  // 110

            let my_let = 210u;
            buffer.my_let_block_shadow = my_let;  // 210
        }

        buffer.my_var_unshadow = my_var;  // 1
        buffer.my_const_unshadow = my_const;  // 100

        my_func(20, 120, my_let, 300);

        buffer.my_var_after_func = my_var;  // 1
        buffer.my_const_after_func = my_const;  // 100
        buffer.my_let_after_func = my_let;  // 200;
      };

      // Note, defined after |main|
      fn my_func(my_var: u32, my_const: u32, my_let: u32, my_func: u32) {
        buffer.my_var_param_shadow = my_var;  // 20
        buffer.my_const_param_shadow = my_const;  // 120

        buffer.my_func_param_shadow = my_func; // 300

        // Need block here because of scoping rules for parameters
        {
          var my_var = 30u;
          const my_const = 130u;

          buffer.my_var_param_reshadow = my_var; // 30
          buffer.my_const_param_reshadow = my_const; // 130

          let my_let = 220u;
          buffer.my_let_param_reshadow = my_let; // 220

          let my_func: u32 = 310;
          buffer.my_func_shadow = my_func;  // 310
        }
      }
    )";

        constexpr uint32_t expected[] = {
            // my_var
            1,   // my_var_start
            10,  // my_var_block_shadow
            1,   // my_var_unshadow
            20,  // my_var_param_shadow
            30,  // my_var_param_reshadow
            1,   // my_var_after_func
            // my_const
            100, // my_const_start
            110, // my_const_block_shadow
            100, // my_const_unshadow
            120, // my_const_param_shadow
            130, // my_const_param_reshadow
            100, // my_const_after_func
            // my_let
            210, // my_let_block_shadow
            220, // my_let_param_reshadow
            200, // my_let_after_func
            // my_func
            300, // my_func_param_shadow
            310, // my_func_shadow
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ---------------------------------------------------------------------------
// builtin
// ---------------------------------------------------------------------------
CTS_TEST(g, "builtin")
    .desc("Test that shadowing a builtin name is handled correctly")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_max_shadow: u32,
        max_call: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        let max = 400u;
        buffer.my_max_shadow = max;

        my_func();
      };

      fn my_func() {
        buffer.max_call = max(310u, 410u);
      }
    )";

        constexpr uint32_t expected[] = {
            400, // my_max_shadow
            410, // max_call
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ---------------------------------------------------------------------------
// for_loop
// ---------------------------------------------------------------------------
CTS_TEST(g, "for_loop")
    .desc("Test that shadowing is handled correctly with for loops")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_idx_before: u32,
        my_idx_loop: array<u32, 2>,
        my_idx_after: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        var my_idx = 500u;
        buffer.my_idx_before = my_idx; // 500;
        for (var my_idx = 0u; my_idx < 2u; my_idx++) {
          let pos = my_idx;
          var my_idx = 501u + my_idx;
          buffer.my_idx_loop[pos] = my_idx;  // 501, 502
        }
        buffer.my_idx_after = my_idx; // 500;
      };
    )";

        constexpr uint32_t expected[] = {
            500, // my_idx_before
            501, // my_idx_loop[0]
            502, // my_idx_loop[1]
            500, // my_idx_after
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ---------------------------------------------------------------------------
// while
// ---------------------------------------------------------------------------
CTS_TEST(g, "while")
    .desc("Test that shadowing is handled correctly with while loops")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_idx_before: u32,
        my_idx_loop: array<u32, 2>,
        my_idx_after: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        var my_idx = 0u;
        buffer.my_idx_before = my_idx; // 0;

        var counter = 0u;
        while (counter < 2) {
          var my_idx = 500u + counter;
          buffer.my_idx_loop[counter] = my_idx;  // 500, 501

          counter += 1;
        }

        buffer.my_idx_after = my_idx; // 0;
      };
    )";

        constexpr uint32_t expected[] = {
            0,   // my_idx_before
            500, // my_idx_loop[0]
            501, // my_idx_loop[1]
            0,   // my_idx_after
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
CTS_TEST(g, "loop")
    .desc("Test that shadowing is handled correctly with loops")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_idx_before: u32,
        my_idx_loop: array<u32, 2>,
        my_idx_continuing: array<u32, 2>,
        my_idx_after: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        var my_idx = 0u;
        buffer.my_idx_before = my_idx; // 0;

        var counter = 0u;
        loop {
          var my_idx = 500u + counter;
          buffer.my_idx_loop[counter] = my_idx;  // 500, 501


          continuing {
            var my_idx = 600u + counter;
            buffer.my_idx_continuing[counter] = my_idx; // 600, 601

            counter += 1;
            break if counter == 2;
          }
        }
        buffer.my_idx_after = my_idx; // 0;
      };
    )";

        constexpr uint32_t expected[] = {
            0,   // my_idx_before
            500, // my_idx_loop[0]
            501, // my_idx_loop[1]
            600, // my_idx_continuing[0]
            601, // my_idx_continuing[1]
            0,   // my_idx_after
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ---------------------------------------------------------------------------
// switch
// ---------------------------------------------------------------------------
CTS_TEST(g, "switch")
    .desc("Test that shadowing is handled correctly with a switch")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_idx_before: u32,
        my_idx_case: u32,
        my_idx_default: u32,
        my_idx_after: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        var my_idx = 0u;
        buffer.my_idx_before = my_idx; // 0;

        for (var i = 0; i < 2; i++) {
          switch (i) {
            case 0: {
              var my_idx = 10u;
              buffer.my_idx_case = my_idx; // 10
            }
            default: {
              var my_idx = 20u;
              buffer.my_idx_default = my_idx; // 20
            }
          }
        }

        buffer.my_idx_after = my_idx; // 0;
      };
    )";

        constexpr uint32_t expected[] = {
            0,  // my_idx_before
            10, // my_idx_case
            20, // my_idx_default
            0,  // my_idx_after
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ---------------------------------------------------------------------------
// if
// ---------------------------------------------------------------------------
CTS_TEST(g, "if")
    .desc("Test that shadowing is handled correctly with a switch")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr std::string_view wgsl = R"(
      struct S {
        my_idx_before: u32,
        my_idx_if: u32,
        my_idx_elseif: u32,
        my_idx_else: u32,
        my_idx_after: u32,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        var my_idx = 0u;
        buffer.my_idx_before = my_idx; // 0;

        for (var i = 0; i < 3; i++) {
          if i == 0 {
            var my_idx = 10u;
            buffer.my_idx_if = my_idx; // 10
          } else if i == 1 {
            var my_idx = 20u;
            buffer.my_idx_elseif = my_idx; // 20
          } else {
            var my_idx = 30u;
            buffer.my_idx_else = my_idx; // 30
          }
        }

        buffer.my_idx_after = my_idx; // 0;
      };
    )";

        constexpr uint32_t expected[] = {
            0,  // my_idx_before
            10, // my_idx_if
            20, // my_idx_elseif
            30, // my_idx_else
            0,  // my_idx_after
        };

        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

} // namespace
