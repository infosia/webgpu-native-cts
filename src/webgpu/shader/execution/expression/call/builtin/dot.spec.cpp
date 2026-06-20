// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/dot.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'dot' builtin function. dot(e1, e2) = sum of e1[i]*e2[i]. The f32 +
// abstract_float variants use the order-independent sum-of-products acceptance interval (inherited
// accuracy). The i32/u32/abstract_int variants are bit-EXACT: they mirror the upstream cache's
// ci_dot (Math.imul 32-bit signed products summed as a double, then i32()/u32() wraparound on the
// result) and ai_dot (exact i64 products summed with overflow-undefined filtering). The vector ranges
// and their sparse variants) are reproduced locally from math.ts. f16 deferred (no Metal oracle).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,dot",
    "Execution tests for the 'dot' builtin function");

ParamsBuilder allSources(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder constOnly(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }

ExprType vecAF(int dim) { return vecType(dim, ScalarKind::AbstractFloat); }
ExprType vecF32(int dim) { return vecType(dim, ScalarKind::F32); }

// dot uses the dense vectorRange for vec2 and the sparse range for vec3/vec4 (per the cache).
std::vector<std::vector<double>> dotRange(fp::FPKind kind, int dim) {
    return dim == 2 ? fp::vectorRange(kind, 2) : fp::sparseVectorRange(kind, dim);
}

// ---- Integer vector ranges (math.ts). Reproduced locally (dot is their only consumer here). ----

// kInterestingI32Values (math.ts): [i32.neg.max=0, trunc(0/2)=0, -256, -10, -1, 0, 1, 10, 256,
// trunc(i32.max/2), i32.max].
const std::vector<int32_t>& interestingI32() {
    static const std::vector<int32_t> v = {0,   0, -256, -10, -1, 0, 1, 10, 256,
                                           INT32_MAX / 2, INT32_MAX};
    return v;
}
// kInterestingU32Values (math.ts): [0, 1, 10, 256, trunc(u32.max/2), u32.max].
const std::vector<uint32_t>& interestingU32() {
    static const std::vector<uint32_t> v = {0u, 1u, 10u, 256u, 4294967295u / 2u, 4294967295u};
    return v;
}
// kInterestingI64Values (math.ts): [i64.neg.max=0, trunc(0/2)=0, -256, -10, -1, 0, 1, 10, 256,
// i64.max/2, i64.max].
const std::vector<int64_t>& interestingI64() {
    static const std::vector<int64_t> v = {0,   0, -256, -10, -1, 0, 1, 10, 256,
                                           9223372036854775807LL / 2, 9223372036854775807LL};
    return v;
}

// vectorI32Range(dim): each interesting value inserted in each position, padded with small constants.
std::vector<std::vector<int32_t>> vectorI32Range(int dim) {
    std::vector<std::vector<int32_t>> out;
    for (int32_t f : interestingI32()) {
        if (dim == 2) {
            out.push_back({f, 1});
            out.push_back({-1, f});
        } else if (dim == 3) {
            out.push_back({f, 1, -2});
            out.push_back({-1, f, 2});
            out.push_back({1, -2, f});
        } else {
            out.push_back({f, -1, 2, 3});
            out.push_back({1, f, -2, 3});
            out.push_back({1, 2, f, -3});
            out.push_back({-1, 2, -3, f});
        }
    }
    return out;
}
std::vector<std::vector<uint32_t>> vectorU32Range(int dim) {
    std::vector<std::vector<uint32_t>> out;
    for (uint32_t f : interestingU32()) {
        if (dim == 2) {
            out.push_back({f, 1u});
            out.push_back({1u, f});
        } else if (dim == 3) {
            out.push_back({f, 1u, 2u});
            out.push_back({1u, f, 2u});
            out.push_back({1u, 2u, f});
        } else {
            out.push_back({f, 1u, 2u, 3u});
            out.push_back({1u, f, 2u, 3u});
            out.push_back({1u, 2u, f, 3u});
            out.push_back({1u, 2u, 3u, f});
        }
    }
    return out;
}
std::vector<std::vector<int64_t>> vectorI64Range(int dim) {
    std::vector<std::vector<int64_t>> out;
    for (int64_t f : interestingI64()) {
        if (dim == 2) {
            out.push_back({f, 1});
            out.push_back({-1, f});
        } else if (dim == 3) {
            out.push_back({f, 1, -2});
            out.push_back({-1, f, 2});
            out.push_back({1, -2, f});
        } else {
            out.push_back({f, -1, 2, 3});
            out.push_back({1, f, -2, 3});
            out.push_back({1, 2, f, -3});
            out.push_back({-1, 2, -3, f});
        }
    }
    return out;
}

// sparseVectorI32Range(dim): sparseI32Range() spread over positions by index (math.ts).
std::vector<std::vector<int32_t>> sparseVectorI32Range(int dim) {
    const std::vector<int32_t>& src = interestingI32();
    std::vector<std::vector<int32_t>> out;
    for (int idx = 0; idx < static_cast<int>(src.size()); ++idx) {
        const int32_t i = src[static_cast<size_t>(idx)];
        std::vector<int32_t> row;
        for (int pos = 0; pos < dim; ++pos) {
            const bool here = (idx % dim) == pos;
            row.push_back(here ? i : ((pos % 2 == 0) ? idx : -idx));
        }
        out.push_back(row);
    }
    return out;
}
std::vector<std::vector<uint32_t>> sparseVectorU32Range(int dim) {
    const std::vector<uint32_t>& src = interestingU32();
    std::vector<std::vector<uint32_t>> out;
    for (int idx = 0; idx < static_cast<int>(src.size()); ++idx) {
        const uint32_t i = src[static_cast<size_t>(idx)];
        std::vector<uint32_t> row;
        for (int pos = 0; pos < dim; ++pos) {
            const bool here = (idx % dim) == pos;
            // math.ts uses -idx for odd positions; for u32 this wraps to 2^32 - idx.
            row.push_back(here ? i : ((pos % 2 == 0) ? static_cast<uint32_t>(idx)
                                                     : static_cast<uint32_t>(-idx)));
        }
        out.push_back(row);
    }
    return out;
}
std::vector<std::vector<int64_t>> sparseVectorI64Range(int dim) {
    const std::vector<int64_t>& src = interestingI64();
    std::vector<std::vector<int64_t>> out;
    for (int idx = 0; idx < static_cast<int>(src.size()); ++idx) {
        const int64_t i = src[static_cast<size_t>(idx)];
        std::vector<int64_t> row;
        for (int pos = 0; pos < dim; ++pos) {
            const bool here = (idx % dim) == pos;
            row.push_back(here ? i : ((pos % 2 == 0) ? static_cast<int64_t>(idx)
                                                     : -static_cast<int64_t>(idx)));
        }
        out.push_back(row);
    }
    return out;
}

// ---- Integer dot reference computations (mirror the upstream cache exactly). ----

// Math.imul: 32-bit signed multiply with wraparound.
int32_t imul(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
// ci_dot (cache): sum of Math.imul products, accumulated as a double.
double ci_dot_i32(const std::vector<int32_t>& x, const std::vector<int32_t>& y) {
    double acc = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        acc += static_cast<double>(imul(x[i], y[i]));
    }
    return acc;
}
double ci_dot_u32(const std::vector<uint32_t>& x, const std::vector<uint32_t>& y) {
    double acc = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const int32_t p = imul(static_cast<int32_t>(x[i]), static_cast<int32_t>(y[i]));
        acc += static_cast<double>(p);
    }
    return acc;
}


