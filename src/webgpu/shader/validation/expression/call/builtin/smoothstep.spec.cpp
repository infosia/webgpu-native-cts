// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/smoothstep.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

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
    "shader,validation,expression,call,builtin,smoothstep",
    "Validation tests for the smoothstep() builtin.");

static bool typeRequiresF16(const bt::Type& ty) { return ty.kind == bt::ScalarKind::F16; }

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of smoothstep() rejects "
          "invalid values")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("value1",
                    [](const ParamRecord&) {
                        return std::vector<Value>{Value(int64_t(-1000)), Value(int64_t(-10)),
                                                  Value(int64_t(0)), Value(int64_t(10)),
                                                  Value(int64_t(1000))};
                    })
            .expand("value2", [](const ParamRecord&) {
                return std::vector<Value>{Value(int64_t(-1000)), Value(int64_t(-10)),
                                          Value(int64_t(0)), Value(int64_t(10)),
                                          Value(int64_t(1000))};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const int64_t value1 = t.param<int64_t>("value1");
        const int64_t value2 = t.param<int64_t>("value2");
        const bool expectedResult = value1 != value2;
        b::validateConstOrOverrideBuiltinEval(
            t, "smoothstep", expectedResult,
            {b::createBuiltinValue(ty, b::RangeValue::makeI(value1)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(value2)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(0))},
            stage);
    });

