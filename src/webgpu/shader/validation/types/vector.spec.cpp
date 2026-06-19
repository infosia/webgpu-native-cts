// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/vector.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,vector",
    "Validation tests for vector types");

struct CodeCase {
    const char* name;
    const char* wgsl;
    bool ok;
};

// Mirrors upstream kCases (object key order preserved).
static const std::vector<CodeCase>& kCases() {
    static const std::vector<CodeCase> cases = {
        // Valid vector types
        {"vec2_bool", "alias T = vec2<bool>;", true},
        {"vec3_bool", "alias T = vec3<bool>;", true},
        {"vec4_bool", "alias T = vec4<bool>;", true},
        {"vec2_i32", "alias T = vec2<i32>;", true},
        {"vec3_i32", "alias T = vec3<i32>;", true},
        {"vec4_i32", "alias T = vec4<i32>;", true},
        {"vec2_u32", "alias T = vec2<u32>;", true},
        {"vec3_u32", "alias T = vec3<u32>;", true},
        {"vec4_u32", "alias T = vec4<u32>;", true},
        {"vec2_f32", "alias T = vec2<f32>;", true},
        {"vec3_f32", "alias T = vec3<f32>;", true},
        {"vec4_f32", "alias T = vec4<f32>;", true},
        {"vec2_f16", "enable f16;\nalias T = vec2<f16>;", true},
        {"vec3_f16", "enable f16;\nalias T = vec3<f16>;", true},
        {"vec4_f16", "enable f16;\nalias T = vec4<f16>;", true},

        // Pre-declared type aliases
        {"vec2i", "const c : vec2i = vec2<i32>();", true},
        {"vec3i", "const c : vec3i = vec3<i32>();", true},
        {"vec4i", "const c : vec4i = vec4<i32>();", true},
        {"vec2u", "const c : vec2u = vec2<u32>();", true},
        {"vec3u", "const c : vec3u = vec3<u32>();", true},
        {"vec4u", "const c : vec4u = vec4<u32>();", true},
        {"vec2f", "const c : vec2f = vec2<f32>();", true},
        {"vec3f", "const c : vec3f = vec3<f32>();", true},
        {"vec4f", "const c : vec4f = vec4<f32>();", true},
        {"vec2h", "enable f16;\nconst c : vec2h = vec2<f16>();", true},
        {"vec3h", "enable f16;\nconst c : vec3h = vec3<f16>();", true},
        {"vec4h", "enable f16;\nconst c : vec4h = vec4<f16>();", true},

        // pass
        {"trailing_comma", "alias T = vec3<u32,>;", true},
        {"aliased_el_ty", "alias EL = i32;\nalias T = vec3<EL>;", true},

        // invalid
        {"vec", "alias T = vec;", false},
        {"vec_f32", "alias T = vec<f32>;", false},
        {"vec1_i32", "alias T = vec1<i32>;", false},
        {"vec5_u32", "alias T = vec5<u32>;", false},
        {"missing_el_ty", "alias T = vec3<>;", false},
        {"missing_t_left", "alias T = vec3 u32>;", false},
        {"missing_t_right", "alias T = vec3<u32;", false},
        {"vec_of_array", "alias T = vec3<array<i32, 2>>;", false},
        {"vec_of_runtime_array", "alias T = vec3<array<i32>>;", false},
        {"vec_of_struct", "struct S { i : i32 }\nalias T = vec3<S>;", false},
        {"vec_of_atomic", "alias T = vec3<atomic<i32>>;", false},
        {"vec_of_matrix", "alias T = vec3<mat2x2f>;", false},
        {"vec_of_vec", "alias T = vec3<vec2f>;", false},
        {"no_bool_shortform", "const c : vec2b = vec2<bool>();", false},
    };
    return cases;
}

static const CodeCase& findCase(const std::string& name) {
    for (const CodeCase& c : kCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const CodeCase dummy{"", "", false};
    return dummy;
}

static std::vector<Value> caseNames() {
    std::vector<Value> values;
    for (const CodeCase& c : kCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "vector")
    .desc("Tests validation of vector types")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const CodeCase& c = findCase(t.param<std::string>("case"));
        t.expectCompileResult(c.ok, c.wgsl);
    });

}  // namespace
