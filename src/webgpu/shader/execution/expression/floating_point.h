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
    F16,      // IEEE-754 binary16 (half)
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
FPInterval subtractionInterval(FPKind kind, double x, double y); // correctly-rounded x - y
FPInterval multiplicationInterval(FPKind kind, double x, double y); // correctly-rounded x * y
FPInterval negationInterval(FPKind kind, double n); // correctly-rounded -n
// remainder(x, y) = x - y*trunc(x/y) at f32 (inherited) accuracy; 'kind' selects materialization.
FPInterval remainderInterval(FPKind kind, double x, double y);

// --- Matrix acceptance-interval functions (phaseY13 Stage B/4) ---
// All operate at f32 precision (inherited). Matrices are column-major m[col][row]. The result is a
// column-major grid of intervals result[col][row]. additionMatrixMatrix/subtractionMatrixMatrix are
// componentwise; the multiplication variants accumulate dot products (order-independent sums).
std::vector<std::vector<FPInterval>> additionMatrixMatrixInterval(
    FPKind kind, const std::vector<std::vector<double>>& x,
    const std::vector<std::vector<double>>& y);
std::vector<std::vector<FPInterval>> subtractionMatrixMatrixInterval(
    FPKind kind, const std::vector<std::vector<double>>& x,
    const std::vector<std::vector<double>>& y);
std::vector<std::vector<FPInterval>> multiplicationMatrixScalarInterval(
    FPKind kind, const std::vector<std::vector<double>>& mat, double scalar);
std::vector<std::vector<FPInterval>> multiplicationMatrixMatrixInterval(
    FPKind kind, const std::vector<std::vector<double>>& x,
    const std::vector<std::vector<double>>& y);
std::vector<FPInterval> multiplicationMatrixVectorInterval(
    FPKind kind, const std::vector<std::vector<double>>& mat, const std::vector<double>& v);
std::vector<FPInterval> multiplicationVectorMatrixInterval(
    FPKind kind, const std::vector<double>& v, const std::vector<std::vector<double>>& mat);

// --- Transcendental builtin acceptance intervals (phaseY13 Stage B/2) ---
// These are all defined via the f32 trait (inherited accuracy), so 'kind' selects only the input/
// output materialization and the domain constants. The math is computed at f32 precision.
// Trig:
FPInterval sinInterval(FPKind kind, double n);   // absolute-error 2^-11, domain [-pi, pi]
FPInterval tanInterval(FPKind kind, double n);   // sin(n) / cos(n)
FPInterval asinInterval(FPKind kind, double n);  // atan2 + abs-error, domain [-1, 1]
FPInterval acosInterval(FPKind kind, double n);  // atan2 + abs-error, domain [-1, 1]
FPInterval atanInterval(FPKind kind, double n);  // ULP(4096)
FPInterval atan2Interval(FPKind kind, double y, double x); // ULP(4096), domain-split + extrema
// Hyperbolic:
FPInterval sinhInterval(FPKind kind, double n);  // (exp(n) - exp(-n)) * 0.5
FPInterval coshInterval(FPKind kind, double n);  // (exp(n) + exp(-n)) * 0.5
FPInterval tanhInterval(FPKind kind, double n);  // sinh/cosh + abs-error
FPInterval asinhInterval(FPKind kind, double n); // log(x + sqrt(x*x + 1))
FPInterval atanhInterval(FPKind kind, double n); // log((1+x)/(1-x)) * 0.5
// acosh has two formulations; both are tested (acosh_alt and acosh_primary).
FPInterval acoshAlternativeInterval(FPKind kind, double n); // log(x + sqrt((x+1)*(x-1)))
FPInterval acoshPrimaryInterval(FPKind kind, double n);     // log(x + sqrt(x*x - 1))
// Exp/log/pow:
FPInterval expInterval(FPKind kind, double n);   // ULP(3 + 2*|n|)
FPInterval exp2Interval(FPKind kind, double n);  // ULP(3 + 2*|n|)
FPInterval logInterval(FPKind kind, double n);   // abs-error 2^-21 on [0.5,2], else ULP(3); domain >0
FPInterval log2Interval(FPKind kind, double n);  // abs-error 2^-21 on [0.5,2], else ULP(3); domain >0
FPInterval powInterval(FPKind kind, double x, double y); // exp2(y * log2(x))

