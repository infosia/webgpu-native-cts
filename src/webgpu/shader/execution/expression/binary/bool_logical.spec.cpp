// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/bool_logical.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the boolean binary logical expression operations.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,bool_logical",
    "Execution Tests for the boolean binary logical expression operations");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder inputSourceOnlyParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

const ExprType BOOL = scalarType(ScalarKind::Bool);

// Builds the 4 cases for (false,false)/(true,false)/(false,true)/(true,true) given expected results.
std::vector<Case> cases4(bool ff, bool tf, bool ft, bool tt) {
    return {
        {{CaseValue(boolean(false)), CaseValue(boolean(false))}, CaseValue(boolean(ff))},
        {{CaseValue(boolean(true)), CaseValue(boolean(false))}, CaseValue(boolean(tf))},
        {{CaseValue(boolean(false)), CaseValue(boolean(true))}, CaseValue(boolean(ft))},
        {{CaseValue(boolean(true)), CaseValue(boolean(true))}, CaseValue(boolean(tt))},
    };
}

CTS_TEST(testGroup, "and").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, binaryOp("&"), {BOOL, BOOL}, BOOL, cfgInputSource(t), cfgVectorize(t),
        cases4(false, false, false, true));
});
CTS_TEST(testGroup, "and_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompound(t, "&=", {BOOL, BOOL}, BOOL, cfgInputSource(t), cfgVectorize(t),
                cases4(false, false, false, true));
});
CTS_TEST(testGroup, "and_short_circuit").params(inputSourceOnlyParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, binaryOp("&&"), {BOOL, BOOL}, BOOL, cfgInputSource(t), 0,
        cases4(false, false, false, true));
});
CTS_TEST(testGroup, "or").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, binaryOp("|"), {BOOL, BOOL}, BOOL, cfgInputSource(t), cfgVectorize(t),
        cases4(false, true, true, true));
});
CTS_TEST(testGroup, "or_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompound(t, "|=", {BOOL, BOOL}, BOOL, cfgInputSource(t), cfgVectorize(t),
                cases4(false, true, true, true));
});
CTS_TEST(testGroup, "or_short_circuit").params(inputSourceOnlyParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, binaryOp("||"), {BOOL, BOOL}, BOOL, cfgInputSource(t), 0,
        cases4(false, true, true, true));
});
CTS_TEST(testGroup, "equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, binaryOp("=="), {BOOL, BOOL}, BOOL, cfgInputSource(t), cfgVectorize(t),
        cases4(true, false, false, true));
});
CTS_TEST(testGroup, "not_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, binaryOp("!="), {BOOL, BOOL}, BOOL, cfgInputSource(t), cfgVectorize(t),
        cases4(false, true, true, false));
});

} // namespace
