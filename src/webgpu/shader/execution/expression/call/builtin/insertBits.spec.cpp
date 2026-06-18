// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/insertBits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'insertBits' builtin function.

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,insertBits",
    "Execution tests for the 'insertBits' builtin function.");

CTS_TEST(testGroup, "integer")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("signed", {false, true})
            .combine("width", {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isSigned = t.param<bool>("signed");
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ScalarKind kind = isSigned ? ScalarKind::I32 : ScalarKind::U32;
        const ExprType T = vecType(width, kind);

        auto bitScalar = [&](uint32_t b) { return isSigned ? i32Bits(b) : u32Bits(b); };
        auto V = [&](uint32_t x, uint32_t y, uint32_t z, uint32_t w) -> CaseValue {
            switch (width) {
                case 1:
                    return CaseValue(bitScalar(x));
                case 2:
                    return vec2(bitScalar(x), bitScalar(y));
                case 3:
                    return vec3(bitScalar(x), bitScalar(y), bitScalar(z));
                default:
                    return vec4(bitScalar(x), bitScalar(y), bitScalar(z), bitScalar(w));
            }
        };
        auto V1 = [&](uint32_t x) { return V(x, x, x, x); };

        std::vector<Case> cases;
        auto add = [&](const CaseValue& e, const CaseValue& nb, uint32_t off, uint32_t cnt,
                       const CaseValue& exp) {
            cases.push_back({{e, nb, u32(off), u32(cnt)}, exp});
        };

        add(V1(0x00000000u), V1(0x00000000u), 0u, 32u, V1(0x00000000u));
        add(V1(0x00000000u), V1(0x00000000u), 1u, 10u, V1(0x00000000u));
        add(V1(0x00000000u), V1(0x00000000u), 2u, 5u, V1(0x00000000u));
        add(V1(0x00000000u), V1(0x00000000u), 0u, 1u, V1(0x00000000u));
        add(V1(0x00000000u), V1(0x00000000u), 31u, 1u, V1(0x00000000u));
        add(V1(0x00000000u), V1(0xFFFFFFFFu), 0u, 32u, V1(0xFFFFFFFFu));
        add(V1(0xFFFFFFFFu), V1(0x00000000u), 0u, 32u, V1(0x00000000u));
        add(V1(0x00000000u), V1(0xFFFFFFFFu), 0u, 1u, V1(0x00000001u));
        add(V1(0xFFFFFFFFu), V1(0x00000000u), 0u, 1u, V1(0xFFFFFFFEu));
        add(V1(0x00000000u), V1(0xFFFFFFFFu), 31u, 1u, V1(0x80000000u));
        add(V1(0xFFFFFFFFu), V1(0x00000000u), 31u, 1u, V1(0x7FFFFFFFu));
        add(V1(0x00000000u), V1(0xFFFFFFFFu), 1u, 10u, V1(0x000007FEu));
        add(V1(0xFFFFFFFFu), V1(0x00000000u), 1u, 10u, V1(0xFFFFF801u));
        add(V1(0x00000000u), V1(0xFFFFFFFFu), 2u, 5u, V1(0x0000007Cu));
        add(V1(0xFFFFFFFFu), V1(0x00000000u), 2u, 5u, V1(0xFFFFFF83u));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 0u, 32u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 0u, 32u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 1u, 31u, V(0x12A44A44u, 0x9C6719C6u, 0x55555554u, 0xAAAAAAAAu));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 1u, 31u, V(0x12A44A45u, 0x9C6719C7u, 0x55555555u, 0xAAAAAAABu));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 18u, V(0x89488000u, 0xE338C000u, 0xAAAA8000u, 0x55554000u));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 18u, V(0x8948BFFFu, 0xE338FFFFu, 0xAAAABFFFu, 0x55557FFFu));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 7u, V(0x00088000u, 0x0018C000u, 0x000A8000u, 0x00154000u));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 7u, V(0xFFE8BFFFu, 0xFFF8FFFFu, 0xFFEABFFFu, 0xFFF57FFFu));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 4u, V(0x00008000u, 0x0000C000u, 0x00028000u, 0x00014000u));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 4u, V(0xFFFCBFFFu, 0xFFFCFFFFu, 0xFFFEBFFFu, 0xFFFD7FFFu));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 3u, V(0x00008000u, 0x0000C000u, 0x00008000u, 0x00014000u));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 3u, V(0xFFFEBFFFu, 0xFFFEFFFFu, 0xFFFEBFFFu, 0xFFFF7FFFu));
        add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 18u, 3u, V(0x00080000u, 0x000C0000u, 0x00080000u, 0x00140000u));
        add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 18u, 3u, V(0xFFEBFFFFu, 0xFFEFFFFFu, 0xFFEBFFFFu, 0xFFF7FFFFu));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0x00000000u), 1u, 31u, V(0x00000000u, 0x00000001u, 0x00000000u, 0x00000001u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 1u, 31u, V(0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFEu, 0xFFFFFFFFu));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0x00000000u), 14u, 18u, V(0x00002522u, 0x00000CE3u, 0x00002AAAu, 0x00001555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 14u, 18u, V(0xFFFFE522u, 0xFFFFCCE3u, 0xFFFFEAAAu, 0xFFFFD555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0x00000000u), 14u, 7u, V(0x89402522u, 0xCE200CE3u, 0xAAA02AAAu, 0x55401555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 14u, 7u, V(0x895FE522u, 0xCE3FCCE3u, 0xAABFEAAAu, 0x555FD555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0x00000000u), 14u, 4u, V(0x89502522u, 0xCE300CE3u, 0xAAA82AAAu, 0x55541555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 14u, 4u, V(0x8953E522u, 0xCE33CCE3u, 0xAAABEAAAu, 0x5557D555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0x00000000u), 14u, 3u, V(0x89522522u, 0xCE320CE3u, 0xAAAA2AAAu, 0x55541555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 14u, 3u, V(0x8953E522u, 0xCE33CCE3u, 0xAAABEAAAu, 0x5555D555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0x00000000u), 18u, 3u, V(0x89422522u, 0xCE238CE3u, 0xAAA2AAAAu, 0x55415555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 18u, 3u, V(0x895E2522u, 0xCE3F8CE3u, 0xAABEAAAAu, 0x555D5555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 18u, 3u, V(0x894A2522u, 0xCE2F8CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 14u, 7u, V(0x8948A522u, 0xCE38CCE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 0u, 0u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 1u, 0u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 2u, 0u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 31u, 0u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 32u, 0u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
        add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V1(0xFFFFFFFFu), 0u, 0u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));

        if (inputSourceFromParam(t.param<std::string>("inputSource")) != InputSource::Const) {
            add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 50u, 3u, V1(0x00000000u));
            add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 50u, 3u, V1(0xFFFFFFFFu));
            add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 50u, 3u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
            add(V1(0x00000000u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 0u, 99u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
            add(V1(0xFFFFFFFFu), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 0u, 99u, V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
            add(V1(0x00000000u), V1(0x00000001u), 31u, 99u, V1(0x80000000u));
            add(V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), V(0x89522522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u), 20u, 99u, V(0x52222522u, 0xCE338CE3u, 0xAAAAAAAAu, 0x55555555u));
            add(V1(0x00000005u), V1(0x00000001u), 1u, 4294967295u, V1(0x00000003u));
        }


        run(t, builtin("insertBits"),
            {T, T, scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)}, T,
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
