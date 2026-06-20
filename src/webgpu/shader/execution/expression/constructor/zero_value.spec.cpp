// Ported from gpuweb/cts src/webgpu/shader/execution/expression/constructor/zero_value.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for zero value constructors. The 'structure' g.test is deferred to Stage B
// (it needs module-scope struct predeclaration support in the harness).

#include <cstdint>
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

// NOTE: g.test('structure') is DEFERRED to Stage B (it needs module-scope struct predeclaration in
// the harness).

} // namespace
