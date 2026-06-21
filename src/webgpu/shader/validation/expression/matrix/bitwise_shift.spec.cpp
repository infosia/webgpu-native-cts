// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/matrix/bitwise_shift.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,matrix,bitwise_shift",
    "Validation tests for matrix bitwise shift expressions.");

// kBitshiftOperators (object key order preserved).
struct Operator {
    const char* name;
    const char* op;
};
static const std::vector<Operator>& kOperators() {
    static const std::vector<Operator> v = {
        {"left", "<<"},
        {"right", ">>"},
    };
    return v;
}

// kTests (object key order preserved). `src` is the WGSL value spelling.
struct Argument {
    const char* name;
    const char* src;
    bool is_f16;
};
static const std::vector<Argument>& kTests() {
    static const std::vector<Argument> v = {
        {"bool", "false", false},
        {"vec", "vec2()", false},
        {"i32", "1i", false},
        {"u32", "1u", false},
        {"ai", "1", false},
        {"f32", "1f", false},
        {"f16", "1h", true},
        {"af", "1.0", false},
        {"texture", "t", false},
        {"sampler", "s", false},
        {"atomic", "a", false},
        {"struct", "str", false},
        {"array", "arr", false},
        {"matf_matching", "mat2x3f()", false},
        {"matf_no_match", "mat4x4f()", false},
        {"math", "mat2x3h()", true},
    };
    return v;
}

static std::vector<Value> operatorNames() {
    std::vector<Value> values;
    for (const Operator& o : kOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}
static const Operator& findOperator(const std::string& name) {
    for (const Operator& o : kOperators()) {
        if (name == o.name) {
            return o;
        }
    }
    static const Operator dummy{"", ""};
    return dummy;
}
static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const Argument& a : kTests()) {
        values.emplace_back(std::string(a.name));
    }
    return values;
}
static const Argument& findTest(const std::string& name) {
    for (const Argument& a : kTests()) {
        if (name == a.name) {
            return a;
        }
    }
    static const Argument dummy{"", "", false};
    return dummy;
}

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

CTS_TEST(g, "invalid")
    .desc("Validates that bitshift expressions are never accepted for matrix types.")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            // 1u is the control that the test passes
            .combine("rhs", {"1u", "ai", "mat2x3f()", "mat2x3h()"})
            .combine("test", testNames())
            .combine("swap", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const Argument& test = findTest(t.param<std::string>("test"));
        const std::string rhsParam = t.param<std::string>("rhs");
        std::string lhs = test.src;
        std::string rhs = rhsParam == "ai" ? std::string("mat2x3(0, 0, 0, 0, 0, 0)") : rhsParam;

        if (t.param<bool>("swap")) {
            std::string a = lhs;
            lhs = rhs;
            rhs = a;
        }

        const std::string enables =
            (test.is_f16 || startsWith(rhsParam, "mat2x3h(")) ? "enable f16;" : "";
        const std::string code =
            "\n" + enables +
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<i32>;"
            "\n"
            "\nstruct S { u : u32 }"
            "\n"
            "\nvar<private> arr : array<u32, 4>;"
            "\nvar<private> str : S;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = " + lhs + " " + findOperator(t.param<std::string>("op")).op + " " + rhs + ";"
            "\n}"
            "\n";

        const bool pass =
            (lhs == "1u" || lhs == "1i" || lhs == "1") && (rhs == "1u" || rhs == "1");
        t.expectCompileResult(pass, code);
    });

}  // namespace