// ToInt32 / ToUint32: JS TypedArray coercion of a double (wraparound mod 2^32). The upstream cache
// scalarizes the dot SUM with i32()/u32() (NOT quantizeToI32/U32 — those only quantize the inputs),
// so the result wraps, matching the GPU's 2's-complement integer dot.
int32_t toInt32(double n) {
    const double m = std::fmod(std::trunc(n), 4294967296.0);
    uint32_t u = static_cast<uint32_t>(static_cast<int64_t>(m < 0 ? m + 4294967296.0 : m));
    return static_cast<int32_t>(u);
}
uint32_t toUint32(double n) {
    const double m = std::fmod(std::trunc(n), 4294967296.0);
    return static_cast<uint32_t>(static_cast<int64_t>(m < 0 ? m + 4294967296.0 : m));
}

// i32/u32 dot cases: cartesian product of the ranges, ci_dot -> ToInt32/ToUint32 (wraparound).
std::vector<Case> generateDotI32Cases(const std::vector<std::vector<int32_t>>& p0s,
                                      const std::vector<std::vector<int32_t>>& p1s) {
    std::vector<Case> cases;
    for (const auto& a : p0s) {
        for (const auto& b : p1s) {
            const int32_t r = toInt32(ci_dot_i32(a, b));
            std::vector<Scalar> ai, bi;
            for (int32_t v : a) {
                ai.push_back(i32(v));
            }
            for (int32_t v : b) {
                bi.push_back(i32(v));
            }
            cases.push_back({{CaseValue::vec(ai), CaseValue::vec(bi)}, CaseValue(i32(r))});
        }
    }
    return cases;
}
std::vector<Case> generateDotU32Cases(const std::vector<std::vector<uint32_t>>& p0s,
                                      const std::vector<std::vector<uint32_t>>& p1s) {
    std::vector<Case> cases;
    for (const auto& a : p0s) {
        for (const auto& b : p1s) {
            const uint32_t r = toUint32(ci_dot_u32(a, b));
            std::vector<Scalar> au, bu;
            for (uint32_t v : a) {
                au.push_back(u32(v));
            }
            for (uint32_t v : b) {
                bu.push_back(u32(v));
            }
            cases.push_back({{CaseValue::vec(au), CaseValue::vec(bu)}, CaseValue(u32(r))});
        }
    }
    return cases;
}

