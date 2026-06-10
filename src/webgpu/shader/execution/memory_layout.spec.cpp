// Ported from gpuweb/cts src/webgpu/shader/execution/memory_layout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Deviations from upstream (documented per docs/05-porting-guide.md):
// - Upstream's `beforeAllSubcases` skips are folded into the test body; both
//   tests have only the single implicit subcase per case, so the behavior is
//   identical (the whole case is skipped either way).
// - Upstream conditionally runs the `skip_uniform` cases under
//   aspace='uniform' when `hasLanguageFeature('uniform_buffer_standard_layout')`
//   is true. The harness does not expose the WGPUInstance to test bodies, so
//   `wgpuInstanceHasWGSLLanguageFeature` cannot be queried here (same
//   limitation as api/validation/render_pipeline/resource_compatibility).
//   Those subcases are therefore always runtime-skipped with a reason, which
//   matches upstream behavior on implementations without the language feature.
// - The upstream table entry `mat3x3h_align8` declares `y : mat2x3h` (an
//   upstream quirk); it is ported faithfully.

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,memory_layout",
    "Test memory layout requirements");

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, std::string::traits_type::length(s)};
}

// ---------------------------------------------------------------------------
// kLayoutCases — faithful port of the upstream table (order preserved; the
// `case` param values are these names in this order).
// ---------------------------------------------------------------------------

struct LayoutCase {
    const char* name;
    const char* type;
    const char* decl;  // nullptr when upstream omits `decl`
    const char* read_assign;
    const char* write_assign;
    uint32_t offset;
    bool f16;
    bool f32;
    bool skip_uniform;
};

