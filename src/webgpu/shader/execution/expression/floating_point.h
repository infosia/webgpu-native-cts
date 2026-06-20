// Ported from gpuweb/cts src/webgpu/util/floating_point.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// FP-interval acceptance framework. A faithful C++ port of the parts of upstream
// util/floating_point.ts (the FP model) and util/math.ts (range generators) needed by the
// representative FP builtins/operators ported in phaseY13 Stage B Stage 1 (abs/floor/ceil/trunc/
// sqrt/cos and f32 binary addition), plus the abstract-float (f64) path those builtins also test.
//
// The model produces, for each test case, a per-result-element acceptance interval [begin, end]
// (double, with +/-inf endpoints for the unbounded interval). These feed the expression run()
// harness via Case::expectedAccept (ExpectedElement::interval). For f32 results the interval is
// applied to the read-back f32 decoded as a double; for abstract-float results it is applied to
// the reconstructed f64. Subnormal flushing (FTZ) is folded into the interval endpoints, so the
// read-back value is not separately flushed (both flushed and unflushed land inside).

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

namespace cts {
namespace expression {
namespace fp {

// The floating-point kind the interval / traits operate on.
enum class FPKind {
    F32,
    Abstract, // abstract-float (f64 under the hood)
};

// A closed interval [begin, end] of floating-point numbers (doubles). The unbounded interval is
// [-inf, +inf]; contains(NaN) is true only for it. Mirrors upstream FPInterval. 'begin <= end'.
struct FPInterval {
    FPKind kind = FPKind::F32;
    double begin = 0.0;
    double end = 0.0;

    FPInterval() = default;
    FPInterval(FPKind k, double point) : kind(k), begin(point), end(point) {}
    FPInterval(FPKind k, double b, double e) : kind(k), begin(b), end(e) {}

    bool isPoint() const { return begin == end; }
    // Finite per the kind's finite range (f32: [-max, max]; abstract: any non-inf/non-NaN double).
    bool isFinite() const;
    // Whether n (or interval) is contained. NaN is contained iff this is the unbounded interval.
    bool contains(double n) const;
    bool containsInterval(const FPInterval& n) const { return begin <= n.begin && end >= n.end; }
};

// The f32 / abstract traits as free functions parameterized by kind. Only the subset needed by the
// Stage-1 builtins is implemented; everything else is intentionally absent (report, don't fake).

// The unbounded interval for the kind ([-inf, +inf]).
FPInterval unboundedInterval(FPKind kind);
// [begin, end] clamped/validated for the kind (no rounding; preserve unbounded if non-finite).
FPInterval toInterval(FPKind kind, double begin, double end);
FPInterval toIntervalPoint(FPKind kind, double n);

// The tightest interval containing all the given intervals (spanIntervals).
FPInterval spanIntervals(const std::vector<FPInterval>& intervals);

// Fundamental error intervals (around a point n):
FPInterval correctlyRoundedInterval(FPKind kind, double n);
FPInterval absoluteErrorInterval(FPKind kind, double n, double errorRange);
FPInterval ulpInterval(FPKind kind, double n, double numULP);

// Acceptance-interval generators for the representative builtins/operators.
FPInterval absInterval(FPKind kind, double n);
FPInterval floorInterval(FPKind kind, double n);
FPInterval ceilInterval(FPKind kind, double n);
FPInterval truncInterval(FPKind kind, double n);
FPInterval inverseSqrtInterval(FPKind kind, double n);
FPInterval divisionInterval(FPKind kind, double x, double y);
FPInterval sqrtInterval(FPKind kind, double n); // = 1 / inverseSqrt(n)
FPInterval cosInterval(FPKind kind, double n);  // absolute-error 2^-11 (f32), domain [-pi, pi]
FPInterval additionInterval(FPKind kind, double x, double y); // correctly-rounded x + y

// Quantize a double to the kind's representable set (f32: round-to-nearest; abstract: identity).
double quantize(FPKind kind, double n);

// Range generators (math.ts). f32 ranges always operate on f32 values; the abstract scalar range is
// a spread over the f64 bit space.
std::vector<double> scalarF32Range();
const std::vector<double>& sparseScalarF32Range(); // kInterestingF32Values
std::vector<std::vector<double>> sparseVectorF32Range(int dim);
std::vector<double> linearRange(double a, double b, int numSteps);
std::vector<double> scalarF64Range();

// fullI64Range (math.ts): biased spread over [i64.min, -1] ++ 0 ++ [1, i64.max]. Used by abs's
// abstract_int test (bit-exact, not interval). 50 negative + 0 + 50 positive = 101 values.
std::vector<int64_t> fullI64Range();

// ---------------------------------------------------------------------------
// Case builders (Case from expression.h) — mirror upstream generateScalarToIntervalCases etc.
// The interval ops take a single number and return an FPInterval (the per-builtin generators above
// with 'kind' bound). 'filter' == true means 'finite' (const): drop a case if its interval is
// non-finite; false means 'unfiltered'. Result kind controls how the interval is encoded onto the
// Case (F32 -> f32 input/result with expectedAccept floatWidth 32; Abstract -> abstract-float
// input/result with expectedAccept floatWidth 64).
// ---------------------------------------------------------------------------

using ScalarToInterval = std::function<FPInterval(double)>;
using ScalarPairToInterval = std::function<FPInterval(double, double)>;

// abs/floor/ceil/trunc/sqrt/cos: one f32 (or abstract) scalar in, one scalar interval out.
std::vector<Case> generateScalarToIntervalCases(
    FPKind kind,
    const std::vector<double>& params,
    bool finiteFilter,
    const ScalarToInterval& op);

// f32 binary addition (scalar): two scalars in, one scalar interval out. Cartesian product.
std::vector<Case> generateScalarPairToIntervalCases(
    FPKind kind,
    const std::vector<double>& param0s,
    const std::vector<double>& param1s,
    bool finiteFilter,
    const ScalarPairToInterval& op);

// f32 binary addition (vector . scalar): vector + scalar -> vector interval (per-element op).
std::vector<Case> generateVectorScalarToVectorCases(
    FPKind kind,
    const std::vector<std::vector<double>>& vectors,
    const std::vector<double>& scalars,
    bool finiteFilter,
    const ScalarPairToInterval& op); // op(vectorElement, scalar)

// f32 binary addition (scalar . vector): scalar + vector -> vector interval (per-element op).
std::vector<Case> generateScalarVectorToVectorCases(
    FPKind kind,
    const std::vector<double>& scalars,
    const std::vector<std::vector<double>>& vectors,
    bool finiteFilter,
    const ScalarPairToInterval& op); // op(scalar, vectorElement)

} // namespace fp
} // namespace expression
} // namespace cts
