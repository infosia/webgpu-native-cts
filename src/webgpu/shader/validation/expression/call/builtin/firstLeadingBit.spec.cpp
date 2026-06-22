// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/firstLeadingBit.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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

constexpr const char* kBuiltin = "firstLeadingBit";

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,firstLeadingBit",
    "Validation tests for the firstLeadingBit() builtin.");

// kValuesTypes = kConcreteIntegerScalarsAndVectors
CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of firstLeadingBit() never "
          "errors")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConcreteIntegerScalarsAndVectors()))
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
        // firstLeadingBit() should never error
        b::validateConstOrOverrideBuiltinEval(t, kBuiltin, /*expectedResult=*/true,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

// kFloatTypes = [u32, ...kFloatScalarsAndVectors]. u32 is included to confirm that
// validation fails due to a type issue and not something else.
static const std::vector<bt::Type>& kFloatTypes() {
    static std::vector<bt::Type> v = [] {
        std::vector<bt::Type> out = {bt::scalar(bt::ScalarKind::U32)};
        for (const bt::Type& ty : b::kFloatScalarsAndVectors()) {
            out.push_back(ty);
        }
        return out;
    }();
    return v;
}

CTS_TEST(g, "float_argument")
    .desc("Validates that float arguments are rejected by firstLeadingBit()")
    .params([](ParamsBuilder u) { return u.combine("type", b::typeKeys(kFloatTypes())); })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = ty == bt::scalar(bt::ScalarKind::U32);
        b::validateConstOrOverrideBuiltinEval(t, kBuiltin, expectedResult,
                                              {b::createBuiltinValue(ty, b::RangeValue::makeI(0))},
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
        {"valid", "", "(1u)", true},
        {"no_parens", "", "", false},
        {"too_few_args", "", "()", false},
        {"too_many_args", "", "(1u,2u)", false},
        {"alias", "", "(u32_alias(1))", true},
        {"bool", "", "(false)", false},
        {"vec_bool", "", "(vec2<bool>(false,true))", false},
        {"matrix", "", "(mat2x2(1,1,1,1))", false},
        {"atomic", "", "(a)", false},
        {"array", "var arry: array<u32, 5>;", "(arry)", false},
        {"array_runtime", "", "(k.arry)", false},
        {"struct", "var x: A;", "(x)", false},
        {"enumerant", "", "(read_write)", false},
        {"ptr", "var<function> f = 1u;\n               let p: ptr<function, u32> = &f;", "(p)",
         false},
        {"ptr_deref", "var<function> f = 1u;\n               let p: ptr<function, u32> = &f;",
         "(*p)", true},
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
    .desc("Test compilation validation of firstLeadingBit with variously shaped and typed "
          "arguments")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const ArgTest& at = findTest(t.param<std::string>("test"));
        const std::string code =
            std::string(
                "alias u32_alias = u32;\n\n"
                "      @group(0) @binding(0) var s: sampler;\n"
                "      @group(0) @binding(1) var t: texture_2d<f32>;\n\n"
                "      var<workgroup> a: atomic<u32>;\n\n"
                "      struct A {\n"
                "        i: u32,\n"
                "      }\n"
                "      struct B {\n"
                "        arry: array<u32>,\n"
                "      }\n"
                "      @group(0) @binding(3) var<storage> k: B;\n\n\n"
                "      @vertex\n"
                "      fn main() -> @builtin(position) vec4<f32> {\n        ") +
            at.preamble + "\n        _ = " + kBuiltin + at.args +
            ";\n        return vec4<f32>(.4, .2, .3, .1);\n      }";
        t.expectCompileResult(at.pass, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of firstLeadingBit must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + kBuiltin + "(1u); }");
    });

}  // namespace