const LayoutCase kLayoutCases[] = {
    {"vec2u_align8", "S_vec2u_align",
     R"(struct S_vec2u_align {
      x : u32,
      y : vec2u,
    })",
     "out = in.y[1]", "out.y[1] = in", 12, false, false, false},
    {"vec3u_align16", "S_vec3u_align",
     R"(struct S_vec3u_align {
      x : u32,
      y : vec3u,
    })",
     "out = in.y[2]", "out.y[2] = in", 24, false, false, false},
    {"vec4u_align16", "S_vec4u_align",
     R"(struct S_vec4u_align {
      x : u32,
      y : vec4u,
    })",
     "out = in.y[0]", "out.y[0] = in", 16, false, false, false},
    {"struct_align32", "S_align32",
     R"(struct S_align32 {
      x : u32,
      @align(32) y : u32,
    })",
     "out = in.y;", "out.y = in", 32, false, false, false},
    {"vec2h_align4", "S_vec2h_align",
     R"(struct S_vec2h_align {
      x : f16,
      y : vec2h,
    })",
     "out = u32(in.y[0])", "out.y[0] = f16(in)", 4, true, false, false},
    {"vec3h_align8", "S_vec3h_align",
     R"(struct S_vec3h_align {
      x : f16,
      y : vec3h,
    })",
     "out = u32(in.y[2])", "out.y[2] = f16(in)", 12, true, false, false},
    {"vec4h_align8", "S_vec4h_align",
     R"(struct S_vec4h_align {
      x : f16,
      y : vec4h,
    })",
     "out = u32(in.y[2])", "out.y[2] = f16(in)", 12, true, false, false},
    {"vec2f_align8", "S_vec2f_align",
     R"(struct S_vec2f_align {
      x : u32,
      y : vec2f,
    })",
     "out = u32(in.y[1])", "out.y[1] = f32(in)", 12, false, true, false},
    {"vec3f_align16", "S_vec3f_align",
     R"(struct S_vec3f_align {
      x : u32,
      y : vec3f,
    })",
     "out = u32(in.y[2])", "out.y[2] = f32(in)", 24, false, true, false},
    {"vec4f_align16", "S_vec4f_align",
     R"(struct S_vec4f_align {
      x : u32,
      y : vec4f,
    })",
     "out = u32(in.y[0])", "out.y[0] = f32(in)", 16, false, true, false},
    {"vec3i_size12", "S_vec3i_size",
     R"(struct S_vec3i_size {
      x : vec3i,
      y : u32,
    })",
     "out = in.y", "out.y = in", 12, false, false, false},
    {"vec3h_size6", "S_vec3h_size",
     R"(struct S_vec3h_size {
      x : vec3h,
      y : f16,
      z : f16,
    })",
     "out = u32(in.z)", "out.z = f16(in)", 8, true, false, false},
    {"size80", "S_size80",
     R"(struct S_size80 {
      @size(80) x : u32,
      y : u32,
    })",
     "out = in.y", "out.y = in", 80, false, false, false},
    {"atomic_align4", "S_atomic_align",
     R"(struct S_atomic_align {
      x : u32,
      y : atomic<u32>,
    })",
     "out = atomicLoad(&in.y)", "atomicStore(&out.y, in)", 4, false, false, false},
    {"atomic_size4", "S_atomic_size",
     R"(struct S_atomic_size {
      x : atomic<u32>,
      y : u32,
    })",
     "out = in.y", "out.y = in", 4, false, false, false},
    {"mat2x2f_align8", "S_mat2x2f_align",
     R"(struct S_mat2x2f_align {
      x : u32,
      y : mat2x2f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 8, false, true, false},
    {"mat3x2f_align8", "S_mat3x2f_align",
     R"(struct S_mat3x2f_align {
      x : u32,
      y : mat3x2f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 8, false, true, false},
    {"mat4x2f_align8", "S_mat4x2f_align",
     R"(struct S_mat4x2f_align {
      x : u32,
      y : mat4x2f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 8, false, true, false},
    {"mat2x3f_align16", "S_mat2x3f_align",
     R"(struct S_mat2x3f_align {
      x : u32,
      y : mat2x3f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 16, false, true, false},
    {"mat3x3f_align16", "S_mat3x3f_align",
     R"(struct S_mat3x3f_align {
      x : u32,
      y : mat3x3f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 16, false, true, false},
    {"mat4x3f_align16", "S_mat4x3f_align",
     R"(struct S_mat4x3f_align {
      x : u32,
      y : mat4x3f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 16, false, true, false},
    {"mat2x4f_align16", "S_mat2x4f_align",
     R"(struct S_mat2x4f_align {
      x : u32,
      y : mat2x4f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 16, false, true, false},
    {"mat3x4f_align16", "S_mat3x4f_align",
     R"(struct S_mat3x4f_align {
      x : u32,
      y : mat3x4f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 16, false, true, false},
    {"mat4x4f_align16", "S_mat4x4f_align",
     R"(struct S_mat4x4f_align {
      x : u32,
      y : mat4x4f,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f32(in)", 16, false, true, false},
    {"mat2x2h_align4", "S_mat2x2h_align",
     R"(struct S_mat2x2h_align {
      x : u32,
      y : mat2x2h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 4, true, false, false},
    {"mat3x2h_align4", "S_mat3x2h_align",
     R"(struct S_mat3x2h_align {
      x : u32,
      y : mat3x2h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 4, true, false, false},
    {"mat4x2h_align4", "S_mat4x2h_align",
     R"(struct S_mat4x2h_align {
      x : u32,
      y : mat4x2h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 4, true, false, false},
    {"mat2x3h_align8", "S_mat2x3h_align",
     R"(struct S_mat2x3h_align {
      x : u32,
      y : mat2x3h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, false},
    {"mat3x3h_align8", "S_mat3x3h_align",
     R"(struct S_mat3x3h_align {
      x : u32,
      y : mat2x3h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, false},
    {"mat4x3h_align8", "S_mat4x3h_align",
     R"(struct S_mat4x3h_align {
      x : u32,
      y : mat4x3h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, false},
    {"mat2x4h_align8", "S_mat2x4h_align",
     R"(struct S_mat2x4h_align {
      x : u32,
      y : mat2x4h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, false},
    {"mat3x4h_align8", "S_mat3x4h_align",
     R"(struct S_mat3x4h_align {
      x : u32,
      y : mat3x4h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, false},
    {"mat4x4h_align8", "S_mat4x4h_align",
     R"(struct S_mat4x4h_align {
      x : u32,
      y : mat4x4h,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, false},
    {"mat2x2f_size", "S_mat2x2f_size",
     R"(struct S_mat2x2f_size {
      x : mat2x2f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 16, false, false, false},
    {"mat3x2f_size", "S_mat3x2f_size",
     R"(struct S_mat3x2f_size {
      x : mat3x2f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 24, false, false, false},
    {"mat4x2f_size", "S_mat4x2f_size",
     R"(struct S_mat4x2f_size {
      x : mat4x2f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 32, false, false, false},
    {"mat2x3f_size", "S_mat2x3f_size",
     R"(struct S_mat2x3f_size {
      x : mat2x3f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 32, false, false, false},
    {"mat3x3f_size", "S_mat3x3f_size",
     R"(struct S_mat3x3f_size {
      x : mat3x3f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 48, false, false, false},
    {"mat4x3f_size", "S_mat4x3f_size",
     R"(struct S_mat4x3f_size {
      x : mat4x3f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 64, false, false, false},
    {"mat2x4f_size", "S_mat2x4f_size",
     R"(struct S_mat2x4f_size {
      x : mat2x4f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 32, false, false, false},
    {"mat3x4f_size", "S_mat3x4f_size",
     R"(struct S_mat3x4f_size {
      x : mat3x4f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 48, false, false, false},
    {"mat4x4f_size", "S_mat4x4f_size",
     R"(struct S_mat4x4f_size {
      x : mat4x4f,
      y : u32,
    })",
     "out = in.y", "out.y = in", 64, false, false, false},
    {"mat2x2h_size", "S_mat2x2h_size",
     R"(struct S_mat2x2h_size {
      x : mat2x2h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 8, true, false, false},
    {"mat3x2h_size", "S_mat3x2h_size",
     R"(struct S_mat3x2h_size {
      x : mat3x2h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 12, true, false, false},
    {"mat4x2h_size", "S_mat4x2h_size",
     R"(struct S_mat4x2h_size {
      x : mat4x2h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 16, true, false, false},
    {"mat2x3h_size", "S_mat2x3h_size",
     R"(struct S_mat2x3h_size {
      x : mat2x3h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 16, true, false, false},
    {"mat3x3h_size", "S_mat3x3h_size",
     R"(struct S_mat3x3h_size {
      x : mat3x3h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 24, true, false, false},
    {"mat4x3h_size", "S_mat4x3h_size",
     R"(struct S_mat4x3h_size {
      x : mat4x3h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 32, true, false, false},
    {"mat2x4h_size", "S_mat2x4h_size",
     R"(struct S_mat2x4h_size {
      x : mat2x4h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 16, true, false, false},
    {"mat3x4h_size", "S_mat3x4h_size",
     R"(struct S_mat3x4h_size {
      x : mat3x4h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 24, true, false, false},
    {"mat4x4h_size", "S_mat4x4h_size",
     R"(struct S_mat4x4h_size {
      x : mat4x4h,
      y : f16,
    })",
     "out = u32(in.y)", "out.y = f16(in)", 32, true, false, false},
    {"struct_align_vec2i", "S_struct_align_vec2i",
     R"(struct Inner {
      x : u32,
      y : vec2i,
    }
    struct S_struct_align_vec2i {
      x : u32,
      y : Inner,
    })",
     "out = in.y.x", "out.y.x = in", 8, false, false, true},
    {"struct_align_vec3i", "S_struct_align_vec3i",
     R"(struct Inner {
      x : u32,
      y : vec3i,
    }
    struct S_struct_align_vec3i {
      x : u32,
      y : Inner,
    })",
     "out = in.y.x", "out.y.x = in", 16, false, false, false},
    {"struct_align_vec4i", "S_struct_align_vec4i",
     R"(struct Inner {
      x : u32,
      y : vec4i,
    }
    struct S_struct_align_vec4i {
      x : u32,
      y : Inner,
    })",
     "out = in.y.x", "out.y.x = in", 16, false, false, false},
    {"struct_align_vec2h", "S_struct_align_vec2h",
     R"(struct Inner {
      x : f16,
      y : vec2h,
    }
    struct S_struct_align_vec2h {
      x : f16,
      y : Inner,
    })",
     "out = u32(in.y.x)", "out.y.x = f16(in)", 4, true, false, true},
    {"struct_align_vec3h", "S_struct_align_vec3h",
     R"(struct Inner {
      x : f16,
      y : vec3h,
    }
    struct S_struct_align_vec3h {
      x : f16,
      y : Inner,
    })",
     "out = u32(in.y.x)", "out.y.x = f16(in)", 8, true, false, true},
    {"struct_align_vec4h", "S_struct_align_vec4h",
     R"(struct Inner {
      x : f16,
      y : vec4h,
    }
    struct S_struct_align_vec4h {
      x : f16,
      y : Inner,
    })",
     "out = u32(in.y.x)", "out.y.x = f16(in)", 8, true, false, true},
    {"struct_size_roundup", "S_struct_size_roundup",
     R"(struct Inner {
      x : vec3u,
    }
    struct S_struct_size_roundup {
      x : Inner,
      y : u32,
    })",
     "out = in.y", "out.y = in", 16, false, false, false},
    {"struct_inner_size", "S_struct_inner_size",
     R"(struct Inner {
      @size(112) x : u32,
    }
    struct S_struct_inner_size {
      x : Inner,
      y : u32,
    })",
     "out = in.y", "out.y = in", 112, false, false, false},
    {"struct_inner_align", "S_struct_inner_align",
     R"(struct Inner {
      @align(64) x : u32,
    }
    struct S_struct_inner_align {
      x : u32,
      y : Inner,
    })",
     "out = in.y.x", "out.y.x = in", 64, false, false, false},
    {"struct_inner_size_and_align", "S_struct_inner_size_and_align",
     R"(struct Inner {
      @align(32) @size(33) x : u32,
    }
    struct S_struct_inner_size_and_align {
      x : Inner,
      y : Inner,
    })",
     "out = in.y.x", "out.y.x = in", 64, false, false, false},
    {"struct_override_size", "S_struct_override_size",
     R"(struct Inner {
      @size(32) x : u32,
    }
    struct S_struct_override_size {
      @size(64) x : Inner,
      y : u32,
    })",
     "out = in.y", "out.y = in", 64, false, false, false},
    {"struct_double_align", "S_struct_double_align",
     R"(struct Inner {
      x : u32,
      @align(32) y : u32,
    }
    struct S_struct_double_align {
      x : u32,
      @align(64) y : Inner,
    })",
     "out = in.y.y", "out.y.y = in", 96, false, false, false},
    {"array_vec3u_align", "S_array_vec3u_align",
     R"(struct S_array_vec3u_align {
      x : u32,
      y : array<vec3u, 2>,
    })",
     "out = in.y[0][0]", "out.y[0][0] = in", 16, false, false, false},
    {"array_vec3h_align", "S_array_vec3h_align",
     R"(struct S_array_vec3h_align {
      x : f16,
      y : array<vec3h, 2>,
    })",
     "out = u32(in.y[0][0])", "out.y[0][0] = f16(in)", 8, true, false, true},
    {"array_vec3u_stride", "S_array_vec3u_stride",
     R"(struct S_array_vec3u_stride {
      x : array<vec3u, 4>,
    })",
     "out = in.x[1][0]", "out.x[1][0] = in", 16, false, false, false},
    {"array_vec3h_stride", "S_array_vec3h_stride",
     R"(struct S_array_vec3h_stride {
      x : array<vec3h, 4>,
    })",
     "out = u32(in.x[1][0])", "out.x[1][0] = f16(in)", 8, true, false, true},
    {"array_stride_size", "array<S_stride, 4>",
     R"(struct S_stride {
      @size(16) x : u32,
    })",
     "out = in[2].x", "out[2].x = in", 32, false, false, false},
    {"array_mat2x2f_stride", "array<mat2x2f, 4>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f32(in)", 16, false, true, false},
    {"array_mat2x2h_stride", "array<mat2x2h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 8, true, false, true},
    {"array_mat3x2f_stride", "array<mat3x2f, 3>", nullptr,
     "out = u32(in[2][0][0])", "out[2][0][0] = f32(in)", 48, false, true, true},
    {"array_mat3x2h_stride", "array<mat3x2h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 12, true, false, true},
    {"array_mat4x2f_stride", "array<mat4x2f, 4>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f32(in)", 32, false, true, false},
    {"array_mat4x2h_stride", "array<mat4x2h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 16, true, false, false},
    {"array_mat2x3f_stride", "array<mat2x3f, 4>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f32(in)", 32, false, true, false},
    {"array_mat2x3h_stride", "array<mat2x3h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 16, true, false, false},
    {"array_mat3x3f_stride", "array<mat3x3f, 3>", nullptr,
     "out = u32(in[2][0][0])", "out[2][0][0] = f32(in)", 96, false, true, false},
    {"array_mat3x3h_stride", "array<mat3x3h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 24, true, false, true},
    {"array_mat4x3f_stride", "array<mat4x3f, 4>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f32(in)", 64, false, true, false},
    {"array_mat4x3h_stride", "array<mat4x3h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 32, true, false, false},
    {"array_mat2x4f_stride", "array<mat2x4f, 4>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f32(in)", 32, false, true, false},
    {"array_mat2x4h_stride", "array<mat2x4h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 16, true, false, false},
    {"array_mat3x4f_stride", "array<mat3x4f, 3>", nullptr,
     "out = u32(in[2][0][0])", "out[2][0][0] = f32(in)", 96, false, true, false},
    {"array_mat3x4h_stride", "array<mat3x4h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 24, true, false, true},
    {"array_mat4x4f_stride", "array<mat4x4f, 4>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f32(in)", 64, false, true, false},
    {"array_mat4x4h_stride", "array<mat4x4h, 2>", nullptr,
     "out = u32(in[1][0][0])", "out[1][0][0] = f16(in)", 32, true, false, false},
};

constexpr size_t kNumLayoutCases = sizeof(kLayoutCases) / sizeof(kLayoutCases[0]);

std::vector<Value> layoutCaseNames() {
    std::vector<Value> names;
    names.reserve(kNumLayoutCases);
    for (const LayoutCase& c : kLayoutCases) {
        names.emplace_back(std::string(c.name));
    }
    return names;
}

const LayoutCase& findLayoutCase(AllFeaturesMaxLimitsGpuTest& t, const std::string& name) {
    for (const LayoutCase& c : kLayoutCases) {
        if (name == c.name) {
            return c;
        }
    }
    t.fail("unknown layout case: " + name);
}

// Shared runtime skips: upstream `beforeAllSubcases` + the in-body f16 guard.
void applyCommonSkips(AllFeaturesMaxLimitsGpuTest& t, const LayoutCase& tc, const std::string& aspace) {
    // Don't test atomics in non-storage address spaces (initialization boilerplate).
    if (std::string(tc.type).find("atomic") != std::string::npos && aspace != "storage") {
        t.skip("Skipping atomic test for non-storage address space");
    }
    if (tc.f16 && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// Creates the auto-layout compute pipeline, binds {0: in, 1: out}, dispatches
// a single workgroup and submits.
void runLayoutShader(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& code,
    WGPUBuffer inBuffer,
    uint64_t inSize,
    WGPUBuffer outBuffer,
    uint64_t outSize) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr;  // 'auto' layout
    pipeDesc.compute.module = module;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = inBuffer;
    entries[0].offset = 0;
    entries[0].size = inSize;
    entries[1].binding = 1;
    entries[1].buffer = outBuffer;
    entries[1].offset = 0;
    entries[1].size = outSize;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
}

// Magic number is 42 in the various representations.
uint32_t magicNumberFor(const LayoutCase& tc) {
    if (tc.f16) {
        return 0x5140u;  // f16(42.0) in the low half-word
    }
    if (tc.f32) {
        return 0x42280000u;  // f32(42.0)
    }
    return 42u;
}

// ---------------------------------------------------------------------------
// read_layout
// ---------------------------------------------------------------------------

CTS_TEST(g, "read_layout")
    .desc("Test reading memory layouts")
    .params([](ParamsBuilder u) {
        return u.combine("case", layoutCaseNames())
            .combine("aspace", {"storage", "uniform", "workgroup", "function", "private"})
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto caseName = t.param<std::string>("case");
        auto aspace = t.param<std::string>("aspace");
        const LayoutCase& tc = findLayoutCase(t, caseName);

        // Upstream runs the skip_uniform cases under aspace='uniform' only when
        // the 'uniform_buffer_standard_layout' WGSL language feature is present.
        // The instance is not reachable from test bodies, so always skip (see
        // file header note).
        if (tc.skip_uniform && aspace == "uniform") {
            t.skip("Uniform requires 16 byte alignment "
                   "(uniform_buffer_standard_layout language-feature query unavailable)");
        }
        applyCommonSkips(t, tc, aspace);

        const std::string type = tc.type;

        std::string code;
        code += "\n";
        code += tc.f16 ? "enable f16;" : "";
        code += "\n";
        code += tc.decl != nullptr ? tc.decl : "";
        code += "\n\n@group(0) @binding(1)\nvar<storage, read_write> out : u32;\n";

        if (aspace == "uniform") {
            code += "@group(0) @binding(0)\n      var<uniform> in : " + type + ";";
        } else if (aspace == "storage") {
            // Use read_write for input data to support atomics.
            code += "@group(0) @binding(0)\n      var<storage, read_write> in : " + type + ";";
        } else {
            code += "@group(0) @binding(0)\n      var<storage> pre_in : " + type + ";";
            if (aspace == "workgroup") {
                code += "\n        var<workgroup> in : " + type + ";";
            } else if (aspace == "private") {
                code += "\n        var<private> in : " + type + ";";
            }
        }

        code += "\n@compute @workgroup_size(1,1,1)\nfn main() {\n";

        if (aspace == "workgroup" || aspace == "function" || aspace == "private") {
            if (aspace == "function") {
                code += "var in : " + type + ";\n";
            }
            code += "in = pre_in;";
            if (aspace == "workgroup") {
                code += "workgroupBarrier();\n";
            }
        }

        code += "\n" + std::string(tc.read_assign) + ";\n}";

        WGPUBufferUsage usage = WGPUBufferUsage_CopySrc;
        if (aspace == "uniform") {
            usage |= WGPUBufferUsage_Uniform;
        } else {
            usage |= WGPUBufferUsage_Storage;
        }

        // Input: 128 32-bit words, all zero except the magic number at `offset`.
        std::vector<uint32_t> inData(128, 0u);
        inData[tc.offset / 4] = magicNumberFor(tc);
        WGPUBuffer inBuffer = t.makeBufferWithContents(
            inData.data(), inData.size() * sizeof(uint32_t), usage);

        // Output: a single zero-initialized 32-bit word (never pre-filled with
        // the expected value).
        const uint32_t outZero = 0u;
        WGPUBuffer outBuffer = t.makeBufferWithContents(
            &outZero, sizeof(outZero),
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        runLayoutShader(t, code, inBuffer, inData.size() * sizeof(uint32_t), outBuffer, sizeof(outZero));

        const uint32_t expected = 42u;
        t.expectGPUBufferValuesEqual(outBuffer, &expected, sizeof(expected));
    });

// ---------------------------------------------------------------------------
// write_layout
// ---------------------------------------------------------------------------

CTS_TEST(g, "write_layout")
    .desc("Test writing memory layouts")
    .params([](ParamsBuilder u) {
        return u.combine("case", layoutCaseNames())
            .combine("aspace", {"storage", "workgroup", "function", "private"})
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto caseName = t.param<std::string>("case");
        auto aspace = t.param<std::string>("aspace");
        const LayoutCase& tc = findLayoutCase(t, caseName);

        applyCommonSkips(t, tc, aspace);

        const std::string type = tc.type;

        std::string code;
        code += "\n";
        code += tc.f16 ? "enable f16;" : "";
        code += "\n";
        code += tc.decl != nullptr ? tc.decl : "";
        code += "\n\n@group(0) @binding(0)\nvar<storage> in : u32;\n";

        if (aspace == "storage") {
            code += "@group(0) @binding(1)\n      var<storage, read_write> out : " + type + ";\n";
        } else {
            code += "@group(0) @binding(1)\n      var<storage, read_write> post_out : " + type + ";\n";
            if (aspace == "workgroup") {
                code += "var<workgroup> out : " + type + ";\n";
            } else if (aspace == "private") {
                code += "var<private> out : " + type + ";\n";
            }
        }

        code += "\n@compute @workgroup_size(1,1,1)\nfn main() {\n";

        if (aspace == "function") {
            code += "var out : " + type + ";\n";
        }

        code += std::string(tc.write_assign) + ";\n";
        if (aspace == "workgroup" || aspace == "function" || aspace == "private") {
            if (aspace == "workgroup") {
                code += "workgroupBarrier();\n";
            }
            code += "post_out = out;";
        }

        code += "\n}";

        // Input: a single 32-bit word holding 42.
        const uint32_t inValue = 42u;
        WGPUBuffer inBuffer = t.makeBufferWithContents(
            &inValue, sizeof(inValue), WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);

        // Output: 128 zero-initialized 32-bit words (never pre-filled with the
        // expected value).
        std::vector<uint32_t> outZeros(128, 0u);
        WGPUBuffer outBuffer = t.makeBufferWithContents(
            outZeros.data(), outZeros.size() * sizeof(uint32_t),
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

        runLayoutShader(t, code, inBuffer, sizeof(inValue), outBuffer, outZeros.size() * sizeof(uint32_t));

        // Expected: all zero except the magic number at `offset`.
        std::vector<uint32_t> expected(128, 0u);
        expected[tc.offset / 4] = magicNumberFor(tc);
        t.expectGPUBufferValuesEqual(outBuffer, expected.data(), expected.size() * sizeof(uint32_t));
    });

}  // namespace
