// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/test_types.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// A selection of different types used by statement validation tests. Mirrors
// upstream kTestTypes (Record key insertion order preserved). `requires_`/`header`
// are empty strings when upstream omits the corresponding optional field.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "cts/test.h"

namespace cts::shader_validation::statement {

struct TestType {
    std::string value;     // WGSL expression that produces a value of the given type
    std::string requires_; // optional shader enable required to use the type ("" if none)
    std::string header;    // optional module-scope WGSL declaration required ("" if none)
};

// Ordered key list (mirrors keysOf(kTestTypes) — Record insertion order).
inline const std::vector<std::string>& kTestTypeOrder() {
    static const std::vector<std::string> order = {
        "bool",   "i32",    "u32",    "f32",    "f16",    "abstract-int", "abstract-float",
        "vec2af", "vec3af", "vec4af", "vec2ai", "vec3ai", "vec4ai",       "vec2f",
        "vec3h",  "vec4u",  "vec3b",  "mat2x3f", "mat4x2h", "array",       "atomic",
        "struct", "texture", "sampler",
    };
    return order;
}

inline const std::map<std::string, TestType>& kTestTypes() {
    static const std::map<std::string, TestType> m = {
        {"bool", {"true", "", ""}},
        {"i32", {"1i", "", ""}},
        {"u32", {"1u", "", ""}},
        {"f32", {"1f", "", ""}},
        {"f16", {"1h", "f16", ""}},
        {"abstract-int", {"1", "", ""}},
        {"abstract-float", {"1.0", "", ""}},
        {"vec2af", {"vec2(1.0)", "", ""}},
        {"vec3af", {"vec3(1.0)", "", ""}},
        {"vec4af", {"vec4(1.0)", "", ""}},
        {"vec2ai", {"vec2(1)", "", ""}},
        {"vec3ai", {"vec3(1)", "", ""}},
        {"vec4ai", {"vec4(1)", "", ""}},
        {"vec2f", {"vec2f(1)", "", ""}},
        {"vec3h", {"vec3h(1)", "f16", ""}},
        {"vec4u", {"vec4u(1)", "", ""}},
        {"vec3b", {"vec3<bool>(true)", "", ""}},
        {"mat2x3f", {"mat2x3f(1, 2, 3, 4, 5, 6)", "", ""}},
        {"mat4x2h", {"mat4x2h(1, 2, 3, 4, 5, 6, 7, 8)", "f16", ""}},
        {"array", {"array<i32, 4>(1, 2, 3, 4)", "", ""}},
        {"atomic", {"A", "", "var<workgroup> A : atomic<i32>;"}},
        {"struct", {"Str(1)", "", "struct Str{ i : i32 }"}},
        {"texture", {"T", "", "@group(0) @binding(0) var T : texture_2d<f32>;"}},
        {"sampler", {"S", "", "@group(0) @binding(1) var S : sampler;"}},
    };
    return m;
}

inline std::vector<cts::Value> testTypeNames() {
    std::vector<cts::Value> values;
    for (const std::string& name : kTestTypeOrder()) {
        values.emplace_back(name);
    }
    return values;
}

}  // namespace cts::shader_validation::statement
