// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupBitwise.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupAnd, subgroupOr, and subgroupXor.
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
#include "webgpu/shader/validation/expression/binary/binary_types.h"

using namespace cts;
namespace sg = cts::subgroups;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<sg::SubgroupTest> g = MakeTestGroup<sg::SubgroupTest>(
    "shader,execution,expression,call,builtin,subgroupBitwise",
    "Execution tests for subgroupAnd, subgroupOr, and subgroupXor");

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

// kOps = ['subgroupAnd', 'subgroupOr', 'subgroupXor']
std::vector<Value> kOps() {
    return {Value(std::string("subgroupAnd")), Value(std::string("subgroupOr")),
            Value(std::string("subgroupXor"))};
}

// kTypes = kConcreteSignedIntegerScalarsAndVectors ++ kConcreteUnsignedIntegerScalarsAndVectors.
// These are exactly the first 8 entries of kConcreteNumericScalarsAndVectors
// (i32, vec2i, vec3i, vec4i, u32, vec2u, vec3u, vec4u).
const std::vector<bt::Type>& kTypes() {
    static const std::vector<bt::Type> v = {
        bt::scalar(bt::ScalarKind::I32), bt::vec(2, bt::ScalarKind::I32),
        bt::vec(3, bt::ScalarKind::I32), bt::vec(4, bt::ScalarKind::I32),
        bt::scalar(bt::ScalarKind::U32), bt::vec(2, bt::ScalarKind::U32),
        bt::vec(3, bt::ScalarKind::U32), bt::vec(4, bt::ScalarKind::U32),
    };
    return v;
}
bt::Type typeByName(const std::string& name) {
    for (const bt::Type& ty : kTypes()) {
        if (ty.toString() == name) {
            return ty;
        }
    }
    return bt::scalar(bt::ScalarKind::U32);
}

// Performs the appropriate bitwise operation on v1 and v2.
uint32_t bitwise(const std::string& op, uint32_t v1, uint32_t v2) {
    if (op == "subgroupAnd") {
        return v1 & v2;
    }
    if (op == "subgroupOr") {
        return v1 | v2;
    }
    return v1 ^ v2; // subgroupXor
}

// Returns the identity value for the subgroup operations.
uint32_t identity(const std::string& op) {
    if (op == "subgroupAnd") {
        return ~0u;
    }
    return 0u; // subgroupOr, subgroupXor
}

// Checks the results for data type test.
std::optional<std::string> checkDataTypes(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const bt::Type& type,
    const std::string& op,
    uint32_t offset) {
    std::map<uint32_t, uint32_t> expected;
    const size_t halfMeta = metadata.size() / 2;
    for (size_t i = 0; i < halfMeta; ++i) {
        const uint32_t groupId = metadata[i + halfMeta];
        auto it = expected.find(groupId);
        uint32_t expect = it == expected.end() ? identity(op) : it->second;
        expect = bitwise(op, expect, static_cast<uint32_t>(i) + offset);
        expected[groupId] = expect;
    }

    uint32_t numEles = 1;
    uint32_t stride = 1;
    if (type.isVector()) {
        numEles = static_cast<uint32_t>(type.width);
        stride = numEles == 3 ? 4u : numEles;
    }
    for (size_t inv = 0; inv < output.size() / stride; ++inv) {
        const uint32_t groupId = metadata[inv + halfMeta];
        auto it = expected.find(groupId);
        const uint32_t expect = it == expected.end() ? 0u : it->second;
        for (uint32_t ele = 0; ele < numEles; ++ele) {
            const uint32_t res = output[inv * stride + ele];
            if (res != expect) {
                std::ostringstream msg;
                msg << "Invocation " << inv << ", component " << ele
                    << ": incorrect result\n- expected: " << expect << "\n-      got: " << res;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "data_types")
    .desc("Tests allowed data types")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        std::vector<Value> types;
        for (const bt::Type& ty : kTypes()) {
            types.emplace_back(ty.toString());
        }
        return u.combine("type", types).beginSubcases().combine("wgSize", wgSizes).combine("op",
                                                                                            kOps());
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const bt::Type type = typeByName(t.param<std::string>("type"));
        const std::string op = t.param<std::string>("op");
        uint32_t numEles = 1;
        if (type.isVector()) {
            numEles = type.width == 3 ? 4u : static_cast<uint32_t>(type.width);
        }

        const bt::Type scalarTy = bt::scalarTypeOf(type);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<" << type.toString() << ">;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  group_id : array<u32, " << wgThreads << ">\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << ") {\n\n"
             << "  // Record subgroup invocation id for this invocation.\n"
             << "  metadata.id[lid] = id;\n\n"
             << "  // Record a unique id for this subgroup (avoid 0).\n"
             << "  let group_id = subgroupBroadcastFirst(lid + 1);\n"
             << "  metadata.group_id[lid] = group_id;\n\n"
             << "  outputs[lid] = " << op << "(" << type.toString() << "(" << scalarTy.toString()
             << "(lid + inputs[0])));\n"
             << "}";

        const uint32_t magicOffset = 0x7fff000f;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, numEles, std::vector<uint32_t>{magicOffset},
            [type, op](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkDataTypes(metadata, output, type, op, 0x7fff000f);
            });
    });

