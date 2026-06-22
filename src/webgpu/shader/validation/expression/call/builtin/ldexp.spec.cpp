// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/ldexp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// ldexp(e1: float, e2: i32/abstract-int). The second arg's element type tracks
// the first arg's width/abstractness (supportedSecondArgTypes). `values` sweeps
// the first-arg full range and a bias range for the exponent; the result errors
// if e2 > bias + 1 or the scaled value overflows the float type.

#include <cmath>
#include <map>
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
    "shader,validation,expression,call,builtin,ldexp", "Validation tests for the ldexp() builtin.");

// kValidArgumentTypesA = kConvertableToFloatScalarsAndVectors.
static const std::vector<bt::Type>& kValidArgumentTypesA() {
    return b::kConvertableToFloatScalarsAndVectors();
}

// kValidArgumentTypesB = [abstractInt, vec2/3/4 abstractInt,
//                         ...kConcreteSignedIntegerScalarsAndVectors]
static bt::Type typeBByName(const std::string& name) {
    return bt::typeByName(name);
}

// supportedSecondArgTypes(typeA): width 1 => [abstractInt, i32];
// otherwise [vecN abstractInt, vecN i32].
static std::vector<bt::Type> supportedSecondArgTypes(const bt::Type& typeA) {
    const int width = typeA.isScalar() ? 1 : typeA.width;
    if (width == 1) {
        return {bt::scalar(bt::ScalarKind::AbstractInt), bt::scalar(bt::ScalarKind::I32)};
    }
    return {bt::vec(width, bt::ScalarKind::AbstractInt), bt::vec(width, bt::ScalarKind::I32)};
}

static int biasForScalarKind(bt::ScalarKind k) {
    switch (k) {
        case bt::ScalarKind::F16:
            return 15;
        case bt::ScalarKind::F32:
            return 127;
        case bt::ScalarKind::AbstractFloat:
        case bt::ScalarKind::AbstractInt:
            return 1023;
        default:
            return 0;
    }
}

