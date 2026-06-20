// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/f32_conversion.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the f32 conversion operations (f32(e)). Source types: bool, u32, i32,
// abstract_int, f32, f16 (deferred), abstract_float, plus the matrix variants. Where the source is a
// float, the result is the f32-correctly-rounded interval; integer/bool sources convert exactly
// (correctly-rounded). f16 and f16_mat are skipped (shader-f16 has no Metal oracle).

#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"
#include "webgpu/shader/execution/expression/unary/unary_ranges_common.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,f32_conversion", "Execution Tests for the f32 conversion operations");

ParamsBuilder sourceVectorizeUndef(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder constVectorizeUndef(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}
ParamsBuilder constColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// vectorizeToExpression: f32(...) for scalar, vecN<f32>(...) for vectorize N.
ExpressionBuilder f32ConvExpr(int vectorize) {
    if (vectorize == 0) {
        return conversion("f32");
    }
    return conversion("vec" + std::to_string(vectorize) + "<f32>");
}
ExpressionBuilder matConvExpr(int cols, int rows) {
    return conversion("mat" + std::to_string(cols) + "x" + std::to_string(rows) + "<f32>");
}

const ExprType F32 = scalarType(ScalarKind::F32);

// correctlyRoundedMatrix op: each element correctlyRoundedInterval(F32, e).
fp::MatrixToMatrix correctlyRoundedMatrixOp() {
    return [](const std::vector<std::vector<double>>& m) {
        std::vector<std::vector<fp::FPInterval>> r;
        r.reserve(m.size());
        for (const std::vector<double>& col : m) {
            std::vector<fp::FPInterval> rc;
            rc.reserve(col.size());
            for (double e : col) {
                rc.push_back(fp::correctlyRoundedInterval(fp::FPKind::F32, e));
            }
            r.push_back(std::move(rc));
        }
        return r;
    };
}

} // namespace

CTS_TEST(g, "bool").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    {
        Case c;
        c.inputs.push_back(CaseValue(boolean(true)));
        c.expected = CaseValue(f32Bits(0x3f800000u)); // 1.0
        c.expectedAccept.push_back(acceptInterval(32, 1.0, 1.0));
        cases.push_back(std::move(c));
    }
    {
        Case c;
        c.inputs.push_back(CaseValue(boolean(false)));
        c.expected = CaseValue(f32Bits(0x00000000u)); // 0.0
        c.expectedAccept.push_back(acceptInterval(32, 0.0, 0.0));
        cases.push_back(std::move(c));
    }
    run(t, f32ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::Bool)}, F32, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "u32").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t v : unary_ranges::fullU32Range()) {
        Case c;
        c.inputs.push_back(CaseValue(u32(v)));
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F32, static_cast<double>(v));
        c.expected = CaseValue(f32Bits(0u));
        c.expectedAccept.push_back(acceptInterval(32, iv.begin, iv.end));
        cases.push_back(std::move(c));
    }
    run(t, f32ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::U32)}, F32, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "i32").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t v : unary_ranges::fullI32Range()) {
        Case c;
        c.inputs.push_back(CaseValue(i32(v)));
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F32, static_cast<double>(v));
        c.expected = CaseValue(f32Bits(0u));
        c.expectedAccept.push_back(acceptInterval(32, iv.begin, iv.end));
        cases.push_back(std::move(c));
    }
    run(t, f32ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::I32)}, F32, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_int").params(constVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    // f32 finite range: [-3.4028234663852886e38, 3.4028234663852886e38] (largest finite f32).
    const double f32Max = 3.4028234663852886e38;
    const double f32Min = -f32Max;
    for (int64_t v : fp::fullI64Range()) {
        const double dv = static_cast<double>(v);
        if (!(dv >= f32Min && dv <= f32Max)) {
            continue; // f32FiniteRangeInterval.contains(v)
        }
        Case c;
        c.inputs.push_back(CaseValue(abstractInt64(v)));
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F32, dv);
        c.expected = CaseValue(f32Bits(0u));
        c.expectedAccept.push_back(acceptInterval(32, iv.begin, iv.end));
        cases.push_back(std::move(c));
    }
    run(t, f32ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::AbstractInt)}, F32,
        InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, fp::scalarF32Range(), /*finite=*/false,
        [](double n) { return fp::correctlyRoundedInterval(fp::FPKind::F32, n); });
    run(t, f32ConvExpr(cfgVectorize(t)), {F32}, F32, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f32_mat").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F32);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateMatrixToMatrixCases(fp::FPKind::F32, fp::FPKind::F32,
                                                 fp::sparseMatrixF32Range(cols, rows),
                                                 /*finite=*/isConst, correctlyRoundedMatrixOp());
    run(t, matConvExpr(cols, rows), {mt}, mt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    // f32(e) where e is f16: all f16 values are exactly representable in f32. Input is f16, output is
    // f32 (correctly-rounded interval, width 32). Built manually for the distinct in/out kinds.
    std::vector<Case> cases;
    for (double n : fp::scalarF16Range()) {
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F32, n);
        Case c;
        c.inputs.push_back(CaseValue(fp::scalarInput(fp::FPKind::F16, n)));
        c.expected = CaseValue(f32Bits(0u));
        c.expectedAccept.push_back(acceptInterval(32, iv.begin, iv.end));
        cases.push_back(std::move(c));
    }
    run(t, f32ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::F16)}, F32, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "f16_mat").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mtIn = matType(cols, rows, ScalarKind::F16);
    const ExprType mtOut = matType(cols, rows, ScalarKind::F32);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    // f16 matrix -> f32 matrix: input f16, output f32, each element correctly-rounded to f32.
    auto cases = fp::generateMatrixToMatrixCases(fp::FPKind::F16, fp::FPKind::F32,
                                                 fp::sparseMatrixF16Range(cols, rows),
                                                 /*finite=*/isConst, correctlyRoundedMatrixOp());
    run(t, matConvExpr(cols, rows), {mtIn}, mtOut, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "abstract_float").params(constVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // f32(e) where e is AbstractFloat: correctly-rounded to f32. Input is abstract-float (f64);
    // output is f32 (interval width 32). 'finite' filter drops cases whose f32 interval is non-finite.
    std::vector<Case> f32Cases;
    for (double n : fp::scalarF64Range()) {
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F32, n);
        if (!iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(abstractFloatValue(n)));
        c.expected = CaseValue(f32Bits(0u));
        c.expectedAccept.push_back(acceptInterval(32, iv.begin, iv.end));
        f32Cases.push_back(std::move(c));
    }
    run(t, f32ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::AbstractFloat)}, F32,
        InputSource::Const, cfgVectorize(t), f32Cases);
});

CTS_TEST(g, "abstract_float_mat").params(constColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mtIn = matType(cols, rows, ScalarKind::AbstractFloat);
    const ExprType mtOut = matType(cols, rows, ScalarKind::F32);
    auto cases = fp::generateMatrixToMatrixCases(fp::FPKind::Abstract, fp::FPKind::F32,
                                                 fp::sparseMatrixF64Range(cols, rows),
                                                 /*finite=*/true, correctlyRoundedMatrixOp());
    run(t, matConvExpr(cols, rows), {mtIn}, mtOut, InputSource::Const, 0, cases);
});
