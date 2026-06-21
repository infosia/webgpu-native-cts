// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/div_rem.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kAllScalarsAndVectors / kConcreteNumericScalarsAndVectors are Type tables keyed
// by Type.toString() (vectors as `vecN<elementType>`); lhs/rhs carry those keys
// and Types are reconstructed via typeByName(). The result-type annotation uses
// Type.toString(). kInvalidTypes is keyed by a scalar `type` name.
// scalar_vector_out_of_range uses the ported validateConstOrOverrideBinaryOpEval
// helper (const_override.h).
//
// PORT NOTE — scalar_vector_out_of_range subcase expansion: upstream uses
// `.expandWithParams(p => cases)` to emit subcases carrying the four columns
// leftValue/rightValue/error/leftRuntime, whose VALUES depend on the case-level
// `lhs`/`rhs`. The C++ harness has no data-dependent multi-key record expander,
// so this port emits one discriminator subcase column `divCase=<index>` (the
// per-(lhs) base-case index) and reconstructs leftValue/rightValue/error/
// leftRuntime in the body. The CASE-level query (op,lhs,rhs) and the case COUNT
// are identical to upstream; only the subcase-name spelling differs (subcases
// share the case query, so test identity/expectations are unaffected).

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
    "shader,validation,expression,binary,div_rem",
    "Validation tests for division and remainder expressions.");