static std::vector<int64_t> biasRange(const bt::Type& type) {
    const int bias = biasForScalarKind(bt::scalarTypeOf(type).kind);
    return {-bias - 2,
            -bias,
            static_cast<int64_t>(std::floor(-bias * 0.5)),
            0,
            static_cast<int64_t>(std::floor(bias * 0.5)),
            bias,
            bias + 2};
}

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of ldexp() never errors")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("typeA", b::typeKeys(kValidArgumentTypesA()))
            .expand("typeB",
                    [](const ParamRecord& p) {
                        const bt::Type typeA =
                            bt::typeByName(valueAs<std::string>(*findParam(p, "typeA")));
                        std::vector<Value> out;
                        for (const bt::Type& tb : supportedSecondArgTypes(typeA)) {
                            out.emplace_back(tb.toString());
                        }
                        return out;
                    })
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "typeA"))));
            })
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           typeBByName(valueAs<std::string>(*findParam(p, "typeB"))));
            })
            .beginSubcases()
            .expand("a",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "typeA"))), 5));
                    })
            .expand("b", [](const ParamRecord& p) {
                const bt::Type typeA = bt::typeByName(valueAs<std::string>(*findParam(p, "typeA")));
                std::vector<Value> out;
                for (int64_t v : biasRange(typeA)) {
                    out.emplace_back(v);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type typeA = bt::typeByName(t.param<std::string>("typeA"));
        const bt::Type typeB = typeBByName(t.param<std::string>("typeB"));
        const bt::Type scalarTypeA = bt::scalarTypeOf(typeA);
        const int bias = biasForScalarKind(scalarTypeA.kind);
        b::ConstantOrOverrideValueChecker vCheck(t, scalarTypeA);

        const b::RangeValue arv = b::rangeValueFromParam(*findParam(t.params(), "a"));
        const int64_t bExp = valueAs<int64_t>(*findParam(t.params(), "b"));
        const bool validExponent = bExp <= bias + 1;

        const double a = rvNum(arv);
        vCheck.checkedResult(a * std::pow(2.0, static_cast<double>(bExp)));

        b::validateConstOrOverrideBuiltinEval(
            t, "ldexp", validExponent && vCheck.allChecksPassed(),
            {b::createBuiltinValue(typeA, arv), b::createBuiltinValue(typeB, b::RangeValue::makeI(bExp))},
            stage);
    });

CTS_TEST(g, "partial_values")
    .desc("Validates e2 <= bias + 1 when e1 is a runtime value")
    .params([](ParamsBuilder u) {
        const std::vector<Value> stages = {Value(std::string("constant")),
                                           Value(std::string("override")),
                                           Value(std::string("runtime"))};
        return u.combine("stage", stages)
            .combine("typeA", b::typeKeys(kValidArgumentTypesA()))
            .filter([](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "typeA")));
                const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
                return k != bt::ScalarKind::AbstractInt && k != bt::ScalarKind::AbstractFloat;
            })
            .expand("typeB",
                    [](const ParamRecord& p) {
                        const bt::Type typeA =
                            bt::typeByName(valueAs<std::string>(*findParam(p, "typeA")));
                        std::vector<Value> out;
                        for (const bt::Type& tb : supportedSecondArgTypes(typeA)) {
                            out.emplace_back(tb.toString());
                        }
                        return out;
                    })
            .filter([](const ParamRecord& p) {
                const bt::Type ty = typeBByName(valueAs<std::string>(*findParam(p, "typeB")));
                const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
                return k != bt::ScalarKind::AbstractInt && k != bt::ScalarKind::AbstractFloat;
            })
            .beginSubcases()
            .expand("value",
                    [](const ParamRecord& p) {
                        const bt::Type typeA =
                            bt::typeByName(valueAs<std::string>(*findParam(p, "typeA")));
                        const int bias = biasForScalarKind(bt::scalarTypeOf(typeA).kind);
                        return std::vector<Value>{Value(int64_t(bias)), Value(int64_t(bias + 1)),
                                                  Value(int64_t(bias + 2))};
                    })
            .combine("in_shader", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type tyA = bt::typeByName(t.param<std::string>("typeA"));
        const bt::Type tyB = typeBByName(t.param<std::string>("typeB"));
        const bt::Type scalarB = bt::scalarTypeOf(tyB);
        const int64_t value = t.param<int64_t>("value");
        const bool inShader = t.param<bool>("in_shader");
        const std::string enable = (tyA.kind == bt::ScalarKind::F16) ? "enable f16;" : "";

        std::string bArg;
        if (stage == "constant") {
            bArg = b::builtinValueWgsl(b::createBuiltinValue(tyB, b::RangeValue::makeI(value)));
        } else if (stage == "override") {
            bArg = tyB.toString() + "(o_b)";
        } else {
            bArg = "v_b";
        }
        const std::string wgsl = "\n" + enable + "\noverride o_b : " + scalarB.toString() +
                                 ";\nfn foo() {\n  var v_b : " + tyB.toString() + ";\n  var v : " +
                                 tyA.toString() + ";\n  let tmp = ldexp(v, " + bArg + ");\n}";

        const int bias = biasForScalarKind(bt::scalarTypeOf(tyA).kind);
        const bool error = value > bias + 1;
        const bool shaderError = error && stage == "constant";
        const bool pipelineError = inShader && error && stage == "override";
        t.expectCompileResult(!shaderError, wgsl);
        if (!shaderError) {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = !pipelineError;
            args.code = wgsl;
            args.constants = {{"o_b", static_cast<double>(value)}};
            args.reference = {"o_b"};
            if (inShader) {
                args.statements = {"foo();"};
            }
            t.expectPipelineResult(args);
        }
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec3(0), vec3(1))"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_1arg", "(vec3(0))"},
        {"bad_3arg", "(vec3(0), vec3(1), vec3(2))"},
        {"bad_vec_scalar", "(vec3(0), 1)"},
        {"bad_scalar_vec", "(0, vec3(1))"},
        {"bad_vec_sizes", "(vec3(0), vec2(1))"},
        {"bad_0bool", "(false, vec3(1))"},
        {"bad_0array", "(array(1.1,2.2), vec3(1))"},
        {"bad_0struct", "(modf(2.2), vec3(1))"},
        {"bad_0int", "(0i, 1i)"},
        {"bad_0uint", "(0u, 1i)"},
        {"bad_0vec2i", "(vec2i(0), vec2i(1))"},
        {"bad_0vec3i", "(vec3i(0), vec3i(1))"},
        {"bad_0vec4i", "(vec4i(0), vec4i(1))"},
        {"bad_0vec2u", "(vec2u(0), vec2i(1))"},
        {"bad_0vec3u", "(vec3u(0), vec3i(1))"},
        {"bad_0vec4u", "(vec4u(0), vec4i(1))"},
        {"bad_1bool", "(vec3(0), true)"},
        {"bad_1array", "(vec3(0), array(1.1,2.2))"},
        {"bad_1struct", "(vec3(0), modf(2.2))"},
        {"bad_1f32", "(0f, 1f)"},
        {"bad_1f16", "(0f, 1h)"},
        {"bad_1uint", "(0f, 1u)"},
        {"bad_1vec2f", "(vec2f(0), vec2f(1))"},
        {"bad_1vec3f", "(vec3f(0), vec3f(1))"},
        {"bad_1vec4f", "(vec4f(0), vec4f(1))"},
        {"bad_1vec2h", "(vec2f(0), vec2h(1))"},
        {"bad_1vec3h", "(vec3f(0), vec3h(1))"},
        {"bad_1vec4h", "(vec4f(0), vec4h(1))"},
        {"bad_1vec2u", "(vec2f(0), vec2u(1))"},
        {"bad_1vec3u", "(vec3f(0), vec3u(1))"},
        {"bad_1vec4u", "(vec4f(0), vec4u(1))"},
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
    .desc("Test compilation failure of ldexp with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good", std::string("const c = ldexp") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of ldexp must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "ldexp" + argSuffix("good") +
                                       "; }");
    });

}  // namespace
