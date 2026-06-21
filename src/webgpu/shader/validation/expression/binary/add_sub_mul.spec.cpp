// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/add_sub_mul.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kAllScalarsAndVectors / kConcreteNumericScalarsAndVectors are Type tables keyed
// by Type.toString() (vectors as `vecN<elementType>`); lhs/rhs params carry those
// keys and Types are reconstructed via typeByName(). The result-type annotation
// uses Type.toString(). kInvalidTypes is keyed by a scalar `type` name.
// scalar_vector_out_of_range uses the ported validateConstOrOverrideBinaryOpEval
// helper (const_override.h) and ports the ULP-boundary value computation
// (nextAfterF32/F16, reinterpretU32AsF32/U16AsF16) from the conversion/math utils.

#include <cmath>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/expression/binary/const_override.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,binary,add_sub_mul",
    "Validation tests for add/sub/mul expressions.");

// Selects the ULP-stepping function for the element type.
enum class NextAfterKind { Increment, F32, F16 };

// kOperators (object key order preserved).
struct ArithOp {
    const char* name;
    const char* op;
};
static const std::vector<ArithOp>& kOperators() {
    static const std::vector<ArithOp> v = {{"add", "+"}, {"sub", "-"}, {"mul", "*"}};
    return v;
}
static std::vector<Value> operatorNames() {
    std::vector<Value> values;
    for (const ArithOp& o : kOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}
static const char* findOp(const std::string& name) {
    for (const ArithOp& o : kOperators()) {
        if (name == o.name) {
            return o.op;
        }
    }
    return "";
}

CTS_TEST(g, "scalar_vector")
    .desc("Validates that scalar and vector expressions are only accepted for compatible numeric "
          "types.")
    .params([](ParamsBuilder u) {
        return u.combine("lhs", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("rhs", bt::typeNamesNoVec34(bt::kAllScalarsAndVectors()))
            .combine("compound_assignment", {Value(false), Value(true)})
            .beginSubcases()
            .combine("op", operatorNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string opStr = findOp(t.param<std::string>("op"));
        const bt::Type lhs = bt::typeByName(t.param<std::string>("lhs"));
        const bt::Type rhs = bt::typeByName(t.param<std::string>("rhs"));
        const bt::Type lhsElement = bt::scalarTypeOf(lhs);
        const bt::Type rhsElement = bt::scalarTypeOf(rhs);
        const bool hasBool =
            lhsElement.kind == bt::ScalarKind::Bool || rhsElement.kind == bt::ScalarKind::Bool;
        const bool hasF16 =
            lhsElement.kind == bt::ScalarKind::F16 || rhsElement.kind == bt::ScalarKind::F16;
        const bool compound = t.param<bool>("compound_assignment");

        bt::Type resType;
        const bool hasResType = bt::resultType(lhs, rhs, /*canConvertScalarToVector=*/true, resType);
        const bool resTypeIsTypeable = hasResType && !bt::isAbstractType(bt::scalarTypeOf(resType));

        std::string code;
        if (compound) {
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nfn f() {\n  var v = " +
                   bt::createWgsl(lhs, 0) + ";\n  v " + opStr + "= " + bt::createWgsl(rhs, 0) +
                   ";\n}\n";
        } else {
            const std::string anno = resTypeIsTypeable ? (" : " + resType.toString()) : "";
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nconst lhs = " +
                   bt::createWgsl(lhs, 0) + ";\nconst rhs = " + bt::createWgsl(rhs, 0) +
                   ";\nconst foo" + anno + " = lhs " + opStr + " rhs;\n";
        }

        bool valid = !hasBool && hasResType;
        if (valid && compound) {
            valid = valid && bt::isConvertible(resType, bt::concreteTypeOf(lhs));
        }
        t.expectCompileResult(valid, code);
    });

// ---- scalar_vector_out_of_range --------------------------------------------
CTS_TEST(g, "scalar_vector_out_of_range")
    .desc("Checks that constant or override evaluation of add/sub/mul operations on scalar/vectors "
          "that produce out of range values cause validation errors.")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            .combine("lhs", bt::typeNames(bt::kConcreteNumericScalarsAndVectors()))
            .expand("rhs",
                    [](const ParamRecord& p) {
                        const std::string lhsName =
                            valueAs<std::string>(*findParam(p, "lhs"));
                        const bt::Type lt = bt::typeByName(lhsName);
                        std::vector<Value> out;
                        out.emplace_back(lhsName);
                        if (lt.isVector()) {
                            out.emplace_back(bt::scalarTypeOf(lt).toString());
                        }
                        return out;
                    })
            .beginSubcases()
            .expand("swap",
                    [](const ParamRecord& p) {
                        const std::string lhsName =
                            valueAs<std::string>(*findParam(p, "lhs"));
                        const std::string rhsName =
                            valueAs<std::string>(*findParam(p, "rhs"));
                        std::vector<Value> out;
                        out.emplace_back(false);
                        if (lhsName != rhsName) {
                            out.emplace_back(true);
                        }
                        return out;
                    })
            .combine("nonZeroIndex", {Value(0), Value(1), Value(2), Value(3)})
            .filter([](const ParamRecord& p) {
                const std::string lhsName = valueAs<std::string>(*findParam(p, "lhs"));
                const int64_t idx = valueAs<int64_t>(*findParam(p, "nonZeroIndex"));
                const bt::Type lt = bt::typeByName(lhsName);
                if (lt.isVector()) {
                    return static_cast<int64_t>(lt.width) > idx;
                }
                return idx == 0;
            })
            .combine("valueCase", {"halfmax", "halfmax+ulp", "sqrtmax", "sqrtmax+ulp"})
            .combine("stage", {"constant", "override"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const std::string valueCase = t.param<std::string>("valueCase");
        const int64_t nonZeroIndex = t.param<int64_t>("nonZeroIndex");
        const bool swap = t.param<bool>("swap");
        std::string lhsName = t.param<std::string>("lhs");
        std::string rhsName = t.param<std::string>("rhs");
        const std::string stage = t.param<std::string>("stage");

        const bt::Type elementType = bt::scalarTypeOf(bt::typeByName(lhsName));

        if (swap) {
            std::swap(lhsName, rhsName);
        }

        // Maximum representable value + ULP stepping for the element type.
        double maxValue = 0.0;
        bool outOfRangeIsError = false;
        NextAfterKind nextAfter = NextAfterKind::Increment;
        switch (elementType.kind) {
            case bt::ScalarKind::F16:
                maxValue = bt::reinterpretU16AsF16(0x7bff);  // kBit.f16.positive.max
                nextAfter = NextAfterKind::F16;
                outOfRangeIsError = true;
                break;
            case bt::ScalarKind::F32:
                maxValue = bt::reinterpretU32AsF32(0x7f7fffff);  // kBit.f32.positive.max
                nextAfter = NextAfterKind::F32;
                outOfRangeIsError = true;
                break;
            case bt::ScalarKind::U32:
                maxValue = 4294967295.0;  // kBit.u32.max
                break;
            case bt::ScalarKind::I32:
                maxValue = 2147483647.0;  // kBit.i32.positive.max
                break;
            default:
                break;
        }

        auto applyNextAfter = [&](double v) -> double {
            switch (nextAfter) {
                case NextAfterKind::F16:
                    return bt::nextAfterF16Positive(v);
                case NextAfterKind::F32:
                    return bt::nextAfterF32Positive(v);
                default:
                    return v + 1.0;
            }
        };

        double value = 0.0;
        if (valueCase == "halfmax") {
            value = std::floor(maxValue / 2.0);
        } else if (valueCase == "halfmax+ulp") {
            value = applyNextAfter(std::ceil(maxValue / 2.0));
        } else if (valueCase == "sqrtmax") {
            value = std::floor(std::sqrt(maxValue));
        } else if (valueCase == "sqrtmax+ulp") {
            value = applyNextAfter(std::ceil(std::sqrt(maxValue)));
        }

        double computedValue = 0.0;
        double leftValue = value;
        const double rightValue = value;
        if (op == "add") {
            computedValue = value + value;
        } else if (op == "sub") {
            computedValue = -value - value;
            leftValue = -value;
        } else if (op == "mul") {
            computedValue = value * value;
        }

        // Quantize element values for float kinds (mirrors type.create()).
        auto quantize = [&](double v) -> double {
            switch (elementType.kind) {
                case bt::ScalarKind::F16:
                    return bt::quantizeToF16(v);
                case bt::ScalarKind::F32:
                    return bt::quantizeToF32(v);
                default:
                    return v;
            }
        };

        const bt::Type lhsType = bt::typeByName(lhsName);
        const bt::Type rhsType = bt::typeByName(rhsName);
        // create(): vector filled with 0, value at nonZeroIndex; scalar = value.
        const bt::EvalValue leftVal = bt::createIndexed(
            lhsType, static_cast<int>(nonZeroIndex), quantize(leftValue), 0.0);
        const bt::EvalValue rightVal = bt::createIndexed(
            rhsType, static_cast<int>(nonZeroIndex), quantize(rightValue), 0.0);

        const bool success = std::abs(computedValue) <= maxValue || !outOfRangeIsError;
        bt::validateConstOrOverrideBinaryOpEval(t, findOp(op), success, stage, leftVal, stage,
                                                rightVal);
    });

// ---- invalid_type_with_itself ----------------------------------------------
struct InvalidType {
    const char* name;
    const char* expr;
};
static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"array", "arr"},  {"ptr", "(&u)"}, {"atomic", "a"},
        {"texture", "t"}, {"sampler", "s"}, {"struct", "str"},
    };
    return v;
}
static std::vector<Value> invalidTypeNames() {
    std::vector<Value> values;
    for (const InvalidType& it : kInvalidTypes()) {
        values.emplace_back(std::string(it.name));
    }
    return values;
}
static std::string invalidControl(const std::string& name, const std::string& e) {
    if (name == "array") return e + "[0]";
    if (name == "ptr") return "*" + e;
    if (name == "atomic") return "atomicLoad(&" + e + ")";
    if (name == "texture") return "i32(textureLoad(" + e + ", vec2(), 0).x)";
    if (name == "sampler") return "i32(textureSampleLevel(t, " + e + ", vec2(), 0).x)";
    if (name == "struct") return e + ".u";
    return e;
}
static const InvalidType& findInvalidType(const std::string& name) {
    for (const InvalidType& it : kInvalidTypes()) {
        if (name == it.name) {
            return it;
        }
    }
    static const InvalidType dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "invalid_type_with_itself")
    .desc("Validates that expressions are never accepted for non-scalar, non-vector, and non-matrix "
          "types.")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            .combine("type", invalidTypeNames())
            .combine("control", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string opStr = findOp(t.param<std::string>("op"));
        const InvalidType& it = findInvalidType(t.param<std::string>("type"));
        const bool control = t.param<bool>("control");
        const std::string expr = control ? invalidControl(it.name, it.expr) : std::string(it.expr);
        const std::string code =
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<i32>;"
            "\n\nstruct S { u : u32 }"
            "\n\nvar<private> u : u32;"
            "\nvar<private> m : mat2x2f;"
            "\nvar<private> arr : array<i32, 4>;"
            "\nvar<private> str : S;"
            "\n\n@compute @workgroup_size(1)\nfn main() {\n  let foo = " +
            expr + " " + opStr + " " + expr + ";\n}\n";
        t.expectCompileResult(control, code);
    });

}  // namespace
