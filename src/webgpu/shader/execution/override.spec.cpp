// Ported from gpuweb/cts src/webgpu/shader/execution/override.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,override",
    "Test override execution");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// kOverrideCases: inline port of the upstream kOverrideCases table.
// Each entry holds the WGSL expression assigned to override y: bool.
// With the default override x: u32 = 2, every expression evaluates to true,
// so the shader writes vec4u(4,4,4,4).
// ---------------------------------------------------------------------------
struct OverrideCase {
    const char* name;
    const char* code; // WGSL expression for override y
};

static constexpr std::array<OverrideCase, 3> kOverrideCases = {{
    // logical short-circuit: x appears only on LHS of &&
    {"logical_lhs_override",  "x == 2 && 1 < 2"},
    // logical short-circuit: x appears only on RHS of ||
    {"logical_rhs_override",  "1 > 2 || x == 2"},
    // logical short-circuit: x appears on both sides of ||
    {"logical_both_override", "x > 2 || x == 2"},
}};

// ---------------------------------------------------------------------------
// Test: logical
// Verify that pipeline-overridable constants are correctly evaluated in LHS/RHS
// positions of short-circuiting logical operators (&&, ||).
//
// The shader declares:
//   override x: u32 = 2;
//   override y: bool = <expr>;
// No pipeline override constants are supplied (defaults apply, so x == 2).
// Every expr evaluates to true → shader writes vec4u(4,4,4,4).
//
// Upstream helper inlining:
//   keysOf(kOverrideCases) → iterated via the kOverrideCases array above.
//   checkElementsEqual(got, expected) → inlined as expectGPUBufferValuesPassCheck
//     comparing 4 uint32 elements to [4,4,4,4].
// ---------------------------------------------------------------------------
CTS_TEST(g, "logical")
    .desc("Test replacing an override in the LHS of a logical statement")
    .params([](ParamsBuilder u) {
        // Upstream: u.combine('case', keysOf(kOverrideCases))
        return u.combine("case", {
            Value(std::string("logical_lhs_override")),
            Value(std::string("logical_rhs_override")),
            Value(std::string("logical_both_override")),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto caseName = t.param<std::string>("case");

        // Find the matching case entry.
        const OverrideCase* entry = nullptr;
        for (const auto& c : kOverrideCases) {
            if (caseName == c.name) {
                entry = &c;
                break;
            }
        }
        if (entry == nullptr) {
            t.fail("unknown case: " + caseName);
            return; // unreachable: t.fail() is [[noreturn]]; silences MSVC null-deref warning
        }

        // Build the WGSL shader source with the per-case override expression.
        std::string code = std::string(R"(
override x: u32 = 2;
override y: bool = )") + entry->code + std::string(R"(;

@group(0) @binding(0) var<storage, read_write> v : vec4u;

@compute @workgroup_size(1)
fn main() {
  if (y) {
      v = vec4u(4, 4, 4, 4);
  } else {
      v = vec4u(1, 1, 1, 1);
  }
}
)");

        // Create shader module (no override constants supplied — defaults apply).
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout             = nullptr; // auto layout
        pipelineDesc.compute.module     = shaderModule;
        pipelineDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        // Storage buffer: zero-filled (16 bytes = vec4u).
        // Upstream uses makeBufferWithContents(new Uint32Array([0,0,0,0]), STORAGE|COPY_SRC|COPY_DST).
        // We use makeBufferWithContents with zero data (satisfies the zero-fill rule).
        constexpr uint32_t kBufferSize = 16; // 4 x uint32
        const uint32_t zeros[4] = {0, 0, 0, 0};
        WGPUBuffer buffer = t.makeBufferWithContents(
            zeros,
            kBufferSize,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        // Bind group: pipeline auto-layout, binding 0 = storage buffer.
        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBindGroupEntry entry0 = WGPU_BIND_GROUP_ENTRY_INIT;
        entry0.binding = 0;
        entry0.buffer  = buffer;
        entry0.offset  = 0;
        entry0.size    = kBufferSize;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &entry0;
        WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        // Encode and submit: beginComputePass → setPipeline → setBindGroup → dispatch → end.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

        // Verify readback equals [4, 4, 4, 4].
        // Inlined from upstream checkElementsEqual(got, new Uint32Array([4,4,4,4])).
        t.expectGPUBufferValuesPassCheck(
            buffer,
            [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 16) {
                    return std::string("readback buffer too small (need >= 16 bytes)");
                }
                uint32_t vals[4];
                for (int i = 0; i < 4; ++i) {
                    // Little-endian read of uint32 at offset i*4.
                    vals[i] = static_cast<uint32_t>(actual[i * 4 + 0])
                            | (static_cast<uint32_t>(actual[i * 4 + 1]) << 8)
                            | (static_cast<uint32_t>(actual[i * 4 + 2]) << 16)
                            | (static_cast<uint32_t>(actual[i * 4 + 3]) << 24);
                }
                for (int i = 0; i < 4; ++i) {
                    if (vals[i] != 4u) {
                        std::ostringstream msg;
                        msg << "buffer element [" << i << "]: expected 4, got " << vals[i]
                            << " (full: [" << vals[0] << "," << vals[1]
                            << "," << vals[2] << "," << vals[3] << "])";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            /*srcByteOffset=*/0,
            /*byteLength=*/static_cast<size_t>(kBufferSize));
    });

} // namespace
