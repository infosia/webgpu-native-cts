// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/and_or_xor.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kAllScalarsAndVectors is a Type table keyed by Type.toString() (vectors
// stringify as `vecN<elementType>`); lhs/rhs params carry those key strings and
// the Type is reconstructed via typeByName(). The result-type annotation uses
// Type.toString(). kInvalidTypes (expr + control transform) is keyed by a scalar
// `type` name and reconstructed in the body. See binary_types.h.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,binary,and_or_xor",
    "Validation tests for logical and bitwise and/or/xor expressions.");

// kOperators (object key order preserved).
struct AndOrXorOp {
    const char* name;
    const char* op;
    bool supportsBool;
};

static const std::vector<AndOrXorOp>& kOperators() {
    static const std::vector<AndOrXorOp> v = {
        {"and", "&", true},
        {"or", "|", true},
        {"xor", "^", false},
    };
    return v;
}

static std::vector<Value> operatorNames() {
    std::vector<Value> values;
    for (const AndOrXorOp& o : kOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}

static const AndOrXorOp& findOperator(const std::string& name) {
    for (const AndOrXorOp& o : kOperators()) {
        if (name == o.name) {
            return o;
        }
    }
    static const AndOrXorOp dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "scalar_vector")
    .desc("Validates that scalar and vector expressions are only accepted for bool or compatible "
          "integer types.")
    .params([](ParamsBuilder u) {
        return u.combine("lhs", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("rhs", bt::typeNamesNoVec34(bt::kAllScalarsAndVectors()))
            .combine("compound_assignment", {Value(false), Value(true)})
            .beginSubcases()
            .combine("op", operatorNames());
    })
    .fn([](ShaderValidationTest& t) {
        const AndOrXorOp& op = findOperator(t.param<std::string>("op"));
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
        const bool hasResType =
            ((bt::isIntegerType(lhsElement) && bt::isIntegerType(rhsElement)) ||
             (hasBool && op.supportsBool))
                ? bt::resultType(lhs, rhs, /*canConvertScalarToVector=*/false, resType)
                : false;
        const bool resTypeIsTypeable = hasResType && !bt::isAbstractType(bt::scalarTypeOf(resType));

        std::string code;
        if (compound) {
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nfn f() {\n  var foo = " +
                   bt::createWgsl(lhs, 0) + ";\n  foo " + op.op + "= " + bt::createWgsl(rhs, 0) +
                   ";\n}\n";
        } else {
            const std::string anno = resTypeIsTypeable ? (" : " + resType.toString()) : "";
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nconst lhs = " +
                   bt::createWgsl(lhs, 0) + ";\nconst rhs = " + bt::createWgsl(rhs, 0) +
                   ";\nconst foo" + anno + " = lhs " + op.op + " rhs;\n";
        }

        bool valid = hasResType;
        if (valid && compound) {
            valid = valid && bt::isConvertible(resType, bt::concreteTypeOf(lhs));
        }
        t.expectCompileResult(valid, code);
    });

// kInvalidTypes (object key order preserved). control transforms applied in body.
struct InvalidType {
    const char* name;
    const char* expr;
};

static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"mat2x2f", "m"}, {"array", "arr"},  {"ptr", "(&u)"}, {"atomic", "a"},
        {"texture", "t"}, {"sampler", "s"},  {"struct", "str"},
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
    if (name == "mat2x2f") return "i32(" + e + "[0][0])";
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

CTS_TEST(g, "invalid_types")
    .desc("Validates that expressions are never accepted for non-scalar and non-vector types.")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            .combine("type", invalidTypeNames())
            .combine("control", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const AndOrXorOp& op = findOperator(t.param<std::string>("op"));
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
            expr + " " + op.op + " " + expr + ";\n}\n";
        t.expectCompileResult(control, code);
    });

}  // namespace
