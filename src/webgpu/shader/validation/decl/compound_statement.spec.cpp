// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/compound_statement.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,decl,compound_statement",
    "Validation tests for declarations in compound statements.");

struct Case {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kConflictTests (object key order preserved).
static const std::vector<Case>& kConflictTests() {
    static const std::vector<Case> cases = {
        {"a", "let x = 1; { let x = 1; }", true},
        {"bc", "{let x = 1; let x = 1; }", false},
        {"d", "{let x = 1; { let x = 1; }}", true},
        {"e", "{let x = 1; } let x = 1;", true},
    };
    return cases;
}

// Mirrors upstream kUseTests (object key order preserved).
static const std::vector<Case>& kUseTests() {
    static const std::vector<Case> cases = {
        {"a", "let y = x; { let x = 1; }", false},
        {"b", "{ let y = x; let x = 1; }", false},
        {"self", "{ let x = (x);}", false},
        {"c_yes", "{ const x = 1; const_assert x == 1; }", true},
        {"c_no", "{ const x = 1; const_assert x == 2; }", false},
        {"d_yes", "{ const x = 1; { const_assert x == 1; }}", true},
        {"d_no", "{ const x = 1; { const_assert x == 2; }}", false},
        {"e", "{ const x = 1; } let y = x;", false},
    };
    return cases;
}

static std::vector<Value> caseNames(const std::vector<Case>& cases) {
    std::vector<Value> values;
    for (const Case& c : cases) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const Case& findCase(const std::vector<Case>& cases, const std::string& name) {
    for (const Case& c : cases) {
        if (name == c.name) {
            return c;
        }
    }
    static const Case dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "decl_conflict")
    .desc("Test a potentially conflicting declaration relative to a declaration in a compound "
          "statement")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kConflictTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const Case& c = findCase(kConflictTests(), t.param<std::string>("case"));
        const std::string wgsl =
            std::string("\n@vertex fn vtx() -> @builtin(position) vec4f {\n  ") + c.src +
            "\n  return vec4f(1);\n}";
        t.expectCompileResult(c.pass, wgsl);
    });

CTS_TEST(g, "decl_use")
    .desc("Test a use of a declaration in a compound statement")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kUseTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const Case& c = findCase(kUseTests(), t.param<std::string>("case"));
        const std::string wgsl =
            std::string("\n@vertex fn vtx() -> @builtin(position) vec4f {\n  ") + c.src +
            "\n  return vec4f(1);\n}";
        t.expectCompileResult(c.pass, wgsl);
    });

} // namespace
