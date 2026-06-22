// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/pack2x16float.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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

constexpr const char* kFn = "pack2x16float";
constexpr const char* kReturnType = "u32";
constexpr const char* kGoodArgs = "(vec2f())";

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,pack2x16float", "Validate pack2x16float");

struct ArgCase {
    const char* name;
    const char* args;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec2f())"},
        {"good_vec2_abstract_float", "(vec2(0.1))"},
        {"bad_0args", "()"},
        {"bad_2args", "(vec2f(),vec2f())"},
        {"bad_abstract_int", "(1)"},
        {"bad_i32", "(1i)"},
        {"bad_f32", "(1f)"},
        {"bad_u32", "(1u)"},
        {"bad_abstract_float", "(0.1)"},
        {"bad_bool", "(false)"},
        {"bad_vec4f", "(vec4f())"},
        {"bad_vec4u", "(vec4u())"},
        {"bad_vec4i", "(vec4i())"},
        {"bad_vec4b", "(vec4<bool>())"},
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
    .desc("Test compilation failure of pack2x16float with various numbers of and types of arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        const ArgCase& c = argByName(arg);
        const bool good = arg == "good" || arg == "good_vec2_abstract_float";
        t.expectCompileResult(good, std::string("const c = ") + kFn + c.args + ";");
    });

CTS_TEST(g, "return")
    .desc("Test pack2x16float return value type")
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
    .desc("Result of pack2x16float must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, "fn f() { " + useIt + kFn + kGoodArgs + "; }");
    });

// kValue.f16.positive.max / negative.min boundary sweep values.
static std::vector<Value> valueRangeValues() {
    const double posMax = b::kValueF16().posMax;
    const double negMin = b::kValueF16().negMin;
    return {Value(posMax), Value(posMax + 1), Value(negMin), Value(negMin - 1)};
}

CTS_TEST(g, "value_range")
    .desc("Test failures of pack2x16float when at least one of the input value is out of the range "
          "of binary16")
    .params([](ParamsBuilder u) {
        return u.combine("constantOrOverrideStage", b::kConstantAndOverrideStages())
            .combine("value0", valueRangeValues())
            .combine("value1", valueRangeValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("constantOrOverrideStage");
        const double value0 = valueAs<double>(*findParam(t.params(), "value0"));
        const double value1 = valueAs<double>(*findParam(t.params(), "value1"));
        const double posMax = b::kValueF16().posMax;
        const double negMin = b::kValueF16().negMin;
        const bool success = value0 >= negMin && value0 <= posMax && value1 >= negMin &&
                             value1 <= posMax;
        // [vec2(f32(value0), f32(value1))]
        const bt::Type vec2f = bt::vec(2, bt::ScalarKind::F32);
        const b::BuiltinValue arg = b::createBuiltinValueVec(
            vec2f, {b::RangeValue::makeD(value0), b::RangeValue::makeD(value1)});
        b::validateConstOrOverrideBuiltinEval(t, kFn, success, {arg}, stage);
    });

}  // namespace
