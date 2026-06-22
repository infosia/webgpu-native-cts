// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/any.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,any", "Validation tests for the any() builtin.");

// kArgumentTypes = objectsToRecord(kAllScalarsAndVectors)
CTS_TEST(g, "argument_types")
    .desc("Validates that scalar and vector arguments are rejected by any() if not bool or vecN<bool>")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = bt::scalarTypeOf(type).kind == bt::ScalarKind::Bool;
        b::validateConstOrOverrideBuiltinEval(
            t, "any", expectedResult, {b::createBuiltinValue(type, b::RangeValue::makeI(0))},
            "constant", /*returnType=*/"bool");
    });

struct ArgTest {
    const char* name;
    const char* src;
    bool pass;
};
static const std::vector<ArgTest>& kTests() {
    static const std::vector<ArgTest> v = {
        {"valid", "_ = any(true);", true},
        {"alias", "_ = any(bool_alias(true));", true},
        {"bool", "_ = any(false);", true},
        {"i32", "_ = any(1i);", false},
        {"u32", "_ = any(1u);", false},
        {"f32", "_ = any(1.0f);", false},
        {"f16", "_ = any(1.0h);", false},
        {"vec_bool", "_ = any(vec2<bool>(false, true));", true},
        {"vec2_bool_implicit", "_ = any(vec2(false, true));", true},
        {"vec3_bool_implicit", "_ = any(vec3(true));", true},
        {"vec_i32", "_ = any(vec2<i32>(1, 1));", false},
        {"vec_u32", "_ = any(vec2<u32>(1, 1));", false},
        {"vec_f32", "_ = any(vec2<f32>(1, 1));", false},
        {"vec_f16", "_ = any(vec2<f16>(1, 1));", false},
        {"matrix", "_ = any(mat2x2(1, 1, 1, 1));", false},
        {"atomic", " _ = any(a);", false},
        {"array",
         "var a: array<bool, 5>;\n            _ = any(a);", false},
        {"array_runtime", "_ = any(k.arry);", false},
        {"struct", "var a: A;\n            _ = any(a);", false},
        {"enumerant", "_ = any(read_write);", false},
        {"ptr",
         "var<function> a = true;\n            let p: ptr<function, bool> = &a;\n            _ = "
         "any(p);",
         false},
        {"ptr_deref",
         "var<function> a = true;\n            let p: ptr<function, bool> = &a;\n            _ = "
         "any(*p);",
         true},
        {"sampler", "_ = any(s);", false},
        {"texture", "_ = any(t);", false},
        {"no_args", "_ = any();", false},
        {"too_many_args", "_ = any(true, true);", false},
    };
    return v;
}
static std::vector<Value> testNames() {
    std::vector<Value> out;
    for (const ArgTest& a : kTests()) {
        out.emplace_back(std::string(a.name));
    }
    return out;
}
static const ArgTest& findTest(const std::string& name) {
    for (const ArgTest& a : kTests()) {
        if (name == a.name) {
            return a;
        }
    }
    static const ArgTest dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "must_use")
    .desc("Result of any must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "any(true); }");
    });

CTS_TEST(g, "arguments")
    .desc("Test that any is validated correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string testName = t.param<std::string>("test");
        const ArgTest& at = findTest(testName);
        const bool hasF16 = testName.find("f16") != std::string::npos;
        const std::string enables = hasF16 ? "enable f16;" : "";
        const std::string code = std::string("\n  ") + enables +
                                 "\n  alias bool_alias = bool;\n\n"
                                 "  @group(0) @binding(0) var s: sampler;\n"
                                 "  @group(0) @binding(1) var t: texture_2d<f32>;\n\n"
                                 "  var<workgroup> a: atomic<u32>;\n\n"
                                 "  struct A {\n    i: bool,\n  }\n"
                                 "  struct B {\n    arry: array<u32>,\n  }\n"
                                 "  @group(0) @binding(3) var<storage> k: B;\n\n"
                                 "  @vertex\n  fn main() -> @builtin(position) vec4<f32> {\n    " +
                                 at.src + "\n    return vec4<f32>(.4, .2, .3, .1);\n  }";
        t.expectCompileResult(at.pass, code);
    });

}  // namespace
