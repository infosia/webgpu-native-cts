// Ported from gpuweb/cts src/webgpu/shader/execution/padding.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ============================================================
// Test group
// ============================================================
TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,padding",
    "\nExecution Tests for preservation of padding bytes in structures and arrays.\n");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// runShaderTest (inlined from upstream)
//
// Ported from the upstream runShaderTest helper (padding.spec.ts).
// Creates a compute pipeline from |wgsl|, allocates an output storage buffer
// pre-filled with the 0xdeadbeef sentinel (matching upstream's iterRange fill),
// dispatches one workgroup, then verifies the buffer equals |expected| exactly.
//
// Upstream allocates: makeBufferWithContents(
//   new Uint32Array([...iterRange(expected.length, _i => 0xdeadbeef)]),
//   GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC
// )
// Padding slots in |expected| are therefore also 0xdeadbeef, so an un-written
// padding byte that stays 0xdeadbeef will correctly match the expectation.
// The buffer is intentionally NOT pre-filled with zeros so that a shader that
// forgets to write a non-padding byte is detected.
// ---------------------------------------------------------------------------
static void runShaderTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl,
    const uint32_t* expected,
    size_t expectedCount)
{
    // Build sentinel-filled initial contents: all slots = 0xdeadbeef.
    std::vector<uint32_t> sentinel(expectedCount, 0xDEADBEEFu);

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

    // Bind group: binding 0 → outputBuffer.
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

    // Verify the buffer matches the expected pattern.
    t.expectGPUBufferValuesEqual(
        outputBuffer,
        expected,
        expectedCount * sizeof(uint32_t));
}