// --- Algebraic / multi-arg builtin acceptance intervals (phaseY13 Stage B/3a) ---
// sign / round / fract / step / saturate / degrees / radians / ldexp / quantizeToF16:
FPInterval signInterval(FPKind kind, double n);     // correctly-rounded +/-1/0
FPInterval roundInterval(FPKind kind, double n);    // ties-to-even correctly-rounded
FPInterval fractInterval(FPKind kind, double n);    // x - floor(x), with the 1.0 span rule
FPInterval saturateInterval(FPKind kind, double n); // clamp(n, 0, 1) via min(max)
FPInterval degreesInterval(FPKind kind, double n);  // n * 57.29577... (f32-precision math)
FPInterval radiansInterval(FPKind kind, double n);  // n * 0.01745... (f32-precision math)
// step returns one of [0,0], [1,1], [0,1] (either 0 or 1), or unbounded. Encoded as anyOf.
FPInterval stepInterval(FPKind kind, double edge, double x);
// ldexp(e1, e2): correctly-rounded e1 * 2^e2 (e2 integer). The flush-to-zero special case
// (e2 + bias <= 0 also accepts 0) is applied by the case generator, not here.
FPInterval ldexpInterval(FPKind kind, double e1, double e2);
// quantizeToF16(n): f16-correctly-rounded value (f16 rounding even though the value is f32).
FPInterval quantizeToF16Interval(double n);
// clamp has two formulations (both tested via anyOf): median and min(max).
FPInterval clampMedianInterval(FPKind kind, double x, double y, double z);
FPInterval clampMinMaxInterval(FPKind kind, double x, double low, double high);
// min / max: correctly-rounded.
FPInterval minInterval(FPKind kind, double x, double y);
FPInterval maxInterval(FPKind kind, double x, double y);
// mix has two formulations (both tested via anyOf): imprecise x+(y-x)*z and precise x*(1-z)+y*z.
FPInterval mixImpreciseInterval(FPKind kind, double x, double y, double z);
FPInterval mixPreciseInterval(FPKind kind, double x, double y, double z);
// smoothstep(low, high, x): composed Hermite interval.
FPInterval smoothStepInterval(FPKind kind, double low, double high, double x);
// fma(x, y, z): correctly-rounded single-rounding (additionInterval(multiplicationInterval(x,y),z)).
FPInterval fmaInterval(FPKind kind, double x, double y, double z);

// The exponent bias constant for the kind (f32: 127; abstract: 1023). Used by ldexp's f-t-z rule.
int fpBias(FPKind kind);
// isFinite for the kind, on a raw double.
bool fpIsFinite(FPKind kind, double n);

// Range helper used by several transcendental caches.
std::vector<double> biasedRange(double a, double b, int numSteps);

// f16 kValue constants exposed for the quantizeToF16 case range.
double f16PositiveMin();
double f16PositiveMax();
double f16NegativeMin();
double f16NegativeMax();
double f16PositiveSubMin();
double f16PositiveSubMax();
double f16NegativeSubMin();
double f16NegativeSubMax();

// kValue constants exposed for case-range construction.
double f32PositiveMin();
double f32NegativeMax();
double f32NegativeMin();
double f64PositiveMin();
double f64NegativeMax();
double f64NegativeMin();
// positive/negative.less_than_one: the value closest to +/-1 with magnitude < 1, per kind.
double positiveLessThanOne(FPKind kind);
double negativeLessThanOne(FPKind kind);

// Quantize a double to the kind's representable set (f32: round-to-nearest; abstract: identity).
double quantize(FPKind kind, double n);