// Generates randomized input data.
//
// Case 0: All 0s
// Case 1: All 0xffffs
// Case 2-9: All identity values except an inverted value randomly every 32 values.
//           All values capped to 0xffff
// Case 10+: Random values in the range [0, 2 ** 30]
std::vector<uint32_t> generateInputData(uint32_t seed, uint32_t num, uint32_t identityValue) {
    PRNG prng(seed);

    const uint32_t bound = std::min(num, 32u);
    const uint32_t index = prng.uniformInt(bound);

    std::vector<uint32_t> data(num);
    for (uint32_t x = 0; x < num; ++x) {
        if (seed == 0) {
            data[x] = 0;
        } else if (seed == 1) {
            data[x] = 0xffff;
        } else if (seed < 10) {
            const uint32_t bounded = x % bound;
            uint32_t val = bounded == index ? ~identityValue : identityValue;
            val &= 0xffff;
            data[x] = val;
        } else {
            data[x] = prng.uniformInt(1u << 30);
        }
    }
    return data;
}

// Checks the result of compute tests.
std::optional<std::string> checkBitwiseCompute(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    const std::string& op,
    const std::function<bool(uint32_t id, uint32_t size)>& filter) {
    std::map<uint32_t, uint32_t> expected;
    for (size_t i = 0; i < output.size(); ++i) {
        const uint32_t groupId = metadata[i + output.size()];
        const uint32_t combo = metadata[i];
        const uint32_t id = combo & 0xffffu;
        const uint32_t size = (combo >> 16) & 0xffffu;
        if (filter(id, size)) {
            auto it = expected.find(groupId);
            uint32_t expect = it == expected.end() ? identity(op) : it->second;
            expect = bitwise(op, expect, input[i]);
            expected[groupId] = expect;
        }
    }

    for (size_t i = 0; i < output.size(); ++i) {
        const uint32_t groupId = metadata[i + output.size()];
        const uint32_t combo = metadata[i];
        const uint32_t id = combo & 0xffffu;
        const uint32_t size = (combo >> 16) & 0xffffu;
        const uint32_t res = output[i];
        if (filter(id, size)) {
            auto it = expected.find(groupId);
            const uint32_t expect = it == expected.end() ? 0u : it->second;
            if (res != expect) {
                std::ostringstream msg;
                msg << "Invocation " << i << ": incorrect result\n- expected: " << expect
                    << "\n-      got: " << res;
                return msg.str();
            }
        } else {
            if (res != sg::kDataSentinel) {
                std::ostringstream msg;
                msg << "Invocation " << i << ": unexpected write";
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "compute,all_active")
    .desc("Test bitwise operations with randomized inputs")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumCases; ++x) {
            cases.emplace_back(x);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("case", cases).beginSubcases().combine("wgSize", wgSizes).combine("op",
                                                                                           kOps());
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string op = t.param<std::string>("op");
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id_and_size : array<u32, " << wgThreads << ">,\n"
             << "  group_id : array<u32, " << wgThreads << ">\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) sg_size : u32,\n"
             << ") {\n\n"
             << "  // Record both subgroup invocation id and subgroup size in the same u32.\n"
             << "  // Subgroups sizes are in the range [4, 128] so both values fit.\n"
             << "  metadata.id_and_size[lid] = id | (sg_size << 16);\n\n"
             << "  // Record a unique id for this subgroup (avoid 0).\n"
             << "  let group_id = subgroupBroadcastFirst(lid + 1);\n"
             << "  metadata.group_id[lid] = group_id;\n\n"
             << "  outputs[lid] = " << op << "(inputs[lid]);\n"
             << "}";

        const std::vector<uint32_t> inputData = generateInputData(
            static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads, identity(op));
        const uint32_t uintsPerOutput = 1;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, inputData,
            [inputData, op](const std::vector<uint32_t>& metadata,
                            const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkBitwiseCompute(metadata, output, inputData, op,
                                           [](uint32_t /*id*/, uint32_t /*size*/) { return true; });
            });
    });

