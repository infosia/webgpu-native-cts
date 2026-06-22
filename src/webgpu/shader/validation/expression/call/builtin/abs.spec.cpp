// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/abs.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,abs", "Validation tests for the abs() builtin.");

// kValuesTypes = kAllNumericScalarsAndVectors
CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of abs() never errors")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kAllNumericScalarsAndVectors()))
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
        b::validateConstOrOverrideBuiltinEval(t, "abs", /*expectedResult=*/true,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

struct ParamTest {
    const char* name;
    const char* src;
    bool pass;
};
static const std::vector<ParamTest>& kTests() {
    static const std::vector<ParamTest> v = {
        {"valid", "_ = abs(1);", true},
        {"alias", "_ = abs(i32_alias(1));", true},
        {"bool", "_ = abs(false);", false},
        {"vec_bool", "_ = abs(vec2<bool>(false, true));", false},
        {"matrix", "_ = abs(mat2x2(1, 1, 1, 1));", false},
        {"atomic", " _ = abs(a);", false},
        {"array", "var a: array<u32, 5>;\n          _ = abs(a);", false},
        {"array_runtime", "_ = abs(k.arry);", false},
        {"struct", "var a: A;\n          _ = abs(a);", false},
        {"enumerant", "_ = abs(read_write);", false},
        {"ptr",
         "var<function> a = 1u;\n          let p: ptr<function, u32> = &a;\n          _ = abs(p);",
         false},
        {"ptr_deref",
         "var<function> a = 1u;\n          let p: ptr<function, u32> = &a;\n          _ = abs(*p);",
         true},
        {"sampler", "_ = abs(s);", false},
        {"texture", "_ = abs(t);", false},
        {"no_params", "_ = abs();", false},
        {"too_many_params", "_ = abs(1, 2);", false},
        {"must_use", "abs(1);", false},
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
    .desc("Test that abs is validated correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const ParamTest& pt = findTest(t.param<std::string>("test"));
        const std::string code = std::string(
                                     "\nalias i32_alias = i32;\n\n"
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