// Range generators (math.ts). f32 ranges always operate on f32 values; the abstract scalar range is
// a spread over the f64 bit space.
std::vector<double> scalarF32Range();
const std::vector<double>& sparseScalarF32Range(); // kInterestingF32Values
const std::vector<double>& sparseScalarF64Range(); // kInterestingF64Values
// Sparse range for the kind (f32 -> sparseScalarF32Range, abstract -> sparseScalarF64Range).
const std::vector<double>& sparseScalarRange(FPKind kind);
std::vector<std::vector<double>> sparseVectorF32Range(int dim);
std::vector<std::vector<double>> sparseVectorF64Range(int dim);
// sparseVectorRange for the kind (f32 -> sparseVectorF32Range, abstract -> sparseVectorF64Range).
std::vector<std::vector<double>> sparseVectorRange(FPKind kind, int dim);
std::vector<double> linearRange(double a, double b, int numSteps);
std::vector<double> scalarF64Range();
std::vector<double> scalarF32Range();
// Count-parameterized scalar ranges (math.ts scalarF32Range/scalarF64Range with explicit counts).
// E.g. f32/af arithmetic negation uses {neg_norm:250, neg_sub:20, pos_sub:20, pos_norm:250}.
std::vector<double> scalarF32RangeCounts(int negNorm, int negSub, int posSub, int posNorm);
std::vector<double> scalarF64RangeCounts(int negNorm, int negSub, int posSub, int posNorm);
// scalarRange for the kind (f32 -> scalarF32Range, abstract -> scalarF64Range).
std::vector<double> scalarRangeForKind(FPKind kind);
// scalarF16Range (math.ts): f16 values spread over the f16 bit space. Used by quantizeToF16 and the
// f16 builtin variants. Default counts {pos_sub:10, pos_norm:50, neg_*=pos_*}.
std::vector<double> scalarF16Range();
// sparseScalarF16Range (math.ts kInterestingF16Values): minimal f16 coverage set.
const std::vector<double>& sparseScalarF16Range();
// Dense / sparse vector + sparse matrix f16 ranges (kVectorF16Values / kSparseVectorF16Values /
// kSparseMatrixF16Values). Same template shape as the f32/f64 variants.
std::vector<std::vector<double>> vectorF16Range(int dim);
std::vector<std::vector<double>> sparseVectorF16Range(int dim);
std::vector<std::vector<std::vector<double>>> sparseMatrixF16Range(int cols, int rows);
// sparseI32Range (math.ts kInterestingI32Values). Used by ldexp non-const.
const std::vector<int32_t>& sparseI32Range();
// quantizeToI32 (math.ts): round toward zero into the i32 range.
int32_t quantizeToI32(double n);

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
using ScalarTripleToInterval = std::function<FPInterval(double, double, double)>;

// abs/floor/ceil/trunc/sqrt/cos: one f32 (or abstract) scalar in, one scalar interval out.
std::vector<Case> generateScalarToIntervalCases(
    FPKind kind,
    const std::vector<double>& params,
    bool finiteFilter,
    const ScalarToInterval& op);

// acosh: one scalar in, accepted by anyOf(...) of multiple intervals (two formulations). The
// 'finite' filter drops a case if ANY interval is non-finite.
std::vector<Case> generateScalarToIntervalCasesAnyOf(
    FPKind kind,
    const std::vector<double>& params,
    bool finiteFilter,
    const std::vector<ScalarToInterval>& ops);

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

// min/max: two scalars in, one scalar interval out (Cartesian product). Same shape as the pair
// case above but accepting a non-anyOf op; reuses generateScalarPairToIntervalCases.

// step: two scalars in, accepted by anyOf(...) of multiple intervals (the [0,1] -> {0 or 1} case).
std::vector<Case> generateScalarPairToIntervalCasesAnyOf(
    FPKind kind,
    const std::vector<double>& param0s,
    const std::vector<double>& param1s,
    bool finiteFilter,
    const ScalarPairToInterval& op);

// clamp/saturate/smoothstep/fma/mix scalar: three scalars in, one scalar interval out (or anyOf of
// several formulations). finiteFilter drops a case if ANY of the per-op intervals is non-finite.
std::vector<Case> generateScalarTripleToIntervalCases(
    FPKind kind,
    const std::vector<double>& param0s,
    const std::vector<double>& param1s,
    const std::vector<double>& param2s,
    bool finiteFilter,
    const std::vector<ScalarTripleToInterval>& ops);

// ldexp: (f32/abstract scalar e1) x (i32/abstract-int e2) -> scalar interval, with the
// e2 + bias <= 0 -> anyOf(interval, 0) flush rule. 'e2IsAbstractInt' selects the e2 input type.
std::vector<Case> generateLdexpCases(
    FPKind kind,
    const std::vector<double>& e1s,
    const std::vector<int32_t>& e2s,
    bool finiteFilter, // 'const' also requires e1 * 2^e2 finite (caller pre-filters); see impl
    bool constStage,
    bool e2IsAbstractInt);

// mix vec-pair-scalar: (vecN<f32> e1) x (vecN<f32> e2) x (scalar e3) -> vecN interval, component-
// wise, accepted by anyOf of the per-formulation per-component intervals.
std::vector<Case> generateVectorPairScalarToVectorComponentWiseCase(
    FPKind kind,
    const std::vector<std::vector<double>>& param0s,
    const std::vector<std::vector<double>>& param1s,
    const std::vector<double>& param2s,
    bool finiteFilter,
    const std::vector<ScalarTripleToInterval>& componentWiseOps);

// --- phaseY13 Stage B/3b: geometric / matrix / decomposition Case generators ---
// All compute acceptance intervals at f32 precision (inherited accuracy). 'kind' selects only the
// input/output materialization (F32 vs Abstract f64) and the encoded interval result-element width.
// 'finiteFilter' == true ('finite'/const) drops a case if any of its result intervals is non-finite.

