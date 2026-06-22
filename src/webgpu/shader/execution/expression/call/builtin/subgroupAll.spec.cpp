// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupAll.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupAll.
//
// Note: There is a lack of portability for non-uniform execution so these tests
// restrict themselves to uniform control flow.
// Note: There is no guaranteed mapping between subgroup_invocation_id and
// local_invocation_index. Tests should avoid assuming there is.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/expression/call/builtin/subgroup_util.h"

using namespace cts;
namespace sg = cts::subgroups;

namespace {

TestGroup<sg::SubgroupTest> g = MakeTestGroup<sg::SubgroupTest>(
    "shader,execution,expression,call,builtin,subgroupAll",
    "Execution tests for subgroupAll");

void skipIfNoSubgroups(sg::SubgroupTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
        t.skip("subgroups feature not available");
    }
}

// TinyMT PRNG (faithful port of util/prng.ts). Deterministic per seed.
class PRNG {
  public:
    explicit PRNG(uint32_t seed) {
        state_[0] = seed;
        state_[1] = kMat1;
        state_[2] = kMat2;
        state_[3] = kTMat;
        for (uint32_t i = 1; i < kMinLoop; i++) {
            const uint32_t prev = state_[(i - 1u) & 3u];
            state_[i & 3u] ^= i + 1812433253u * (prev ^ (prev >> 30));
        }
        for (uint32_t i = 0; i < kPreLoop; i++) {
            next();
        }
    }

    uint32_t randomU32() {
        next();
        return temper();
    }

    uint32_t uniformInt(uint64_t n) {
        const uint64_t upperBound = 0x100000000ull;
        const uint64_t keepZoneSize = upperBound - (upperBound % n);
        uint64_t candidate = 0;
        do {
            candidate = randomU32();
        } while (candidate >= keepZoneSize);
        return static_cast<uint32_t>(candidate % n);
    }

  private:
    static constexpr uint32_t kMat1 = 0x8f7011eeu;
    static constexpr uint32_t kMat2 = 0xfc78ff1fu;
    static constexpr uint32_t kTMat = 0x3793fdffu;
    static constexpr uint32_t kMask = 0x7fffffffu;
    static constexpr uint32_t kMinLoop = 8;
    static constexpr uint32_t kPreLoop = 8;
    static constexpr uint32_t kSH0 = 1;
    static constexpr uint32_t kSH1 = 10;
    static constexpr uint32_t kSH8 = 8;

    void next() {
        uint32_t x = (state_[0] & kMask) ^ state_[1] ^ state_[2];
        uint32_t y = state_[3];
        x ^= x << kSH0;
        y ^= (y >> kSH0) ^ x;
        state_[0] = state_[1];
        state_[1] = state_[2];
        state_[2] = x ^ (y << kSH1);
        state_[3] = y;
        if ((y & 1u) != 0u) {
            state_[1] ^= kMat1;
            state_[2] ^= kMat2;
        }
    }

    uint32_t temper() {
        uint32_t t0 = state_[3];
        uint32_t t1 = state_[0] + (state_[2] >> kSH8);
        t0 ^= t1;
        if ((t1 & 1u) != 0u) {
            t0 ^= kTMat;
        }
        return t0;
    }

    std::array<uint32_t, 4> state_{};
};

constexpr uint32_t kNumCases = 15;

// Generate input data for testing.
//
// Data is generated in the following categories:
// Seed 0 generates all 0 data
// Seed 1 generates all 1 data
// Seeds 2-9 generates all 1s except for a zero randomly once per 32 elements
// Seeds 10+ generate all random data
std::vector<uint32_t> generateInputData(uint32_t seed, uint32_t num) {
    PRNG prng(seed);

    const uint32_t bound = std::min(num, 32u);
    const uint32_t index = prng.uniformInt(bound);

    std::vector<uint32_t> data(num);
    for (uint32_t x = 0; x < num; ++x) {
        if (seed == 0) {
            data[x] = 0;
        } else if (seed == 1) {
            data[x] = 1;
        } else if (seed < 10) {
            const uint32_t bounded = x % bound;
            data[x] = bounded == index ? 0u : 1u;
        } else {
            data[x] = prng.uniformInt(2);
        }
    }
    return data;
}

