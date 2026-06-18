// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/extractBits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'extractBits' builtin function.

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,extractBits",
    "Execution tests for the 'extractBits' builtin function.");

ParamsBuilder inputSourceWidthParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("width", {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(2)),
                           Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

CTS_TEST(testGroup, "u32")
    .params(inputSourceWidthParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ScalarKind kind = ScalarKind::U32;
        const ExprType T = vecType(width, kind);
        auto bitScalar = [&](uint32_t b) { return u32Bits(b); };
        auto V = [&](uint32_t x, uint32_t y, uint32_t z, uint32_t w) -> CaseValue {
            switch (width) {
                case 1: return CaseValue(bitScalar(x));
                case 2: return vec2(bitScalar(x), bitScalar(y));
                case 3: return vec3(bitScalar(x), bitScalar(y), bitScalar(z));
                default: return vec4(bitScalar(x), bitScalar(y), bitScalar(z), bitScalar(w));
            }
        };
        auto V1 = [&](uint32_t x) { return V(x, x, x, x); };
        std::vector<Case> cases;
        auto add = [&](const CaseValue& e, uint32_t off, uint32_t cnt, const CaseValue& exp) {
            cases.push_back({{e, u32(off), u32(cnt)}, exp});
        };
        add(V1(0x00000000u), 0u, 32u, V1(0x00000000u));
        add(V1(0x00000000u), 1u, 10u, V1(0x00000000u));
        add(V1(0x00000000u), 2u, 5u, V1(0x00000000u));
        add(V1(0x00000000u), 0u, 1u, V1(0x00000000u));
        add(V1(0x00000000u), 31u, 1u, V1(0x00000000u));
        add(V1(0xFFFFFFFFu), 0u, 32u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), 1u, 10u, V1(0x000003FFu));
        add(V1(0xFFFFFFFFu), 2u, 5u, V1(0x0000001Fu));
        add(V1(0xFFFFFFFFu), 0u, 1u, V1(0x00000001u));
        add(V1(0xFFFFFFFFu), 31u, 1u, V1(0x00000001u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 0u, 32u, V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 1u, 31u, V(0x000EE000u, 0x7FF01FFFu, 0x002AA800u, 0x00155400u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 18u, V(0x00000077u, 0x0003FF80u, 0x00000155u, 0x000000AAu));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 7u, V(0x00000077u, 0x00000000u, 0x00000055u, 0x0000002Au));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 4u, V(0x00000007u, 0x00000000u, 0x00000005u, 0x0000000Au));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 3u, V(0x00000007u, 0x00000000u, 0x00000005u, 0x00000002u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 18u, 3u, V(0x00000007u, 0x00000000u, 0x00000005u, 0x00000002u));
        add(V1(0x00000001u), 0u, 1u, V1(0x00000001u));
        add(V1(0x80000000u), 31u, 1u, V1(0x00000001u));
        add(V1(0xFFFFFFFFu), 0u, 0u, V1(0x00000000u));
        add(V1(0x00000000u), 0u, 0u, V1(0x00000000u));
        add(V1(0x00000001u), 0u, 0u, V1(0x00000000u));
        add(V1(0x80000000u), 31u, 0u, V1(0x00000000u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 0u, 0u, V1(0x00000000u));
        if (inputSourceFromParam(t.param<std::string>("inputSource")) != InputSource::Const) {
            add(V1(0x00000001u), 0u, 99u, V1(0x00000001u));
            add(V1(0x80000000u), 31u, 99u, V1(0x00000001u));
            add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 0u, 99u, V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u));
            add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 99u, V(0x00000077u, 0x0003FF80u, 0x00000155u, 0x000000AAu));
            add(V1(0x00000005u), 1u, 4294967295u, V1(0x00000002u));
        }
        run(t, builtin("extractBits"), {T, scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)}, T,
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

CTS_TEST(testGroup, "i32")
    .params(inputSourceWidthParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ScalarKind kind = ScalarKind::I32;
        const ExprType T = vecType(width, kind);
        auto bitScalar = [&](uint32_t b) { return i32Bits(b); };
        auto V = [&](uint32_t x, uint32_t y, uint32_t z, uint32_t w) -> CaseValue {
            switch (width) {
                case 1: return CaseValue(bitScalar(x));
                case 2: return vec2(bitScalar(x), bitScalar(y));
                case 3: return vec3(bitScalar(x), bitScalar(y), bitScalar(z));
                default: return vec4(bitScalar(x), bitScalar(y), bitScalar(z), bitScalar(w));
            }
        };
        auto V1 = [&](uint32_t x) { return V(x, x, x, x); };
        std::vector<Case> cases;
        auto add = [&](const CaseValue& e, uint32_t off, uint32_t cnt, const CaseValue& exp) {
            cases.push_back({{e, u32(off), u32(cnt)}, exp});
        };
        add(V1(0x00000000u), 0u, 32u, V1(0x00000000u));
        add(V1(0x00000000u), 1u, 10u, V1(0x00000000u));
        add(V1(0x00000000u), 2u, 5u, V1(0x00000000u));
        add(V1(0x00000000u), 0u, 1u, V1(0x00000000u));
        add(V1(0x00000000u), 31u, 1u, V1(0x00000000u));
        add(V1(0xFFFFFFFFu), 0u, 32u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), 1u, 10u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), 2u, 5u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), 0u, 1u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), 31u, 1u, V1(0xFFFFFFFFu));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 0u, 32u, V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 1u, 31u, V(0x000EE000u, 0xFFF01FFFu, 0x002AA800u, 0x00155400u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 18u, V(0x00000077u, 0xFFFFFF80u, 0x00000155u, 0x000000AAu));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 7u, V(0xFFFFFFF7u, 0x00000000u, 0xFFFFFFD5u, 0x0000002Au));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 4u, V(0x00000007u, 0x00000000u, 0x00000005u, 0xFFFFFFFAu));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 3u, V(0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFDu, 0x00000002u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 18u, 3u, V(0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFDu, 0x00000002u));
        add(V1(0x00000001u), 0u, 1u, V1(0xFFFFFFFFu));
        add(V1(0x80000000u), 31u, 1u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), 0u, 0u, V1(0x00000000u));
        add(V1(0x00000000u), 0u, 0u, V1(0x00000000u));
        add(V1(0x00000001u), 0u, 0u, V1(0x00000000u));
        add(V1(0x80000000u), 31u, 0u, V1(0x00000000u));
        add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 0u, 0u, V1(0x00000000u));
        if (inputSourceFromParam(t.param<std::string>("inputSource")) != InputSource::Const) {
            add(V1(0x00000001u), 0u, 99u, V1(0x00000001u));
            add(V1(0x80000000u), 31u, 99u, V1(0xFFFFFFFFu));
            add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 0u, 99u, V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u));
            add(V(0x001DC000u, 0xFFE03FFFu, 0x00555000u, 0x002AA800u), 14u, 99u, V(0x00000077u, 0xFFFFFF80u, 0x00000155u, 0x000000AAu));
            add(V1(0x00000005u), 1u, 4294967295u, V1(0x00000002u));
        }
        run(t, builtin("extractBits"), {T, scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)}, T,
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