// length(scalar): one scalar in, scalar interval out (= sqrt(n*n)). Uses the scalar range.
std::vector<Case> generateLengthScalarCases(FPKind kind, const std::vector<double>& params,
                                            bool finiteFilter);
// length(vecN): one vector in, scalar interval out (= sqrt(dot(v,v))).
std::vector<Case> generateLengthVectorCases(FPKind kind,
                                            const std::vector<std::vector<double>>& vectors,
                                            bool finiteFilter);
// distance(scalar, scalar): two scalars in, scalar interval out.
std::vector<Case> generateDistanceScalarCases(FPKind kind, const std::vector<double>& param0s,
                                              const std::vector<double>& param1s, bool finiteFilter);
// distance(vecN, vecN): two vectors in, scalar interval out. Cartesian product.
std::vector<Case> generateDistanceVectorCases(FPKind kind,
                                              const std::vector<std::vector<double>>& v0s,
                                              const std::vector<std::vector<double>>& v1s,
                                              bool finiteFilter);
// dot(vecN, vecN): two vectors in, scalar interval out (order-independent sum). Cartesian product.
std::vector<Case> generateDotCases(FPKind kind, const std::vector<std::vector<double>>& v0s,
                                   const std::vector<std::vector<double>>& v1s, bool finiteFilter);
// normalize(vecN): one vector in, vector interval out.
std::vector<Case> generateNormalizeCases(FPKind kind,
                                         const std::vector<std::vector<double>>& vectors,
                                         bool finiteFilter);
// cross(vec3, vec3): two vec3s in, vec3 interval out. Cartesian product.
std::vector<Case> generateCrossCases(FPKind kind, const std::vector<std::vector<double>>& v0s,
                                     const std::vector<std::vector<double>>& v1s, bool finiteFilter);
// reflect(vecN, vecN): two vectors in, vector interval out. Cartesian product.
std::vector<Case> generateReflectCases(FPKind kind, const std::vector<std::vector<double>>& v0s,
                                       const std::vector<std::vector<double>>& v1s,
                                       bool finiteFilter);
// refract(vecN, vecN, scalar): two vectors + scalar in, vector interval out (upstream refractInterval).
std::vector<Case> generateRefractCases(FPKind kind, const std::vector<std::vector<double>>& iVs,
                                       const std::vector<std::vector<double>>& sVs,
                                       const std::vector<double>& rs, bool finiteFilter);
// faceForward(vecN, vecN, vecN): three vectors in, vector interval out via anyOf of the candidates.
std::vector<Case> generateFaceForwardCases(FPKind kind,
                                           const std::vector<std::vector<double>>& xs,
                                           const std::vector<std::vector<double>>& ys,
                                           const std::vector<std::vector<double>>& zs,
                                           bool finiteFilter);
// determinant(matCxC): one square matrix in, scalar interval out.
std::vector<Case> generateDeterminantCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& matrices, bool finiteFilter);
// transpose(matCxR): one CxR matrix in, RxC matrix interval out (correctly-rounded element move).
std::vector<Case> generateTransposeCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& matrices, bool finiteFilter);
// modf(scalar/vecN): correctly-rounded fract or whole part. 'whole' selects whole vs fract.
std::vector<Case> generateModfScalarCases(FPKind kind, const std::vector<double>& params, bool whole);
std::vector<Case> generateModfVectorCases(FPKind kind,
                                          const std::vector<std::vector<double>>& vectors, bool whole);
// frexp(scalar/vecN): correctly-rounded fract part, or exact i32/abstract-int exp part. Subnormal
// (non-zero) inputs are skipped (the result is implementation-defined for them).
std::vector<Case> generateFrexpScalarFractCases(FPKind kind, const std::vector<double>& params);
std::vector<Case> generateFrexpScalarExpCases(FPKind kind, const std::vector<double>& params);
std::vector<Case> generateFrexpVectorFractCases(FPKind kind,
                                                const std::vector<std::vector<double>>& vectors);
std::vector<Case> generateFrexpVectorExpCases(FPKind kind,
                                              const std::vector<std::vector<double>>& vectors);