// Checks the result of a subgroupAll operation.
std::optional<std::string> checkAll(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    uint32_t numInvs,
    const std::vector<uint32_t>& input,
    const std::function<bool(uint32_t id, uint32_t size)>& filter) {
    // First, generate expected results.
    std::map<uint32_t, uint32_t> expected;
    for (uint32_t inv = 0; inv < numInvs; ++inv) {
        const uint32_t size = metadata[inv];
        const uint32_t id = metadata[inv + numInvs];
        if (!filter(id, size)) {
            continue;
        }
        const uint32_t subgroupId = output[numInvs + inv];
        auto it = expected.find(subgroupId);
        uint32_t v = it == expected.end() ? 1u : it->second;
        v &= input[inv];
        expected[subgroupId] = v;
    }

    // Second, check against actual results.
    for (uint32_t inv = 0; inv < numInvs; ++inv) {
        const uint32_t size = metadata[inv];
        const uint32_t id = metadata[inv + numInvs];
        const uint32_t res = output[inv];
        if (filter(id, size)) {
            const uint32_t subgroupId = output[numInvs + inv];
            auto it = expected.find(subgroupId);
            const uint32_t expectedV = it == expected.end() ? 0u : it->second;
            if (expectedV != res) {
                std::ostringstream msg;
                msg << "Invocation " << inv << ":\n- expected: " << expectedV
                    << "\n-      got: " << res;
                return msg.str();
            }
        } else {
            if (res != sg::kDataSentinel) {
                std::ostringstream msg;
                msg << "Invocation " << inv << " unexpected write:\n- subgroup invocation id: " << id
                    << "\n-          subgroup size: " << size;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "compute,all_active")
    .desc("Test compute subgroupAll")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumCases; ++x) {
            cases.emplace_back(x);
        }
        return u.combine("wgSize", wgSizes).beginSubcases().combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  subgroup_size: array<u32, " << wgThreads << ">,\n"
             << "  subgroup_invocation_id: array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n"
             << "  metadata.subgroup_size[lid] = subgroupSize;\n\n"
             << "  metadata.subgroup_invocation_id[lid] = id;\n\n"
             << "  // Record a representative subgroup id.\n"
             << "  outputs[lid + " << wgThreads << "] = subgroupBroadcastFirst(lid);\n\n"
             << "  let res = select(0u, 1u, subgroupAll(bool(inputs[lid])));\n"
             << "  outputs[lid] = res;\n"
             << "}";

        const std::vector<uint32_t> inputData =
            generateInputData(static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads);

        const uint32_t uintsPerOutput = 2;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, inputData,
            [wgThreads, inputData](const std::vector<uint32_t>& metadata,
                                   const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkAll(metadata, output, wgThreads, inputData,
                                [](uint32_t /*id*/, uint32_t /*size*/) { return true; });
            });
    });

CTS_TEST(g, "compute,split")
    .desc("Test that only active invocation participate")
    .params([](ParamsBuilder u) {
        std::vector<Value> predicates;
        for (const sg::PredicateCase& c : sg::kPredicateCases()) {
            predicates.emplace_back(c.name);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumCases; ++x) {
            cases.emplace_back(x);
        }
        return u.combine("predicate", predicates)
            .beginSubcases()
            .combine("wgSize", wgSizes)
            .combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::PredicateCase& testcase =
            sg::predicateCaseByName(t.param<std::string>("predicate"));
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  subgroup_size : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_invocation_id : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n"
             << "  metadata.subgroup_size[lid] = subgroupSize;\n\n"
             << "  // Record subgroup invocation id for this invocation.\n"
             << "  metadata.subgroup_invocation_id[lid] = id;\n\n"
             << "  // Record a generated subgroup id.\n"
             << "  outputs[" << wgThreads << " + lid] = subgroupBroadcastFirst(lid);\n\n"
             << "  if " << testcase.cond << " {\n"
             << "    outputs[lid] = select(0u, 1u, subgroupAll(bool(inputs[lid])));\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        const std::vector<uint32_t> inputData =
            generateInputData(static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads);

        const uint32_t uintsPerOutput = 2;
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, inputData,
            [wgThreads, inputData, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkAll(metadata, output, wgThreads, inputData, filter);
            });
    });

