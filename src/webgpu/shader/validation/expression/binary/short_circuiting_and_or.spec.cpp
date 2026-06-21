// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/short_circuiting_and_or.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kAllScalarsAndVectors is keyed by Type.toString() (vectors as `vecN<elementType>`);
// lhs/rhs params carry those keys and Types are reconstructed via typeByName().
// kInvalidTypes/kInvalidRhsExpressions/kInvalidArrayCounts are keyed by a scalar
// `name` and reconstructed in the body. See binary_types.h.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,binary,short_circuiting_and_or",
    "Validation tests for short-circuiting && and || expressions.");

// ---- scalar_vector ----------------------------------------------------------
CTS_TEST(g, "scalar_vector")
    .desc("Validates that scalar and vector short-circuiting operators are only accepted for scalar "
          "booleans.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("lhs", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("rhs", bt::typeNamesNoVec34(bt::kAllScalarsAndVectors()))
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type lhs = bt::typeByName(t.param<std::string>("lhs"));
        const bt::Type rhs = bt::typeByName(t.param<std::string>("rhs"));
        const bt::Type lhsElement = bt::scalarTypeOf(lhs);
        const bt::Type rhsElement = bt::scalarTypeOf(rhs);
        const bool hasF16 =
            lhsElement.kind == bt::ScalarKind::F16 || rhsElement.kind == bt::ScalarKind::F16;
        const std::string op = t.param<std::string>("op");
        const std::string code =
            std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nconst lhs = " +
            bt::createWgsl(lhs, 0) + ";\nconst rhs = " + bt::createWgsl(rhs, 0) +
            ";\nconst foo = lhs " + op + " rhs;\n";

        bool valid = false;
        if (lhs.isScalar() && rhs.isScalar()) {
            valid =
                lhsElement.kind == bt::ScalarKind::Bool && rhsElement.kind == bt::ScalarKind::Bool;
        }
        t.expectCompileResult(valid, code);
    });

// ---- invalid_types ----------------------------------------------------------
struct InvalidType {
    const char* name;
    const char* expr;
};

static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"mat2x2f", "m"}, {"array", "arr"},  {"ptr", "(&b)"}, {"atomic", "a"},
        {"texture", "t"}, {"sampler", "s"},  {"struct", "str"},
    };
    return v;
}

static std::vector<Value> invalidTypeNames() {
    std::vector<Value> values;
    for (const InvalidType& it : kInvalidTypes()) {
        values.emplace_back(std::string(it.name));
    }
    return values;
}

static std::string invalidControl(const std::string& name, const std::string& e) {
    if (name == "mat2x2f") return "bool(" + e + "[0][0])";
    if (name == "array") return e + "[0]";
    if (name == "ptr") return "*" + e;
    if (name == "atomic") return "bool(atomicLoad(&" + e + "))";
    if (name == "texture") return "bool(textureLoad(" + e + ", vec2(), 0).x)";
    if (name == "sampler") return "bool(textureSampleLevel(t, " + e + ", vec2(), 0).x)";
    if (name == "struct") return e + ".b";
    return e;
}

static const InvalidType& findInvalidType(const std::string& name) {
    for (const InvalidType& it : kInvalidTypes()) {
        if (name == it.name) {
            return it;
        }
    }
    static const InvalidType dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "invalid_types")
    .desc("Validates that short-circuiting expressions are never accepted for non-scalar and "
          "non-vector types.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("type", invalidTypeNames())
            .combine("control", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const InvalidType& it = findInvalidType(t.param<std::string>("type"));
        const bool control = t.param<bool>("control");
        const std::string op = t.param<std::string>("op");
        const std::string expr = control ? invalidControl(it.name, it.expr) : std::string(it.expr);
        const std::string code =
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<i32>;"
            "\n\nstruct S { b : bool }"
            "\n\nvar<private> b : bool;"
            "\nvar<private> m : mat2x2f;"
            "\nvar<private> arr : array<bool, 4>;"
            "\nvar<private> str : S;"
            "\n\n@compute @workgroup_size(1)\nfn main() {\n  let foo = " +
            expr + " " + op + " " + expr + ";\n}\n";
        t.expectCompileResult(control, code);
    });

