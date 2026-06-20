// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/transpose.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'transpose' builtin function. transpose(matCxR) -> matRxC: exact element
// move (correctly-rounded per element). Inherited accuracy (abstract as accurate as f32). f16
// deferred (no Metal oracle).

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,transpose",
    "Execution tests for the 'transpose' builtin function");

ParamsBuilder allSourcesCR(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("cols", {2, 3, 4})
        .combine("rows", {2, 3, 4});
}
ParamsBuilder constCR(ParamsBuilder u) {
    return u.combine("inputSource", {"const"}).combine("cols", {2, 3, 4}).combine("rows", {2, 3, 4});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }

} // namespace

CTS_TEST(g, "abstract_float").params(constCR).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    auto cases = fp::generateTransposeCases(fp::FPKind::Abstract,
                                            fp::sparseMatrixF64Range(cols, rows), true);
    run(t, builtin("transpose"), {matType(cols, rows, ScalarKind::AbstractFloat)},
        matType(rows, cols, ScalarKind::AbstractFloat), InputSource::Const, 0, cases);
});

CTS_TEST(g, "f32").params(allSourcesCR).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    auto cases = fp::generateTransposeCases(fp::FPKind::F32, fp::sparseMatrixF32Range(cols, rows),
                                            isConst(t));
    run(t, builtin("transpose"), {matType(cols, rows, ScalarKind::F32)},
        matType(rows, cols, ScalarKind::F32), cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(allSourcesCR).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
