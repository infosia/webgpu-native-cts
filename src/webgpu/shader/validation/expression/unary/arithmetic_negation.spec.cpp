// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/unary/arithmetic_negation.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The `scalar_vector` test combines over keysOf(objectsToRecord(kAllScalarsAndVectors)).
// kAllScalarsAndVectors (from util/conversion.ts) is the ordered list
//   kAllBoolScalarsAndVectors ++ kAllNumericScalarsAndVectors, where
//   kAllNumericScalarsAndVectors = kConvertableToFloatScalarsAndVectors ++
//   kConcreteIntegerScalarsAndVectors. The record key for each type is its
//   Type.toString(): scalars render as their kind ("bool", "i32", "u32", "f32",
//   "f16", "abstract-int", "abstract-float"); vectors as "vecW<element>". Those
//   exact key strings are the case-query `type` names below, in upstream order.
//
// `type.create(0).wgsl()` is the VALUE spelling (i32 -> "i32(0)", u32 -> "0u",
// f32 -> "0.0f", f16 -> "0.0h", abstract-int -> "0", abstract-float -> "0.0",
// bool -> "false", vecW(...) repeating the element value). Each is precomputed.
//
// Value cannot hold the upstream Type objects, so the `type` param is keyed by the
// scalar name string and the row (value spelling, element-signedness, f16 usage)
// is reconstructed in the body via a local lookup helper. `invalid_types` keys
// each kInvalidTypes entry by name and reconstructs the expr/control fields.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,unary,arithmetic_negation",
    "Validation tests for arithmetic negation expressions.");

// ---- scalar_vector: kAllScalarsAndVectors (order preserved) -----------------
struct ScalarVectorType {
    const char* name;    // Type.toString() == record key
    const char* value;   // type.create(0).wgsl()
    const char* element; // scalarTypeOf(type).kind
    bool signed_;        // scalarTypeOf(type).signed
    bool usesF16;        // scalarTypeOf(type) === Type.f16
};

static const std::vector<ScalarVectorType>& kScalarAndVectorTypes() {
    static const std::vector<ScalarVectorType> v = {
        {"bool", "false", "bool", false, false},
        {"vec2<bool>", "vec2(false, false)", "bool", false, false},
        {"vec3<bool>", "vec3(false, false, false)", "bool", false, false},
        {"vec4<bool>", "vec4(false, false, false, false)", "bool", false, false},
        {"abstract-int", "0", "abstract-int", true, false},
        {"abstract-float", "0.0", "abstract-float", true, false},
        {"f32", "0.0f", "f32", true, false},
        {"f16", "0.0h", "f16", true, true},
        {"vec2<abstract-int>", "vec2(0, 0)", "abstract-int", true, false},
        {"vec3<abstract-int>", "vec3(0, 0, 0)", "abstract-int", true, false},
        {"vec4<abstract-int>", "vec4(0, 0, 0, 0)", "abstract-int", true, false},
        {"vec2<abstract-float>", "vec2(0.0, 0.0)", "abstract-float", true, false},
        {"vec2<f32>", "vec2(0.0f, 0.0f)", "f32", true, false},
        {"vec2<f16>", "vec2(0.0h, 0.0h)", "f16", true, true},
        {"vec3<abstract-float>", "vec3(0.0, 0.0, 0.0)", "abstract-float", true, false},
        {"vec3<f32>", "vec3(0.0f, 0.0f, 0.0f)", "f32", true, false},
        {"vec3<f16>", "vec3(0.0h, 0.0h, 0.0h)", "f16", true, true},
        {"vec4<abstract-float>", "vec4(0.0, 0.0, 0.0, 0.0)", "abstract-float", true, false},
        {"vec4<f32>", "vec4(0.0f, 0.0f, 0.0f, 0.0f)", "f32", true, false},
        {"vec4<f16>", "vec4(0.0h, 0.0h, 0.0h, 0.0h)", "f16", true, true},
        {"i32", "i32(0)", "i32", true, false},
        {"vec2<i32>", "vec2(i32(0), i32(0))", "i32", true, false},
        {"vec3<i32>", "vec3(i32(0), i32(0), i32(0))", "i32", true, false},
        {"vec4<i32>", "vec4(i32(0), i32(0), i32(0), i32(0))", "i32", true, false},
        {"u32", "0u", "u32", false, false},
        {"vec2<u32>", "vec2(0u, 0u)", "u32", false, false},
        {"vec3<u32>", "vec3(0u, 0u, 0u)", "u32", false, false},
        {"vec4<u32>", "vec4(0u, 0u, 0u, 0u)", "u32", false, false},
    };
    return v;
}

