// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/atan2.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kValuesTypes = kFloatScalarsAndVectors. invalid_argument_{y,x} use a type table
// mixing f32, concrete integers, bools and ALL matrices; the upstream
// `instanceof VectorValue` check is always false for a Type, so the other arg is
// always f32 (faithfully replicated).

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
    "shader,validation,expression,call,builtin,atan2", "Validation tests for the atan2() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of atan2() rejects invalid "
          "values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kFloatScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("y",
                    [](const ParamRecord& p) {
                        const bt::Type ty =
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                        return b::rangeValues(b::uniqueRanges(
                            {b::sparseMinusThreePiToThreePiRangeForType(ty),
                             b::fullRangeForType(ty, 4)}));
                    })
            .expand("x", [](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                return b::rangeValues(
                    b::uniqueRanges({b::sparseMinusThreePiToThreePiRangeForType(ty),
                                     b::fullRangeForType(ty, 4)}));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue yrv = b::rangeValueFromParam(*findParam(t.params(), "y"));
        const b::RangeValue xrv = b::rangeValueFromParam(*findParam(t.params(), "x"));
        const double x = rvNum(xrv);
        const double y = rvNum(yrv);
        const bool expectedResult =
            b::isRepresentable(std::abs(std::atan2(x, y)), bt::scalarTypeOf(ty).kind);
        b::validateConstOrOverrideBuiltinEval(
            t, "atan2", expectedResult,
            {b::createBuiltinValue(ty, yrv), b::createBuiltinValue(ty, xrv)}, stage);
    });

// kInvalidArgumentTypes = [f32, ...kConcreteIntegerScalarsAndVectors,
//                          ...kAllBoolScalarsAndVectors, ...kAllMatrices]
static std::vector<Value> invalidArgTypeKeys() {
    std::vector<Value> out;
    out.emplace_back(std::string("f32"));
    for (const bt::Type& t : b::kConcreteIntegerScalarsAndVectors()) {
        out.emplace_back(t.toString());
    }
    // kAllBoolScalarsAndVectors: bool, vec2/3/4 bool
    out.emplace_back(std::string("bool"));
    out.emplace_back(std::string("vec2<bool>"));
    out.emplace_back(std::string("vec3<bool>"));
    out.emplace_back(std::string("vec4<bool>"));
    for (const b::MatType& m : b::kAllMatrices()) {
        out.emplace_back(m.toString());
    }
    return out;
}

CTS_TEST(g, "invalid_argument_y")
    .desc("Validates that scalar and vector integer arguments are rejected by atan2()")
    .params([](ParamsBuilder u) { return u.combine("type", invalidArgTypeKeys()); })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("type");
        b::validateConstOrOverrideBuiltinEval(
            t, "atan2", /*expectedResult=*/name == "f32",
            {b::createByKey(name, 1),
             b::createBuiltinValue(bt::scalar(bt::ScalarKind::F32), b::RangeValue::makeI(1))},
            "constant");
    });

CTS_TEST(g, "invalid_argument_x")
    .desc("Validates that scalar and vector integer arguments are rejected by atan2()")
    .params([](ParamsBuilder u) { return u.combine("type", invalidArgTypeKeys()); })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("type");
        b::validateConstOrOverrideBuiltinEval(
            t, "atan2", /*expectedResult=*/name == "f32",
            {b::createBuiltinValue(bt::scalar(bt::ScalarKind::F32), b::RangeValue::makeI(1)),
             b::createByKey(name, 1)},
            "constant");
    });