// ---------------------------------------------------------------------------
// floatBits: reinterpret a float as its uint32 bit pattern (MSVC-portable).
// ---------------------------------------------------------------------------
static uint32_t floatBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// ============================================================
// struct_implicit
// ============================================================
CTS_TEST(g, "struct_implicit")
    .desc(
        "Test that padding bytes in between structure members are preserved.\n\n"
        "This test defines a structure that has implicit padding and creates a read-write storage\n"
        "buffer with that structure type. The shader assigns the whole variable at once, and we\n"
        "then test that data in the padding bytes was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl = R"(
      struct S {
        a : u32,
        // 12 bytes of padding
        b : vec3<u32>,
        // 4 bytes of padding
        c : vec2<u32>,
        // 8 bytes of padding
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        buffer = S(0x12345678, vec3(0xabcdef01), vec2(0x98765432));
      }
    )";
        // clang-format off
        const uint32_t expected[] = {
            // a : u32
            0x12345678u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // b : vec3<u32>
            0xABCDEF01u, 0xABCDEF01u, 0xABCDEF01u, 0xDEADBEEFu,
            // c : vec2<u32>
            0x98765432u, 0x98765432u, 0xDEADBEEFu, 0xDEADBEEFu,
        };
        // clang-format on
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// struct_explicit
// ============================================================
CTS_TEST(g, "struct_explicit")
    .desc(
        "Test that padding bytes in between structure members are preserved.\n\n"
        "This test defines a structure with explicit padding attributes and creates a read-write\n"
        "storage buffer with that structure type. The shader assigns the whole variable at once,\n"
        "and we then test that data in the padding bytes was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl = R"(
      struct S {
        a : u32,
        // 12 bytes of padding
        @align(16) @size(20) b : u32,
        // 16 bytes of padding
        @size(12) c : u32,
        // 8 bytes of padding
      }
      @group(0) @binding(0) var<storage, read_write> buffer : S;

      @compute @workgroup_size(1)
      fn main() {
        buffer = S(0x12345678, 0xabcdef01, 0x98765432);
      }
    )";
        // clang-format off
        const uint32_t expected[] = {
            // a : u32
            0x12345678u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // @align(16) @size(20) b : u32
            0xABCDEF01u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // @size(12) c : u32
            0x98765432u, 0xDEADBEEFu, 0xDEADBEEFu,
        };
        // clang-format on
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// struct_nested
// ============================================================
CTS_TEST(g, "struct_nested")
    .desc(
        "Test that padding bytes in nested structures are preserved.\n\n"
        "This test defines a set of nested structures that have padding and creates a read-write\n"
        "storage buffer with the root structure type. The shader assigns the whole variable at\n"
        "once, and we then test that data in the padding bytes was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl = R"(
      // Size of S1 is 48 bytes.
      // Alignment of S1 is 16 bytes.
      struct S1 {
        a : u32,
        // 12 bytes of padding
        b : vec3<u32>,
        // 4 bytes of padding
        c : vec2<u32>,
        // 8 bytes of padding
      }

      // Size of S2 is 112 bytes.
      // Alignment of S2 is 48 bytes.
      struct S2 {
        a2 : u32,
        // 12 bytes of padding
        b2 : S1,
        c2 : S1,
      }

      // Size of S3 is 144 bytes.
      // Alignment of S3 is 48 bytes.
      struct S3 {
        a3 : S1,
        b3 : S2,
        c3 : S2,
      }

      @group(0) @binding(0) var<storage, read_write> buffer : S3;

      @compute @workgroup_size(1)
      fn main() {
        buffer = S3();
      }
    )";
        // clang-format off
        const uint32_t expected[] = {
            // a3 : S1
            // a3.a : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // a3.b : vec3<u32>
            0x00000000u, 0x00000000u, 0x00000000u, 0xDEADBEEFu,
            // a3.c : vec2<u32>
            0x00000000u, 0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu,

            // b3 : S2
            // b3.a2 : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // b3.b2 : S1
            // b3.b2.a : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // b3.b2.b : vec3<u32>
            0x00000000u, 0x00000000u, 0x00000000u, 0xDEADBEEFu,
            // b3.b2.c : vec2<u32>
            0x00000000u, 0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu,
            // b3.c2 : S1
            // b3.c2.a : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // b3.c2.b : vec3<u32>
            0x00000000u, 0x00000000u, 0x00000000u, 0xDEADBEEFu,
            // b3.c2.c : vec2<u32>
            0x00000000u, 0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu,

            // c3 : S2
            // c3.a2 : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // c3.b2 : S1
            // c3.b2.a : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // c3.b2.b : vec3<u32>
            0x00000000u, 0x00000000u, 0x00000000u, 0xDEADBEEFu,
            // c3.b2.c : vec2<u32>
            0x00000000u, 0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu,
            // c3.c2 : S1
            // c3.c2.a : u32
            0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            // c3.c2.b : vec3<u32>
            0x00000000u, 0x00000000u, 0x00000000u, 0xDEADBEEFu,
            // c3.c2.c : vec2<u32>
            0x00000000u, 0x00000000u, 0xDEADBEEFu, 0xDEADBEEFu,
        };
        // clang-format on
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// array_of_vec3
// ============================================================
CTS_TEST(g, "array_of_vec3")
    .desc(
        "Test that padding bytes in between array elements are preserved.\n\n"
        "This test defines creates a read-write storage buffer with type array<vec3, 4>. The\n"
        "shader assigns the whole variable at once, and we then test that data in the padding\n"
        "bytes was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl = R"(
      @group(0) @binding(0) var<storage, read_write> buffer : array<vec3<u32>, 4>;

      @compute @workgroup_size(1)
      fn main() {
        buffer = array<vec3<u32>, 4>(
          vec3(0x12345678),
          vec3(0xabcdef01),
          vec3(0x98765432),
          vec3(0x0f0f0f0f),
        );
      }
    )";
        // clang-format off
        const uint32_t expected[] = {
            // buffer[0]
            0x12345678u, 0x12345678u, 0x12345678u, 0xDEADBEEFu,
            // buffer[1]
            0xABCDEF01u, 0xABCDEF01u, 0xABCDEF01u, 0xDEADBEEFu,
            // buffer[2]
            0x98765432u, 0x98765432u, 0x98765432u, 0xDEADBEEFu,
            // buffer[3]
            0x0F0F0F0Fu, 0x0F0F0F0Fu, 0x0F0F0F0Fu, 0xDEADBEEFu,
        };
        // clang-format on
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// array_of_vec3h
// Requires shader-f16 feature; runtime-skipped when unavailable.
// ============================================================
CTS_TEST(g, "array_of_vec3h")
    .desc(
        "Test that padding bytes in between array elements are preserved when f16 elements are\n"
        "used.\n\n"
        "This test defines creates a read-write storage buffer with type array<vec3h, 4>. The\n"
        "shader assigns the whole variable at once, and we then test that data in the padding\n"
        "bytes was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const std::string wgsl = R"(
      enable f16;
      @group(0) @binding(0) var<storage, read_write> buffer : array<vec3<f16>, 4>;

      @compute @workgroup_size(1)
      fn main() {
        buffer = array<vec3<f16>, 4>(
          vec3(1h),
          vec3(2h),
          vec3(3h),
          vec3(4h),
        );
      }
    )";
        // Each vec3<f16> element occupies 8 bytes (3x f16 + 2 bytes padding).
        // The buffer is stored as Uint32Array (2 uint32 per element = 8 bytes):
        //   slot 0: low 16 bits = first f16, high 16 bits = second f16
        //   slot 1: low 16 bits = third f16,  high 16 bits = 0xdead (padding sentinel)
        // f16 bit patterns: 1h=0x3c00, 2h=0x4000, 3h=0x4200, 4h=0x4400.
        // Upstream expected (little-endian layout):
        //   buffer[0]: 0x3c003c00, 0xdead3c00
        //   buffer[1]: 0x40004000, 0xdead4000
        //   buffer[2]: 0x42004200, 0xdead4200
        //   buffer[3]: 0x44004400, 0xdead4400
        // The 0xdead... values: upper 16 bits of the second uint32 are padding (0xdead from
        // 0xdeadbeef sentinel), lower 16 bits hold the third lane of the vec3<f16>.
        // clang-format off
        const uint32_t expected[] = {
            // buffer[0]: vec3(1h) = [0x3c00, 0x3c00, 0x3c00] + 2 bytes padding
            0x3c003c00u, 0xDEAD3c00u,
            // buffer[1]: vec3(2h) = [0x4000, 0x4000, 0x4000] + 2 bytes padding
            0x40004000u, 0xDEAD4000u,
            // buffer[2]: vec3(3h) = [0x4200, 0x4200, 0x4200] + 2 bytes padding
            0x42004200u, 0xDEAD4200u,
            // buffer[3]: vec3(4h) = [0x4400, 0x4400, 0x4400] + 2 bytes padding
            0x44004400u, 0xDEAD4400u,
        };
        // clang-format on
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// array_of_vec3h,elementwise
// Requires shader-f16 feature; runtime-skipped when unavailable.
// ============================================================
CTS_TEST(g, "array_of_vec3h,elementwise")
    .desc(
        "Test that padding bytes in between array elements are preserved when f16 elements are\n"
        "used.\n\n"
        "This test defines creates a read-write storage buffer with type array<vec3h, 4>. The\n"
        "shader assigns one element per thread, and we then test that data in the padding bytes\n"
        "was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        // Note: this test dispatches @workgroup_size(4) — one thread per element.
        const std::string wgsl = R"(
      enable f16;
      @group(0) @binding(0) var<storage, read_write> buffer : array<vec3<f16>>;

      @compute @workgroup_size(4)
      fn main(@builtin(local_invocation_index) lid : u32) {
        buffer[lid] = vec3h(f16(lid + 1));
      }
    )";
        // Same layout as array_of_vec3h above.
        // clang-format off
        const uint32_t expected[] = {
            // buffer[0]: vec3(1h)
            0x3c003c00u, 0xDEAD3c00u,
            // buffer[1]: vec3(2h)
            0x40004000u, 0xDEAD4000u,
            // buffer[2]: vec3(3h)
            0x42004200u, 0xDEAD4200u,
            // buffer[3]: vec3(4h)
            0x44004400u, 0xDEAD4400u,
        };
        // clang-format on
        // The elementwise shader uses @workgroup_size(4) for 1 dispatch workgroup → 4 threads.
        // We reuse the same runShaderTest helper which dispatches workgroups(1,1,1).
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// array_of_struct
// ============================================================
CTS_TEST(g, "array_of_struct")
    .desc(
        "Test that padding bytes in between array elements are preserved.\n\n"
        "This test defines creates a read-write storage buffer with type array<S, 4>, where S is\n"
        "a structure that contains padding bytes. The shader assigns the whole variable at once,\n"
        "and we then test that data in the padding bytes was preserved.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl = R"(
      struct S {
        a : u32,
        b : vec3<u32>,
      }
      @group(0) @binding(0) var<storage, read_write> buffer : array<S, 3>;

      @compute @workgroup_size(1)
      fn main() {
        buffer = array<S, 3>(
          S(0x12345678, vec3(0x0f0f0f0f)),
          S(0xabcdef01, vec3(0x7c7c7c7c)),
          S(0x98765432, vec3(0x18181818)),
        );
      }
    )";
        // struct S: a(u32) at offset 0, 12 bytes padding, b(vec3<u32>) at offset 16, 4 bytes padding.
        // Each element is 32 bytes = 8 uint32 words.
        // clang-format off
        const uint32_t expected[] = {
            // buffer[0]
            0x12345678u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            0x0F0F0F0Fu, 0x0F0F0F0Fu, 0x0F0F0F0Fu, 0xDEADBEEFu,
            // buffer[1]
            0xABCDEF01u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            0x7C7C7C7Cu, 0x7C7C7C7Cu, 0x7C7C7C7Cu, 0xDEADBEEFu,
            // buffer[2]
            0x98765432u, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
            0x18181818u, 0x18181818u, 0x18181818u, 0xDEADBEEFu,
        };
        // clang-format on
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// vec3
// ============================================================
CTS_TEST(g, "vec3")
    .desc(
        "Test padding bytes are preserved when assigning to a variable of type vec3 (without a\n"
        "struct).")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl = R"(
      @group(0) @binding(0) var<storage, read_write> buffer : vec3<u32>;

      @compute @workgroup_size(1)
      fn main() {
        buffer = vec3<u32>(0x12345678, 0xabcdef01, 0x98765432);
      }
    )";
        const uint32_t expected[] = {
            0x12345678u, 0xABCDEF01u, 0x98765432u, 0xDEADBEEFu,
        };
        runShaderTest(t, wgsl, expected, sizeof(expected) / sizeof(expected[0]));
    });

// ============================================================
// matCx3
//
// params: columns in {2, 3, 4}, use_struct in {true, false}
// (beginSubcases after combine — subcases, not cases).
// Expected values are computed from the float formulas in the upstream.
// ============================================================
CTS_TEST(g, "matCx3")
    .desc(
        "Test padding bytes are preserved when assigning to a variable of type matCx3.")
    .params([](ParamsBuilder u) {
        return u
            .combine("columns", {2, 3, 4})
            .combine("use_struct", {true, false})
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto cols       = t.param<int64_t>("columns");
        auto use_struct = t.param<bool>("use_struct");

        // Build the WGSL shader.
        // mat${cols}x3<f32>: each column is a vec3<f32> stored with 4-byte padding → 4 floats.
        std::string structAlias = use_struct
            ? "struct S { m : Mat } alias Type = S;"
            : "alias Type = Mat;";

        std::string wgsl =
            "alias Mat = mat" + std::to_string(cols) + "x3<f32>;\n"
            + structAlias + "\n"
            + R"(@group(0) @binding(0) var<storage, read_write> buffer : Type;

      @compute @workgroup_size(1)
      fn main() {
        var m : Mat;
        for (var c = 0u; c < )" + std::to_string(cols) + R"(; c++) {
          m[c] = vec3(f32(c*3 + 1), f32(c*3 + 2), f32(c*3 + 3));
        }
        buffer = Type(m);
      }
    )";

        // Build expected values:
        // For each column c: f32(c*3+1), f32(c*3+2), f32(c*3+3), then 0xdeadbeef padding.
        // Upstream: f_values is a Float32Array; u_values shares the same buffer.
        //   f_values[c*4+0] = c*3+1, f_values[c*4+1] = c*3+2, f_values[c*4+2] = c*3+3
        //   u_values[c*4+3] = 0xdeadbeef
        size_t totalWords = static_cast<size_t>(cols) * 4;
        std::vector<uint32_t> expected(totalWords);
        for (int c = 0; c < static_cast<int>(cols); ++c) {
            expected[static_cast<size_t>(c) * 4 + 0] = floatBits(static_cast<float>(c * 3 + 1));
            expected[static_cast<size_t>(c) * 4 + 1] = floatBits(static_cast<float>(c * 3 + 2));
            expected[static_cast<size_t>(c) * 4 + 2] = floatBits(static_cast<float>(c * 3 + 3));
            expected[static_cast<size_t>(c) * 4 + 3] = 0xDEADBEEFu;
        }

        runShaderTest(t, wgsl, expected.data(), expected.size());
    });

// ============================================================
// array_of_matCx3
//
// params: columns in {2, 3, 4}, use_struct in {true, false}
// (beginSubcases after combine — subcases, not cases).
// ============================================================
CTS_TEST(g, "array_of_matCx3")
    .desc(
        "Test that padding bytes in between array elements are preserved.\n\n"
        "This test defines creates a read-write storage buffer with type array<matCx3<f32>, 4>.\n"
        "The shader assigns the whole variable at once, and we then test that data in the padding\n"
        "bytes was preserved.")
    .params([](ParamsBuilder u) {
        return u
            .combine("columns", {2, 3, 4})
            .combine("use_struct", {true, false})
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto cols       = t.param<int64_t>("columns");
        auto use_struct = t.param<bool>("use_struct");

        std::string structAlias = use_struct
            ? "struct S { m : Mat } alias Type = S;"
            : "alias Type = Mat;";

        std::string wgsl =
            "alias Mat = mat" + std::to_string(cols) + "x3<f32>;\n"
            + structAlias + "\n"
            + R"(@group(0) @binding(0) var<storage, read_write> buffer : array<Type, 4>;

    @compute @workgroup_size(1)
    fn main() {
      var m : Mat;
      for (var c = 0u; c < )" + std::to_string(cols) + R"(; c++) {
        m[c] = vec3(f32(c*3 + 1), f32(c*3 + 2), f32(c*3 + 3));
      }
      buffer = array<Type, 4>(Type(m), Type(m * 2), Type(m * 3), Type(m * 4));
    }
  )";

        // Build expected values: 4 matrix instances, each with cols columns,
        // each column = 4 uint32 words (3 float values + 1 deadbeef padding word).
        // Upstream:
        //   f_values[i*(cols*4) + c*4 + k] = (c*3 + k+1) * (i+1)  for k in 0..2
        //   u_values[i*(cols*4) + c*4 + 3] = 0xdeadbeef
        size_t totalWords = 4 * static_cast<size_t>(cols) * 4;
        std::vector<uint32_t> expected(totalWords);
        for (int i = 0; i < 4; ++i) {
            for (int c = 0; c < static_cast<int>(cols); ++c) {
                size_t base = static_cast<size_t>(i) * static_cast<size_t>(cols) * 4
                            + static_cast<size_t>(c) * 4;
                expected[base + 0] = floatBits(static_cast<float>((c * 3 + 1) * (i + 1)));
                expected[base + 1] = floatBits(static_cast<float>((c * 3 + 2) * (i + 1)));
                expected[base + 2] = floatBits(static_cast<float>((c * 3 + 3) * (i + 1)));
                expected[base + 3] = 0xDEADBEEFu;
            }
        }

        runShaderTest(t, wgsl, expected.data(), expected.size());
    });

} // namespace