// kOperators (object key order preserved).
struct DivOp {
    const char* name;
    const char* op;
};
static const std::vector<DivOp>& kOperators() {
    static const std::vector<DivOp> v = {{"div", "/"}, {"rem", "%"}};
    return v;
}
static std::vector<Value> operatorNames() {
    std::vector<Value> values;
    for (const DivOp& o : kOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}
static const char* findOp(const std::string& name) {
    for (const DivOp& o : kOperators()) {
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
            .combine("op", operatorNames())
            .combine("rhs_value", {Value(0), Value(1)});
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
        const int64_t rhsValue = t.param<int64_t>("rhs_value");

        bt::Type resType;
        const bool hasResType = bt::resultType(lhs, rhs, /*canConvertScalarToVector=*/true, resType);
        const bool resTypeIsTypeable = hasResType && !bt::isAbstractType(bt::scalarTypeOf(resType));

        std::string code;
        if (compound) {
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nfn f() {\n  var v = " +
                   bt::createWgsl(lhs, 0) + ";\n  v " + opStr + "= " +
                   bt::createWgsl(rhs, rhsValue) + ";\n}\n";
        } else {
            const std::string anno = resTypeIsTypeable ? (" : " + resType.toString()) : "";
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nconst lhs = " +
                   bt::createWgsl(lhs, 1) + ";\nconst rhs = " + bt::createWgsl(rhs, rhsValue) +
                   ";\nconst foo" + anno + " = lhs " + opStr + " rhs;\n";
        }

        const bt::Type scalarLHS = bt::scalarTypeOf(bt::concreteTypeOf(lhs));
        const bool integral =
            scalarLHS.kind == bt::ScalarKind::U32 || scalarLHS.kind == bt::ScalarKind::I32;
        bool valid = !hasBool && hasResType;
        if (valid && compound) {
            valid = valid && bt::isConvertible(resType, bt::concreteTypeOf(lhs)) &&
                    (!integral || rhsValue == 1);
        } else {
            valid = valid && rhsValue == 1;
        }
        t.expectCompileResult(valid, code);
    });

// ---- scalar_vector_out_of_range --------------------------------------------
// A base out-of-range case (per the upstream expandWithParams closure).
struct DivCase {
    double leftValue;
    double rightValue;
    bool error;
    bool leftRuntime;
};

// Reconstruct the base-case list for a given (lhs key, rhs key), mirroring the
// upstream `expandWithParams(p => cases)` closure.
static std::vector<DivCase> divCases(const std::string& lhsName, const std::string& rhsName) {
    const bt::Type rhsScalar = bt::scalarTypeOf(bt::typeByName(rhsName));
    const bool partialDivByZeroIsError =
        rhsScalar.kind == bt::ScalarKind::I32 || rhsScalar.kind == bt::ScalarKind::U32;
    std::vector<DivCase> cases = {
        {42, 0, true, false},
        {42, 0, partialDivByZeroIsError, true},
        {0, 0, partialDivByZeroIsError, true},
        {0, 42, false, false},
    };
    if (lhsName == "i32") {
        // -kBit.i32.negative.min = -2147483648; +1 = -2147483647.
        cases.push_back({-2147483648.0, -1, true, false});
        cases.push_back({-2147483647.0, -1, false, false});
    }
    return cases;
}

CTS_TEST(g, "scalar_vector_out_of_range")
    .desc("Checks that constant or override evaluation of div/rem operations on scalar/vectors that "
          "produce out of division by 0 or out of range values cause validation errors.")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            .combine("lhs", bt::typeNames(bt::kConcreteNumericScalarsAndVectors()))
            .expand("rhs",
                    [](const ParamRecord& p) {
                        const std::string lhsName = valueAs<std::string>(*findParam(p, "lhs"));
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
                        const std::string lhsName = valueAs<std::string>(*findParam(p, "lhs"));
                        const std::string rhsName = valueAs<std::string>(*findParam(p, "rhs"));
                        std::vector<Value> out;
                        out.emplace_back(false);
                        if (lhsName != rhsName) {
                            out.emplace_back(true);
                        }
                        return out;
                    })
            .combine("nonOneIndex", {Value(0), Value(1), Value(2), Value(3)})
            .filter([](const ParamRecord& p) {
                const std::string lhsName = valueAs<std::string>(*findParam(p, "lhs"));
                const int64_t idx = valueAs<int64_t>(*findParam(p, "nonOneIndex"));
                const bt::Type lt = bt::typeByName(lhsName);
                if (lt.isVector()) {
                    return static_cast<int64_t>(lt.width) > idx;
                }
                return idx == 0;
            })
            .expand("divCase",
                    [](const ParamRecord& p) {
                        const std::string lhsName = valueAs<std::string>(*findParam(p, "lhs"));
                        const std::string rhsName = valueAs<std::string>(*findParam(p, "rhs"));
                        const size_t n = divCases(lhsName, rhsName).size();
                        std::vector<Value> out;
                        for (size_t i = 0; i < n; ++i) {
                            out.emplace_back(static_cast<int64_t>(i));
                        }
                        return out;
                    })
            .combine("stage", {"constant", "override"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const int64_t nonOneIndex = t.param<int64_t>("nonOneIndex");
        const bool swap = t.param<bool>("swap");
        std::string lhsName = t.param<std::string>("lhs");
        std::string rhsName = t.param<std::string>("rhs");
        const std::string stage = t.param<std::string>("stage");
        const int64_t divCaseIdx = t.param<int64_t>("divCase");

        const std::vector<DivCase> cases = divCases(lhsName, rhsName);
        const DivCase& c = cases[static_cast<size_t>(divCaseIdx)];
        const double leftValue = c.leftValue;
        const double rightValue = c.rightValue;
        const bool error = c.error;
        const bool leftRuntime = c.leftRuntime;

        if (swap) {
            std::swap(lhsName, rhsName);
        }

        const bt::Type lhsType = bt::typeByName(lhsName);
        const bt::Type rhsType = bt::typeByName(rhsName);
        // create(): vector filled with 1, value at nonOneIndex; scalar = value.
        const bt::EvalValue leftVal =
            bt::createIndexed(lhsType, static_cast<int>(nonOneIndex), leftValue, 1.0);
        const bt::EvalValue rightVal =
            bt::createIndexed(rhsType, static_cast<int>(nonOneIndex), rightValue, 1.0);

        const std::string leftStage = leftRuntime ? std::string("runtime") : stage;
        bt::validateConstOrOverrideBinaryOpEval(t, findOp(op), !error, leftStage, leftVal, stage,
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
