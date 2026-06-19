// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/layout_constraints.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - Upstream `validity` is a tagged union (true | 'non-uniform' | 'non-interface'
//     | 'storage' | 'atomic' | false). It is modeled here as the Validity enum;
//     `false` becomes Validity::Never.
//   - `t.hasLanguageFeature('uniform_buffer_standard_layout')` selects whether
//     `uniform` shares storage layout constraints; the enabler returns the true
//     per-backend answer.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,layout_constraints",
    "Validation of address space layout constraints");

enum class Validity { Always, NonUniform, NonInterface, Storage, Atomic, Never };

struct LayoutCase {
    const char* name;
    const char* type;
    const char* decls; // "" if none
    Validity validity;
    bool f16;
};

static const std::vector<LayoutCase>& kLayoutCases() {
    static const std::vector<LayoutCase> cases = {
        // Scalars
        {"u32", "u32", "", Validity::Always, false},
        {"i32", "i32", "", Validity::Always, false},
        {"f32", "f32", "", Validity::Always, false},
        {"f16", "f16", "", Validity::Always, true},
        {"bool", "bool", "", Validity::NonInterface, false},

        // Vectors
        {"vec2u", "vec2u", "", Validity::Always, false},
        {"vec3u", "vec3u", "", Validity::Always, false},
        {"vec4u", "vec4u", "", Validity::Always, false},
        {"vec2i", "vec2i", "", Validity::Always, false},
        {"vec3i", "vec3i", "", Validity::Always, false},
        {"vec4i", "vec4i", "", Validity::Always, false},
        {"vec2f", "vec2f", "", Validity::Always, false},
        {"vec3f", "vec3f", "", Validity::Always, false},
        {"vec4f", "vec4f", "", Validity::Always, false},
        {"vec2h", "vec2h", "", Validity::Always, true},
        {"vec3h", "vec3h", "", Validity::Always, true},
        {"vec4h", "vec4h", "", Validity::Always, true},
        {"vec2b", "vec2<bool>", "", Validity::NonInterface, false},
        {"vec3b", "vec3<bool>", "", Validity::NonInterface, false},
        {"vec4b", "vec4<bool>", "", Validity::NonInterface, false},

        // Matrices
        {"mat2x2f", "mat2x2f", "", Validity::Always, false},
        {"mat2x3f", "mat2x3f", "", Validity::Always, false},
        {"mat2x4f", "mat2x4f", "", Validity::Always, false},
        {"mat3x2f", "mat3x2f", "", Validity::Always, false},
        {"mat3x3f", "mat3x3f", "", Validity::Always, false},
        {"mat3x4f", "mat3x4f", "", Validity::Always, false},
        {"mat4x2f", "mat4x2f", "", Validity::Always, false},
        {"mat4x3f", "mat4x3f", "", Validity::Always, false},
        {"mat4x4f", "mat4x4f", "", Validity::Always, false},
        {"mat2x2h", "mat2x2h", "", Validity::Always, true},
        {"mat2x3h", "mat2x3h", "", Validity::Always, true},
        {"mat2x4h", "mat2x4h", "", Validity::Always, true},
        {"mat3x2h", "mat3x2h", "", Validity::Always, true},
        {"mat3x3h", "mat3x3h", "", Validity::Always, true},
        {"mat3x4h", "mat3x4h", "", Validity::Always, true},
        {"mat4x2h", "mat4x2h", "", Validity::Always, true},
        {"mat4x3h", "mat4x3h", "", Validity::Always, true},
        {"mat4x4h", "mat4x4h", "", Validity::Always, true},

        // Atomics
        {"atomic_u32", "atomic<u32>", "", Validity::Atomic, false},
        {"atomic_i32", "atomic<i32>", "", Validity::Atomic, false},

        // Sized arrays
        {"array_u32", "array<u32, 16>", "", Validity::NonUniform, false},
        {"array_i32", "array<i32, 16>", "", Validity::NonUniform, false},
        {"array_f32", "array<f32, 16>", "", Validity::NonUniform, false},
        {"array_f16", "array<f16, 16>", "", Validity::NonUniform, true},
        {"array_bool", "array<bool, 16>", "", Validity::NonInterface, false},
        {"array_vec2f", "array<vec2f, 16>", "", Validity::NonUniform, false},
        {"array_vec3f", "array<vec3f, 16>", "", Validity::Always, false},
        {"array_vec4f", "array<vec4f, 16>", "", Validity::Always, false},
        {"array_vec2h", "array<vec2h, 16>", "", Validity::NonUniform, true},
        {"array_vec3h", "array<vec3h, 16>", "", Validity::NonUniform, true},
        {"array_vec4h", "array<vec4h, 16>", "", Validity::NonUniform, true},
        {"array_vec2b", "array<vec2<bool>, 16>", "", Validity::NonInterface, false},
        {"array_vec3b", "array<vec3<bool>, 16>", "", Validity::NonInterface, false},
        {"array_vec4b", "array<vec4<bool>, 16>", "", Validity::NonInterface, false},
        {"array_mat2x2f", "array<mat2x2f, 16>", "", Validity::Always, false},
        {"array_mat2x4f", "array<mat2x4f, 16>", "", Validity::Always, false},
        {"array_mat4x2f", "array<mat4x2f, 16>", "", Validity::Always, false},
        {"array_mat4x4f", "array<mat4x4f, 16>", "", Validity::Always, false},
        {"array_mat2x2h", "array<mat2x2h, 16>", "", Validity::NonUniform, true},
        {"array_mat2x4h", "array<mat2x4h, 16>", "", Validity::Always, true},
        {"array_mat3x2h", "array<mat3x2h, 16>", "", Validity::NonUniform, true},
        {"array_mat4x2h", "array<mat4x2h, 16>", "", Validity::Always, true},
        {"array_mat4x4h", "array<mat4x4h, 16>", "", Validity::Always, true},
        {"array_atomic", "array<atomic<u32>, 16>", "", Validity::Atomic, false},

        // Runtime arrays
        {"runtime_array_u32", "array<u32>", "", Validity::Storage, false},
        {"runtime_array_i32", "array<i32>", "", Validity::Storage, false},
        {"runtime_array_f32", "array<f32>", "", Validity::Storage, false},
        {"runtime_array_f16", "array<f16>", "", Validity::Storage, true},
        {"runtime_array_bool", "array<bool>", "", Validity::Never, false},
        {"runtime_array_vec2f", "array<vec2f>", "", Validity::Storage, false},
        {"runtime_array_vec3f", "array<vec3f>", "", Validity::Storage, false},
        {"runtime_array_vec4f", "array<vec4f>", "", Validity::Storage, false},
        {"runtime_array_vec2h", "array<vec2h>", "", Validity::Storage, true},
        {"runtime_array_vec3h", "array<vec3h>", "", Validity::Storage, true},
        {"runtime_array_vec4h", "array<vec4h>", "", Validity::Storage, true},
        {"runtime_array_vec2b", "array<vec2<bool>>", "", Validity::Never, false},
        {"runtime_array_vec3b", "array<vec3<bool>>", "", Validity::Never, false},
        {"runtime_array_vec4b", "array<vec4<bool>>", "", Validity::Never, false},
        {"runtime_array_mat2x2f", "array<mat2x2f>", "", Validity::Storage, false},
        {"runtime_array_mat2x4f", "array<mat2x4f>", "", Validity::Storage, false},
        {"runtime_array_mat4x2f", "array<mat4x2f>", "", Validity::Storage, false},
        {"runtime_array_mat4x4f", "array<mat4x4f>", "", Validity::Storage, false},
        {"runtime_array_mat2x2h", "array<mat2x2h>", "", Validity::Storage, true},
        {"runtime_array_mat2x4h", "array<mat2x4h>", "", Validity::Storage, true},
        {"runtime_array_mat3x2h", "array<mat3x2h>", "", Validity::Storage, true},
        {"runtime_array_mat4x2h", "array<mat4x2h>", "", Validity::Storage, true},
        {"runtime_array_mat4x4h", "array<mat4x4h>", "", Validity::Storage, true},
        {"runtime_array_atomic", "array<atomic<u32>>", "", Validity::Storage, false},

        // Structs (and arrays of structs)
        {"array_struct_u32", "array<S, 16>", "struct S { x : u32 }", Validity::NonUniform, false},
        {"array_struct_u32_size16", "array<S, 16>", "struct S { @size(16) x : u32 }", Validity::Always, false},
        {"array_struct_vec2f", "array<S, 16>", "struct S { x : vec2f }", Validity::NonUniform, false},
        {"array_struct_vec2h", "array<S, 16>", "struct S { x : vec2h }", Validity::NonUniform, true},
        {"array_struct_vec2h_align16", "array<S, 16>", "struct S { @align(16) x : vec2h }", Validity::Always, true},
        {"size_too_small", "S", "struct S { @size(2) x : u32 }", Validity::Never, false},
        {"struct_padding", "S", "struct T { x : u32 }\n    struct S { t : T, x : u32 }", Validity::NonUniform, false},
        {"struct_array_u32", "S", "struct S { x : array<u32, 4> }", Validity::NonUniform, false},
        {"struct_runtime_array_u32", "S", "struct S { x : array<u32> }", Validity::Storage, false},
        {"array_struct_size_5", "array<S, 16>", "struct S { @size(5) x : u32, y : u32 }", Validity::NonUniform, false},
        {"array_struct_size_5x2", "array<S, 16>", "struct S { @size(5) x : u32, @size(5) y : u32 }", Validity::Always, false},
        {"struct_size_5", "S", "struct T { @size(5) x : u32 }\n    struct S { x : u32, y : T }", Validity::NonUniform, false},
        {"struct_size_5_align16", "S", "struct T { @align(16) @size(5) x : u32 }\n    struct S { x : u32, y : T }", Validity::Always, false},
    };
    return cases;
}