CTS_TEST(g, "partial_eval_errors")
    .desc("Validates that low != high")
    .params([](ParamsBuilder u) {
        const std::vector<Value> stages = {Value(std::string("constant")),
                                           Value(std::string("override")),
                                           Value(std::string("runtime"))};
        return u.combine("lowStage", stages)
            .combine("highStage", stages)
            .combine("type", b::typeKeys(b::kConvertableToFloatScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                const bt::ScalarKind k = bt::scalarTypeOf(ty).kind;
                return k != bt::ScalarKind::AbstractInt && k != bt::ScalarKind::AbstractFloat;
            })
            .beginSubcases()
            .expand("low",
                    [](const ParamRecord&) {
                        return std::vector<Value>{Value(int64_t(0)), Value(int64_t(10))};
                    })
            .expand("high",
                    [](const ParamRecord&) {
                        return std::vector<Value>{Value(int64_t(0)), Value(int64_t(10))};
                    })
            .combine("in_shader", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const bt::Type ty = bt::typeByName(typeName);
        const bt::Type scalarTy = bt::scalarTypeOf(ty);
        const std::string scalarName = bt::scalarKindString(scalarTy.kind);
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
                                 ";\noverride o_high : " + scalarName + ";\nfn foo() {\n  var x : " +
                                 typeName + ";\n  var v_low : " + typeName + ";\n  var v_high : " +
                                 typeName + ";\n  let tmp = smoothstep(" + lowArg + ", " + highArg +
                                 ", x);\n}";
        const bool error = low == high;
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

CTS_TEST(g, "argument_types")
    .desc("Validates that scalar and vector arguments are rejected by smoothstep() if not float "
          "type or vecN<float type>")
    .params([](ParamsBuilder u) {
        return u.combine("type", b::typeKeys(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bool expectedResult = b::isConvertibleToFloatType(bt::scalarTypeOf(ty));
        const std::string returnType = b::concreteTypeOfFloat(ty).toString();
        b::validateConstOrOverrideBuiltinEval(
            t, "smoothstep", expectedResult,
            {b::createBuiltinValue(ty, b::RangeValue::makeI(0)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(1)),
             b::createBuiltinValue(ty, b::RangeValue::makeI(2))},
            "constant", returnType);
    });

struct ParamTest {
    const char* name;
    const char* src;
    bool pass;
};
static const std::vector<ParamTest>& kTests() {
    static const std::vector<ParamTest> v = {
        {"valid", "_ = smoothstep(0.0, 42.0, 0.5);", true},
        {"alias", "_ = smoothstep(f32_alias(0), f32_alias(42), f32_alias(0.5));", true},
        {"bool", "_ = smoothstep(false, false, false);", false},
        {"i32", "_ = smoothstep(1i, 2i, 1i);", false},
        {"u32", "_ = smoothstep(1u, 2u, 1u);", false},
        {"f32", "_ = smoothstep(1.0f, 2.0f, 1.0f);", true},
        {"f16", "_ = smoothstep(1h, 2h, 1h);", true},
        {"mixed_aint_afloat", "_ = smoothstep(1.0, 2, 1);", true},
        {"mixed_f32_afloat", "_ = smoothstep(1.0f, 2.0, 1.0);", true},
        {"mixed_f16_afloat", "_ = smoothstep(1.0h, 2.0, 1.0);", true},
        {"vec_bool",
         "_ = smoothstep(vec2<bool>(false, true), vec2<bool>(false, true), vec2<bool>(false, "
         "true));",
         false},
        {"vec_i32", "_ = smoothstep(vec2<i32>(1, 1), vec2<i32>(1, 1), vec2<i32>(1, 1));", false},
        {"vec_u32", "_ = smoothstep(vec2<u32>(1, 1), vec2<u32>(1, 1), vec2<u32>(1, 1));", false},
        {"vec_f32", "_ = smoothstep(vec2<f32>(0, 0), vec2<f32>(1, 1), vec2<f32>(1, 1));", true},
        {"matrix", "_ = smoothstep(mat2x2(1, 1, 1, 1), mat2x2(1, 1, 1, 1), mat2x2(1, 1, 1, 1));",
         false},
        {"atomic", " _ = smoothstep(a, a, a);", false},
        {"array", "var a: array<bool, 5>;\n            _ = smoothstep(a, a, a);", false},
        {"array_runtime", "_ = smoothstep(k.arry, k.arry, k.arry);", false},
        {"struct", "var a: A;\n            _ = smoothstep(a, a, a);", false},
        {"enumerant", "_ = smoothstep(read_write, read_write, read_write);", false},
        {"ptr",
         "var<function> a = 1.0;\n            let p: ptr<function, f32> = &a;\n            _ = "
         "smoothstep(p, p, p);",
         false},
        {"ptr_deref",
         "var<function> a = 1.0;\n            let p: ptr<function, f32> = &a;\n            _ = "
         "smoothstep(*p, *p, *p);",
         true},
        {"sampler", "_ = smoothstep(s, s, s);", false},
        {"texture", "_ = smoothstep(t, t, t);", false},
        {"no_args", "_ = smoothstep();", false},
        {"too_few_args", "_ = smoothstep(1.0, 2.0);", false},
        {"too_many_args", "_ = smoothstep(1.0, 2.0, 3.0, 4.0);", false},
        {"must_use", "smoothstep(1.0,2.0,3.0);", false},
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
    .desc("Test that smoothstep is validated correctly when called with different arguments.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string testName = t.param<std::string>("test");
        const ParamTest& pt = findTest(testName);
        const std::string enables =
            testName.find("f16") != std::string::npos ? "enable f16;" : "";
        const std::string code =
            std::string("\n  ") + enables +
            "\n  alias f32_alias = f32;\n\n"
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

CTS_TEST(g, "early_eval_errors")
    .desc("Validates that high must be greater than low")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"low", Value(int64_t(1))}, {"high", Value(int64_t(2))}},
                ParamRecord{{"low", Value(int64_t(2))}, {"high", Value(int64_t(1))}},
                ParamRecord{{"low", Value(int64_t(1))}, {"high", Value(int64_t(1))}},
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const int64_t low = t.param<int64_t>("low");
        const int64_t high = t.param<int64_t>("high");
        const bt::Type f32t = bt::scalar(bt::ScalarKind::F32);
        b::validateConstOrOverrideBuiltinEval(
            t, "smoothstep", /*expectedResult=*/low != high,
            {b::createBuiltinValue(f32t, b::RangeValue::makeI(low)),
             b::createBuiltinValue(f32t, b::RangeValue::makeI(high)),
             b::createBuiltinValue(f32t, b::RangeValue::makeI(0))},
            stage);
    });

}  // namespace
