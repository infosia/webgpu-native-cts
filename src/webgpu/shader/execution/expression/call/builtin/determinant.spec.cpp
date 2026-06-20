// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/determinant.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'determinant' builtin function. determinant(matCxC) -> scalar, via the
// upstream co-factor determinantInterval. Inputs are PRNG-generated integer matrices in a range that
// keeps the determinant exactly computable (mirrors determinant.cache.ts, incl. the TinyMT PRNG and
// the seed/range tables). Inherited accuracy (abstract as accurate as f32). f16 deferred.

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,determinant",
    "Execution tests for the 'determinant' builtin function");

// --- TinyMT PRNG (faithful port of util/prng.ts) ---
class Prng {
  public:
    explicit Prng(uint32_t seed) {
        state_[0] = seed;
        state_[1] = kMat1;
        state_[2] = kMat2;
        state_[3] = kTMat;
        for (uint32_t i = 1; i < kMinLoop; ++i) {
            const uint32_t prev = state_[(i - 1) & 3];
            state_[i & 3] ^= i + imul(1812433253u, prev ^ (prev >> 30));
        }
        for (uint32_t i = 0; i < kPreLoop; ++i) {
            next();
        }
    }

    // Uniform integer in [0, N-1], unbiased rejection sampling (Knuth GB_FLIP).
    uint32_t uniformInt(uint32_t N) {
        const uint64_t upperBound = (1ull << 16) * (1ull << 16); // 2^32
        const uint64_t keepZoneSize = upperBound - (upperBound % N);
        uint32_t candidate;
        do {
            candidate = randomU32();
        } while (static_cast<uint64_t>(candidate) >= keepZoneSize);
        return candidate % N;
    }

  private:
    static uint32_t imul(uint32_t a, uint32_t b) { return a * b; } // 32-bit wraparound

    void next() {
        uint32_t n0 = (state_[0] & kMask) ^ state_[1] ^ state_[2];
        uint32_t n1 = state_[3];
        n0 ^= n0 << kSH0;
        n1 ^= (n1 >> kSH0) ^ n0;
        state_[0] = state_[1];
        state_[1] = state_[2];
        state_[2] = n0 ^ (n1 << kSH1);
        state_[3] = n1;
        if ((n1 & 1u) != 0) {
            state_[1] ^= kMat1;
            state_[2] ^= kMat2;
        }
    }
    uint32_t temper() {
        uint32_t t0 = state_[3];
        uint32_t t1 = state_[0] + (state_[2] >> kSH8);
        t0 ^= t1;
        if ((t1 & 1u) != 0) {
            t0 ^= kTMat;
        }
        return t0;
    }
    uint32_t randomU32() {
        next();
        return temper();
    }

    static constexpr uint32_t kMat1 = 0x8f7011eeu;
    static constexpr uint32_t kMat2 = 0xfc78ff1fu;
    static constexpr uint32_t kTMat = 0x3793fdffu;
    static constexpr uint32_t kMask = 0x7fffffffu;
    static constexpr uint32_t kMinLoop = 8;
    static constexpr uint32_t kPreLoop = 8;
    static constexpr uint32_t kSH0 = 1;
    static constexpr uint32_t kSH1 = 10;
    static constexpr uint32_t kSH8 = 8;

    uint32_t state_[4] = {0, 0, 0, 0};
};

constexpr int kNumSamples = 20;

// Per (fpwidth, dim) element magnitude bound (rangeTable in the cache).
int rangeBound(int fpwidth, int dim) {
    if (fpwidth == 16) {
        return dim == 2 ? 32 : (dim == 3 ? 8 : 4);
    }
    return dim == 2 ? 2896 : (dim == 3 ? 161 : 38);
}

double randomMatrixEntry(Prng& p, int dim, int fpwidth) {
    const int N = rangeBound(fpwidth, dim);
    // balanced = uniformInt(N) - floor(N/2).
    return static_cast<double>(static_cast<int>(p.uniformInt(static_cast<uint32_t>(N))) - (N / 2));
}

// Column-major matrix m[col][row].
std::vector<std::vector<double>> randomSquareMatrix(Prng& p, int dim, int fpwidth) {
    static const double kMul[3] = {1.0, 2.0, 0.25};
    const double multiplier = kMul[p.uniformInt(3)];
    std::vector<std::vector<double>> m(static_cast<size_t>(dim),
                                       std::vector<double>(static_cast<size_t>(dim), 0.0));
    for (int c = 0; c < dim; ++c) {
        for (int r = 0; r < dim; ++r) {
            m[static_cast<size_t>(c)][static_cast<size_t>(r)] =
                multiplier * randomMatrixEntry(p, dim, fpwidth);
        }
    }
    return m;
}

std::vector<std::vector<std::vector<double>>> makeMatrices(int seed, int dim, int fpwidth) {
    Prng p(static_cast<uint32_t>(seed));
    std::vector<std::vector<std::vector<double>>> out;
    out.reserve(kNumSamples);
    for (int i = 0; i < kNumSamples; ++i) {
        out.push_back(randomSquareMatrix(p, dim, fpwidth));
    }
    return out;
}

ParamsBuilder allSourcesDims(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("dim", {2, 3, 4});
}
ParamsBuilder constDims(ParamsBuilder u) {
    return u.combine("inputSource", {"const"}).combine("dim", {2, 3, 4});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }

} // namespace

CTS_TEST(g, "abstract_float").params(constDims).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    auto cases = fp::generateDeterminantCases(fp::FPKind::Abstract, makeMatrices(dim + 64, dim, 32),
                                              true);
    run(t, builtin("determinant"), {matType(dim, dim, ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, 0, cases);
});

CTS_TEST(g, "f32").params(allSourcesDims).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    auto cases = fp::generateDeterminantCases(fp::FPKind::F32, makeMatrices(dim + 32, dim, 32),
                                              isConst(t));
    run(t, builtin("determinant"), {matType(dim, dim, ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(allSourcesDims).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
