// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/pack2x16float.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// (and the accompanying pack2x16float.cache.ts case generator). SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'pack2x16float' builtin function.
//
// @const fn pack2x16float(e: vec2<f32>) -> u32
// Each component is converted to an IEEE-754 binary16 (round-to-nearest-even, FTZ allowed)
// and packed into bits 16*i..16*i+15. The acceptance is the set of all u32 patterns that
// arise from the allowed f16 roundings (and +/-0 variants when subnormals/zeros are
// involved), per pack2x16float.cache.ts. If any input is non-finite as f16 the result is
// unbounded (any value accepted); such cases are filtered out for const-eval.

#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include "webgpu/shader/execution/expression/call/builtin/pack_unpack_utils.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::packunpack;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,pack2x16float",
    "Execution tests for the 'pack2x16float' builtin function.");

constexpr uint16_t kF16PosZero = 0x0000u;
constexpr uint16_t kF16NegZero = 0x8000u;

// Generates all possible valid u16 bit fields for a given f32 -> f16 conversion, assuming
// FTZ is allowed for both the f32 and f16 value. Mirrors pack2x16float.cache.ts generateU16s.
std::vector<uint16_t> generateU16s(double n) {
    bool containsSubnormals = isSubnormalF32Value(n);
    std::vector<uint16_t> nU16s = correctlyRoundedF16Bits(n);
    for (uint16_t u : nU16s) {
        if (isSubnormalF16Bits(u)) {
            containsSubnormals = true;
        }
    }
    bool containsPosZero = false, containsNegZero = false;
    for (uint16_t u : nU16s) {
        if (u == kF16PosZero) {
            containsPosZero = true;
        }
        if (u == kF16NegZero) {
            containsNegZero = true;
        }
    }
    if (!containsNegZero && (containsPosZero || containsSubnormals)) {
        nU16s.push_back(kF16NegZero);
    }
    if (!containsPosZero && (containsNegZero || containsSubnormals)) {
        nU16s.push_back(kF16PosZero);
    }
    return nU16s;
}

// Returns the set of acceptable u32 results, or empty to signal "undefined" (unbounded).
std::set<uint32_t> packResults(double x, double y) {
    if (!isFiniteF16Value(x) || !isFiniteF16Value(y)) {
        return {}; // undefined -> any
    }
    std::set<uint32_t> results;
    for (uint16_t lo : generateU16s(x)) {
        for (uint16_t hi : generateU16s(y)) {
            results.insert(static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16));
        }
    }
    return results;
}

CTS_TEST(testGroup, "pack")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const InputSource src =
            inputSourceFromParam(t.param<std::string>("inputSource"));
        const bool filterUndefined = (src == InputSource::Const);

        const std::vector<double> range = scalarF32Range();
        std::vector<Case> cases;
        for (double rx : range) {
            for (double ry : range) {
                const double x = quantizeToF32(rx);
                const double y = quantizeToF32(ry);
                const std::set<uint32_t> results = packResults(x, y);
                if (filterUndefined && results.empty()) {
                    continue;
                }
                Case c;
                c.inputs = {vec2(f32Bits(f32ToBits(static_cast<float>(x))),
                                 f32Bits(f32ToBits(static_cast<float>(y))))};
                if (results.empty()) {
                    // Unbounded: accept any u32.
                    c.expected = u32(0);
                    c.expectedAccept = {acceptAny()};
                } else {
                    c.expected = u32(*results.begin());
                    std::vector<uint32_t> bits(results.begin(), results.end());
                    c.expectedAccept = {acceptBitsSet(std::move(bits))};
                }
                cases.push_back(std::move(c));
            }
        }

        run(t, builtin("pack2x16float"), {vecType(2, ScalarKind::F32)},
            scalarType(ScalarKind::U32), src, 0, cases);
    });

} // namespace