CTS_TEST(g, "compute,split")
    .desc("Test that only active invocations participate")
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
            .combine("op", kOps())
            .combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::PredicateCase& testcase =
            sg::predicateCaseByName(t.param<std::string>("predicate"));
        const std::string op = t.param<std::string>("op");
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
             << "  id_and_size : array<u32, " << wgThreads << ">,\n"
             << "  group_id : array<u32, " << wgThreads << ">\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n\n"
             << "  // Record both subgroup invocation id and subgroup size in the same u32.\n"
             << "  // Subgroups sizes are in the range [4, 128] so both values fit.\n"
             << "  metadata.id_and_size[lid] = id | (subgroupSize << 16);\n\n"
             << "  // Record a unique id for this subgroup (avoid 0).\n"
             << "  let group_id = subgroupBroadcastFirst(lid + 1);\n"
             << "  metadata.group_id[lid] = group_id;\n\n"
             << "  if " << testcase.cond << " {\n"
             << "    outputs[lid] = " << op << "(inputs[lid]);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        const std::vector<uint32_t> inputData = generateInputData(
            static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads, identity(op));
        const uint32_t uintsPerOutput = 1;
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, inputData,
            [inputData, op, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkBitwiseCompute(metadata, output, inputData, op, filter);
            });
    });

// Checks bitwise ops results from a fragment shader.
std::optional<std::string> checkBitwiseFragment(
    const std::vector<uint32_t>& data,
    const std::vector<uint32_t>& input,
    const std::string& op,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height) {
    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;

    // Determine if the subgroup should be included in the checks.
    std::map<uint32_t, bool> inBounds;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t subgroupId = data[offset + 1];
            if (subgroupId == 0) {
                std::ostringstream msg;
                msg << "Internal error: helper invocation at (" << col << ", " << row << ")";
                return msg.str();
            }

            auto it = inBounds.find(subgroupId);
            bool ok = it == inBounds.end() ? true : it->second;
            ok = ok && row != height - 1 && col != width - 1;
            inBounds[subgroupId] = ok;
        }
    }

    bool anyInBounds = false;
    for (const auto& kv : inBounds) {
        anyInBounds = anyInBounds || kv.second;
    }
    if (!anyInBounds) {
        // This variant would not reliably test behavior.
        return std::nullopt;
    }

    // Iteration skips subgroups in the last row or column to avoid helper
    // invocations because it is not guaranteed whether or not they participate
    // in the subgroup operation.
    std::map<uint32_t, uint32_t> expected;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t subgroupId = data[offset + 1];

            if (subgroupId == 0) {
                std::ostringstream msg;
                msg << "Internal error: helper invocation at (" << col << ", " << row << ")";
                return msg.str();
            }

            auto bit = inBounds.find(subgroupId);
            const bool subgroupInBounds = bit == inBounds.end() ? true : bit->second;
            if (!subgroupInBounds) {
                continue;
            }

            auto it = expected.find(subgroupId);
            uint32_t v = it == expected.end() ? identity(op) : it->second;
            v = bitwise(op, v, input[row * width + col]);
            expected[subgroupId] = v;
        }
    }

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t res = data[offset];
            const uint32_t subgroupId = data[offset + 1];

            if (subgroupId == 0) {
                // Inactive in the fragment.
                continue;
            }

            auto bit = inBounds.find(subgroupId);
            const bool subgroupInBounds = bit == inBounds.end() ? true : bit->second;
            if (!subgroupInBounds) {
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
    .desc("Tests bitwise operations in fragment shaders")
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
        return u.combine("size", sizes)
            .beginSubcases()
            .combine("case", cases)
            .combine("op", kOps())
            .combineWithParams(formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const WGPUTextureFormat format = WGPUTextureFormat_RG32Uint;
        const std::string op = t.param<std::string>("op");
        const uint32_t numInputs = size[0] * size[1];

        const sg::SubgroupSizes sizes = sg::getSubgroupSizes(t);
        const uint32_t innerTexels = (size[0] - 1) * (size[1] - 1);
        if (innerTexels < sizes.minSize) {
            t.skip("Too few texels to be reliable");
        }

        const std::vector<uint32_t> inputData = generateInputData(
            static_cast<uint32_t>(t.param<int64_t>("case")), numInputs, identity(op));

        const std::string ident = identity(op) == 0 ? "0" : "0xffffffff";
        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, " << inputData.size() << ">;\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << ") -> @location(0) vec2u {\n"
                 << "  // Generate a subgroup id based on linearized position, avoid 0.\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n"
                 << "  let subgroup_id = subgroupBroadcastFirst(linear + 1);\n\n"
                 << "  // Filter out possible helper invocations.\n"
                 << "  let x_in_range = u32(pos.x) < (" << size[0] << " - 1);\n"
                 << "  let y_in_range = u32(pos.y) < (" << size[1] << " - 1);\n"
                 << "  let in_range = x_in_range && y_in_range;\n"
                 << "  let input = select(" << ident << ", inputs[linear].x, in_range);\n\n"
                 << "  let res = " << op << "(input);\n"
                 << "  return vec2u(res, subgroup_id);\n"
                 << "}";

        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], inputData, sg::FragmentInputKind::U32,
            [inputData, op, size](
                const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkBitwiseFragment(data, inputData, op, WGPUTextureFormat_RG32Uint, size[0],
                                            size[1]);
            });
    });

CTS_TEST(g, "fragment,split").unimplemented();

} // namespace
