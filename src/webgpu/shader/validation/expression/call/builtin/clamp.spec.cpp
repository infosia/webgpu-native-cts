// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/clamp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kValuesTypes = kConvertableToFloatScalarsAndVectors ++ kConcreteIntegerScalarsAndVectors.
// `low_high` builds custom WGSL across constant/override/runtime arg stages; the
// numeric low/high are small ints (0/1) so the double `a <= b` comparison is exact.

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
    "shader,validation,expression,call,builtin,clamp", "Validation tests for the clamp() builtin.");

// kValuesTypes = kConvertableToFloatScalarsAndVectors ++ kConcreteIntegerScalarsAndVectors
static std::vector<bt::Type> kValuesTypesTable() {
    std::vector<bt::Type> v = b::kConvertableToFloatScalarsAndVectors();
    for (const bt::Type& t : b::kConcreteIntegerScalarsAndVectors()) {
        v.push_back(t);
    }
    return v;
}
static const std::vector<bt::Type>& kValuesTypes() {
    static const std::vector<bt::Type> v = kValuesTypesTable();
    return v;
}

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of clamp() rejects invalid "
          "values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(kValuesTypes()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("e",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 3));
                    })
            .expand("low",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 4));
                    })
            .expand("high", [](const ParamRecord& p) {
                return b::rangeValues(b::fullRangeForType(
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 4));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue e = b::rangeValueFromParam(*findParam(t.params(), "e"));
        const b::RangeValue low = b::rangeValueFromParam(*findParam(t.params(), "low"));
        const b::RangeValue high = b::rangeValueFromParam(*findParam(t.params(), "high"));
        const bool expectedResult = rvNum(low) <= rvNum(high);
        b::validateConstOrOverrideBuiltinEval(
            t, "clamp", expectedResult,
            {b::createBuiltinValue(ty, e), b::createBuiltinValue(ty, low),
             b::createBuiltinValue(ty, high)},
            stage);
    });

CTS_TEST(g, "mismatched")
    .desc("Validates that even with valid types, if types do not match, clamp() errors")
    .params([](ParamsBuilder u) {
        return u.combine("e", b::typeKeys(kValuesTypes()))
            .beginSubcases()
            .combine("low", b::typeKeys(kValuesTypes()))
            .combine("high", b::typeKeys(kValuesTypes()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type e = bt::typeByName(t.param<std::string>("e"));
        const bt::Type low = bt::typeByName(t.param<std::string>("low"));
        const bt::Type high = bt::typeByName(t.param<std::string>("high"));
        const bool expectedResult =
            (bt::isConvertible(low, e) && bt::isConvertible(high, e)) ||
            (bt::isConvertible(e, low) && bt::isConvertible(high, low)) ||
            (bt::isConvertible(e, high) && bt::isConvertible(low, high));
        b::validateConstOrOverrideBuiltinEval(
            t, "clamp", expectedResult,
            {b::createBuiltinValue(e, b::RangeValue::makeI(1)),
             b::createBuiltinValue(low, b::RangeValue::makeI(0)),
             b::createBuiltinValue(high, b::RangeValue::makeI(2))},
            "constant");
    });

static bool typeRequiresF16(const bt::Type& ty) { return ty.kind == bt::ScalarKind::F16; }

CTS_TEST(g, "low_high")
    .desc("Validates that low <= high.")
    .params([](ParamsBuilder u) {
        const std::vector<Value> stages = {Value(std::string("constant")),
                                           Value(std::string("override")),
                                           Value(std::string("runtime"))};
        return u.combine("type", b::typeKeys(kValuesTypes()))
            .combine("lowStage", stages)
            .combine("highStage", stages)
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"low", Value(int64_t(0))}, {"high", Value(int64_t(1))}},
                ParamRecord{{"low", Value(int64_t(1))}, {"high", Value(int64_t(1))}},
                ParamRecord{{"low", Value(int64_t(1))}, {"high", Value(int64_t(0))}},
            })
            .filter([](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
                return k != bt::ScalarKind::AbstractInt && k != bt::ScalarKind::AbstractFloat;
            })
            .combine("in_shader", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const bt::Type ty = bt::typeByName(typeName);
        const bt::Type scalar = bt::scalarTypeOf(ty);
        const std::string scalarName = bt::scalarKindString(scalar.kind);
        const std::string lowStage = t.param<std::string>("lowStage");
        const std::string highStage = t.param<std::string>("highStage");
        const int64_t low = t.param<int64_t>("low");
        const int64_t high = t.param<int64_t>("high");
        const bool inShader = t.param<bool>("in_shader");

        auto argFor = [&](const std::string& stage, int64_t value, const std::string& oName,
                          const std::string& vName) -> std::string {
            if (stage == "constant") {
                return b::builtinValueWgsl(b::createBuiltinValue(ty, b::RangeValue::makeI(value)));
            }
            if (stage == "override") {
                return ty.toString() + "(" + oName + ")";
            }
            return vName;
        };
        const std::string lowArg = argFor(lowStage, low, "o_low", "v_low");
        const std::string highArg = argFor(highStage, high, "o_high", "v_high");
        const std::string enable = typeRequiresF16(ty) ? "enable f16;" : "";
        const std::string wgsl = "\n" + enable + "\noverride o_low : " + scalarName +
                                 ";\noverride o_high : " + scalarName + ";\nfn foo() {\n  var v_low : " +
                                 typeName + ";\n  var v_high : " + typeName + ";\n  var v : " +
                                 typeName + ";\n  let tmp = clamp(v, " + lowArg + ", " + highArg +
                                 ");\n}";
        const bool error = low > high;
        const bool shaderError = error && lowStage == "constant" && highStage == "constant";
        const bool pipelineError =
            inShader && error && lowStage != "runtime" && highStage != "runtime";
        t.expectCompileResult(!shaderError, wgsl);
        if (!shaderError) {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = !pipelineError;
            args.code = wgsl;
            args.constants = {{"o_low", static_cast<double>(low)},
                              {"o_high", static_cast<double>(high)}};
            args.reference = {"o_low", "o_high"};
            if (inShader) {
                args.statements = {"foo();"};
            }
            t.expectPipelineResult(args);
        }
    });

