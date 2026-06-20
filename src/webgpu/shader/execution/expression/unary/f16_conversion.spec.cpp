// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/f16_conversion.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the f16 conversion operations (f16(e)). Source types: bool, u32, i32,
// abstract_int, f32, f16, abstract_float, plus the matrix variants. Where the source is a float, the
// result is the f16-correctly-rounded interval; integer/bool sources convert exactly
// (correctly-rounded to f16). Mirrors f32_conversion but produces f16; gated on the 'shader-f16'
// feature. The integer/abstract sources add the f16 extrema (65504, -65504) and (for const) filter to
// the f16 finite range; the float sources add 65535.996/-65535.996 (which round to f16 inf).

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"
#include "webgpu/shader/execution/expression/unary/unary_ranges_common.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,f16_conversion", "Execution Tests for the f16 conversion operations");

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
void requireF16(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// vectorizeToExpression: f16(...) for scalar, vecN<f16>(...) for vectorize N.
ExpressionBuilder f16ConvExpr(int vectorize) {
    if (vectorize == 0) {
        return conversion("f16");
    }
    return conversion("vec" + std::to_string(vectorize) + "<f16>");
}
ExpressionBuilder matConvExpr(int cols, int rows) {
    return conversion("mat" + std::to_string(cols) + "x" + std::to_string(rows) + "<f16>");
}

const ExprType F16 = scalarType(ScalarKind::F16);

// f16 finite range: [-65504, 65504] (largest finite f16). Mirrors f16FiniteRangeInterval.contains.
bool inF16Finite(double v) { return v >= fp::f16NegativeMin() && v <= fp::f16PositiveMax(); }

// Encode an f32 input value (quantized from a double) as its 32-bit pattern for f32Bits().
Scalar f32Input(double v) {
    const float f = static_cast<float>(fp::quantize(fp::FPKind::F32, v));
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return f32Bits(bits);
}

// Encode an f16 result interval onto a case (acceptInterval, or acceptUnbounded for the unbounded).
ExpectedElement f16Expected(const fp::FPInterval& iv) {
    if (!iv.isFinite()) {
        return acceptUnbounded(16);
    }
    return acceptInterval(16, iv.begin, iv.end);
}

// correctlyRoundedMatrix op: each element correctlyRoundedInterval(F16, e).
fp::MatrixToMatrix correctlyRoundedMatrixOp() {
    return [](const std::vector<std::vector<double>>& m) {
        std::vector<std::vector<fp::FPInterval>> r;
        r.reserve(m.size());
        for (const std::vector<double>& col : m) {
            std::vector<fp::FPInterval> rc;
            rc.reserve(col.size());
            for (double e : col) {
                rc.push_back(fp::correctlyRoundedInterval(fp::FPKind::F16, e));
            }
            r.push_back(std::move(rc));
        }
        return r;
    };
}

} // namespace

CTS_TEST(g, "bool").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    std::vector<Case> cases;
    {
        Case c;
        c.inputs.push_back(CaseValue(boolean(true)));
        c.expected = CaseValue(f16Bits(0x3c00u)); // 1.0
        c.expectedAccept.push_back(acceptInterval(16, 1.0, 1.0));
        cases.push_back(std::move(c));
    }
    {
        Case c;
        c.inputs.push_back(CaseValue(boolean(false)));
        c.expected = CaseValue(f16Bits(0x0000u)); // 0.0
        c.expectedAccept.push_back(acceptInterval(16, 0.0, 0.0));
        cases.push_back(std::move(c));
    }
    run(t, f16ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::Bool)}, F16, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "u32").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    std::vector<uint32_t> values = unary_ranges::fullU32Range();
    values.push_back(65504u);
    std::vector<Case> cases;
    for (uint32_t v : values) {
        const double dv = static_cast<double>(v);
        if (isConst && !inF16Finite(dv)) {
            continue; // f16FiniteRangeInterval.contains(v) for the const stage
        }
        Case c;
        c.inputs.push_back(CaseValue(u32(v)));
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F16, dv);
        c.expected = CaseValue(f16Bits(0u));
        c.expectedAccept.push_back(f16Expected(iv));
        cases.push_back(std::move(c));
    }
    run(t, f16ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::U32)}, F16, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "i32").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    std::vector<int32_t> values = unary_ranges::fullI32Range();
    values.push_back(65504);
    values.push_back(-65504);
    std::vector<Case> cases;
    for (int32_t v : values) {
        const double dv = static_cast<double>(v);
        if (isConst && !inF16Finite(dv)) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(i32(v)));
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F16, dv);
        c.expected = CaseValue(f16Bits(0u));
        c.expectedAccept.push_back(f16Expected(iv));
        cases.push_back(std::move(c));
    }
    run(t, f16ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::I32)}, F16, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_int").params(constVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    std::vector<int64_t> values = fp::fullI64Range();
    values.push_back(65504);
    values.push_back(-65504);
    std::vector<Case> cases;
    for (int64_t v : values) {
        const double dv = static_cast<double>(v);
        if (!inF16Finite(dv)) {
            continue; // f16FiniteRangeInterval.contains(Number(v))
        }
        Case c;
        c.inputs.push_back(CaseValue(abstractInt64(v)));
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F16, dv);
        c.expected = CaseValue(f16Bits(0u));
        c.expectedAccept.push_back(f16Expected(iv));
        cases.push_back(std::move(c));
    }
    run(t, f16ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::AbstractInt)}, F16,
        InputSource::Const, cfgVectorize(t), cases);
});

