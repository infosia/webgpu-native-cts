// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/normalize.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,normalize",
    "Validation tests for the normalize() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of normalize() rejects "
          "invalid values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatVectors()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("value", [](const ParamRecord& p) {
                return b::rangeValues(b::fullRangeForType(
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type")))));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        const double v = rvNum(rv);

        bool expectedResult = true;
        const double vv = b::quantizeForKind(k, v * v);
        const double dp = b::quantizeForKind(k, vv * ty.width);
        const double len = b::quantizeForKind(k, std::sqrt(dp));
        if (std::isinf(vv) || std::isinf(dp) || len == 0) {
            expectedResult = false;
        }

        // Skip subnormal computations to avoid the flush-to-zero ambiguity.
        t.skipIf(b::isSubnormalForKind(k, vv) || b::isSubnormalForKind(k, dp) ||
                 b::isSubnormalForKind(k, len));

        b::validateConstOrOverrideBuiltinEval(t, "normalize", expectedResult,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

// kInvalidArgumentTypes = [f32, f16, abstractInt, bool, vec2/3/4 bool,
//                          ...kConcreteIntegerScalarsAndVectors]
static std::vector<bt::Type> kInvalidArgumentTypesTable() {
    std::vector<bt::Type> v = {bt::scalar(bt::ScalarKind::F32), bt::scalar(bt::ScalarKind::F16),
                               bt::scalar(bt::ScalarKind::AbstractInt),
                               bt::scalar(bt::ScalarKind::Bool), bt::vec(2, bt::ScalarKind::Bool),
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
    .desc("Validates that all scalar arguments and vector integer or boolean arguments are "
          "rejected by normalize()")
    .params([](ParamsBuilder u) {
        return u.combine("type", b::typeKeys(kInvalidArgumentTypes()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        b::validateConstOrOverrideBuiltinEval(t, "normalize", /*expectedResult=*/false,
                                              {b::createBuiltinValue(ty, b::RangeValue::makeI(0))},
                                              "constant");
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec3f(1, 0, 0))"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_2args", "(vec3f(),vec3f())"},
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
    .desc("Test compilation failure of normalize  with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good",
                              std::string("const c = normalize") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of normalize must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "normalize" +
                                       argSuffix("good") + "; }");
    });

}  // namespace
