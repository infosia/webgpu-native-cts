// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/acosh.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,acosh", "Validation tests for the acosh() builtin.");

// AbstractInt is converted to AbstractFloat before calling into the builtin.
static bt::ScalarKind isReprKind(const bt::Type& ty) {
    const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
    return k == bt::ScalarKind::AbstractInt ? bt::ScalarKind::AbstractFloat : k;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of acosh() rejects invalid "
          "values")
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
                return b::rangeValues(b::uniqueRanges(
                    {b::minusTwoToTwoRangeForType(ty), b::fullRangeForType(ty)}));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        const double num = rv.isInt ? static_cast<double>(rv.i) : rv.d;
        const bool expectedResult = b::isRepresentable(std::acosh(num), isReprKind(ty));
        b::validateConstOrOverrideBuiltinEval(t, "acosh", expectedResult,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

// kIntegerArgumentTypes = kConcreteIntegerScalarsAndVectors (no f32).
CTS_TEST(g, "integer_argument")
    .desc("Validates that scalar and vector integer arguments are rejected by acosh()")
    .params([](ParamsBuilder u) {
        return u.combine("type", b::typeKeys(b::kConcreteIntegerScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        // expectedResult = (type === Type.f32); none of the concrete integers are f32.
        b::validateConstOrOverrideBuiltinEval(t, "acosh", /*expectedResult=*/false,
                                              {b::createBuiltinValue(ty, b::RangeValue::makeI(1))},
                                              "constant");
    });

struct ParamTest {
    const char* name;
    const char* src;
    bool pass;
};
static const std::vector<ParamTest>& kTests() {
    static const std::vector<ParamTest> v = {
        {"valid", "_ = acosh(1);", true},
        {"alias", "_ = acosh(f32_alias(1));", true},
        {"bool", "_ = acosh(false);", false},
        {"i32", "_ = acosh(1i);", false},
        {"u32", "_ = acosh(1u);", false},
        {"vec_bool", "_ = acosh(vec2<bool>(false, true));", false},
        {"vec_i32", "_ = acosh(vec2<i32>(1, 1));", false},
        {"vec_u32", "_ = acosh(vec2<u32>(1, 1));", false},
        {"matrix", "_ = acosh(mat2x2(1, 1, 1, 1));", false},
        {"atomic", " _ = acosh(a);", false},
        {"array", "var a: array<u32, 5>;\n          _ = acosh(a);", false},
        {"array_runtime", "_ = acosh(k.arry);", false},
        {"struct", "var a: A;\n          _ = acosh(a);", false},
        {"enumerant", "_ = acosh(read_write);", false},
        {"ptr",
         "var<function> a = 1f;\n          let p: ptr<function, f32> = &a;\n          _ = acosh(p);",
         false},
        {"ptr_deref",
         "var<function> a = 1f;\n          let p: ptr<function, f32> = &a;\n          _ = "
         "acosh(*p);",
         true},
        {"sampler", "_ = acosh(s);", false},
        {"texture", "_ = acosh(t);", false},
        {"no_params", "_ = acosh();", false},
        {"too_many_params", "_ = acosh(1, 2);", false},
        {"less_then_one", "_ = acosh(.9f);", false},
        {"must_use", "acosh(1);", false},
    };
    return v;
}
static std::vector<Value> testNames() {
    std::vector<Value> out;
    for (const ParamTest& pt : kTests()) {
        out.emplace_back(std::string(pt.name));
    }
    return out;
}
static const ParamTest& findTest(const std::string& name) {
    for (const ParamTest& pt : kTests()) {
        if (name == pt.name) {
            return pt;
        }
    }
    static const ParamTest dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parameters")
    .desc("Test that acosh is validated correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const ParamTest& pt = findTest(t.param<std::string>("test"));
        const std::string code = std::string(
                                     "\nalias f32_alias = f32;\n\n"
                                     "@group(0) @binding(0) var s: sampler;\n"
                                     "@group(0) @binding(1) var t: texture_2d<f32>;\n\n"
                                     "var<workgroup> a: atomic<u32>;\n\n"
                                     "struct A {\n  i: u32,\n}\n"
                                     "struct B {\n  arry: array<u32>,\n}\n"
                                     "@group(0) @binding(3) var<storage> k: B;\n\n"
                                     "@vertex\nfn main() -> @builtin(position) vec4<f32> {\n  ") +
                                 pt.src +
                                 "\n  return vec4<f32>(.4, .2, .3, .1);\n}";
        t.expectCompileResult(pt.pass, code);
    });

}  // namespace