// abstract_int dot (ai_dot): EXACT i64 products; result undefined (case dropped) if any product,
// the straight sum, OR any permutation's partial sum overflows i64. vec2 needs no permutation
// (commutative). int64 overflow is detected without 128-bit types (MSVC-safe).

// Exact i64 multiply with overflow detection. Returns false (and r) on success.
bool mulOverflow(int64_t a, int64_t b, int64_t& r) {
    if (a == 0 || b == 0) {
        r = 0;
        return false;
    }
    // INT64_MIN * -1 overflows.
    if ((a == INT64_MIN && b == -1) || (b == INT64_MIN && a == -1)) {
        return true;
    }
    const int64_t p = a * b;
    if (p / b != a) {
        return true;
    }
    r = p;
    return false;
}
// Exact i64 add with overflow detection.
bool addOverflow(int64_t a, int64_t b, int64_t& r) {
    if (b > 0 && a > INT64_MAX - b) {
        return true;
    }
    if (b < 0 && a < INT64_MIN - b) {
        return true;
    }
    r = a + b;
    return false;
}
bool dotProductsOOB(const std::vector<int64_t>& x, const std::vector<int64_t>& y,
                    std::vector<int64_t>& products, int64_t& sum) {
    sum = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        int64_t p;
        if (mulOverflow(x[i], y[i], p)) {
            return true;
        }
        products.push_back(p);
        if (addOverflow(sum, p, sum)) {
            return true;
        }
    }
    return false;
}
// Returns true if SOME permutation of the products has a partial sum that overflows i64.
bool anyPermutationOOB(std::vector<int64_t> products) {
    std::sort(products.begin(), products.end());
    do {
        int64_t partial = 0;
        for (int64_t p : products) {
            if (addOverflow(partial, p, partial)) {
                return true;
            }
        }
    } while (std::next_permutation(products.begin(), products.end()));
    return false;
}
std::vector<Case> generateDotAbstractIntCases(const std::vector<std::vector<int64_t>>& p0s,
                                              const std::vector<std::vector<int64_t>>& p1s) {
    std::vector<Case> cases;
    for (const auto& a : p0s) {
        for (const auto& b : p1s) {
            std::vector<int64_t> products;
            int64_t sum;
            if (dotProductsOOB(a, b, products, sum)) {
                continue; // undefined -> dropped
            }
            if (a.size() != 2 && anyPermutationOOB(products)) {
                continue; // undefined -> dropped
            }
            const int64_t r = sum;
            std::vector<Scalar> as, bs;
            for (int64_t v : a) {
                as.push_back(abstractInt64(v));
            }
            for (int64_t v : b) {
                bs.push_back(abstractInt64(v));
            }
            cases.push_back({{CaseValue::vec(as), CaseValue::vec(bs)},
                             CaseValue(abstractInt64(r))});
        }
    }
    return cases;
}

} // namespace