static std::vector<Value> layoutCaseNames() {
    std::vector<Value> values;
    for (const LayoutCase& c : kLayoutCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const LayoutCase& findLayoutCase(const std::string& name) {
    for (const LayoutCase& c : kLayoutCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const LayoutCase dummy{"", "", "", Validity::Never, false};
    return dummy;
}

CTS_TEST(g, "layout_constraints")
    .desc("Test address space layout constraints")
    .params([](ParamsBuilder u) {
        return u.combine("case", layoutCaseNames())
                .beginSubcases()
                .combine("aspace", {std::string("storage"), std::string("uniform"),
                                    std::string("function"), std::string("private"),
                                    std::string("workgroup")});
    })
    .fn([](ShaderValidationTest& t) {
        const LayoutCase& testcase = findLayoutCase(t.param<std::string>("case"));
        const std::string aspace = t.param<std::string>("aspace");
        const std::string decls = testcase.decls;

        std::string code =
            std::string("\n") + (testcase.f16 ? "enable f16;" : "") +
            "\n" + decls + "\n\n";

        if (aspace == "storage") {
            code += "@group(0) @binding(0) var<storage, read_write> v : " + std::string(testcase.type) + ";\n";
        } else if (aspace == "uniform") {
            code += "@group(0) @binding(0) var<uniform> v : " + std::string(testcase.type) + ";\n";
        } else if (aspace == "workgroup") {
            code += "var<workgroup> v : " + std::string(testcase.type) + ";\n";
        } else if (aspace == "private") {
            code += "var<private> v : " + std::string(testcase.type) + ";\n";
        }

        code += "@compute @workgroup_size(1,1,1)\n    fn main() {\n    ";
        if (aspace == "function") {
            code += "var v : " + std::string(testcase.type) + ";\n";
        }
        code += "}\n";

        // If `uniform_buffer_standard_layout` is supported, `uniform` shares the
        // `storage` layout constraints.
        const bool ubo_std_layout = t.hasLanguageFeature("uniform_buffer_standard_layout");

        const bool is_interface = aspace == "uniform" || aspace == "storage";
        const bool supports_atomic = aspace == "storage" || aspace == "workgroup";
        const bool expect =
            testcase.validity == Validity::Always ||
            (testcase.validity == Validity::NonUniform && (aspace != "uniform" || ubo_std_layout)) ||
            (testcase.validity == Validity::NonInterface && !is_interface) ||
            (testcase.validity == Validity::Storage && aspace == "storage") ||
            (testcase.validity == Validity::Atomic && supports_atomic);
        t.expectCompileResult(expect, code);
    });

} // namespace
