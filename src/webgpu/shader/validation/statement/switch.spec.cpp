// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/switch.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The integer `Type[t].create(N).wgsl()` value spellings used by the case-type
// tests are: i32 -> `i32(N)`, u32 -> `Nu`, abstract-int -> `N` (conversion.ts).

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/statement/test_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
using cts::shader_validation::statement::kTestTypes;
using cts::shader_validation::statement::TestType;
using cts::shader_validation::statement::testTypeNames;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,switch",
    "Validation tests for 'switch' statements'");

// Type[t].create(value).wgsl() for the integer types used below.
static std::string intValueWgsl(const std::string& type, int value) {
    if (type == "i32") {
        return "i32(" + std::to_string(value) + ")";
    }
    if (type == "u32") {
        return std::to_string(value) + "u";
    }
    // abstract-int
    return std::to_string(value);
}

CTS_TEST(g, "condition_type")
    .desc("Tests that a 'switch' condition must be of an integer type")
    .params([](ParamsBuilder u) { return u.combine("type", testTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const TestType& type = kTestTypes().at(typeName);
        const std::string code =
            "\n" + (type.requires_.empty() ? std::string("") : ("enable " + type.requires_ + ";")) +
            "\n\n" + type.header + "\n\nfn f() -> bool {\n  switch " + type.value +
            " {\n    case 1: {\n      return true;\n    }\n    default: {\n      return false;\n   "
            " }\n  }\n}\n";
        const bool pass = typeName == "i32" || typeName == "u32" || typeName == "abstract-int";
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "condition_type_match_case_type")
    .desc("Tests that a 'switch' condition must have a common type with its case values")
    .params([](ParamsBuilder u) {
        return u.combine("cond_type", {"i32", "u32", "abstract-int"})
            .combine("case_type", {"i32", "u32", "abstract-int"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string condType = t.param<std::string>("cond_type");
        const std::string caseType = t.param<std::string>("case_type");
        const std::string code =
            "\nfn f() -> bool {\nswitch " + intValueWgsl(condType, 1) + " {\n  case " +
            intValueWgsl(caseType, 2) +
            ": {\n    return true;\n  }\n  default: {\n    return false;\n  }\n}\n}\n";
        const bool pass =
            condType == caseType || condType == "abstract-int" || caseType == "abstract-int";
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "case_types_match")
    .desc("Tests that switch case types must have a common type")
    .params([](ParamsBuilder u) {
        return u.combine("case_a_type", {"i32", "u32", "abstract-int"})
            .combine("case_b_type", {"i32", "u32", "abstract-int"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseAType = t.param<std::string>("case_a_type");
        const std::string caseBType = t.param<std::string>("case_b_type");
        const std::string code =
            "\nfn f() -> bool {\nswitch 1 {\n  case " + intValueWgsl(caseAType, 1) +
            ": {\n    return true;\n  }\n  case " + intValueWgsl(caseBType, 2) +
            ": {\n    return true;\n  }\n  default: {\n    return false;\n  }\n}\n}\n";
        const bool pass =
            caseAType == caseBType || caseAType == "abstract-int" || caseBType == "abstract-int";
        t.expectCompileResult(pass, code);
    });

struct Test {
    const char* name;
    const char* wgsl;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"L_default", "switch L { default {} }", true},
        {"L_paren_default", "switch (L) { default {} }", true},
        {"L_case_1_2_default", "switch L { case 1, 2 {} default {} }", true},
        {"L_case_1_case_2_default", "switch L { case 1 {} case 2 {} default {} }", true},
        {"L_case_1_colon_case_2_colon_default_colon",
         "switch L { case 1: {} case 2: {} default: {} }", true},
        {"L_case_1_colon_default_colon", "switch L { case 1: {} default: {} }", true},
        {"L_case_1_colon_default", "switch L { case 1: {} default {} }", true},
        {"L_case_1_default_2", "switch L { case 1, default, 2 {} }", true},
        {"L_case_1_default_case_2", "switch L { case 1 {} default {} case 2 {} }", true},
        {"L_case_1_default_colon", "switch L { case 1 {} default: {} }", true},
        {"L_case_1_default", "switch L { case 1 {} default {} }", true},
        {"L_case_2_1_default", "switch L { case 2, 1 {} default {} }", true},
        {"L_case_2_case_1_default", "switch L { case 2 {} case 1 {} default {} }", true},
        {"L_case_2_default_case_1", "switch L { case 2 {} default {} case 1 {} }", true},
        {"L_case_builtin_default", "switch L { case max(1,2) {} default {} }", true},
        {"L_case_C1_case_C2_default", "switch L { case C1 {} case C2 {} default {} }", true},
        {"L_case_C1_default", "switch L { case C1 {} default {} }", true},
        {"L_case_default_1", "switch L { case default, 1 {} }", true},
        {"L_case_default_2_1", "switch L { case default, 2, 1 {} }", true},
        {"L_case_default_2_case_1", "switch L { case default, 2 {} case 1 {} }", true},
        {"L_case_default", "switch L { case default {} }", true},
        {"L_case_expr_default", "switch L { case 1+1 {} default {} }", true},
        {"L_default_break", "switch L { default { break; } }", true},
        {"L_default_case_1_2", "switch L { default {} case 1, 2 {} }", true},
        {"L_default_case_1_break", "switch L { default {} case 1 { break; } }", true},
        {"L_default_case_1_case_2", "switch L { default {} case 1 {} case 2 {} }", true},
        {"L_default_case_1_colon_break", "switch L { default {} case 1: { break; } }", true},
        {"L_default_case_2_case_1", "switch L { default {} case 2 {} case 1 {} }", true},
        {"L_default_colon_break", "switch L { default: { break; } }", true},
        {"L_default_colon", "switch L { default: {} }", true},
        {"L_no_block", "switch L", false},
        {"L_empty_block", "switch L {}", false},
        {"L_no_default", "switch L { case 1 {} }", false},
        {"L_default_default", "switch L { default, default {} }", false},
        {"L_default_block_default_block", "switch L { default {} default {} }", false},
        {"L_case_1_case_1_default", "switch L { case 1 {} case 1 {} default {} }", false},
        {"L_case_C1_case_C1_default", "switch L { case C1 {} case C1 {} default {} }", false},
        {"L_case_C2_case_expr_default", "switch L { case C2 {} case 1+1 {} default {} }", false},
        {"L_default_1", "switch L { default, 1 {} }", false},
        {"L_default_2_case_1", "switch L { default, 2 {} case 1 {} }", false},
        {"no_cond", "switch { default{} }", false},
        {"no_cond_no_block", "switch;", false},
        {"lparen_L", "switch (L { default {}}", false},
        {"L_lparen", "switch L) { default {}}", false},
        {"lparen_L_lparen", "switch )L) { default {}}", false},
        {"rparen_L_rparen", "switch (L( { default {}}", false},
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
    static const Test dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Test that 'switch' statements are parsed correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const std::string code =
            "\nfn f() {\n  let L = 1;\n  const C1 = 1;\n  const C2 = 2;\n  " +
            std::string(test.wgsl) + "\n}";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
