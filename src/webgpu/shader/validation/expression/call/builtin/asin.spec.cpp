// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/asin.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,asin", "Validation tests for the asin() builtin.");

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of asin() rejects invalid "
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
        const bool expectedResult =
            rv.isInt ? (b::absBigInt(rv.i) <= 1) : (std::abs(rv.d) <= 1.0);
        b::validateConstOrOverrideBuiltinEval(t, "asin", expectedResult,
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
    .desc("Validates that scalar and vector integer arguments are rejected by asin()")
    .params([](ParamsBuilder u) { return u.combine("type", b::typeKeys(kIntegerArgumentTypes())); })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = ty.kind == bt::ScalarKind::F32 && ty.isScalar();
        b::validateConstOrOverrideBuiltinEval(t, "asin", expectedResult,
                                              {b::createBuiltinValue(ty, b::RangeValue::makeI(0))},
                                              "constant");
    });

struct ParamTest {
    const char* name;
    const char* src;
    bool pass;
};
static const std::vector<ParamTest>& kTests() {
    static const std::vector<ParamTest> v = {
        {"valid", "_ = asin(1);", true},
        {"alias", "_ = asin(f32_alias(1));", true},
        {"bool", "_ = asin(false);", false},
        {"i32", "_ = asin(1i);", false},
        {"u32", "_ = asin(1u);", false},
        {"vec_bool", "_ = asin(vec2<bool>(false, true));", false},
        {"vec_i32", "_ = asin(vec2<i32>(1, 1));", false},
        {"vec_u32", "_ = asin(vec2<u32>(1, 1));", false},
        {"matrix", "_ = asin(mat2x2(1, 1, 1, 1));", false},
        {"atomic", " _ = asin(a);", false},
        {"array", "var a: array<u32, 5>;\n          _ = asin(a);", false},
        {"array_runtime", "_ = asin(k.arry);", false},
        {"struct", "var a: A;\n          _ = asin(a);", false},
        {"enumerant", "_ = asin(read_write);", false},
        {"ptr",
         "var<function> a = 1f;\n          let p: ptr<function, f32> = &a;\n          _ = asin(p);",
         false},
        {"ptr_deref",
         "var<function> a = 1f;\n          let p: ptr<function, f32> = &a;\n          _ = asin(*p);",
         true},
        {"sampler", "_ = asin(s);", false},
        {"texture", "_ = asin(t);", false},
        {"no_params", "_ = asin();", false},
        {"too_many_params", "_ = asin(1, 2);", false},
        {"greater_then_one", "_ = asin(1.1f);", false},
        {"less_then_negative_one", "_ = asin(-1.1f);", false},
        {"must_use", "asin(1);", false},
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
    .desc("Test that asin is validated correctly.")
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
                                     "@group(0) @binding(3) var<storage> k: B;\n\n\n"
                                     "@vertex\nfn main() -> @builtin(position) vec4<f32> {\n  ") +
                                 pt.src +
                                 "\n  return vec4<f32>(.4, .2, .3, .1);\n}";
        t.expectCompileResult(pt.pass, code);
    });

}  // namespace
