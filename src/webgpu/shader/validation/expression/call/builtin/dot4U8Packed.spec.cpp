// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/dot4U8Packed.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
constexpr const char* kFn = "dot4U8Packed";

TestGroup<ShaderValidationTest> g =
    MakeTestGroup<ShaderValidationTest>("shader,validation,expression,call,builtin,dot4U8Packed",
                                        "Validate dot4U8Packed");

// kArgCases (object order preserved). `good` == "(1u,2u)".
struct ArgCase {
    const char* name;
    const char* args;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(1u,2u)"},
        {"bad_0args", "()"},
        {"bad_1args", "(1u)"},
        {"bad_3args", "(1u,2u,3u)"},
        {"bad_0i32", "(1i,2u)"},
        {"bad_0f32", "(1f,2u)"},
        {"bad_0bool", "(false,2u)"},
        {"bad_0vec2u", "(vec2u(),2u)"},
        {"bad_1i32", "(1u,2i)"},
        {"bad_1f32", "(1u,2f)"},
        {"bad_1bool", "(1u,true)"},
        {"bad_1vec2u", "(1u,vec2u())"},
        {"bad_bool_bool", "(false,true)"},
        {"bad_bool2_bool2", "(vec2<bool>(),vec2(false,true))"},
        {"bad_0array", "(array(1))"},
        {"bad_0struct", "(modf(1.1))"},
    };
    return v;
}
static const char* kGoodArgs = "(1u,2u)";
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
    .desc("Test absence of dot4U8Packed when packed_4x8_integer_dot_product is not supported.")
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
    .desc("Test presence of dot4U8Packed when packed_4x8_integer_dot_product is supported.")
    .params([](ParamsBuilder u) { return u.combine("requires", {Value(false), Value(true)}); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported(kFeature);
        const bool req = t.param<bool>("requires");
        const std::string preamble = req ? std::string("requires ") + kFeature + "; " : "";
        const std::string code = preamble + "const c = " + kFn + kGoodArgs + ";";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "args")
    .desc("Test compilation failure of dot4U8Packed with various numbers of and types of arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported(kFeature);
        const ArgCase& c = findArg(t.param<std::string>("arg"));
        t.expectCompileResult(std::string(c.name) == "good",
                              std::string("const c = ") + kFn + c.args + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of dot4U8Packed must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported(kFeature);
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + kFn + kGoodArgs + "; }");
    });

}  // namespace
