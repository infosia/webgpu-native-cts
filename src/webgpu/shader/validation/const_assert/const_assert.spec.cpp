// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/const_assert/const_assert.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,const_assert,const_assert",
    "Validation tests for const_assert");

// Builds a const_assert() statement. scope == "module" -> module-scope statement,
// otherwise wrap it in a function. Mirrors upstream buildStaticAssert.
static std::string buildStaticAssert(const std::string& expr, const std::string& scope) {
    const std::string stmt = "const_assert (" + expr + ");";
    return scope == "module" ? stmt : ("fn f() { " + stmt + " }");
}

struct Condition {
    const char* name;
    const char* expr;
    bool val;
};

// Mirrors upstream kConditionCases (object key order preserved).
static const std::vector<Condition>& kConditionCases() {
    static const std::vector<Condition> cases = {
        {"any_false", "any(vec3(false, false, false))", false},
        {"any_true", "any(vec3(false, true, false))", true},
        {"binary_op_eq_const_false", "one + 5 == two", false},
        {"binary_op_eq_const_true", "one + 1 == two", true},
        {"const_eq_literal_float_false", "one == 0.0", false},
        {"const_eq_literal_float_true", "one == 1.0", true},
        {"const_eq_literal_int_false", "one == 10", false},
        {"const_eq_literal_int_true", "one == 1", true},
        {"literal_false", "false", false},
        {"literal_not_false", "!false", true},
        {"literal_not_true", "!true", false},
        {"literal_true", "true", true},
        {"min_max_false", "min(three, max(two, one)) == 3", false},
        {"min_max_true", "min(three, max(two, one)) == 2", true},
        {"variable_false", "is_false", false},
        {"variable_not_false", "!is_false", true},
        {"variable_not_true", "!is_true", false},
        {"variable_true", "is_true", true},
    };
    return cases;
}

static const Condition& findCondition(const std::string& name) {
    for (const Condition& c : kConditionCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const Condition dummy{"", "", false};
    return dummy;
}

static std::vector<Value> conditionNames() {
    std::vector<Value> values;
    for (const Condition& c : kConditionCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const char* kConditionConstants =
    "\nconst one = 1;\nconst two = 2;\nconst three = 3;\nconst is_true = true;\nconst is_false = "
    "false;\n";

CTS_TEST(g, "constant_expression_no_assert")
    .desc("Test that const_assert does not assert on a true conditional expression")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .beginSubcases()
            .combine("case", conditionNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Condition& c = findCondition(t.param<std::string>("case"));
        const std::string scope = t.param<std::string>("scope");
        const std::string expr = c.val ? std::string(c.expr) : ("!(" + std::string(c.expr) + ")");
        t.expectCompileResult(true, kConditionConstants + buildStaticAssert(expr, scope));
    });

CTS_TEST(g, "constant_expression_assert")
    .desc("Test that const_assert does assert on a false conditional expression")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .beginSubcases()
            .combine("case", conditionNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Condition& c = findCondition(t.param<std::string>("case"));
        const std::string scope = t.param<std::string>("scope");
        const std::string expr = c.val ? ("!(" + std::string(c.expr) + ")") : std::string(c.expr);
        t.expectCompileResult(false, kConditionConstants + buildStaticAssert(expr, scope));
    });

CTS_TEST(g, "constant_expression_logical_or_no_assert")
    .desc("Test that const_assert does not assert on a condition expression that contains a "
          "logical-or which evaluates to true")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .beginSubcases()
            .combine("lhs", conditionNames())
            .combine("rhs", conditionNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Condition& lhs = findCondition(t.param<std::string>("lhs"));
        const Condition& rhs = findCondition(t.param<std::string>("rhs"));
        const std::string scope = t.param<std::string>("scope");
        const std::string expr =
            "(" + std::string(lhs.expr) + ") || (" + std::string(rhs.expr) + ")";
        const bool val = lhs.val || rhs.val;
        const std::string assertExpr = val ? expr : ("!(" + expr + ")");
        t.expectCompileResult(true, kConditionConstants + buildStaticAssert(assertExpr, scope));
    });

CTS_TEST(g, "constant_expression_logical_or_assert")
    .desc("Test that const_assert does assert on a condition expression that contains a "
          "logical-or which evaluates to false")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .beginSubcases()
            .combine("lhs", conditionNames())
            .combine("rhs", conditionNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Condition& lhs = findCondition(t.param<std::string>("lhs"));
        const Condition& rhs = findCondition(t.param<std::string>("rhs"));
        const std::string scope = t.param<std::string>("scope");
        const std::string expr =
            "(" + std::string(lhs.expr) + ") || (" + std::string(rhs.expr) + ")";
        const bool val = lhs.val || rhs.val;
        const std::string assertExpr = val ? ("!(" + expr + ")") : expr;
        t.expectCompileResult(false, kConditionConstants + buildStaticAssert(assertExpr, scope));
    });

CTS_TEST(g, "constant_expression_logical_and_no_assert")
    .desc("Test that const_assert does not assert on a condition expression that contains a "
          "logical-and which evaluates to true")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .beginSubcases()
            .combine("lhs", conditionNames())
            .combine("rhs", conditionNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Condition& lhs = findCondition(t.param<std::string>("lhs"));
        const Condition& rhs = findCondition(t.param<std::string>("rhs"));
        const std::string scope = t.param<std::string>("scope");
        const std::string expr =
            "(" + std::string(lhs.expr) + ") && (" + std::string(rhs.expr) + ")";
        const bool val = lhs.val && rhs.val;
        const std::string assertExpr = val ? expr : ("!(" + expr + ")");
        t.expectCompileResult(true, kConditionConstants + buildStaticAssert(assertExpr, scope));
    });

CTS_TEST(g, "constant_expression_logical_and_assert")
    .desc("Test that const_assert does assert on a condition expression that contains a "
          "logical-and which evaluates to false")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .beginSubcases()
            .combine("lhs", conditionNames())
            .combine("rhs", conditionNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Condition& lhs = findCondition(t.param<std::string>("lhs"));
        const Condition& rhs = findCondition(t.param<std::string>("rhs"));
        const std::string scope = t.param<std::string>("scope");
        const std::string expr =
            "(" + std::string(lhs.expr) + ") && (" + std::string(rhs.expr) + ")";
        const bool val = lhs.val && rhs.val;
        const std::string assertExpr = val ? ("!(" + expr + ")") : expr;
        t.expectCompileResult(false, kConditionConstants + buildStaticAssert(assertExpr, scope));
    });

CTS_TEST(g, "evaluation_stage")
    .desc("Test that the const_assert expression must be a constant expression.")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {"module", "function"})
            .combine("stage", {"constant", "override", "runtime"})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string scope = t.param<std::string>("scope");
        const std::string stage = t.param<std::string>("stage");
        const std::string staticAssert = buildStaticAssert("value", scope);
        if (stage == "constant") {
            t.expectCompileResult(true, "const value = true;\n" + staticAssert);
        } else if (stage == "override") {
            t.expectCompileResult(false, "override value = true;\n" + staticAssert);
        } else {  // runtime
            t.expectCompileResult(false, "var<private> value = true;\n" + staticAssert);
        }
    });

}  // namespace