// Dense vector ranges (math.ts vectorF32Range/vectorF64Range; kVectorF32Values/kVectorF64Values).
std::vector<std::vector<double>> vectorF32Range(int dim);
std::vector<std::vector<double>> vectorF64Range(int dim);
std::vector<std::vector<double>> vectorRange(FPKind kind, int dim);
// Sparse matrix ranges (math.ts sparseMatrixF32Range/sparseMatrixF64Range). Column-major m[col][row].
std::vector<std::vector<std::vector<double>>> sparseMatrixF32Range(int cols, int rows);
std::vector<std::vector<std::vector<double>>> sparseMatrixF64Range(int cols, int rows);
std::vector<std::vector<std::vector<double>>> sparseMatrixRange(FPKind kind, int cols, int rows);
// kInterestingF32Values / kInterestingF64Values (math.ts), as the scalar sparse ranges already are.
const std::vector<double>& interestingF32Values();
const std::vector<double>& interestingF64Values();
// Sparse scalar range for the kind (used by refract's r param). f32 -> sparseScalarF32Range.
// (sparseScalarRange already declared above.)

// --- phaseY13 Stage B/4: binary operator Case generators (matrix / vector results) ---
// MatrixPair op: two CxR matrices -> CxR (or, for mat*mat, KxR x CxK -> CxR) interval matrix.
using MatrixPairToMatrix = std::function<std::vector<std::vector<FPInterval>>(
    const std::vector<std::vector<double>>&, const std::vector<std::vector<double>>&)>;
using MatrixScalarToMatrix = std::function<std::vector<std::vector<FPInterval>>(
    const std::vector<std::vector<double>>&, double)>;
using ScalarMatrixToMatrix = std::function<std::vector<std::vector<FPInterval>>(
    double, const std::vector<std::vector<double>>&)>;
using MatrixToMatrix =
    std::function<std::vector<std::vector<FPInterval>>(const std::vector<std::vector<double>>&)>;
using MatrixVectorToVector = std::function<std::vector<FPInterval>(
    const std::vector<std::vector<double>>&, const std::vector<double>&)>;
using VectorMatrixToVector = std::function<std::vector<FPInterval>(
    const std::vector<double>&, const std::vector<std::vector<double>>&)>;

// matrix + matrix / matrix * matrix: Cartesian product of the matrix lists. 'finiteFilter' drops a
// case whose result has any non-finite element.
std::vector<Case> generateMatrixPairToMatrixCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& xs,
    const std::vector<std::vector<std::vector<double>>>& ys, bool finiteFilter,
    const MatrixPairToMatrix& op);
// matrix * scalar.
std::vector<Case> generateMatrixScalarToMatrixCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& mats,
    const std::vector<double>& scalars, bool finiteFilter, const MatrixScalarToMatrix& op);
// scalar * matrix.
std::vector<Case> generateScalarMatrixToMatrixCases(
    FPKind kind, const std::vector<double>& scalars,
    const std::vector<std::vector<std::vector<double>>>& mats, bool finiteFilter,
    const ScalarMatrixToMatrix& op);
// matrix -> matrix (unary conversion, e.g. f32(mat) correctly-rounded). 'resultKind' is the output
// materialization (e.g. F32 for f32(abstract_mat)); 'kind' is the input materialization.
std::vector<Case> generateMatrixToMatrixCases(
    FPKind inputKind, FPKind resultKind, const std::vector<std::vector<std::vector<double>>>& mats,
    bool finiteFilter, const MatrixToMatrix& op);
// matrix * vector.
std::vector<Case> generateMatrixVectorToVectorCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& mats,
    const std::vector<std::vector<double>>& vecs, bool finiteFilter,
    const MatrixVectorToVector& op);
// vector * matrix.
std::vector<Case> generateVectorMatrixToVectorCases(
    FPKind kind, const std::vector<std::vector<double>>& vecs,
    const std::vector<std::vector<std::vector<double>>>& mats, bool finiteFilter,
    const VectorMatrixToVector& op);

// f32 comparison (bool result, exact). One scalar pair in, a bool out via 'truthFunc', with the
// subnormal-flush anyOf rule (any of the flushed/unflushed truth values accepted). Mirrors the
// upstream makeCase. 'pairs' is vectorF32Range(2) / vectorF64Range(2) (each a [lhs, rhs]).
using TruthFunc = std::function<bool(double, double)>;
std::vector<Case> generateComparisonCases(FPKind kind,
                                          const std::vector<std::vector<double>>& pairs,
                                          const TruthFunc& truthFunc);

// selectNCases (case.ts): deterministically select n of the cases by crc32 of the input spelling.
// 'dis' is the discriminator string ('mix_scalar' / 'mix_vector'). When n >= cases.size() returns
// all of them. Operates on whatever Case list is passed (the input spelling is reconstructed from
// the case's scalar inputs to match upstream's c.input.toString()).
std::vector<Case> selectNCases(const std::string& dis, size_t n, const std::vector<Case>& cases);

} // namespace fp
} // namespace expression
} // namespace cts
