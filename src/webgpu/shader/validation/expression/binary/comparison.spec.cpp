// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/comparison.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kAllScalarsAndVectors is a Type table keyed by Type.toString() (vectors
// stringify as `vecN<elementType>`); the lhs/rhs params carry those key strings
// and the Type is reconstructed via typeByName(). The kInvalidTypes config
// (expr + control transform) is keyed by a scalar `type` name and reconstructed
// in the body. See binary_types.h for the ported conversion.ts Type model.

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
    "shader,validation,expression,binary,comparison",
    "Validation tests for comparison expressions.");

// kComparisonOperators (object key order preserved).
struct CmpOp {
    const char* name;
    const char* op;
    bool supportsBool;
};

static const std::vector<CmpOp>& kComparisonOperators() {
    static const std::vector<CmpOp> v = {
        {"eq", "==", true}, {"ne", "!=", true},  {"gt", ">", false},
        {"ge", ">=", false}, {"lt", "<", false}, {"le", "<=", false},
    };
    return v;
}

static std::vector<Value> comparisonOpNames() {
    std::vector<Value> values;
    for (const CmpOp& o : kComparisonOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}

static const CmpOp& findCmpOp(const std::string& name) {
    for (const CmpOp& o : kComparisonOperators()) {
        if (name == o.name) {
            return o;
        }
    }
    static const CmpOp dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "scalar_vector")
    .desc("Validates that scalar and vector comparison expressions are only accepted for compatible "
          "types.")
    .params([](ParamsBuilder u) {
        return u.combine("lhs", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("rhs", bt::typeNamesNoVec34(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("op", comparisonOpNames());
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type lhs = bt::typeByName(t.param<std::string>("lhs"));
        const bt::Type rhs = bt::typeByName(t.param<std::string>("rhs"));
        const bt::Type lhsElement = bt::scalarTypeOf(lhs);
        const bt::Type rhsElement = bt::scalarTypeOf(rhs);
        const bool hasF16 =
            lhsElement.kind == bt::ScalarKind::F16 || rhsElement.kind == bt::ScalarKind::F16;
        const CmpOp& op = findCmpOp(t.param<std::string>("op"));
        const std::string code =
            std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nconst lhs = " +
            bt::createWgsl(lhs, 0) + ";\nconst rhs = " + bt::createWgsl(rhs, 0) +
            ";\nconst foo = lhs " + op.op + " rhs;\n";

        bool valid = false;

        // Determine if the element types are comparable.
        bool elementIsCompatible = false;
        if (lhsElement.kind == bt::ScalarKind::AbstractInt) {
            elementIsCompatible = rhsElement.kind != bt::ScalarKind::Bool;
        } else if (rhsElement.kind == bt::ScalarKind::AbstractInt) {
            elementIsCompatible = lhsElement.kind != bt::ScalarKind::Bool;
        } else if (lhsElement.kind == bt::ScalarKind::AbstractFloat) {
            elementIsCompatible = bt::isFloatType(rhsElement);
        } else if (rhsElement.kind == bt::ScalarKind::AbstractFloat) {
            elementIsCompatible = bt::isFloatType(lhsElement);
        } else {
            elementIsCompatible = lhsElement.kind == rhsElement.kind;
        }

        // Determine if the full type is comparable.
        if (lhs.isScalar() && rhs.isScalar()) {
            valid = elementIsCompatible;
        } else if (lhs.isVector() && rhs.isVector()) {
            valid = lhs.width == rhs.width && elementIsCompatible;
        }

        if (lhsElement.kind == bt::ScalarKind::Bool) {
            valid = valid && op.supportsBool;
        }

        t.expectCompileResult(valid, code);
    });

// kInvalidTypes (object key order preserved). control transforms applied in body.
struct InvalidType {
    const char* name;
    const char* expr;
    // control(e): see body switch reproducing the upstream arrow functions.
};

static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"mat2x2f", "m"}, {"array", "arr"},     {"ptr", "(&u)"}, {"atomic", "a"},
        {"texture", "t"}, {"sampler", "s"},     {"struct", "str"},
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

// control(name, e): mirror upstream kInvalidTypes[type].control.
static std::string invalidControl(const std::string& name, const std::string& e) {
    if (name == "mat2x2f") return e + "[0]";
    if (name == "array") return e + "[0]";
    if (name == "ptr") return "*" + e;
    if (name == "atomic") return "atomicLoad(&" + e + ")";
    if (name == "texture") return "textureLoad(" + e + ", vec2(), 0)";
    if (name == "sampler") return "textureSampleLevel(t, " + e + ", vec2(), 0)";
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
    .desc("Validates that comparison expressions are never accepted for non-scalar and non-vector "
          "types.")
    .params([](ParamsBuilder u) {
        return u.combine("op", comparisonOpNames())
            .combine("type", invalidTypeNames())
            .combine("control", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const CmpOp& op = findCmpOp(t.param<std::string>("op"));
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
