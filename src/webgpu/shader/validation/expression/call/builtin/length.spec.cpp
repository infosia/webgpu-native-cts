// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/length.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// length() validation. The vec2/3/4 tests sweep per-element values and keep only
// subcases where the squared-length intermediate representability matches the
// result representability (matching upstream's _result expand+filter), then
// expect a pass iff the result is representable.

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
    "shader,validation,expression,call,builtin,length",
    "Validation tests for the length() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

// AbstractInt is converted to AbstractFloat before calling into the builtin.
static bt::ScalarKind builtinFloatKind(const bt::Type& ty) {
    const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
    return k == bt::ScalarKind::AbstractInt ? bt::ScalarKind::AbstractFloat : k;
}

struct CalcResult {
    bool isIntermediateRepresentable;
    bool isResultRepresentable;
};
static CalcResult calculate(const std::vector<double>& vec, const bt::Type& ty) {
    double squareSum = 0.0;
    for (double e : vec) {
        squareSum += e * e;
    }
    const double result = std::sqrt(squareSum);
    const bt::ScalarKind k = builtinFloatKind(ty);
    return CalcResult{b::isRepresentable(squareSum, k), b::isRepresentable(result, k)};
}

CTS_TEST(g, "scalar")
    .desc("Validates that constant evaluation and override evaluation of length() with the input "
          "scalar value always compiles without error")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatScalar()))
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
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        b::validateConstOrOverrideBuiltinEval(t, "length", /*expectedResult=*/true,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

// Shared vecN test body: width 2/3/4, range count 5/4/3, element keys x,y[,z][,w].
static void runVecTest(ShaderValidationTest& t, const std::vector<const char*>& keys) {
    const std::string stage = t.param<std::string>("stage");
    const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
    std::vector<b::RangeValue> elems;
    std::vector<double> nums;
    for (const char* key : keys) {
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), key));
        elems.push_back(rv);
        nums.push_back(rvNum(rv));
    }
    const CalcResult r = calculate(nums, ty);
    b::validateConstOrOverrideBuiltinEval(t, "length", /*expectedResult=*/r.isResultRepresentable,
                                          {b::createBuiltinValueVec(ty, elems)}, stage);
}

static bool vecFilter(const ParamRecord& p, const std::vector<const char*>& keys) {
    const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
    std::vector<double> nums;
    for (const char* key : keys) {
        nums.push_back(rvNum(b::rangeValueFromParam(*findParam(p, key))));
    }
    const CalcResult r = calculate(nums, ty);
    return r.isResultRepresentable == r.isIntermediateRepresentable;
}

CTS_TEST(g, "vec2")
    .desc("Validates that constant evaluation and override evaluation of length() with a vec2 "
          "compiles with valid values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatVec2()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("x",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .expand("y",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .filter([](const ParamRecord& p) { return vecFilter(p, {"x", "y"}); });
    })
    .fn([](ShaderValidationTest& t) { runVecTest(t, {"x", "y"}); });

CTS_TEST(g, "vec3")
    .desc("Validates that constant evaluation and override evaluation of length() with a vec3 "
          "compiles with valid values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatVec3()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("x",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 4));
                    })
            .expand("y",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 4));
                    })
            .expand("z",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 4));
                    })
            .filter([](const ParamRecord& p) { return vecFilter(p, {"x", "y", "z"}); });
    })
    .fn([](ShaderValidationTest& t) { runVecTest(t, {"x", "y", "z"}); });

CTS_TEST(g, "vec4")
    .desc("Validates that constant evaluation and override evaluation of length() with a vec4 "
          "compiles with valid values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatVec4()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("x",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 3));
                    })
            .expand("y",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 3));
                    })
            .expand("z",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 3));
                    })
            .expand("w",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 3));
                    })
            .filter([](const ParamRecord& p) { return vecFilter(p, {"x", "y", "z", "w"}); });
    })
    .fn([](ShaderValidationTest& t) { runVecTest(t, {"x", "y", "z", "w"}); });

// kIntegerArgumentTypes = [f32, ...kConcreteIntegerScalarsAndVectors]
static std::vector<bt::Type> kIntegerArgumentTypesTable() {
    std::vector<bt::Type> v = {bt::scalar(bt::ScalarKind::F32)};
    for (const bt::Type& t : b::kConcreteIntegerScalarsAndVectors()) {
        v.push_back(t);
    }
    return v;
}
static const std::vector<bt::Type>& kIntegerArgumentTypes() {
    static const std::vector<bt::Type> v = kIntegerArgumentTypesTable();
    return v;
}

CTS_TEST(g, "integer_argument")
    .desc("Validates that scalar and vector integer arguments are rejected by length()")
    .params([](ParamsBuilder u) {
        return u.combine("type", b::typeKeys(kIntegerArgumentTypes()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        b::validateConstOrOverrideBuiltinEval(
            t, "length", /*expectedResult=*/ty == bt::scalar(bt::ScalarKind::F32),
            {b::createBuiltinValue(ty, b::RangeValue::makeI(1))}, "constant");
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(1.1)"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_2args", "(1.0,2.0)"},
        {"bad_0i32", "(1i)"},
        {"bad_0u32", "(1u)"},
        {"bad_0bool", "(false)"},
        {"bad_0vec2u", "(vec2u())"},
        {"bad_0mat", "(mat2x2f())"},
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
    .desc("Test compilation failure of length with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good", std::string("const c = length") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of length must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use,
                              std::string("fn f() { ") + useIt + "length" + argSuffix("good") + "; }");
    });

}  // namespace