// kLhsForShortCircuit: '&&' -> false, '||' -> true.
static bool lhsForShortCircuit(const std::string& op) { return op == "||"; }

// kInvalidRhsExpressions (object key order preserved).
struct RhsExpr {
    const char* name;
    const char* expr;
};
static const std::vector<RhsExpr>& kInvalidRhsExpressions() {
    static const std::vector<RhsExpr> v = {
        {"overflow", "i32(1<<thirty_one) < 0"},
        {"div_zero_i32", "(1 / zero_i32) == 0"},
        {"div_zero_f32", "(one_f32 / 0) == 0"},
        {"builtin", "sqrt(-one_f32) == 0"},
    };
    return v;
}
static std::vector<Value> rhsExprNames() {
    std::vector<Value> values;
    for (const RhsExpr& r : kInvalidRhsExpressions()) {
        values.emplace_back(std::string(r.name));
    }
    return values;
}
static const char* findRhsExpr(const std::string& name) {
    for (const RhsExpr& r : kInvalidRhsExpressions()) {
        if (name == r.name) {
            return r.expr;
        }
    }
    return "";
}

CTS_TEST(g, "invalid_rhs_const")
    .desc("Validates that a short-circuiting expression with a const-expression LHS guards the "
          "evaluation of its RHS expression.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("rhs", rhsExprNames())
            .combine("short_circuit", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const bool shortCircuit = t.param<bool>("short_circuit");
        bool lhs = lhsForShortCircuit(op);
        if (!shortCircuit) {
            lhs = !lhs;
        }
        const std::string code =
            std::string("\nconst thirty_one = 31u;\nconst zero_i32 = 0i;\nconst one_f32 = 1.0f;\n\n"
                        "@compute @workgroup_size(1)\nfn main() {\n  let foo = ") +
            (lhs ? "true" : "false") + " " + op + " " + findRhsExpr(t.param<std::string>("rhs")) +
            ";\n}\n";
        t.expectCompileResult(shortCircuit, code);
    });

CTS_TEST(g, "invalid_rhs_fn_override")
    .desc("Validates that a short-circuiting expression in functions with a override-expression LHS "
          "guards the evaluation of its RHS expression.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("rhs", rhsExprNames())
            .combine("short_circuit", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const bool shortCircuit = t.param<bool>("short_circuit");
        bool lhs = lhsForShortCircuit(op);
        if (!shortCircuit) {
            lhs = !lhs;
        }
        const std::string code =
            "\noverride cond : bool;\noverride thirty_one = 31u;\noverride zero_i32 = 0i;\noverride "
            "one_f32 = 1.0f;";
        const std::string codeEntry =
            "let foo = cond " + op + " " + findRhsExpr(t.param<std::string>("rhs")) + ";";
        ShaderValidationTest::PipelineArgs args;
        args.expectedResult = shortCircuit;
        args.code = code;
        args.constants["cond"] = lhs ? 1.0 : 0.0;
        args.statements = {codeEntry};
        t.expectPipelineResult(args);
    });

CTS_TEST(g, "invalid_rhs_override")
    .desc("Validates that a short-circuiting expression with an override-expression LHS guards the "
          "evaluation of its RHS expression.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("rhs", rhsExprNames())
            .combine("short_circuit", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const bool shortCircuit = t.param<bool>("short_circuit");
        bool lhs = lhsForShortCircuit(op);
        if (!shortCircuit) {
            lhs = !lhs;
        }
        const std::string code =
            std::string("\noverride cond : bool;\noverride zero_i32 = 0i;\noverride one_f32 = "
                        "1.0f;\noverride thirty_one = 31u;\noverride foo = cond ") +
            op + " " + findRhsExpr(t.param<std::string>("rhs")) + ";\n";
        ShaderValidationTest::PipelineArgs args;
        args.expectedResult = shortCircuit;
        args.code = code;
        args.constants["cond"] = lhs ? 1.0 : 0.0;
        args.reference = {"foo"};
        t.expectPipelineResult(args);
    });

