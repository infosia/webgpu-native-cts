// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/floor.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,floor", "Validation tests for the floor() builtin.");

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of floor() never errors")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                const std::string stage = valueAs<std::string>(*findParam(p, "stage"));
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                return b::stageSupportsType(stage, ty);
            })
            .beginSubcases()
            .expand("value", [](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                return b::rangeValues(b::fullRangeForType(ty));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        b::validateConstOrOverrideBuiltinEval(t, "floor", /*expectedResult=*/true,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

static std::vector<bt::Type> kIntegerArgumentTypes() {
    std::vector<bt::Type> v = {bt::scalar(bt::ScalarKind::F32)};
    for (const bt::Type& ty : b::kConcreteIntegerScalarsAndVectors()) {
        v.push_back(ty);
    }
    return v;
}

CTS_TEST(g, "integer_argument")
    .desc("Validates that scalar and vector integer arguments are rejected by floor()")
    .params([](ParamsBuilder u) { return u.combine("type", b::typeKeys(kIntegerArgumentTypes())); })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = ty.kind == bt::ScalarKind::F32 && ty.isScalar();
        b::validateConstOrOverrideBuiltinEval(t, "floor", expectedResult,
                                              {b::createBuiltinValue(ty, b::RangeValue::makeI(0))},
                                              "constant");
    });

struct ArgCase {
    const char* name;
    const char* args;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(1.1)"},          {"bad_no_parens", ""},   {"bad_0args", "()"},
        {"bad_2args", "(1.0,2.0)"}, {"bad_0i32", "(1i)"},    {"bad_0u32", "(1u)"},
        {"bad_0bool", "(false)"},   {"bad_0vec2u", "(vec2u())"}, {"bad_0array", "(array(1.1,2.2))"},
        {"bad_0struct", "(modf(2.2))"},
    };
    return v;
}
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& c : kArgCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
static const char* findArgs(const std::string& name) {
    for (const ArgCase& c : kArgCases()) {
        if (name == c.name) {
            return c.args;
        }
    }
    return "";
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of floor with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        const std::string code = std::string("const c = floor") + findArgs(arg) + ";";
        t.expectCompileResult(arg == "good", code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of floor must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, "fn f() { " + useIt + "floor(1.1); }");
    });

}  // namespace
