// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/unpack2x16float.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// (and unpack2x16float.cache.ts). SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'unpack2x16float' builtin function.
//
// @const fn unpack2x16float(e: u32) -> vec2<f32>
// Component i is the f32 value of bits 16*i..16*i+15 interpreted as an IEEE-754 binary16.
// Acceptance is FP.f32.unpack2x16floatInterval: each half is quantizeToF16Interval of the
// exact f16 value, i.e. the point value plus +/-0 when the half is a subnormal f16. If
// either half is non-finite as f16 the whole result is unbounded (any value); such cases
// are filtered out for const-eval.

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/call/builtin/pack_unpack_utils.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::packunpack;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,unpack2x16float",
    "Execution tests for the 'unpack2x16float' builtin function.");

CTS_TEST(testGroup, "unpack")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));
        const bool filterUndefined = (src == InputSource::Const);

        std::vector<Case> cases;
        for (uint32_t n : fullU32Range()) {
            const uint16_t h0 = static_cast<uint16_t>(n & 0xFFFFu);
            const uint16_t h1 = static_cast<uint16_t>((n >> 16) & 0xFFFFu);
            const double v0 = f16BitsToDouble(h0);
            const double v1 = f16BitsToDouble(h1);
            const bool unbounded = !isFiniteF16Value(v0) || !isFiniteF16Value(v1);

            if (unbounded && filterUndefined) {
                continue;
            }

            Case c;
            c.inputs = {CaseValue(u32(n))};
            c.expected = CaseValue::vec({f32Bits(0), f32Bits(0)});
            if (unbounded) {
                c.expectedAccept = {acceptAny(), acceptAny()};
            } else {
                auto interval = [](uint16_t bits, double v) {
                    // quantizeToF16Interval of an exact f16 value: [v] plus 0 when subnormal.
                    if (isSubnormalF16Bits(bits)) {
                        return acceptInterval(32, std::min(v, 0.0), std::max(v, 0.0));
                    }
                    return acceptInterval(32, v, v);
                };
                c.expectedAccept = {interval(h0, v0), interval(h1, v1)};
            }
            cases.push_back(std::move(c));
        }

        run(t, builtin("unpack2x16float"), {scalarType(ScalarKind::U32)},
            vecType(2, ScalarKind::F32), src, 0, cases);
    });

} // namespace
