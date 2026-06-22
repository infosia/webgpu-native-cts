// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/step.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,step", "Validation tests for the step() builtin.");

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of step() error on invalid "
          "inputs.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("a",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .expand("b", [](const ParamRecord& p) {
                return b::rangeValues(b::fullRangeForType(
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue a = b::rangeValueFromParam(*findParam(t.params(), "a"));
        const b::RangeValue bb = b::rangeValueFromParam(*findParam(t.params(), "b"));
        b::validateConstOrOverrideBuiltinEval(
            t, "step", /*expectedResult=*/true,
            {b::createBuiltinValue(ty, a), b::createBuiltinValue(ty, bb)}, stage);
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(1.2, 2.3)"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_1arg", "(1.2)"},
        {"bad_3arg", "(1.2, 2.3, 4.5)"},
        {"bad_0bool", "(false, 2.3)"},
        {"bad_0array", "(array(1.1,2.2), 2.3)"},
        {"bad_0struct", "(modf(2.2), 2.3)"},
        {"bad_0uint", "(1u, 2.3)"},
        {"bad_0int", "(1i, 2.3)"},
        {"bad_0vec2i", "(vec2i(), 2.3)"},
        {"bad_0vec2u", "(vec2u(), 2.3)"},
        {"bad_0vec3i", "(vec3i(), 2.3)"},
        {"bad_0vec3u", "(vec3u(), 2.3)"},
        {"bad_0vec4i", "(vec4i(), 2.3)"},
        {"bad_0vec4u", "(vec4u(), 2.3)"},
        {"bad_1bool", "(1.2, false)"},
        {"bad_1array", "(1.2, array(1.1,2.2))"},
        {"bad_1struct", "(1.2, modf(2.2))"},
        {"bad_1uint", "(1.2, 1u)"},
        {"bad_1int", "(1.2, 1i)"},
        {"bad_1vec2i", "(1.2, vec2i())"},
        {"bad_1vec2u", "(1.2, vec2u())"},
        {"bad_1vec3i", "(1.2, vec3i())"},
        {"bad_1vec3u", "(1.2, vec3u())"},
        {"bad_1vec4i", "(1.2, vec4i())"},
        {"bad_1vec4u", "(1.2, vec4u())"},
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
    .desc("Test compilation failure of step with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good",
                              std::string("const c = step") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of step must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "step" + argSuffix("good") +
                                       "; }");
    });

}  // namespace
