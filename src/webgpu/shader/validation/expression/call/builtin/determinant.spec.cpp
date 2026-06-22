// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/determinant.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/call/builtin/const_override_builtin.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,determinant",
    "Validation tests for the determinant() builtin.");

// Suffix per element type: abstract-int '', abstract-float '.0', f32 'f', f16 'h'.
static std::string suffixForType(const std::string& type) {
    if (type == "abstract-int") {
        return "";
    }
    if (type == "abstract-float") {
        return ".0";
    }
    if (type == "f32") {
        return "f";
    }
    return "h";  // f16
}

CTS_TEST(g, "matrix_args")
    .desc("Test compilation failure of determinant with variously shaped matrices")
    .params([](ParamsBuilder u) {
        const std::vector<Value> rc = {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))};
        const std::vector<Value> types = {
            Value(std::string("abstract-int")), Value(std::string("abstract-float")),
            Value(std::string("f32")), Value(std::string("f16"))};
        return u.combine("cols", rc).combine("rows", rc).combine("type", types);
    })
    .fn([](ShaderValidationTest& t) {
        const int64_t cols = t.param<int64_t>("cols");
        const int64_t rows = t.param<int64_t>("rows");
        const std::string type = t.param<std::string>("type");
        const std::string suffix = suffixForType(type);
        std::string els;
        for (int64_t e = 0; e < cols * rows; ++e) {
            if (e) {
                els += ", ";
            }
            els += std::to_string(e) + suffix;
        }
        const std::string arg = "(mat" + std::to_string(cols) + "x" + std::to_string(rows) + "(" +
                                els + "))";
        const std::string body = "const c = determinant" + arg + ";";
        const std::vector<std::string> enables =
            type == "f16" ? std::vector<std::string>{"f16"} : std::vector<std::string>{};
        t.expectCompileResult(cols == rows, t.wrapInEntryPoint(body, enables));
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(mat2x2(0.0, 2.0, 3.0, 4.0))"},
        {"bad_no_parens", ""},
        {"bad_too_few", "()"},
        {"bad_too_many", "(mat2x2(0.0, 2.0, 3.0, 4.0), mat2x2(0.0, 2.0, 3.0, 4.0))"},
        {"bad_0i32", "(1i)"},
        {"bad_0u32", "(1u)"},
        {"bad_0bool", "(false)"},
        {"bad_0vec2u", "(vec2u())"},
        {"bad_0array", "(array(1.1,2.2))"},
        {"bad_0struct", "(modf(2.2))"},
    };
    return v;
}
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& a : kArgCases()) {
        out.emplace_back(std::string(a.name));
    }
    return out;
}
static const char* argSuffix(const std::string& name) {
    for (const ArgCase& a : kArgCases()) {
        if (name == a.name) {
            return a.suffix;
        }
    }
    return "";
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of determinant with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good",
                              std::string("const c = determinant") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of determinant must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "determinant" +
                                       argSuffix("good") + "; }");
    });

}  // namespace
