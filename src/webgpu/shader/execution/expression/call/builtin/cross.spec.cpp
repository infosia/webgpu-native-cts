// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/cross.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'cross' builtin function. cross(a,b) (vec3) = the per-component
// determinant interval. Inherited accuracy (abstract as accurate as f32). f16 deferred (no oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,cross",
    "Execution tests for the 'cross' builtin function");

ParamsBuilder allSources(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder constOnly(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }

ExprType vecAF() { return vecType(3, ScalarKind::AbstractFloat); }
ExprType vecF32() { return vecType(3, ScalarKind::F32); }
ExprType vecF16() { return vecType(3, ScalarKind::F16); }

} // namespace

CTS_TEST(g, "abstract_float").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateCrossCases(fp::FPKind::Abstract, fp::vectorF64Range(3),
                                        fp::vectorF64Range(3), true);
    run(t, builtin("cross"), {vecAF(), vecAF()}, vecAF(), InputSource::Const, 0, cases);
});

CTS_TEST(g, "f32").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateCrossCases(fp::FPKind::F32, fp::vectorF32Range(3), fp::vectorF32Range(3),
                                        isConst(t));
    run(t, builtin("cross"), {vecF32(), vecF32()}, vecF32(), cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateCrossCases(fp::FPKind::F16, fp::vectorF16Range(3), fp::vectorF16Range(3),
                                        isConst(t));
    run(t, builtin("cross"), {vecF16(), vecF16()}, vecF16(), cfgInputSource(t), 0, cases);
});
