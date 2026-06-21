// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/precedence.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for operator precedence.
//
// Port note: upstream keys binary operators / expressions by their string name and
// looks the corresponding struct up from a Record. Value cannot carry the struct,
// so each combine() keys by the scalar name and the struct is reconstructed in the
// body / filter via a local lookup helper.

#include <map>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,precedence",
    "Validation tests for operator precedence.");

// Bit set for the binary operator groups.
constexpr int kMultiplicative = 1 << 0;
constexpr int kAdditive = 1 << 1;
constexpr int kShift = 1 << 2;
constexpr int kRelational = 1 << 3;
constexpr int kBinaryAnd = 1 << 4;
constexpr int kBinaryXor = 1 << 5;
constexpr int kBinaryOr = 1 << 6;
constexpr int kLogical = 1 << 7;

// Set of other operators that each operator can precede without any parentheses.
static int canPrecedeWithoutParens(int group) {
    switch (group) {
        case kMultiplicative:
            return kMultiplicative | kAdditive | kRelational;
        case kAdditive:
            return kMultiplicative | kAdditive | kRelational;
        case kShift:
            return kRelational | kLogical;
        case kRelational:
            return kMultiplicative | kAdditive | kShift | kLogical;
        case kBinaryAnd:
            return kBinaryAnd;
        case kBinaryXor:
            return kBinaryXor;
        case kBinaryOr:
            return kBinaryOr;
        case kLogical:
            return kRelational;
        default:
            return 0;
    }
}

struct BinaryOperatorInfo {
    const char* name;
    const char* op;
    int group;
};

// Mirrors upstream kBinaryOperators (object key order preserved).
static const std::vector<BinaryOperatorInfo>& kBinaryOperators() {
    static const std::vector<BinaryOperatorInfo> v = {
        {"mul", "*", kMultiplicative},
        {"div", "/", kMultiplicative},
        {"mod", "%", kMultiplicative},
        {"add", "+", kAdditive},
        {"sub", "-", kAdditive},
        {"shl", "<<", kShift},
        {"shr", ">>", kShift},
        {"lt", "<", kRelational},
        {"gt", ">", kRelational},
        {"le", "<=", kRelational},
        {"ge", ">=", kRelational},
        {"eq", "==", kRelational},
        {"ne", "!=", kRelational},
        {"bin_and", "&", kBinaryAnd},
        {"bin_xor", "^", kBinaryXor},
        {"bin_or", "|", kBinaryOr},
        {"log_and", "&&", kLogical},
        {"log_or", "||", kLogical},
    };
    return v;
}

