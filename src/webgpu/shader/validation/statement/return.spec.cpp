// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/return.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// This port reproduces the relevant slices of webgpu/util/conversion.ts:
//   - `${Type[t]}`           -> the WGSL type spelling   (TypeInfo::spelling)
//   - `Type[t].create(1).wgsl()` -> the value spelling   (TypeInfo::value)
//   - scalarTypeOf(...).kind -> TypeInfo::scalarKind     (for the f16 enable)
//   - isConvertible(src,dst) -> isConvertible() below    (shape + scalar rules)
// The kTestTypes / kTestTypesNoAbstract orderings are preserved exactly.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,return",
    "Validation tests for 'return' statements'");

struct TypeInfo {
    const char* name;      // table key
    const char* spelling;  // `${Type[t]}`
    const char* value;     // Type[t].create(1).wgsl()
    const char* shape;     // isConvertible shapeOf()
    const char* scalarKind;  // scalarTypeOf(t).kind
};

// Mirrors conversion.ts entries for the types referenced by this spec.
static const TypeInfo& typeInfo(const std::string& name) {
    static const std::vector<TypeInfo> v = {
        {"bool", "bool", "true", "scalar", "bool"},
        {"i32", "i32", "i32(1)", "scalar", "i32"},
        {"u32", "u32", "1u", "scalar", "u32"},
        {"f32", "f32", "1.0f", "scalar", "f32"},
        {"f16", "f16", "1.0h", "scalar", "f16"},
        {"vec2f", "vec2<f32>", "vec2(1.0f, 1.0f)", "vec2", "f32"},
        {"vec3h", "vec3<f16>", "vec3(1.0h, 1.0h, 1.0h)", "vec3", "f16"},
        {"vec4u", "vec4<u32>", "vec4(1u, 1u, 1u, 1u)", "vec4", "u32"},
        {"vec3b", "vec3<bool>", "vec3(true, true, true)", "vec3", "bool"},
        {"mat2x3f", "mat2x3<f32>", "mat2x3(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f)", "mat2x3", "f32"},
        {"mat4x2h", "mat4x2<f16>",
         "mat4x2(1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h)", "mat4x2", "f16"},
        {"abstract-int", "abstract-int", "1", "scalar", "abstract-int"},
        {"abstract-float", "abstract-float", "1.0", "scalar", "abstract-float"},
        {"vec2af", "vec2<abstract-float>", "vec2(1.0, 1.0)", "vec2", "abstract-float"},
        {"vec3af", "vec3<abstract-float>", "vec3(1.0, 1.0, 1.0)", "vec3", "abstract-float"},
        {"vec4af", "vec4<abstract-float>", "vec4(1.0, 1.0, 1.0, 1.0)", "vec4", "abstract-float"},
        {"vec2ai", "vec2<abstract-int>", "vec2(1, 1)", "vec2", "abstract-int"},
        {"vec3ai", "vec3<abstract-int>", "vec3(1, 1, 1)", "vec3", "abstract-int"},
        {"vec4ai", "vec4<abstract-int>", "vec4(1, 1, 1, 1)", "vec4", "abstract-int"},
    };
    for (const TypeInfo& ti : v) {
        if (name == ti.name) {
            return ti;
        }
    }
    static const TypeInfo dummy{"", "", "", "", ""};
    return dummy;
}

// kTestTypesNoAbstract (order preserved).
static const std::vector<std::string>& kTestTypesNoAbstract() {
    static const std::vector<std::string> v = {
        "bool", "i32",   "u32",   "f32",     "f16",
        "vec2f", "vec3h", "vec4u", "vec3b", "mat2x3f", "mat4x2h",
    };
    return v;
}

// kTestTypes = [...kTestTypesNoAbstract, abstract..., vec..af/ai] (order preserved).
static const std::vector<std::string>& kTestTypes() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> all = kTestTypesNoAbstract();
        for (const char* extra : {"abstract-int", "abstract-float", "vec2af", "vec3af", "vec4af",
                                  "vec2ai", "vec3ai", "vec4ai"}) {
            all.emplace_back(extra);
        }
        return all;
    }();
    return v;
}

// Mirrors conversion.ts isConvertible(src, dst).
static bool isConvertible(const TypeInfo& src, const TypeInfo& dst) {
    const std::string srcName = src.name;
    const std::string dstName = dst.name;
    if (srcName == dstName) {
        return true;
    }
    if (std::string(src.shape) != std::string(dst.shape)) {
        return false;
    }
    const std::string elSrc = src.scalarKind;
    const std::string elDst = dst.scalarKind;
    if (elSrc == "abstract-float") {
        return elDst == "abstract-float" || elDst == "f16" || elDst == "f32";
    }
    if (elSrc == "abstract-int") {
        return elDst == "abstract-int" || elDst == "abstract-float" || elDst == "f16" ||
               elDst == "f32" || elDst == "u32" || elDst == "i32";
    }
    return false;
}

static std::string f16EnableIf(bool needsF16) {
    return needsF16 ? "enable f16;" : "";
}

