// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/unary/logical_negation.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The `scalar_vector` test combines over keysOf(objectsToRecord(kAllScalarsAndVectors)).
// See arithmetic_negation.spec.cpp for the full description of the type table,
// its ordering, the record key strings, and the `type.create(0).wgsl()` value
// spellings (all identical here). Logical negation is accepted only when the
// element type is bool.
//
// Value cannot hold the upstream Type objects, so `type` is keyed by the scalar
// name string and the row reconstructed via a local lookup helper. `invalid_types`
// keys each kInvalidTypes entry by name; `parse` keys kTests by name.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,unary,logical_negation",
    "Validation tests for logical negation expressions.");

// ---- scalar_vector: kAllScalarsAndVectors (order preserved) -----------------
struct ScalarVectorType {
    const char* name;     // Type.toString() == record key
    const char* value;    // type.create(0).wgsl()
    const char* element;  // scalarTypeOf(type).kind
    bool usesF16;         // scalarTypeOf(type) === Type.f16
};

static const std::vector<ScalarVectorType>& kScalarAndVectorTypes() {
    static const std::vector<ScalarVectorType> v = {
        {"bool", "false", "bool", false},
        {"vec2<bool>", "vec2(false, false)", "bool", false},
        {"vec3<bool>", "vec3(false, false, false)", "bool", false},
        {"vec4<bool>", "vec4(false, false, false, false)", "bool", false},
        {"abstract-int", "0", "abstract-int", false},
        {"abstract-float", "0.0", "abstract-float", false},
        {"f32", "0.0f", "f32", false},
        {"f16", "0.0h", "f16", true},
        {"vec2<abstract-int>", "vec2(0, 0)", "abstract-int", false},
        {"vec3<abstract-int>", "vec3(0, 0, 0)", "abstract-int", false},
        {"vec4<abstract-int>", "vec4(0, 0, 0, 0)", "abstract-int", false},
        {"vec2<abstract-float>", "vec2(0.0, 0.0)", "abstract-float", false},
        {"vec2<f32>", "vec2(0.0f, 0.0f)", "f32", false},
        {"vec2<f16>", "vec2(0.0h, 0.0h)", "f16", true},
        {"vec3<abstract-float>", "vec3(0.0, 0.0, 0.0)", "abstract-float", false},
        {"vec3<f32>", "vec3(0.0f, 0.0f, 0.0f)", "f32", false},
        {"vec3<f16>", "vec3(0.0h, 0.0h, 0.0h)", "f16", true},
        {"vec4<abstract-float>", "vec4(0.0, 0.0, 0.0, 0.0)", "abstract-float", false},
        {"vec4<f32>", "vec4(0.0f, 0.0f, 0.0f, 0.0f)", "f32", false},
        {"vec4<f16>", "vec4(0.0h, 0.0h, 0.0h, 0.0h)", "f16", true},
        {"i32", "i32(0)", "i32", false},
        {"vec2<i32>", "vec2(i32(0), i32(0))", "i32", false},
        {"vec3<i32>", "vec3(i32(0), i32(0), i32(0))", "i32", false},
        {"vec4<i32>", "vec4(i32(0), i32(0), i32(0), i32(0))", "i32", false},
        {"u32", "0u", "u32", false},
        {"vec2<u32>", "vec2(0u, 0u)", "u32", false},
        {"vec3<u32>", "vec3(0u, 0u, 0u)", "u32", false},
        {"vec4<u32>", "vec4(0u, 0u, 0u, 0u)", "u32", false},
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
    static const ScalarVectorType dummy{"", "", "", false};
    return dummy;
}

CTS_TEST(g, "scalar_vector")
    .desc(
        "Validates that scalar and vector logical negation expressions are only accepted for bool "
        "types.")
    .params([](ParamsBuilder u) {
        return u.combine("type", scalarVectorNames()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const ScalarVectorType& type = findScalarVector(t.param<std::string>("type"));
        const bool pass = std::string(type.element) == "bool";
        const std::string code = "\n" +
                                 (type.usesF16 ? std::string("enable f16;") : std::string("")) +
                                 "\nconst rhs = " + type.value + ";\nconst foo = !rhs;\n";
        t.expectCompileResult(pass, code);
    });

// ---- invalid_types: kInvalidTypes (object key order preserved) --------------
struct InvalidType {
    const char* name;
    const char* expr;
    const char* controlled;  // control(expr)
};

static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"mat2x2f", "m", "bool(m[0][0])"},
        {"array", "arr", "arr[0]"},
        {"ptr", "(&b)", "*(&b)"},
        {"atomic", "a", "bool(atomicLoad(&a))"},
        {"texture", "t", "bool(textureLoad(t, vec2(), 0).x)"},
        {"sampler", "s", "bool(textureSampleLevel(t, s, vec2(), 0).x)"},
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
        "Validates that logical negation expressions are never accepted for non-scalar and "
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
            "\nstruct S { b : bool }"
            "\n"
            "\nvar<private> b : bool;"
            "\nvar<private> m : mat2x2f;"
            "\nvar<private> arr : array<bool, 4>;"
            "\nvar<private> str : S;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = !" + expr + ";"
            "\n}"
            "\n";
        t.expectCompileResult(control, code);
    });

// ---- parse: kTests (object key order preserved) -----------------------------
struct ParseCase {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<ParseCase>& kTests() {
    static const std::vector<ParseCase> v = {
        {"not_bool_literal", "let a = !true;", true},
        {"not_bool_expr", "let a = !(1 == 2);", true},
        {"not_not_bool_literal", "let a = !!true;", true},
        {"not_not_bool_expr", "let a = !!(1 == 2);", true},
        {"not_int_literal", "let a = !42;", false},
        {"not_int_expr", "let a = !(40 + 2);", false},
    };
    return v;
}

static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const ParseCase& c : kTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ParseCase& findTest(const std::string& name) {
    for (const ParseCase& c : kTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ParseCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Test that unary operators are parsed correctly")
    .params([](ParamsBuilder u) {
        return u.combine("stmt", testNames());
    })
    .fn([](ShaderValidationTest& t) {
        const ParseCase& c = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\n@vertex"
            "\nfn vtx() -> @builtin(position) vec4f {"
            "\n  " + std::string(c.src) +
            "\n  return vec4f(1);"
            "\n}"
            "\n    ";
        t.expectCompileResult(c.pass, code);
    });

}  // namespace