// Checks subgroupAll results from a fragment shader.
std::optional<std::string> checkFragmentAll(
    const std::vector<uint32_t>& data,
    const std::vector<uint32_t>& input,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height) {
    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;

    // Iteration skips last row and column to avoid helper invocations because it is not
    // guaranteed whether or not they participate in the subgroup operation.
    std::map<uint32_t, uint32_t> expected;
    for (uint32_t row = 0; row < height - 1; ++row) {
        for (uint32_t col = 0; col < width - 1; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t subgroupId = data[offset + 1];

            if (subgroupId == 0) {
                std::ostringstream msg;
                msg << "Internal error: helper invocation at (" << col << ", " << row << ")";
                return msg.str();
            }

            auto it = expected.find(subgroupId);
            uint32_t v = it == expected.end() ? 1u : it->second;
            // First index of input is an atomic counter.
            v &= input[row * width + col];
            expected[subgroupId] = v;
        }
    }

    for (uint32_t row = 0; row < height - 1; ++row) {
        for (uint32_t col = 0; col < width - 1; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t res = data[offset];
            const uint32_t subgroupId = data[offset + 1];

            if (subgroupId == 0) {
                // Inactive in the fragment.
                continue;
            }

            auto it = expected.find(subgroupId);
            const uint32_t expectedV = it == expected.end() ? 0u : it->second;
            if (expectedV != res) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect results:\n- expected: "
                    << expectedV << "\n-      got: " << res;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "fragment,all_active")
    .desc("Tests subgroupAll in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumCases; ++x) {
            cases.emplace_back(x);
        }
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rg32uint"))}}};
        return u.combine("size", sizes).beginSubcases().combine("case", cases).combineWithParams(
            formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const WGPUTextureFormat format = WGPUTextureFormat_RG32Uint;
        const uint32_t numInputs = size[0] * size[1];
        const std::vector<uint32_t> inputData =
            generateInputData(static_cast<uint32_t>(t.param<int64_t>("case")), numInputs);

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, " << inputData.size() << ">;\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << ") -> @location(0) vec2u {\n"
                 << "  // Generate a subgroup id based on linearized position, but avoid 0.\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n"
                 << "  var subgroup_id = linear + 1;\n"
                 << "  subgroup_id = subgroupBroadcastFirst(subgroup_id);\n\n"
                 << "  // Filter out possible helper invocations.\n"
                 << "  let x_in_range = u32(pos.x) < (" << size[0] << " - 1);\n"
                 << "  let y_in_range = u32(pos.y) < (" << size[1] << " - 1);\n"
                 << "  let in_range = x_in_range && y_in_range;\n"
                 << "  let input = select(1u, inputs[linear].x, in_range);\n\n"
                 << "  let res = select(0u, 1u, subgroupAll(bool(input)));\n"
                 << "  return vec2u(res, subgroup_id);\n"
                 << "}";

        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], inputData, sg::FragmentInputKind::U32,
            [inputData, size](
                const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkFragmentAll(data, inputData, WGPUTextureFormat_RG32Uint, size[0],
                                        size[1]);
            });
    });

// Using subgroup operations in control with fragment shaders
// quickly leads to unportable behavior.
CTS_TEST(g, "fragment,split").unimplemented();

} // namespace
