// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/pack4x8unorm.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

constexpr const char* kFn = "pack4x8unorm";
constexpr const char* kReturnType = "u32";
constexpr const char* kGoodArgs = "(vec4f())";

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,pack4x8unorm", "Validate pack4x8unorm");

struct ArgCase {
    const char* name;
    const char* args;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec4f())"},
        {"good_vec4_abstract_float", "(vec4(0.1))"},
        {"bad_0args", "()"},
        {"bad_2args", "(vec4f(),vec4f())"},
        {"bad_abstract_int", "(1)"},
        {"bad_i32", "(1i)"},
        {"bad_f32", "(1f)"},
        {"bad_u32", "(1u)"},
        {"bad_abstract_float", "(0.1)"},
        {"bad_bool", "(false)"},
        {"bad_vec4u", "(vec4u())"},
        {"bad_vec4i", "(vec4i())"},
        {"bad_vec4b", "(vec4<bool>())"},
        {"bad_vec2f", "(vec2f())"},
        {"bad_vec3f", "(vec3f())"},
        {"bad_array", "(array(1.0, 2.0, 3.0, 4.0))"},
        {"bad_struct", "(modf(1.1))"},
    };
    return v;
}
static const ArgCase& argByName(const std::string& name) {
    for (const ArgCase& c : kArgCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ArgCase dummy{"", "()"};
    return dummy;
}
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& c : kArgCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of pack4x8unorm with various numbers of and types of arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        const ArgCase& c = argByName(arg);
        const bool good = arg == "good" || arg == "good_vec4_abstract_float";
        t.expectCompileResult(good, std::string("const c = ") + kFn + c.args + ";");
    });

CTS_TEST(g, "return")
    .desc("Test pack4x8unorm return value type")
    .params([](ParamsBuilder u) {
        return u.combine("type", {Value(std::string("u32")), Value(std::string("i32")),
                                  Value(std::string("f32")), Value(std::string("bool")),
                                  Value(std::string("vec2u"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        t.expectCompileResult(type == kReturnType,
                              "const c: " + type + " = " + kFn + kGoodArgs + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of pack4x8unorm must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, "fn f() { " + useIt + kFn + kGoodArgs + "; }");
    });

}  // namespace
