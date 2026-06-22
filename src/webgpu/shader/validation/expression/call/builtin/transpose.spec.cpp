// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/transpose.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,transpose",
    "Validation tests for the transpose() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of transpose() accept valid "
          "inputs.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::matrixKeys(b::kAllMatrices()))
            .beginSubcases()
            .expand("value", [](const ParamRecord& p) {
                const b::MatType mat = b::matByName(valueAs<std::string>(*findParam(p, "type")));
                // fullRangeForType over the element type.
                return b::rangeValues(b::fullRangeForType(bt::scalar(mat.kind)));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const b::MatType mat = b::matByName(t.param<std::string>("type"));
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        b::validateConstOrOverrideBuiltinEval(t, "transpose", /*expectedResult=*/true,
                                              {b::createMatrixValue(mat, rvNum(rv))}, stage);
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(mat2x2(0, 1, 2, 3))"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_2arg", "(1.2, 2.3)"},
        {"bad_0bool", "(false)"},
        {"bad_0array", "(array(1.1,2.2))"},
        {"bad_0struct", "(modf(2.2))"},
        {"bad_0uint", "(1u)"},
        {"bad_0int", "(1i)"},
        {"bad_0vec2i", "(vec2i())"},
        {"bad_0vec2u", "(vec2u())"},
        {"bad_0vec3i", "(vec3i())"},
        {"bad_0vec3u", "(vec3u())"},
        {"bad_0vec4i", "(vec4i())"},
        {"bad_0vec4u", "(vec4u())"},
    };
    return v;
}
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& a : kArgCases()) {
        out.emplace_back(std::string(a.name));
    }
    return out;
}
static const char* argSuffix(const std::string& name) {
    for (const ArgCase& a : kArgCases()) {
        if (name == a.name) {
            return a.suffix;
        }
    }
    return "";
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of transpose with variously typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good",
                              std::string("const c = transpose") + argSuffix(arg) + ";");
    });

// kValidArgumentScalarTypes = kFloatScalars (abstract-float, f32, f16).
// kValidReturnScalarTypes = kConcreteFloatScalars (f32, f16).
static std::string scalarCreateZeroWgsl(bt::ScalarKind k) {
    switch (k) {
        case bt::ScalarKind::AbstractFloat:
            return "0.0";
        case bt::ScalarKind::F32:
            return "0.0f";
        case bt::ScalarKind::F16:
            return "0.0h";
        default:
            return "0.0";
    }
}

CTS_TEST(g, "return")
    .desc("Test compilation pass/failure of transpose with variously shaped inputs and outputs")
    .params([](ParamsBuilder u) {
        const std::vector<Value> inputTypes = {Value(std::string("abstract-float")),
                                               Value(std::string("f32")), Value(std::string("f16"))};
        const std::vector<Value> outputTypes = {Value(std::string("f32")), Value(std::string("f16"))};
        const std::vector<Value> rc = {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))};
        return u.combine("input_type", inputTypes)
            .combine("input_rows", rc)
            .combine("input_cols", rc)
            .combine("output_type", outputTypes)
            .combine("output_rows", rc)
            .combine("output_cols", rc);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string inputType = t.param<std::string>("input_type");
        const int64_t inputRows = t.param<int64_t>("input_rows");
        const int64_t inputCols = t.param<int64_t>("input_cols");
        const std::string outputType = t.param<std::string>("output_type");
        const int64_t outputRows = t.param<int64_t>("output_rows");
        const int64_t outputCols = t.param<int64_t>("output_cols");

        const bt::Type argScalar = bt::typeByName(inputType);
        const std::string el = scalarCreateZeroWgsl(argScalar.kind);
        std::string inputValues;
        for (int64_t i = 0; i < inputCols * inputRows; ++i) {
            if (i) {
                inputValues += ", ";
            }
            inputValues += el;
        }
        const std::string inputStr = "mat" + std::to_string(inputCols) + "x" +
                                     std::to_string(inputRows) + "(" + inputValues + ")";

        const std::string enables =
            (inputType == "f16" || outputType == "f16") ? "enable f16;" : "";
        const bt::Type retScalar = bt::typeByName(outputType);
        const bool expectedResult = inputCols == outputRows && inputRows == outputCols &&
                                    bt::isConvertible(argScalar, retScalar);
        const std::string code = enables + "\nconst c: mat" + std::to_string(outputCols) + "x" +
                                 std::to_string(outputRows) + "<" + outputType + "> = transpose(" +
                                 inputStr + ");";
        t.expectCompileResult(expectedResult, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of transpose must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "transpose" +
                                       argSuffix("good") + "; }");
    });

}  // namespace
