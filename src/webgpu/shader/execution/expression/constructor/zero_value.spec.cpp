// Ported from gpuweb/cts src/webgpu/shader/execution/expression/constructor/zero_value.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for zero value constructors. The 'structure' g.test uses the harness's module-
// scope predeclaration support (runWithPredeclaration). NOTE: upstream's 'member_types' param is an
// array of type names; Value cannot hold an array, so it is encoded as a comma-delimited string
// (e.g. "i32,u32"). This changes only the spelling of the member_types query component; the test
// logic and case/subcase structure are identical.

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/constructor/constructor_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::constructor;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,constructor,zero_value",
    "Execution Tests for zero value constructors");

void maybeSkipF16(AllFeaturesMaxLimitsGpuTest& t, ScalarKind k) {
    if (k == ScalarKind::F16 && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// Builds an all-zero CaseValue of the given type.
CaseValue zeroValue(const ExprType& ty) {
    int slots = 1;
    if (ty.form == TypeForm::Matrix) {
        slots = ty.cols * ty.width;
    } else if (ty.form == TypeForm::Array) {
        // array<element, count>: element scalar slots * count.
        int es = 1;
        const ExprType& el = *ty.element;
        if (el.form == TypeForm::ScalarVec) {
            es = el.width;
        } else if (el.form == TypeForm::Matrix) {
            es = el.cols * el.width;
        }
        slots = es * ty.count;
    } else {
        slots = ty.width;
    }
    std::vector<Scalar> els;
    const ScalarKind sk = ty.scalarKind();
    for (int i = 0; i < slots; ++i) {
        els.push_back(scalarOfInt(sk, 0));
    }
    CaseValue v;
    v.width = slots;
    v.elements = els;
    return v;
}

// An ExpressionBuilder that ignores its operands and emits a fixed literal expression.
ExpressionBuilder fixedExpr(const std::string& s) {
    return [s](const std::vector<std::string>&) { return s; };
}

CTS_TEST(testGroup, "scalar")
    .params([](ParamsBuilder u) { return u.combine("type", {"bool", "i32", "u32", "f32", "f16"}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const ExprType ty = scalarType(k);
        run(t, fixedExpr(wgslTypeName(ty) + "()"), {}, ty, InputSource::Const, 0,
            {Case({}, zeroValue(ty))});
    });

CTS_TEST(testGroup, "vector")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "i32", "u32", "f32", "f16"})
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ExprType ty = vecType(width, k);
        run(t, fixedExpr(wgslTypeName(ty) + "()"), {}, ty, InputSource::Const, 0,
            {Case({}, zeroValue(ty))});
    });

CTS_TEST(testGroup, "vector_prefix")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"i32", "u32", "f32", "f16"})
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ExprType ty = vecType(width, k);
        run(t, fixedExpr("vec" + std::to_string(width) + "()"), {}, ty, InputSource::Const, 0,
            {Case({}, zeroValue(ty))});
    });

CTS_TEST(testGroup, "matrix")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"f32", "f16"})
            .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                 Value(static_cast<int64_t>(4))})
            .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                              Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        const ExprType ty = matType(cols, rows, k);
        run(t, fixedExpr(wgslTypeName(ty) + "()"), {}, ty, InputSource::Const, 0,
            {Case({}, zeroValue(ty))});
    });

CTS_TEST(testGroup, "array")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "i32", "u32", "f32", "f16", "vec3f", "vec4i"})
            .combine("length", {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(5)),
                                Value(static_cast<int64_t>(10))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const int length = static_cast<int>(t.param<int64_t>("length"));
        ExprType element;
        if (typeName == "vec3f") {
            element = vecType(3, ScalarKind::F32);
        } else if (typeName == "vec4i") {
            element = vecType(4, ScalarKind::I32);
        } else {
            element = scalarType(scalarKind(typeName));
        }
        maybeSkipF16(t, element.scalarKind());
        const ExprType ty = arrayType(length, element);
        run(t, fixedExpr(wgslTypeName(ty) + "()"), {}, ty, InputSource::Const, 0,
            {Case({}, zeroValue(ty))});
    });

// Parse a member type name into an ExprType (scalar/vec/mat) used by the 'structure' test.
ExprType memberExprType(const std::string& name) {
    if (name == "vec3f") {
        return vecType(3, ScalarKind::F32);
    }
    if (name == "vec4i") {
        return vecType(4, ScalarKind::I32);
    }
    if (name == "vec2i") {
        return vecType(2, ScalarKind::I32);
    }
    if (name == "mat3x2f") {
        return matType(3, 2, ScalarKind::F32);
    }
    return scalarType(scalarKind(name));
}

// Split "a,b,c" into {"a","b","c"}.
std::vector<std::string> splitMemberTypes(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        out.push_back(item);
    }
    return out;
}

bool containsF16(const std::vector<std::string>& types) {
    for (const std::string& t : types) {
        if (t == "f16") {
            return true;
        }
    }
    return false;
}

CTS_TEST(testGroup, "structure")
    .params([](ParamsBuilder u) {
        return u.combine("member_types", {"bool", "u32", "vec3f", "i32,u32",
                                          "i32,f16,vec4i,mat3x2f", "bool,u32,f16,vec3f,vec2i",
                                          "i32,u32,f32,f16,vec3f,vec4i"})
            .combine("nested", {false, true})
            .beginSubcases()
            .expand("member_index", [](const ParamRecord& p) {
                const Value* v = findParam(p, "member_types");
                std::vector<Value> idx;
                if (v != nullptr) {
                    const size_t n = splitMemberTypes(valueAs<std::string>(*v)).size();
                    for (size_t i = 0; i < n; ++i) {
                        idx.push_back(Value(static_cast<int64_t>(i)));
                    }
                }
                return idx;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::vector<std::string> types = splitMemberTypes(t.param<std::string>("member_types"));
        const bool nested = t.param<bool>("nested");
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        if (containsF16(types) && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const ExprType memberType = memberExprType(types[static_cast<size_t>(memberIndex)]);

        // Build the struct predeclaration (placed at module scope; WGSL is order-independent).
        std::string members;
        for (size_t i = 0; i < types.size(); ++i) {
            members += "  member_" + std::to_string(i) + " : " + types[i] + ",\n";
        }
        std::string predeclaration = "struct MyStruct {\n" + members + "};\n" +
                                     "struct OuterStruct {\n  pre : i32,\n  inner : MyStruct,\n" +
                                     "  post : i32,\n};";

        const std::string expr =
            nested ? ("OuterStruct().inner.member_" + std::to_string(memberIndex))
                   : ("MyStruct().member_" + std::to_string(memberIndex));
        ExpressionBuilder fixed = [expr](const std::vector<std::string>&) { return expr; };

        runWithPredeclaration(t, fixed, predeclaration, {}, memberType, InputSource::Const, 0,
                              {Case({}, zeroValue(memberType))});
    });

} // namespace
