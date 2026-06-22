// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/round.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,round", "Validation tests for the round() builtin.");

// fpTraitsFor(scalarType).constants() negative.min / positive.max for the
// concrete/abstract float element kinds.
static double traitNegMin(bt::ScalarKind k) {
    switch (k) {
        case bt::ScalarKind::F32:
            return b::kValueF32().negMin;
        case bt::ScalarKind::F16:
            return b::kValueF16().negMin;
        default:  // abstract-float => f64
            return -b::reinterpretU64AsF64(b::KBit::f64_pos_max);
    }
}
static double traitPosMax(bt::ScalarKind k) {
    switch (k) {
        case bt::ScalarKind::F32:
            return b::kValueF32().posMax;
        case bt::ScalarKind::F16:
            return b::kValueF16().posMax;
        default:  // abstract-float => f64
            return b::reinterpretU64AsF64(b::KBit::f64_pos_max);
    }
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of round() inputs rejects "
          "invalid values")
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
                const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
                if (k == bt::ScalarKind::AbstractInt) {
                    return b::rangeValues(b::fullRangeForType(ty));
                }
                std::vector<b::RangeValue> extra = {
                    b::RangeValue::makeD(traitNegMin(k) + 0.1),
                    b::RangeValue::makeD(traitPosMax(k) - 0.1)};
                return b::rangeValues(b::uniqueRanges({b::fullRangeForType(ty), extra}));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        b::validateConstOrOverrideBuiltinEval(t, "round", /*expectedResult=*/true,
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
    .desc("Validates that scalar and vector integer arguments are rejected by round()")
    .params([](ParamsBuilder u) { return u.combine("type", b::typeKeys(kIntegerArgumentTypes())); })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = ty.kind == bt::ScalarKind::F32 && ty.isScalar();
        b::validateConstOrOverrideBuiltinEval(t, "round", expectedResult,
                                              {b::createBuiltinValue(ty, b::RangeValue::makeI(1))},
                                              "constant");
    });

struct ArgTest {
    const char* name;
    const char* preamble;
    const char* args;
    bool pass;
};
static const std::vector<ArgTest>& kTests() {
    static const std::vector<ArgTest> v = {
        {"valid", "", "(1.0f)", true},
        {"no_parens", "", "", false},
        {"too_few_args", "", "()", false},
        {"too_many_args", "", "(1.f,2.f)", false},
        {"alias", "", "(f32_alias(1.f))", true},
        {"bool", "", "(false)", false},
        {"vec_bool", "", "(vec2<bool>(false,true))", false},
        {"matrix", "", "(mat2x2(1.f,1.f,1.f,1.f))", false},
        {"atomic", "", "(a)", false},
        {"array", "var arry: array<f32, 5>;", "(arry)", false},
        {"array_runtime", "", "(k.arry)", false},
        {"struct", "var x: A;", "(x)", false},
        {"enumerant", "", "(read_write)", false},
        {"ptr",
         "var<function> f = 1.f;\n                     let p: ptr<function, f32> = &f;", "(p)",
         false},
        {"ptr_deref",
         "var<function> f = 1.f;\n                     let p: ptr<function, f32> = &f;", "(*p)",
         true},
        {"sampler", "", "(s)", false},
        {"texture", "", "(t)", false},
    };
    return v;
}
static std::vector<Value> testNames() {
    std::vector<Value> out;
    for (const ArgTest& at : kTests()) {
        out.emplace_back(std::string(at.name));
    }
    return out;
}
static const ArgTest& findTest(const std::string& name) {
    for (const ArgTest& at : kTests()) {
        if (name == at.name) {
            return at;
        }
    }
    static const ArgTest dummy{"", "", "", false};
    return dummy;
}

CTS_TEST(g, "arguments")
    .desc("Test compilation validation of round with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const ArgTest& at = findTest(t.param<std::string>("test"));
        const std::string code =
            std::string(
                "alias f32_alias = f32;\n\n"
                "            @group(0) @binding(0) var s: sampler;\n"
                "            @group(0) @binding(1) var t: texture_2d<f32>;\n\n"
                "            var<workgroup> a: atomic<u32>;\n\n"
                "            struct A {\n              i: f32,\n            }\n"
                "            struct B {\n              arry: array<f32>,\n            }\n"
                "            @group(0) @binding(3) var<storage> k: B;\n\n\n"
                "            @vertex\n            fn main() -> @builtin(position) vec4<f32> "
                "{\n              ") +
            at.preamble + "\n              _ = round" + at.args +
            ";\n              return vec4<f32>(.4, .2, .3, .1);\n            }";
        t.expectCompileResult(at.pass, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of round must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, "fn f() { " + useIt + "round(1.0f); }");
    });

}  // namespace