struct ParamTest {
    const char* name;
    const char* src;
    bool pass;
    bool is_f16;
};
static const std::vector<ParamTest>& kTests() {
    static const std::vector<ParamTest> v = {
        {"af", "_ = atan2(1.2, 2.2);", true, false},
        {"ai", "_ = atan2(1, 2);", true, false},
        {"ai_af", "_ = atan2(1, 2.1);", true, false},
        {"af_ai", "_ = atan2(1.2, 2);", true, false},
        {"ai_f32", "_ = atan2(1, 1.2f);", true, false},
        {"f32_ai", "_ = atan2(1.2f, 1);", true, false},
        {"af_f32", "_ = atan2(1.2, 2.2f);", true, false},
        {"f32_af", "_ = atan2(2.2f, 1.2);", true, false},
        {"f16_ai", "_ = atan2(1.2h, 1);", true, true},
        {"ai_f16", "_ = atan2(1, 1.2h);", true, true},
        {"af_f16", "_ = atan2(1.2, 1.2h);", true, true},
        {"f16_af", "_ = atan2(1.2h, 1.2);", true, true},
        {"mixed_types", "_ = atan2(1.2f, vec2(1.2f));", false, false},
        {"mixed_types_2", "_ = atan2(vec2(1.2f), 1.2f);", false, false},
        {"f16_f32", "_ = atan2(1.2h, 1.2f);", false, true},
        {"u32_f32", "_ = atan2(1u, 1.2f);", false, false},
        {"f32_u32", "_ = atan2(1.2f, 1u);", false, false},
        {"f32_i32", "_ = atan2(1.2f, 1i);", false, false},
        {"i32_f32", "_ = atan2(1i, 1.2f);", false, false},
        {"f32_bool", "_ = atan2(1.2f, true);", false, false},
        {"bool_f32", "_ = atan2(false, 1.2f);", false, false},
        {"vec_f32", "_ = atan2(vec2(1i), vec2(1.2f));", false, false},
        {"f32_vec", "_ = atan2(vec2(1.2f), vec2(1i));", false, false},
        {"matrix", "_ = atan2(mat2x2(1, 1, 1, 1), mat2x2(1, 1, 1, 1));", false, false},
        {"atomic", " _ = atan2(a, a);", false, false},
        {"array", "var a: array<u32, 5>;\n          _ = atan2(a, a);", false, false},
        {"array_runtime", "_ = atan2(k.arry, k.arry);", false, false},
        {"struct", "var a: A;\n          _ = atan2(a, a);", false, false},
        {"enumerant", "_ = atan2(read_write, read_write);", false, false},
        {"ptr",
         "var<function> a = 1f;\n          let p: ptr<function, f32> = &a;\n          _ = atan2(p, "
         "p);",
         false, false},
        {"ptr_deref",
         "var<function> a = 1f;\n          let p: ptr<function, f32> = &a;\n          _ = "
         "atan2(*p, *p);",
         true, false},
        {"sampler", "_ = atan2(s, s);", false, false},
        {"texture", "_ = atan2(t, t);", false, false},
        {"no_params", "_ = atan2();", false, false},
        {"too_many_params", "_ = atan2(1, 2, 3);", false, false},
        {"must_use", "atan2(1, 2);", false, false},
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
    static const ParamTest dummy{"", "", false, false};
    return dummy;
}

CTS_TEST(g, "parameters")
    .desc("Test that atan2 is validated correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const ParamTest& pt = findTest(t.param<std::string>("test"));
        const std::string enable = pt.is_f16 ? "enable f16;" : "";
        const std::string code =
            std::string("\n") + enable +
            "\nalias f32_alias = f32;\n\n"
            "@group(0) @binding(0) var s: sampler;\n"
            "@group(0) @binding(1) var t: texture_2d<f32>;\n\n"
            "var<workgroup> a: atomic<u32>;\n\n"
            "struct A {\n  i: u32,\n}\n"
            "struct B {\n  arry: array<u32>,\n}\n"
            "@group(0) @binding(3) var<storage> k: B;\n\n\n"
            "@vertex\nfn main() -> @builtin(position) vec4<f32> {\n  " +
            pt.src + "\n  return vec4<f32>(.4, .2, .3, .1);\n}";
        t.expectCompileResult(pt.pass, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of atan2 must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "atan2(1, 2); }");
    });

}  // namespace