// ---- return_missing_value ---------------------------------------------------
// type ∈ [...kTestTypesNoAbstract, undefined]; undefined keyed by "undefined".
static std::vector<Value> missingValueTypeNames() {
    std::vector<Value> values;
    for (const std::string& s : kTestTypesNoAbstract()) {
        values.emplace_back(s);
    }
    values.emplace_back(std::string("undefined"));
    return values;
}

CTS_TEST(g, "return_missing_value")
    .desc("Tests that a 'return' must have a value if the function has a return type")
    .params([](ParamsBuilder u) { return u.combine("type", missingValueTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const bool isUndefined = typeName == "undefined";
        const TypeInfo& type = typeInfo(typeName);
        const std::string enable =
            (!isUndefined && std::string(type.scalarKind) == "f16") ? std::string("enable f16;")
                                                                    : std::string("");
        const std::string code =
            "\n" + enable + "\n\nfn f()" +
            (isUndefined ? std::string("") : (std::string("-> ") + type.spelling)) +
            " {\n  return;\n}\n";
        const bool pass = isUndefined;
        t.expectCompileResult(pass, code);
    });

// ---- return_unexpected_value ------------------------------------------------
// type ∈ [...kTestTypes, undefined].
static std::vector<Value> unexpectedValueTypeNames() {
    std::vector<Value> values;
    for (const std::string& s : kTestTypes()) {
        values.emplace_back(s);
    }
    values.emplace_back(std::string("undefined"));
    return values;
}

CTS_TEST(g, "return_unexpected_value")
    .desc("Tests that a 'return' must not have a value if the function has no return type")
    .params([](ParamsBuilder u) { return u.combine("type", unexpectedValueTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const bool isUndefined = typeName == "undefined";
        const TypeInfo& type = typeInfo(typeName);
        const std::string enable =
            (!isUndefined && std::string(type.scalarKind) == "f16") ? std::string("enable f16;")
                                                                    : std::string("");
        const std::string code = "\n" + enable + "\n\nfn f() {\n  return " +
                                 (isUndefined ? std::string("") : std::string(type.value)) + ";\n}\n";
        const bool pass = isUndefined;
        t.expectCompileResult(pass, code);
    });

// ---- return_type_match ------------------------------------------------------
static std::vector<Value> typeNamesOf(const std::vector<std::string>& names) {
    std::vector<Value> values;
    for (const std::string& s : names) {
        values.emplace_back(s);
    }
    return values;
}

CTS_TEST(g, "return_type_match")
    .desc("Tests that a 'return' value type must match the function return type")
    .params([](ParamsBuilder u) {
        return u.combine("return_value_type", typeNamesOf(kTestTypes()))
            .combine("fn_return_type", typeNamesOf(kTestTypesNoAbstract()));
    })
    .fn([](ShaderValidationTest& t) {
        const TypeInfo& returnValueType = typeInfo(t.param<std::string>("return_value_type"));
        const TypeInfo& fnReturnType = typeInfo(t.param<std::string>("fn_return_type"));
        const bool needsF16 = std::string(returnValueType.scalarKind) == "f16" ||
                              std::string(fnReturnType.scalarKind) == "f16";
        const std::string code = "\n" + f16EnableIf(needsF16) + "\n\nfn f() -> " +
                                 fnReturnType.spelling + " {\n  return " + returnValueType.value +
                                 ";\n}\n";
        const bool pass = isConvertible(returnValueType, fnReturnType);
        t.expectCompileResult(pass, code);
    });

// ---- parse ------------------------------------------------------------------
struct Test {
    const char* name;
    const char* wgsl;
    bool passValue;
    bool passNoValue;
};

static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"no_expr", "return;", false, true},
        {"v", "return v;", true, false},
        {"literal", "return 10;", true, false},
        {"expr", "return 1 + 2;", true, false},
        {"paren_expr", "return (1 + 2);", true, false},
        {"call", "return x();", true, false},
        {"v_no_semicolon", "return v", false, false},
        {"expr_no_semicolon", "return 1 + 2", false, false},
        {"phony_assign", "return _ = 1;", false, false},
        {"increment", "return v++;", false, false},
        {"compound_assign", "return v += 4;", false, false},
        {"lparen_literal", "return (4;", false, false},
        {"literal_lparen", "return 4(;", false, false},
        {"rparen_literal", "return )4;", false, false},
        {"literal_rparen", "return 4);", false, false},
        {"lparen_literal_lparen", "return (4(;", false, false},
        {"rparen_literal_rparen", "return )4);", false, false},
    };
    return v;
}

static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const Test& t : kTests()) {
        values.emplace_back(std::string(t.name));
    }
    return values;
}

static const Test& findTest(const std::string& name) {
    for (const Test& t : kTests()) {
        if (name == t.name) {
            return t;
        }
    }
    static const Test dummy{"", "", false, false};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Test that 'return' statements are parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("test", testNames()).combine("fn_returns_value", {false, true});
    })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const bool fnReturnsValue = t.param<bool>("fn_returns_value");
        const std::string code =
            "\nfn f() " + (fnReturnsValue ? std::string("-> i32") : std::string("")) +
            " {\n  let v = 42;\n  " + std::string(test.wgsl) +
            "\n}\nfn x() -> i32 {\n  return 1;\n}\n";
        t.expectCompileResult(fnReturnsValue ? test.passValue : test.passNoValue, code);
    });

}  // namespace
