// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/compound.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,compound",
    "Validation tests for compound statements");

struct Test {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"missing_start", "}", false},
        {"missing_end", "{", false},
        {"empty", "{}", true},
        {"semicolon", "{;}", true},
        {"semicolons", "{;;}", true},
        {"decl", "{const c = 1;}", true},
        {"nested", "{ {} }", true},
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
    .desc("Test that compound statments parse")
    .params([](ParamsBuilder u) { return u.combine("stmt", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\n@vertex\nfn vtx() -> @builtin(position) vec4f {\n  " + std::string(test.src) +
            "\n  return vec4f(1);\n}\n    ";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
