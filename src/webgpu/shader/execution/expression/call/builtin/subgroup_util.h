// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroup_util.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Shared engine for the subgroup/quad builtin execution test family. This is a
// faithful port of upstream's subgroup_util.ts: it performs the bespoke
// compute/fragment dispatch + subgroup_size readback + per-lane validation used
// by every subgroup/quad spec (it does NOT use the generic expression run()).
//
// Exports (matching upstream): kNumCases, kStride, kWGSizes, kPredicateCases,
// kDataSentinel, kFramebufferSizes, runComputeTest, runFragmentTest,
// runAccuracyTest, getUintsPerFramebuffer, generateTypedInputs.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "webgpu/shader/execution/expression/floating_point.h"

namespace cts {
namespace subgroups {

// The floating-point interval framework lives in cts::expression::fp.
using FPKind = expression::fp::FPKind;
using FPInterval = expression::fp::FPInterval;

// Subgroup tests use the AllFeaturesMaxLimits fixture (upstream SubgroupTest
// extends AllFeaturesMaxLimitsGPUTest).
using SubgroupTest = AllFeaturesMaxLimitsGpuTest;

// Number of accuracy cases (the params combine count for runAccuracyTest seeds).
constexpr uint32_t kNumCases = 1000;

// Number of invocations exercised per subgroup metadata slot in accuracy tests.
constexpr uint32_t kStride = 128;

// Repeat the bit pattern every 16 bits for use with 16-bit types.
constexpr uint32_t kDataSentinel = 999u | (999u << 16);

// Workgroup sizes exercised by the compute-path specs ([x, y, z]).
using WGSize = std::array<uint32_t, 3>;
const std::vector<WGSize>& kWGSizes();

// Encodes/decodes a [x,y,z] workgroup size to/from the JSON-array param string
// used as the query value (e.g. "[4,1,1]"). The harness Value type cannot hold
// arrays, so the tuple round-trips through a string param.
std::string wgSizeToString(const WGSize& size);
WGSize wgSizeFromString(const std::string& text);

// Framebuffer sizes exercised by the fragment-path specs ([width, height]).
// Minimum size is [3, 3].
using FramebufferSize = std::array<uint32_t, 2>;
const std::vector<FramebufferSize>& kFramebufferSizes();
std::string framebufferSizeToString(const FramebufferSize& size);
FramebufferSize framebufferSizeFromString(const std::string& text);

// A predicate case: a WGSL condition string and the matching host-side filter
// over (subgroup_invocation_id, subgroup_size).
struct PredicateCase {
    std::string name;
    std::string cond;
    std::function<bool(uint32_t id, uint32_t size)> filter;
};

// The named predicate cases (every_even, every_odd, lower_half, upper_half,
// first_two), in upstream key order.
const std::vector<PredicateCase>& kPredicateCases();
const PredicateCase& predicateCaseByName(const std::string& name);

// Number of uints per framebuffer row and per texel for the given format/size.
struct UintsPerFramebuffer {
    uint32_t uintsPerRow;
    uint32_t uintsPerTexel;
};
UintsPerFramebuffer getUintsPerFramebuffer(WGPUTextureFormat format, uint32_t width, uint32_t height);

// Runs a compute-shader subgroup test.
//
// Assumptions (matching upstream):
//   * group(0) binding(0) is a storage buffer for input data
//   * group(0) binding(1) is an output storage buffer of
//     outputUintsPerElement * wgThreads uints
//   * group(0) binding(2) is an output storage buffer of 2 * wgThreads uints
//
// The output and metadata buffers are filled with kDataSentinel (never the
// expected values), so a missing write is detected. checkFunction receives the
// (metadata, output) readbacks and returns an error string on mismatch.
void runComputeTest(
    SubgroupTest& t,
    const std::string& wgsl,
    const WGSize& wgSize,
    uint32_t outputUintsPerElement,
    const std::vector<uint32_t>& inputData,
    const std::function<std::optional<std::string>(
        const std::vector<uint32_t>& metadata,
        const std::vector<uint32_t>& output)>& checkFunction);

// Runs a subgroup builtin test for fragment shaders.
//
// Draws a full-screen triangle into a `format` framebuffer of `width`x`height`.
// fsShader: location 0 output is the framebuffer; group(0) binding(0) is the
// uniform input data. checker receives the read-back framebuffer (as uints) and
// returns an error string on mismatch.
//
// inputElemSize selects the element interpretation of inputData (4 = u32/f32,
// 2 = packed f16); each input element is expanded to a vec4-stride slot.
enum class FragmentInputKind {
    U32,
    F32,
    F16,
};
void runFragmentTest(
    SubgroupTest& t,
    WGPUTextureFormat format,
    const std::string& fsShader,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& inputData,
    FragmentInputKind inputKind,
    const std::function<std::optional<std::string>(const std::vector<uint32_t>& data)>& checker);

// Runs a floating-point accuracy subgroup test (f16 or f32 reduction).
//
// seed selects deterministic input indices/values via the CTS PRNG; operation
// is the WGSL builtin name; identity is the operation identity; intervalGen
// produces the acceptance interval for the binary reduction of two values.
void runAccuracyTest(
    SubgroupTest& t,
    uint32_t seed,
    const WGSize& wgSize,
    const std::string& operation,
    FPKind type,
    double identity,
    const std::function<FPInterval(double x, double y)>& intervalGen);

// Generates input bit patterns for a builtin's data type, packed into uints.
// 16-bit types are packed two-per-uint. `elements` is the vector width
// (1 for scalar); `requiresF16` selects the f16 packing path. The four
// "interesting" scalar bit patterns are supplied by the caller (they are
// type-specific and live in the spec's binary-type helpers).
std::vector<uint32_t> generateTypedInputs(
    const std::array<uint32_t, 4>& scalarValues,
    uint32_t elements,
    bool requiresF16);

} // namespace subgroups
} // namespace cts
