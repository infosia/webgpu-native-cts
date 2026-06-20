// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/quantizeToF16.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'quantizeToF16' builtin function. Quantizes an f32 as if converted to
// binary16 and back: the acceptance interval is the f16-correctly-rounded value (f16 rounding even
// though the value type is f32). f32 only (no abstract/f16 variants upstream).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,quantizeToF16",
    "Execution tests for the 'quantizeToF16' builtin function");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// f16 boundary values prepended to the range.
std::vector<double> f16Boundaries() {
    return {fp::f16NegativeMin(),   fp::f16NegativeMax(),   fp::f16NegativeSubMin(),
            fp::f16NegativeSubMax(), fp::f16PositiveSubMin(), fp::f16PositiveSubMax(),
            fp::f16PositiveMin(),   fp::f16PositiveMax()};
}

} // namespace

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<double> range = f16Boundaries();
    if (isConst(t)) {
        const std::vector<double> base = fp::scalarF16Range();
        range.insert(range.end(), base.begin(), base.end());
    } else {
        const std::vector<double> base = fp::scalarF32Range();
        range.insert(range.end(), base.begin(), base.end());
    }
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, range, /*finite=*/isConst(t),
        [](double n) { return fp::quantizeToF16Interval(n); });
    run(t, builtin("quantizeToF16"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});
