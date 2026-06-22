// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/pack4xU8Clamp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

constexpr const char* kFeature = "packed_4x8_integer_dot_product";
constexpr const char* kFn = "pack4xU8Clamp";

TestGroup<ShaderValidationTest> g =
    MakeTestGroup<ShaderValidationTest>("shader,validation,expression,call,builtin,pack4xU8Clamp",
                                        "Validate pack4xU8Clamp");

// kArgCases (object order preserved). `good` == "(vec4u())".
struct ArgCase {
    const char* name;
    const char* args;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec4u())"},
        {"bad_0args", "()"},
        {"bad_2args", "(vec4u(),vec4u())"},
        {"bad_0i32", "(1i)"},
        {"bad_0f32", "(1f)"},
        {"bad_0bool", "(false)"},
        {"bad_0vec4i", "(vec4i())"},
        {"bad_0vec4f", "(vec4f())"},
        {"bad_0vec4b", "(vec4<bool>())"},
        {"bad_0vec2u", "(vec2u())"},
        {"bad_0vec3u", "(vec3u())"},
        {"bad_0array", "(array(1))"},
        {"bad_0struct", "(modf(1.1))"},
    };
    return v;
}
static const char* kGoodArgs = "(vec4u())";
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& c : kArgCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
static const ArgCase& findArg(const std::string& name) {
    for (const ArgCase& c : kArgCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ArgCase dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "unsupported")
    .desc("Test absence of pack4xU8Clamp when packed_4x8_integer_dot_product is not supported.")
    .params([](ParamsBuilder u) { return u.combine("requires", {Value(false), Value(true)}); })
    .fn([](ShaderValidationTest& t) {
        // Upstream: skipIfLanguageFeatureSupported (skip when the feature IS supported).
        t.skipIf(t.hasLanguageFeature(kFeature),
                 std::string("WGSL language feature supported: ") + kFeature);
        const bool req = t.param<bool>("requires");
        const std::string preamble = req ? std::string("requires ") + kFeature + "; " : "";
        const std::string code = preamble + "const c = " + kFn + kGoodArgs + ";";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "supported")
    .desc("Test presence of pack4xU8Clamp when packed_4x8_integer_dot_product is supported.")
    .params([](ParamsBuilder u) { return u.combine("requires", {Value(false), Value(true)}); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported(kFeature);
        const bool req = t.param<bool>("requires");
        const std::string preamble = req ? std::string("requires ") + kFeature + "; " : "";
        const std::string code = preamble + "const c = " + kFn + kGoodArgs + ";";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "args")
    .desc("Test compilation failure of pack4xU8Clamp with various numbers of and types of arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported(kFeature);
        const ArgCase& c = findArg(t.param<std::string>("arg"));
        t.expectCompileResult(std::string(c.name) == "good",
                              std::string("const c = ") + kFn + c.args + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of pack4xU8Clamp must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + kFn + kGoodArgs + "; }");
    });

}  // namespace