// --- abstract_int (DEFERRED) ---
// Tint/naga define dot only for `T: fiu32_f16` (f32/i32/u32/f16); abstract-int is NOT an accepted
// element type, so `dot(vec<AbstractInt>, ...)` forces materialization to i32 and the abstract-int
// 64-bit output protocol's `result >> 32` then becomes an invalid (shift >= bit width) i32 shift,
// failing shader creation on Dawn. Upstream emits the identical shader, so no faithful port can make
// Dawn pass these; deferred. (The reference case generators below stay compiled to document intent.)
CTS_TEST(g, "abstract_int_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const auto cases = generateDotAbstractIntCases(vectorI64Range(2), vectorI64Range(2));
    (void)cases;
    t.skip("abstract-int dot: Tint/naga have no abstract-int dot overload (fiu32_f16 only); the "
           "abstract-int 64-bit output protocol's >>32 becomes an invalid i32 shift");
});
CTS_TEST(g, "abstract_int_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const auto cases = generateDotAbstractIntCases(sparseVectorI64Range(3), sparseVectorI64Range(3));
    (void)cases;
    t.skip("abstract-int dot: Tint/naga have no abstract-int dot overload (fiu32_f16 only)");
});
CTS_TEST(g, "abstract_int_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const auto cases = generateDotAbstractIntCases(sparseVectorI64Range(4), sparseVectorI64Range(4));
    (void)cases;
    t.skip("abstract-int dot: Tint/naga have no abstract-int dot overload (fiu32_f16 only)");
});

// --- i32 (bit-exact ci_dot) ---
CTS_TEST(g, "i32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = generateDotI32Cases(vectorI32Range(2), vectorI32Range(2));
    run(t, builtin("dot"), {vecType(2, ScalarKind::I32), vecType(2, ScalarKind::I32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "i32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = generateDotI32Cases(sparseVectorI32Range(3), sparseVectorI32Range(3));
    run(t, builtin("dot"), {vecType(3, ScalarKind::I32), vecType(3, ScalarKind::I32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "i32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = generateDotI32Cases(sparseVectorI32Range(4), sparseVectorI32Range(4));
    run(t, builtin("dot"), {vecType(4, ScalarKind::I32), vecType(4, ScalarKind::I32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), 0, cases);
});

// --- u32 (bit-exact ci_dot) ---
CTS_TEST(g, "u32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = generateDotU32Cases(vectorU32Range(2), vectorU32Range(2));
    run(t, builtin("dot"), {vecType(2, ScalarKind::U32), vecType(2, ScalarKind::U32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "u32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = generateDotU32Cases(sparseVectorU32Range(3), sparseVectorU32Range(3));
    run(t, builtin("dot"), {vecType(3, ScalarKind::U32), vecType(3, ScalarKind::U32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "u32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = generateDotU32Cases(sparseVectorU32Range(4), sparseVectorU32Range(4));
    run(t, builtin("dot"), {vecType(4, ScalarKind::U32), vecType(4, ScalarKind::U32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), 0, cases);
});

// --- abstract_float ---
CTS_TEST(g, "abstract_float_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::Abstract, dotRange(fp::FPKind::Abstract, 2),
                                      dotRange(fp::FPKind::Abstract, 2), true);
    run(t, builtin("dot"), {vecAF(2), vecAF(2)}, scalarType(ScalarKind::AbstractFloat),
        InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::Abstract, dotRange(fp::FPKind::Abstract, 3),
                                      dotRange(fp::FPKind::Abstract, 3), true);
    run(t, builtin("dot"), {vecAF(3), vecAF(3)}, scalarType(ScalarKind::AbstractFloat),
        InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::Abstract, dotRange(fp::FPKind::Abstract, 4),
                                      dotRange(fp::FPKind::Abstract, 4), true);
    run(t, builtin("dot"), {vecAF(4), vecAF(4)}, scalarType(ScalarKind::AbstractFloat),
        InputSource::Const, 0, cases);
});

// --- f32 ---
CTS_TEST(g, "f32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::F32, dotRange(fp::FPKind::F32, 2),
                                      dotRange(fp::FPKind::F32, 2), isConst(t));
    run(t, builtin("dot"), {vecF32(2), vecF32(2)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0,
        cases);
});
CTS_TEST(g, "f32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::F32, dotRange(fp::FPKind::F32, 3),
                                      dotRange(fp::FPKind::F32, 3), isConst(t));
    run(t, builtin("dot"), {vecF32(3), vecF32(3)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0,
        cases);
});
CTS_TEST(g, "f32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::F32, dotRange(fp::FPKind::F32, 4),
                                      dotRange(fp::FPKind::F32, 4), isConst(t));
    run(t, builtin("dot"), {vecF32(4), vecF32(4)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0,
        cases);
});

// --- f16 (deferred) ---
CTS_TEST(g, "f16_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