// f32 source: [...FP.f32.scalarRange(), 65535.996, -65535.996], correctly-rounded to f16.
std::vector<double> f16FromF32Range() {
    std::vector<double> v = fp::scalarF32Range();
    v.push_back(65535.996);
    v.push_back(-65535.996);
    return v;
}

CTS_TEST(g, "f32").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    // f32(e)->f16: input is f32 (quantized), output is the f16 correctly-rounded interval. The
    // generator can't represent distinct in/out scalar kinds, so build cases manually. 'finite'
    // (const) drops cases whose f16 interval is non-finite.
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    std::vector<Case> cases;
    for (double n : f16FromF32Range()) {
        const double q = fp::quantize(fp::FPKind::F32, n);
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F16, q);
        if (isConst && !iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(f32Input(n)));
        c.expected = CaseValue(f16Bits(0u));
        c.expectedAccept.push_back(f16Expected(iv));
        cases.push_back(std::move(c));
    }
    run(t, f16ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::F32)}, F16, cfgInputSource(t),
        cfgVectorize(t), cases);
});

CTS_TEST(g, "f32_mat").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mtIn = matType(cols, rows, ScalarKind::F32);
    const ExprType mtOut = matType(cols, rows, ScalarKind::F16);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateMatrixToMatrixCases(fp::FPKind::F32, fp::FPKind::F16,
                                                 fp::sparseMatrixF32Range(cols, rows),
                                                 /*finite=*/isConst, correctlyRoundedMatrixOp());
    run(t, matConvExpr(cols, rows), {mtIn}, mtOut, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    // f16 source: FP.f16.scalarRange(), correctly-rounded to f16 (an identity within f16).
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F16, fp::scalarF16Range(), /*finite=*/false,
        [](double n) { return fp::correctlyRoundedInterval(fp::FPKind::F16, n); });
    run(t, f16ConvExpr(cfgVectorize(t)), {F16}, F16, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16_mat").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F16);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateMatrixToMatrixCases(fp::FPKind::F16, fp::FPKind::F16,
                                                 fp::sparseMatrixF16Range(cols, rows),
                                                 /*finite=*/isConst, correctlyRoundedMatrixOp());
    run(t, matConvExpr(cols, rows), {mt}, mt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "abstract_float").params(constVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    // f16(e) where e is AbstractFloat: correctly-rounded to f16. Input abstract-float (f64); output
    // f16 (interval width 16). 'finite' drops cases whose f16 interval is non-finite.
    std::vector<double> range = fp::scalarF64Range();
    range.push_back(65535.996);
    range.push_back(-65535.996);
    std::vector<Case> cases;
    for (double n : range) {
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F16, n);
        if (!iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(abstractFloatValue(n)));
        c.expected = CaseValue(f16Bits(0u));
        c.expectedAccept.push_back(acceptInterval(16, iv.begin, iv.end));
        cases.push_back(std::move(c));
    }
    run(t, f16ConvExpr(cfgVectorize(t)), {scalarType(ScalarKind::AbstractFloat)}, F16,
        InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_float_mat").params(constColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mtIn = matType(cols, rows, ScalarKind::AbstractFloat);
    const ExprType mtOut = matType(cols, rows, ScalarKind::F16);
    auto cases = fp::generateMatrixToMatrixCases(fp::FPKind::Abstract, fp::FPKind::F16,
                                                 fp::sparseMatrixF64Range(cols, rows),
                                                 /*finite=*/true, correctlyRoundedMatrixOp());
    run(t, matConvExpr(cols, rows), {mtIn}, mtOut, InputSource::Const, 0, cases);
});
