// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/select.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,select",
    "Validation tests for the select() builtin.");

CTS_TEST(g, "argument_types_1_and_2")
    .desc("Validates that scalar and vector arguments are not rejected by select() for args 1 and 2")
    .params([](ParamsBuilder u) {
        return u.combine("type1", b::typeKeys(bt::kAllScalarsAndVectors()))
            .combine("type2", b::typeKeys(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type1 = bt::typeByName(t.param<std::string>("type1"));
        const bt::Type type2 = bt::typeByName(t.param<std::string>("type2"));
        bool hasReturn = false;
        std::string returnType;
        if (bt::isConvertible(type1, type2)) {
            hasReturn = true;
            returnType = bt::concreteTypeOf(type2).toString();
        } else if (bt::isConvertible(type2, type1)) {
            hasReturn = true;
            returnType = bt::concreteTypeOf(type1).toString();
        }
        b::validateConstOrOverrideBuiltinEval(
            t, "select", /*expectedResult=*/hasReturn,
            {b::createBuiltinValue(type1, b::RangeValue::makeI(0)),
             b::createBuiltinValue(type2, b::RangeValue::makeI(0)),
             b::createBuiltinValue(bt::scalar(bt::ScalarKind::Bool), b::RangeValue::makeI(0))},
            "constant", hasReturn ? returnType : std::string());
    });

CTS_TEST(g, "argument_types_3")
    .desc("Validates that third argument must be bool for select()")
    .params([](ParamsBuilder u) {
        return u.combine("type", b::typeKeys(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = type == bt::scalar(bt::ScalarKind::Bool);
        b::validateConstOrOverrideBuiltinEval(
            t, "select", expectedResult,
            {b::createBuiltinValue(bt::scalar(bt::ScalarKind::I32), b::RangeValue::makeI(0)),
             b::createBuiltinValue(bt::scalar(bt::ScalarKind::I32), b::RangeValue::makeI(0)),
             b::createBuiltinValue(type, b::RangeValue::makeI(0))},
            "constant", bt::scalar(bt::ScalarKind::I32).toString());
    });

CTS_TEST(g, "must_use")
    .desc("Result of select must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "select(1, 2, true); }");
    });

struct ParamTest {
    const char* name;
    const char* src;
    bool pass;
};
static const std::vector<ParamTest>& kTests() {
    static const std::vector<ParamTest> v = {
        {"valid", "_ = select(1, 2, true);", true},
        {"alias", "_ = select(i32_alias(1), i32_alias(2), bool_alias(true));", true},
        {"bool", "_ = select(false, false, true);", true},
        {"i32", "_ = select(1i, 1i, true);", true},
        {"u32", "_ = select(1u, 1u, true);", true},
        {"f32", "_ = select(1.0f, 1.0f, true);", true},
        {"f16", "_ = select(1.0h, 1.0h, true);", true},
        {"mixed_aint_afloat", "_ = select(1, 1.0, true);", true},
        {"mixed_i32_u32", "_ = select(1i, 1u, true);", false},
        {"vec_bool", "_ = select(vec2<bool>(false, true), vec2<bool>(false, true), true);", true},
        {"vec2_bool_implicit", "_ = select(vec2(false, true), vec2(false, true), true);", true},
        {"vec3_bool_implicit", "_ = select(vec3(false), vec3(true), true);", true},
        {"vec_i32", "_ = select(vec2<i32>(1, 1), vec2<i32>(1, 1), true);", true},
        {"vec_u32", "_ = select(vec2<u32>(1, 1), vec2<u32>(1, 1), true);", true},
        {"vec_f32", "_ = select(vec2<f32>(1, 1), vec2<f32>(1, 1), true);", true},
        {"vec_f16", "_ = select(vec2<f16>(1, 1), vec2<f16>(1, 1), true);", true},
        {"matrix", "_ = select(mat2x2(1, 1, 1, 1), mat2x2(1, 1, 1, 1), true);", false},
        {"atomic", " _ = select(a, a, true);", false},
        {"array", "var a: array<bool, 5>;\n            _ = select(a, a, true);", false},
        {"array_runtime", "_ = select(k.arry, k.arry, true);", false},
        {"struct", "var a: A;\n            _ = select(a, a, true);", false},
        {"enumerant", "_ = select(read_write, read_write, true);", false},
        {"ptr",
         "var<function> a = true;\n            let p: ptr<function, bool> = &a;\n            _ = "
         "select(p, p, true);",
         false},
        {"ptr_deref",
         "var<function> a = true;\n            let p: ptr<function, bool> = &a;\n            _ = "
         "select(*p, *p, true);",
         true},
        {"sampler", "_ = select(s, s, true);", false},
        {"texture", "_ = select(t, t, true);", false},
        {"no_args", "_ = select();", false},
        {"too_few_args", "_ = select(1, true);", false},
        {"too_many_args", "_ = select(1, 1, 1, true);", false},
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

CTS_TEST(g, "arguments")
    .desc("Test that select is validated correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string testName = t.param<std::string>("test");
        const ParamTest& pt = findTest(testName);
        const std::string enables =
            testName.find("f16") != std::string::npos ? "enable f16;" : "";
        const std::string code =
            std::string("\n  ") + enables +
            "\n  alias bool_alias = bool;\n"
            "  alias i32_alias = i32;\n\n"
            "  @group(0) @binding(0) var s: sampler;\n"
            "  @group(0) @binding(1) var t: texture_2d<f32>;\n\n"
            "  var<workgroup> a: atomic<u32>;\n\n"
            "  struct A {\n    i: bool,\n  }\n"
            "  struct B {\n    arry: array<u32>,\n  }\n"
            "  @group(0) @binding(3) var<storage> k: B;\n\n"
            "  @vertex\n  fn main() -> @builtin(position) vec4<f32> {\n    " +
            pt.src + "\n    return vec4<f32>(.4, .2, .3, .1);\n  }";
        t.expectCompileResult(pt.pass, code);
    });

}  // namespace