static std::vector<Value> binaryOperatorNames() {
    std::vector<Value> values;
    for (const BinaryOperatorInfo& o : kBinaryOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}

static const BinaryOperatorInfo& findOperator(const std::string& name) {
    for (const BinaryOperatorInfo& o : kBinaryOperators()) {
        if (name == o.name) {
            return o;
        }
    }
    static const BinaryOperatorInfo dummy{"", "", 0};
    return dummy;
}

CTS_TEST(g, "binary_requires_parentheses")
    .desc("Validates that certain binary operators require parentheses to bind correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("op1", binaryOperatorNames())
            .combine("op2", binaryOperatorNames())
            .filter([](const ParamRecord& p) {
                const std::string op1 = valueAs<std::string>(*findParam(p, "op1"));
                const std::string op2 = valueAs<std::string>(*findParam(p, "op2"));
                // Skip expressions that would parse as template lists.
                if (op1 == "lt" && (op2 == "gt" || op2 == "ge" || op2 == "shr")) {
                    return false;
                }
                // Only combine logical operators with relational operators.
                if (findOperator(op1).group == kLogical) {
                    return findOperator(op2).group == kRelational;
                }
                if (findOperator(op2).group == kLogical) {
                    return findOperator(op1).group == kRelational;
                }
                return true;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const BinaryOperatorInfo& op1 = findOperator(t.param<std::string>("op1"));
        const BinaryOperatorInfo& op2 = findOperator(t.param<std::string>("op2"));
        const std::string aTy = op1.group == kLogical ? std::string("bool") : std::string("u32");
        const std::string cTy = op2.group == kLogical ? std::string("bool") : std::string("u32");
        const std::string code =
            std::string("\nvar<private> a : ") + aTy + ";\n" + "var<private> b : u32;\n" +
            "var<private> c : " + cTy + ";\n" + "fn foo() {\n  let foo = a " + op1.op + " b " +
            op2.op + " c;\n}\n";

        const bool valid = (canPrecedeWithoutParens(op1.group) & op2.group) != 0;
        t.expectCompileResult(valid, code);
    });

CTS_TEST(g, "mixed_logical_requires_parentheses")
    .desc("Validates that mixed logical operators require parentheses to bind correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("op1", binaryOperatorNames())
            .combine("op2", binaryOperatorNames())
            .combine("parens", {"none", "left", "right"})
            .filter([](const ParamRecord& p) {
                const int group1 = findOperator(valueAs<std::string>(*findParam(p, "op1"))).group;
                const int group2 = findOperator(valueAs<std::string>(*findParam(p, "op2"))).group;
                return group1 == kLogical && group2 == kLogical;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const BinaryOperatorInfo& op1 = findOperator(t.param<std::string>("op1"));
        const BinaryOperatorInfo& op2 = findOperator(t.param<std::string>("op2"));
        const std::string parens = t.param<std::string>("parens");
        std::string expr =
            std::string("a ") + op1.op + " b " + op2.op + " c;";
        if (parens == "left") {
            expr = std::string("(a ") + op1.op + " b) " + op2.op + " c;";
        } else if (parens == "right") {
            expr = std::string("a ") + op1.op + " (b " + op2.op + " c);";
        }
        const std::string code =
            std::string("\nvar<private> a : bool;\n") + "var<private> b : bool;\n" +
            "var<private> c : bool;\n" + "fn foo() {\n  let bar = " + expr + "\n}\n";
        const bool valid = parens != "none" || op1.name == std::string(op2.name);
        t.expectCompileResult(valid, code);
    });

// The list of miscellaneous other test cases.
struct Expression {
    const char* name;
    const char* expr;
    bool result;
};

// Mirrors upstream kExpressions (object key order preserved).
static const std::vector<Expression>& kExpressions() {
    static const std::vector<Expression> v = {
        {"neg_member", "- str . a", true},
        {"comp_member", "~ str . a", true},
        {"addr_member", "& str . a", true},
        {"log_and_member", "false && str . b", true},
        {"log_or_member", "false || str . b", true},
        {"and_addr", "      v &  &str .a", false},
        {"and_addr_paren", "v & (&str).a", true},
        {"deref_member", "       * ptr_str  . a", false},
        {"deref_member_paren", "(* ptr_str) . a", true},
        {"deref_idx", "       * ptr_vec  [0]", false},
        {"deref_idx_paren", "(* ptr_vec) [1]", true},
    };
    return v;
}

static std::vector<Value> expressionNames() {
    std::vector<Value> values;
    for (const Expression& e : kExpressions()) {
        values.emplace_back(std::string(e.name));
    }
    return values;
}

static const Expression& findExpression(const std::string& name) {
    for (const Expression& e : kExpressions()) {
        if (name == e.name) {
            return e;
        }
    }
    static const Expression dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "other")
    .desc("Test that other operator precedence rules are correctly implemented.")
    .params([](ParamsBuilder u) { return u.combine("expr", expressionNames()); })
    .fn([](ShaderValidationTest& t) {
        const Expression& e = findExpression(t.param<std::string>("expr"));
        const std::string wgsl =
            std::string("\n      struct S {\n        a: i32,\n        b: bool,\n      }\n\n") +
            "      fn main() {\n        var v = 42;\n        var vec = vec4();\n" +
            "        var str = S(42, false);\n        let ptr_vec = &vec;\n" +
            "        let ptr_str = &str;\n\n        let foo = " + e.expr + ";\n      }\n    ";
        t.expectCompileResult(e.result, wgsl);
    });

// Mirrors upstream kLHSExpressions (object key order preserved).
static const std::vector<Expression>& kLHSExpressions() {
    static const std::vector<Expression> v = {
        {"deref_invalid1", "*p.b", false},
        {"deref_invalid2", "*p.a[0]", false},
        {"deref_valid1", "(*p).b", true},
        {"deref_valid2", "(*p).a[2]", true},
        {"addr_valid1", "*&v.b", true},
        {"addr_valid2", "(*&v).b", true},
        {"addr_valid3", "*&(v.b)", true},
    };
    return v;
}

static std::vector<Value> lhsExpressionNames() {
    std::vector<Value> values;
    for (const Expression& e : kLHSExpressions()) {
        values.emplace_back(std::string(e.name));
    }
    return values;
}

static const Expression& findLHSExpression(const std::string& name) {
    for (const Expression& e : kLHSExpressions()) {
        if (name == e.name) {
            return e;
        }
    }
    static const Expression dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "other_lhs")
    .desc("Test precedence of * and [] in LHS")
    .params([](ParamsBuilder u) { return u.combine("expr", lhsExpressionNames()); })
    .fn([](ShaderValidationTest& t) {
        const Expression& e = findLHSExpression(t.param<std::string>("expr"));
        const std::string code =
            std::string("\n    struct S {\n      a : array<i32, 4>,\n      b : i32,\n    }\n") +
            "    fn main() {\n      var v : S;\n      let p = &v;\n      let q = &v.a;\n\n      " +
            e.expr + " = 1i;\n    }";
        t.expectCompileResult(e.result, code);
    });

}  // namespace