static std::vector<Value> scalarVectorNames() {
    std::vector<Value> values;
    for (const ScalarVectorType& s : kScalarAndVectorTypes()) {
        values.emplace_back(std::string(s.name));
    }
    return values;
}

static const ScalarVectorType& findScalarVector(const std::string& name) {
    for (const ScalarVectorType& s : kScalarAndVectorTypes()) {
        if (name == s.name) {
            return s;
        }
    }
    static const ScalarVectorType dummy{"", "", "", false, false};
    return dummy;
}

CTS_TEST(g, "scalar_vector")
    .desc(
        "Validates that scalar and vector numeric negation expressions are accepted for numerical "
        "types that are signed.")
    .params([](ParamsBuilder u) {
        return u.combine("type", scalarVectorNames()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const ScalarVectorType& type = findScalarVector(t.param<std::string>("type"));
        const std::string code = "\n" +
                                 (type.usesF16 ? std::string("enable f16;") : std::string("")) +
                                 "\nconst rhs = " + type.value + ";\nconst foo = -rhs;\n";
        t.expectCompileResult(type.signed_, code);
    });

// ---- invalid_types: kInvalidTypes (object key order preserved) --------------
struct InvalidType {
    const char* name;
    const char* expr;
    // control wraps `expr` into a valid negation operand; precomputed for expr.
    const char* controlled;
};

static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"mat2x2f", "m", "m[0][0]"},
        {"array", "arr", "arr[0]"},
        {"ptr", "(&b)", "*(&b)"},
        {"atomic", "a", "atomicLoad(&a)"},
        {"texture", "t", "textureLoad(t, vec2(), 0).x"},
        {"sampler", "s", "textureSampleLevel(t, s, vec2(), 0).x"},
        {"struct", "str", "str.b"},
    };
    return v;
}

static std::vector<Value> invalidTypeNames() {
    std::vector<Value> values;
    for (const InvalidType& i : kInvalidTypes()) {
        values.emplace_back(std::string(i.name));
    }
    return values;
}

static const InvalidType& findInvalidType(const std::string& name) {
    for (const InvalidType& i : kInvalidTypes()) {
        if (name == i.name) {
            return i;
        }
    }
    static const InvalidType dummy{"", "", ""};
    return dummy;
}

CTS_TEST(g, "invalid_types")
    .desc(
        "Validates that arithmetic negation expressions are never accepted for non-scalar and "
        "non-vector types.")
    .params([](ParamsBuilder u) {
        return u.combine("type", invalidTypeNames())
            .combine("control", {true, false})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const InvalidType& type = findInvalidType(t.param<std::string>("type"));
        const bool control = t.param<bool>("control");
        const std::string expr = control ? std::string(type.controlled) : std::string(type.expr);
        const std::string code =
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<i32>;"
            "\n"
            "\nstruct S { b : i32 }"
            "\n"
            "\nvar<private> b : i32;"
            "\nvar<private> m : mat2x2f;"
            "\nvar<private> arr : array<i32, 4>;"
            "\nvar<private> str : S;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = -" + expr + ";"
            "\n}"
            "\n";
        t.expectCompileResult(control, code);
    });

}  // namespace
