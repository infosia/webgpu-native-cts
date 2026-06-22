// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/pow.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,pow", "Validation tests for the pow() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}
static double quantizeForKind(bt::ScalarKind k, double v) {
    if (k == bt::ScalarKind::F32) {
        return bt::quantizeToF32(v);
    }
    if (k == bt::ScalarKind::F16) {
        return bt::quantizeToF16(v);
    }
    return v;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of pow() rejects invalid "
          "values")
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
        const b::RangeValue arv = b::rangeValueFromParam(*findParam(t.params(), "a"));
        const b::RangeValue brv = b::rangeValueFromParam(*findParam(t.params(), "b"));
        const double a = rvNum(arv);
        const double bb = rvNum(brv);
        bool expectedResult = true;
        if (a < 0 || (a == 0 && bb <= 0)) {
            expectedResult = false;
        }
        if (expectedResult) {
            const double p = quantizeForKind(bt::scalarTypeOf(ty).kind, std::pow(a, bb));
            if (!std::isfinite(p)) {
                expectedResult = false;
            }
        }
        b::validateConstOrOverrideBuiltinEval(
            t, "pow", expectedResult,
            {b::createBuiltinValue(ty, arv), b::createBuiltinValue(ty, brv)}, stage);
    });

// kInvalidArgumentTypes = bool, vec2/3/4 bool, then kConcreteIntegerScalarsAndVectors.
static std::vector<bt::Type> kInvalidArgumentTypesTable() {
    std::vector<bt::Type> v = {bt::scalar(bt::ScalarKind::Bool), bt::vec(2, bt::ScalarKind::Bool),
                               bt::vec(3, bt::ScalarKind::Bool), bt::vec(4, bt::ScalarKind::Bool)};
    for (const bt::Type& t : b::kConcreteIntegerScalarsAndVectors()) {
        v.push_back(t);
    }
    return v;
}
static const std::vector<bt::Type>& kInvalidArgumentTypes() {
    static const std::vector<bt::Type> v = kInvalidArgumentTypesTable();
    return v;
}

CTS_TEST(g, "invalid_argument")
    .desc("Validates that all integer or boolean scalar and vector arguments are rejected by pow()")
    .params([](ParamsBuilder u) { return u.combine("type", b::typeKeys(kInvalidArgumentTypes())); })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        b::validateConstOrOverrideBuiltinEval(
            t, "pow", /*expectedResult=*/false,
            {b::createBuiltinValue(ty, b::RangeValue::makeI(1)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(2))},
            "constant");
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(2.0, 2.0)"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_1args", "(2.0)"},
        {"bad_3args", "(2.0,2.0,2.0)"},
        {"bad_0bool", "(false, 2.0)"},
        {"bad_0array", "(array(1.1,2.2), 2.0)"},
        {"bad_0struct", "(modf(2.2), 2.0)"},
        {"bad_0uint", "(1u, 2.0)"},
        {"bad_0int", "(1i, 2.0)"},
        {"bad_0vec2i", "(vec2i(), 2.0)"},
        {"bad_0vec2u", "(vec2u(), 2.0)"},
        {"bad_0vec3i", "(vec3i(), 2.0)"},
        {"bad_0vec3u", "(vec3u(), 2.0)"},
        {"bad_0vec4i", "(vec4i(), 2.0)"},
        {"bad_0vec4u", "(vec4u(), 2.0)"},
        {"bad_1bool", "(2.0, false)"},
        {"bad_1array", "(2.0, array(1.1,2.2))"},
        {"bad_1struct", "(2.0, modf(2.2))"},
        {"bad_1uint", "(2.0, 1u)"},
        {"bad_1int", "(2.0, 1i)"},
        {"bad_1vec2i", "(2.0, vec2i())"},
        {"bad_1vec2u", "(2.0, vec2u())"},
        {"bad_1vec3i", "(2.0, vec3i())"},
        {"bad_1vec3u", "(2.0, vec3u())"},
        {"bad_1vec4i", "(2.0, vec4i())"},
        {"bad_1vec4u", "(2.0, vec4u())"},
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
    .desc("Test compilation failure of pow with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good", std::string("const c = pow") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of pow must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "pow" + argSuffix("good") +
                                       "; }");
    });

}  // namespace
