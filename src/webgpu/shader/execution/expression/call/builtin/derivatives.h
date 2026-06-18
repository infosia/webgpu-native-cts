// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/derivatives.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Shared harness for the derivative builtin family (dpdx/dpdy/fwidth and their
// Coarse/Fine variants). The render-quad harness and case input set are ported
// directly from upstream. The acceptance interval is computed self-contained in
// C++ rather than via upstream's FP-interval framework + serialized cache:
//   - derivative   = subtractionInterval(u0, u1)   (correctly-rounded, FTZ)
//   - fwidth       = additionInterval(absInterval(dpdx), absInterval(dpdy))
// These reproduce upstream's util/floating_point.ts subtraction/addition/abs
// acceptance intervals (input flushing, correctly-rounded result with the
// unbounded-precision-addition edge widening, output flushing via spanning).

#pragma once

#include <string>

#include "cts/gpu.h"

namespace cts {
namespace derivatives {

// The set of "interesting" f32 inputs, matching util/math.ts sparseScalarF32Range().
const std::vector<float>& sparseScalarF32Range();

// Runs the dpdx/dpdy (and Coarse/Fine) quad harness for the given builtin.
// vectorize == 0 means scalar; 2/3/4 select vecN. The result must lie within the
// acceptance interval of either the fine or coarse derivative (for non-Fine
// builtins both pairs are accepted, matching upstream's coarse fallback).
void runDerivativeTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& builtin,
    bool non_uniform_discard,
    int vectorize);

// Runs the fwidth (and Coarse/Fine) full-quad harness for the given builtin.
void runFWidthTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& builtin,
    bool non_uniform_discard,
    int vectorize);

} // namespace derivatives
} // namespace cts