CTS_TEST(g, "nested_invalid_rhs_override")
    .desc("Validates that nested short-circuiting expressions with an override-expression LHS "
          "guards the evaluation of its RHS expression.")
    .params([](ParamsBuilder u) {
        return u.combine("op_a", {"&&", "||"})
            .combine("op_b", {"&&", "||"})
            .combine("cond_a_val", {Value(false), Value(true)})
            .combine("cond_b_val", {Value(false), Value(true)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string opA = t.param<std::string>("op_a");
        const std::string opB = t.param<std::string>("op_b");
        const bool condAVal = t.param<bool>("cond_a_val");
        const bool condBVal = t.param<bool>("cond_b_val");
        const std::string code = "\noverride cond_a : bool;\noverride cond_b : bool;\noverride "
                                 "zero_i32 = 0i;\n";
        const std::string codeEntry = "let foo = (cond_a " + opA + "  cond_b ) " + opB +
                                      "   (1 / zero_i32) == 0;";
        const bool opAOr = lhsForShortCircuit(opA);
        const bool opBOr = lhsForShortCircuit(opB);
        const bool lhs = opAOr ? (condAVal || condBVal) : (condAVal && condBVal);
        const bool evalsRhs = opBOr ? !lhs : lhs;
        ShaderValidationTest::PipelineArgs args;
        args.expectedResult = !evalsRhs;
        args.code = code;
        args.constants["cond_a"] = condAVal ? 1.0 : 0.0;
        args.constants["cond_b"] = condBVal ? 1.0 : 0.0;
        args.statements = {codeEntry};
        t.expectPipelineResult(args);
    });

// kInvalidArrayCounts (object key order preserved).
struct ArrayCount {
    const char* name;
    const char* expr;
};
static const std::vector<ArrayCount>& kInvalidArrayCounts() {
    static const std::vector<ArrayCount> v = {
        {"negative", "value - 2"},
        {"sqrt_neg1", "u32(sqrt(value - 2))"},
        {"nested", "10 + array<i32, value - 2>()[0]"},
    };
    return v;
}
static std::vector<Value> arrayCountNames() {
    std::vector<Value> values;
    for (const ArrayCount& a : kInvalidArrayCounts()) {
        values.emplace_back(std::string(a.name));
    }
    return values;
}
static const char* findArrayCount(const std::string& name) {
    for (const ArrayCount& a : kInvalidArrayCounts()) {
        if (name == a.name) {
            return a.expr;
        }
    }
    return "";
}

CTS_TEST(g, "invalid_array_count_on_rhs")
    .desc("Validates that an invalid array count expression is not guarded by a short-circuiting "
          "expression.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("rhs", arrayCountNames())
            .combine("control", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const bool control = t.param<bool>("control");
        const std::string lhs = op == "&&" ? "false" : "true";
        const std::string code =
            std::string("\nconst value = ") + (control ? "10" : "1") +
            ";\n\n@compute @workgroup_size(1)\nfn main() {\n  let foo = " + lhs + " " + op +
            " array<bool, " + findArrayCount(t.param<std::string>("rhs")) + ">()[0];\n}\n";
        t.expectCompileResult(control, code);
    });

CTS_TEST(g, "array_override")
    .desc("Validates that override initializing expressions works in conjunction with arrays")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"&&", "||"})
            .combine("a_val", {Value(0), Value(1)})
            .combine("b_val", {Value(0), Value(1)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const int64_t aVal = t.param<int64_t>("a_val");
        const int64_t bVal = t.param<int64_t>("b_val");
        const std::string code =
            std::string("\noverride a_val:i32;\noverride b_val:i32;\noverride bad_size = (a_val - "
                        "10);\noverride good_size = (b_val + 10);\nvar<workgroup> "
                        "zero_array:array<i32, select(bad_size, good_size, a_val == 1 ") +
            op + " b_val == 1 )>;\n";
        const std::string codeEntry = "let foo = zero_array[0];";
        const bool opAOr = lhsForShortCircuit(op);
        const bool condVal = opAOr ? (aVal == 1 || bVal == 1) : (aVal == 1 && bVal == 1);
        ShaderValidationTest::PipelineArgs args;
        args.expectedResult = condVal;
        args.code = code;
        args.constants["a_val"] = static_cast<double>(aVal);
        args.constants["b_val"] = static_cast<double>(bVal);
        args.statements = {codeEntry};
        t.expectPipelineResult(args);
    });

}  // namespace
