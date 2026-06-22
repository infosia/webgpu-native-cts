// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/faceForward.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cmath>
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
    "shader,validation,expression,call,builtin,faceForward",
    "Validation tests for the faceForward() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of faceForward() never errors")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatVectors()))
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
            .expand("b",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .expand("c", [](const ParamRecord& p) {
                return b::rangeValues(b::fullRangeForType(
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
        const b::RangeValue arv = b::rangeValueFromParam(*findParam(t.params(), "a"));
        const b::RangeValue brv = b::rangeValueFromParam(*findParam(t.params(), "b"));
        const b::RangeValue crv = b::rangeValueFromParam(*findParam(t.params(), "c"));
        const double bb = rvNum(brv);
        const double c = rvNum(crv);

        bool expectedResult = true;
        const double bc = b::quantizeForKind(k, bb * c);
        const double dp = b::quantizeForKind(k, bc * ty.width);
        if (!std::isfinite(dp)) {
            expectedResult = false;
        }

        b::validateConstOrOverrideBuiltinEval(
            t, "faceForward", expectedResult,
            {b::createBuiltinValue(ty, arv), b::createBuiltinValue(ty, brv),
             b::createBuiltinValue(ty, crv)},
            stage);
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec3(0), vec3(1), vec3(0.5))"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_1arg", "(vec3(0))"},
        {"bad_2arg", "(vec3(0), vec3(1))"},
        {"bad_4arg", "(vec3(0), vec3(1), vec3(0.5), vec3(3))"},
        {"bad_0bool", "(false, vec3(1), vec3(0.5))"},
        {"bad_0array", "(array(1.1,2.2), vec3(1), vec3(0.5))"},
        {"bad_0struct", "(modf(2.2), vec3(1), vec3(0.5))"},
        {"bad_0int", "(1i, vec3(1), vec3(0.5))"},
        {"bad_0uint", "(1u, vec3(1), vec3(0.5))"},
        {"bad_0vec2i", "(vec2i(0), vec2(1), vec2(0.5))"},
        {"bad_0vec3i", "(vec3i(0), vec3(1), vec3(0.5))"},
        {"bad_0vec4i", "(vec4i(0), vec4(1), vec4(0.5))"},
        {"bad_0vec2u", "(vec2u(0), vec2(1), vec2(0.5))"},
        {"bad_0vec3u", "(vec3u(0), vec3(1), vec3(0.5))"},
        {"bad_0vec4u", "(vec4u(0), vec4(1), vec4(0.5))"},
        {"bad_1bool", "(vec3(0), true, vec3(0.5))"},
        {"bad_1array", "(vec3(0), array(1.1,2.2), vec3(0.5))"},
        {"bad_1struct", "(vec3(0), modf(2.2), vec3(0.5))"},
        {"bad_1int", "(vec3(0), 1i, vec3(0.5))"},
        {"bad_1uint", "(vec3(0), 1u, vec3(0.5))"},
        {"bad_1vec2i", "(vec2(1), vec2i(1), vec2(0.5))"},
        {"bad_1vec3i", "(vec3(1), vec3i(1), vec3(0.5))"},
        {"bad_1vec4i", "(vec4(1), vec4i(1), vec4(0.5))"},
        {"bad_1vec2u", "(vec2(1), vec2u(1), vec2(0.5))"},
        {"bad_1vec3u", "(vec3(1), vec3u(1), vec3(0.5))"},
        {"bad_1vec4u", "(vec4(1), vec4u(1), vec4(0.5))"},
        {"bad_2bool", "(vec3(0), vec3(1), true)"},
        {"bad_2array", "(vec3(0), vec3(1), array(1.1,2.2))"},
        {"bad_2struct", "(vec3(0), vec3(1), modf(2.2))"},
        {"bad_2int", "(vec3(0), vec3(1), 1i)"},
        {"bad_2uint", "(vec3(0), vec3(1), 1u)"},
        {"bad_2vec2i", "(vec2(1), vec2(1), vec2i(1))"},
        {"bad_2vec3i", "(vec3(1), vec3(1), vec3i(1))"},
        {"bad_2vec4i", "(vec4(1), vec4(1), vec4i(1))"},
        {"bad_2vec2u", "(vec2(1), vec2(1), vec2u(1))"},
        {"bad_2vec3u", "(vec3(1), vec3(1), vec3u(1))"},
        {"bad_2vec4u", "(vec4(1), vec4(1), vec4u(1))"},
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
    .desc("Test compilation failure of faceForward with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good",
                              std::string("const c = faceForward") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of faceForward must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "faceForward" +
                                       argSuffix("good") + "; }");
    });

}  // namespace