CTS_TEST(g, "low_high_abstract")
    .desc("Values low <= high for abstracts")
    .params([](ParamsBuilder u) {
        return u.combine("type", {Value(std::string("abstract-int")),
                                  Value(std::string("abstract-float"))})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"low", Value(int64_t(0))}, {"high", Value(int64_t(1))}},
                ParamRecord{{"low", Value(int64_t(1))}, {"high", Value(int64_t(1))}},
                ParamRecord{{"low", Value(int64_t(1))}, {"high", Value(int64_t(0))}},
            });
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const int64_t low = t.param<int64_t>("low");
        const int64_t high = t.param<int64_t>("high");
        b::validateConstOrOverrideBuiltinEval(
            t, "clamp", /*expectedResult=*/low <= high,
            {b::createBuiltinValue(ty, b::RangeValue::makeI(1)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(low)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(high))},
            "constant");
    });

// kInputArgTypes
struct InputArg {
    const char* name;
    const char* preamble;
    const char* arg;
    bool pass;
};
static const std::vector<InputArg>& kInputArgTypes() {
    static const std::vector<InputArg> v = {
        {"f32", "", "0.0f", true},
        {"bool", "", "false", false},
        {"mat2x2<f32>", "", "mat2x2(0.0f, 0.0f, 0.0f, 0.0f)", false},
        {"alias", "", "f32_alias(1.f)", true},
        {"vec_bool", "", "vec2<bool>(false,true)", false},
        {"atomic", "", "a", false},
        {"array", "var arry: array<f32, 5>;", "arry", false},
        {"array_runtime", "", "k.arry", false},
        {"struct", "var x: A;", "x", false},
        {"enumerant", "", "read_write", false},
        {"ptr", "var<function> f = 1.f;\n               let p: ptr<function, f32> = &f;", "p",
         false},
        {"ptr_deref", "var<function> f = 1.f;\n               let p: ptr<function, f32> = &f;", "*p",
         true},
        {"sampler", "", "s", false},
        {"texture", "", "t", false},
    };
    return v;
}
static std::vector<Value> inputArgNames() {
    std::vector<Value> out;
    for (const InputArg& a : kInputArgTypes()) {
        out.emplace_back(std::string(a.name));
    }
    return out;
}
static const InputArg& findInputArg(const std::string& name) {
    for (const InputArg& a : kInputArgTypes()) {
        if (name == a.name) {
            return a;
        }
    }
    static const InputArg dummy{"", "", "", false};
    return dummy;
}

CTS_TEST(g, "arguments")
    .desc("Test compilation validation of clamp with variously typed arguments")
    .params([](ParamsBuilder u) { return u.combine("type", inputArgNames()); })
    .fn([](ShaderValidationTest& t) {
        const InputArg& a = findInputArg(t.param<std::string>("type"));
        const std::string preamble = a.preamble[0] != '\0' ? std::string(a.preamble) : std::string();
        const std::string code =
            std::string("alias f32_alias = f32;\n\n"
                        "      @group(0) @binding(0) var s: sampler;\n"
                        "      @group(0) @binding(1) var t: texture_2d<f32>;\n\n"
                        "      var<workgroup> a: atomic<u32>;\n\n"
                        "      struct A {\n        i: u32,\n      }\n"
                        "      struct B {\n        arry: array<u32>,\n      }\n"
                        "      @group(0) @binding(3) var<storage> k: B;\n\n\n"
                        "      @vertex\n      fn main() -> @builtin(position) vec4<f32> {\n        ") +
            preamble + "\n        _ = clamp(" + a.arg + "," + a.arg + "," + a.arg +
            ");\n        return vec4<f32>(.4, .2, .3, .1);\n      }";
        t.expectCompileResult(a.pass, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of clamp must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "clamp(1.f,0.f,1.f); }");
    });

}  // namespace
