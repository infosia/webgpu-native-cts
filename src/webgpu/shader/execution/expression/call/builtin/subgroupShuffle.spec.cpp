// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupShuffle.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupShuffle, subgroupShuffleUp, subgroupShuffleDown,
// and subgroupShuffleXor.
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
    "shader,execution,expression,call,builtin,subgroupShuffle",
    "Execution tests for subgroupShuffle, subgroupShuffleUp, subgroupShuffleDown, and "
    "subgroupShuffleXor.");

void skipIfNoSubgroups(sg::SubgroupTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
        t.skip("subgroups feature not available");
    }
}
void skipIfNoF16(sg::SubgroupTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// kUpDownOps; kOps = ['subgroupShuffle', 'subgroupShuffleXor', shuffleUp, shuffleDown].
std::vector<Value> kUpDownOps() {
    return {Value(std::string("subgroupShuffleUp")), Value(std::string("subgroupShuffleDown"))};
}
std::vector<Value> kOps() {
    return {Value(std::string("subgroupShuffle")), Value(std::string("subgroupShuffleXor")),
            Value(std::string("subgroupShuffleUp")), Value(std::string("subgroupShuffleDown"))};
}

constexpr uint32_t kNumCases = 16;

const std::vector<bt::Type>& kTypes() {
    static const std::vector<bt::Type> v = {
        bt::scalar(bt::ScalarKind::I32), bt::vec(2, bt::ScalarKind::I32),
        bt::vec(3, bt::ScalarKind::I32), bt::vec(4, bt::ScalarKind::I32),
        bt::scalar(bt::ScalarKind::U32), bt::vec(2, bt::ScalarKind::U32),
        bt::vec(3, bt::ScalarKind::U32), bt::vec(4, bt::ScalarKind::U32),
        bt::scalar(bt::ScalarKind::F16), bt::vec(2, bt::ScalarKind::F16),
        bt::vec(3, bt::ScalarKind::F16), bt::vec(4, bt::ScalarKind::F16),
        bt::scalar(bt::ScalarKind::F32), bt::vec(2, bt::ScalarKind::F32),
        bt::vec(3, bt::ScalarKind::F32), bt::vec(4, bt::ScalarKind::F32),
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
bool requiresF16(const bt::Type& ty) { return ty.kind == bt::ScalarKind::F16; }

std::array<uint32_t, 4> generateScalarValues(const bt::Type& type) {
    switch (bt::scalarTypeOf(type).kind) {
        case bt::ScalarKind::U32:
            return {0x00000000u, 0xffffffffu, 1111u, 2222u};
        case bt::ScalarKind::I32:
            return {0x00000000u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
        case bt::ScalarKind::F32:
            return {0x00000000u, 0x7f7ffffeu, 0xff7ffffeu, 0xbf800000u};
        case bt::ScalarKind::F16:
            return {0x0000u, 0x7bfeu, 0xfbfeu, 0xbc00u};
        default:
            return {0u, 0u, 0u, 0u};
    }
}
std::vector<uint32_t> generateTypedInputs(const bt::Type& type) {
    const std::array<uint32_t, 4> scalarValues = generateScalarValues(type);
    uint32_t elements = 1;
    if (type.isVector()) {
        elements = static_cast<uint32_t>(type.width);
    }
    return sg::generateTypedInputs(scalarValues, elements, requiresF16(type));
}

// This size is selected to guarantee a single subgroup.
constexpr int kSize = 4;

// ---------------------------------------------------------------------------
// shuffle,id
// ---------------------------------------------------------------------------

struct ShuffleCase {
    std::string name;
    std::string id; // WGSL expression
    std::function<uint32_t(const std::vector<uint32_t>& input, int id)> expected;
};
const std::vector<ShuffleCase>& kShuffleCases() {
    static const std::vector<ShuffleCase> cases = {
        {"no_shuffle", "id", [](const std::vector<uint32_t>& in, int id) { return in[id]; }},
        {"broadcast", "input[2]", [](const std::vector<uint32_t>& in, int) { return in[2]; }},
        {"rotate_1_up", "select(id - 1, " + std::to_string(kSize) + " - 1, id == 0)",
         [](const std::vector<uint32_t>& in, int id) {
             const int idx = id == 0 ? kSize - 1 : id - 1;
             return in[idx];
         }},
        {"rotate_2_down", "(id + 2) % " + std::to_string(kSize),
         [](const std::vector<uint32_t>& in, int id) {
             const int idx = (id + 2) % kSize;
             return in[idx];
         }},
        {"reversed", std::to_string(kSize) + " - id - 1",
         [](const std::vector<uint32_t>& in, int id) { return in[kSize - id - 1]; }},
        {"clamped", "clamp(id + 2, 1, 3)",
         [](const std::vector<uint32_t>& in, int id) {
             const int idx = std::max(std::min(id + 2, 3), 1);
             return in[idx];
         }},
    };
    return cases;
}
const ShuffleCase& shuffleCaseByName(const std::string& name) {
    for (const ShuffleCase& c : kShuffleCases()) {
        if (c.name == name) {
            return c;
        }
    }
    std::abort();
}

std::optional<std::string> checkShuffleId(
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    const std::function<uint32_t(const std::vector<uint32_t>&, int)>& expected) {
    for (int i = 0; i < kSize; ++i) {
        const uint32_t expect = expected(input, i);
        const uint32_t res = output[i];
        if (res != expect) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": incorrect results\n- expected: " << expect
                << "\n-      got: " << res;
            return msg.str();
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "shuffle,id")
    .desc("Tests various ways to shuffle invocations")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (const ShuffleCase& c : kShuffleCases()) {
            cases.emplace_back(c.name);
        }
        return u.combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const ShuffleCase& testcase = shuffleCaseByName(t.param<std::string>("case"));

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : array<u32>; // unused\n\n"
             << "@compute @workgroup_size(" << kSize << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << ") {\n"
             << "  _ = metadata[0];\n\n"
             << "  output[id] = subgroupShuffle(input[id], " << testcase.id << ");\n"
             << "}";

        std::vector<uint32_t> inputData(kSize);
        for (int x = 0; x < kSize; ++x) {
            inputData[x] = x;
        }
        sg::runComputeTest(
            t, wgsl.str(), {kSize, 1, 1}, 1, inputData,
            [inputData, &testcase](
                const std::vector<uint32_t>& /*metadata*/,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkShuffleId(output, inputData, testcase.expected);
            });
    });

// ---------------------------------------------------------------------------
// shuffleUpDown,delta
// ---------------------------------------------------------------------------

struct UpDownCase {
    std::string name;
    std::string delta;
    std::string diagnostic;
    // returns {has, value}
    std::function<std::pair<bool, uint32_t>(const std::vector<uint32_t>&, int, const std::string&)>
        expected;
};
const std::vector<UpDownCase>& kUpDownCases() {
    static const std::vector<UpDownCase> cases = {
        {"no_shuffle", "0", "error",
         [](const std::vector<uint32_t>& in, int id, const std::string&) {
             return std::make_pair(true, in[id]);
         }},
        {"dynamic_1", "input[1]", "off",
         [](const std::vector<uint32_t>& in, int id, const std::string& op) {
             int idx;
             if (op == "subgroupShuffleUp") {
                 idx = id - 1;
                 if (idx < 0) {
                     return std::make_pair(false, 0u);
                 }
                 return std::make_pair(true, in[idx]);
             }
             idx = id + 1;
             if (idx > 3) {
                 return std::make_pair(false, 0u);
             }
             return std::make_pair(true, in[idx]);
         }},
        {"override_2", "override_idx", "error",
         [](const std::vector<uint32_t>& in, int id, const std::string& op) {
             int idx;
             if (op == "subgroupShuffleUp") {
                 idx = id - 2;
                 if (idx < 0) {
                     return std::make_pair(false, 0u);
                 }
                 return std::make_pair(true, in[idx]);
             }
             idx = id + 2;
             if (idx > 3) {
                 return std::make_pair(false, 0u);
             }
             return std::make_pair(true, in[idx]);
         }},
    };
    return cases;
}
const UpDownCase& upDownCaseByName(const std::string& name) {
    for (const UpDownCase& c : kUpDownCases()) {
        if (c.name == name) {
            return c;
        }
    }
    std::abort();
}

std::optional<std::string> checkShuffleUpDownDelta(
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    const std::string& op,
    const std::function<
        std::pair<bool, uint32_t>(const std::vector<uint32_t>&, int, const std::string&)>&
        expected) {
    for (int i = 0; i < kSize; ++i) {
        const std::pair<bool, uint32_t> expect = expected(input, i, op);
        const uint32_t res = output[i];
        // Faithful to upstream: only compares when expect is truthy (non-zero).
        if (expect.first && expect.second != 0 && expect.second != res) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": incorrect results\n- expected: " << expect.second
                << "\n-      got: " << res;
            return msg.str();
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "shuffleUpDown,delta")
    .desc("Test ShuffleUp and ShuffleDown deltas")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (const UpDownCase& c : kUpDownCases()) {
            cases.emplace_back(c.name);
        }
        return u.combine("op", kUpDownOps()).combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string op = t.param<std::string>("op");
        const UpDownCase& testcase = upDownCaseByName(t.param<std::string>("case"));

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n"
             << "diagnostic(" << testcase.diagnostic << ", subgroup_uniformity);\n\n"
             << "override override_idx = 2u;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : array<u32>; // unused\n\n"
             << "@compute @workgroup_size(" << kSize << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << ") {\n"
             << "  _ = metadata[0];\n\n"
             << "  output[id] = " << op << "(input[id], " << testcase.delta << ");\n"
             << "}";

        std::vector<uint32_t> inputData(kSize);
        for (int x = 0; x < kSize; ++x) {
            inputData[x] = x;
        }
        sg::runComputeTest(
            t, wgsl.str(), {kSize, 1, 1}, 1, inputData,
            [inputData, op, &testcase](
                const std::vector<uint32_t>& /*metadata*/,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkShuffleUpDownDelta(output, inputData, op, testcase.expected);
            });
    });

// ---------------------------------------------------------------------------
// shuffleXor,mask
// ---------------------------------------------------------------------------

struct MaskCase {
    std::string name;
    std::string mask;
    uint32_t value;
    std::string diagnostic;
};
const std::vector<MaskCase>& kMaskCases() {
    static const std::vector<MaskCase> cases = {
        {"no_shuffle", "0", 0, "error"},
        {"dynamic_1", "input[1]", 1, "off"},
        {"override_2", "override_idx", 2, "error"},
        {"expr_3", "input[1] + input[2]", 3, "off"},
    };
    return cases;
}
const MaskCase& maskCaseByName(const std::string& name) {
    for (const MaskCase& c : kMaskCases()) {
        if (c.name == name) {
            return c;
        }
    }
    std::abort();
}

std::optional<std::string> checkShuffleMask(
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    uint32_t mask) {
    for (int i = 0; i < kSize; ++i) {
        const uint32_t expect = input[i ^ mask];
        const uint32_t res = output[i];
        if (res != expect) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": incorrect result\n- expected: " << expect
                << "\n-      got: " << res;
            return msg.str();
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "shuffleXor,mask")
    .desc("Test ShuffleXor masks")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (const MaskCase& c : kMaskCases()) {
            cases.emplace_back(c.name);
        }
        return u.combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const MaskCase& testcase = maskCaseByName(t.param<std::string>("case"));

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n"
             << "diagnostic(" << testcase.diagnostic << ", subgroup_uniformity);\n\n"
             << "override override_idx = 2u;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : array<u32>; // unused\n\n"
             << "@compute @workgroup_size(" << kSize << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << ") {\n"
             << "  _ = metadata[0];\n\n"
             << "  output[id] = subgroupShuffleXor(input[id], " << testcase.mask << ");\n"
             << "}";

        std::vector<uint32_t> inputData(kSize);
        for (int x = 0; x < kSize; ++x) {
            inputData[x] = x;
        }
        const uint32_t value = testcase.value;
        sg::runComputeTest(
            t, wgsl.str(), {kSize, 1, 1}, 1, inputData,
            [inputData, value](
                const std::vector<uint32_t>& /*metadata*/,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkShuffleMask(output, inputData, value);
            });
    });

// ---------------------------------------------------------------------------
// Randomized compute tests.
// ---------------------------------------------------------------------------

// Generate randomized inputs for testing shuffles.
std::vector<uint32_t> generateInputs(uint32_t seed, uint32_t numInputs) {
    struct PRNG {
        std::array<uint32_t, 4> s{};
        explicit PRNG(uint32_t seed) {
            s = {seed, 0x8f7011eeu, 0xfc78ff1fu, 0x3793fdffu};
            for (uint32_t i = 1; i < 8; i++) {
                const uint32_t prev = s[(i - 1u) & 3u];
                s[i & 3u] ^= i + 1812433253u * (prev ^ (prev >> 30));
            }
            for (uint32_t i = 0; i < 8; i++) {
                next();
            }
        }
        void next() {
            uint32_t x = (s[0] & 0x7fffffffu) ^ s[1] ^ s[2];
            uint32_t y = s[3];
            x ^= x << 1;
            y ^= (y >> 1) ^ x;
            s[0] = s[1];
            s[1] = s[2];
            s[2] = x ^ (y << 10);
            s[3] = y;
            if ((y & 1u) != 0u) {
                s[1] ^= 0x8f7011eeu;
                s[2] ^= 0xfc78ff1fu;
            }
        }
        uint32_t randomU32() {
            next();
            uint32_t t0 = s[3];
            uint32_t t1 = s[0] + (s[2] >> 8);
            t0 ^= t1;
            if ((t1 & 1u) != 0u) {
                t0 ^= 0x3793fdffu;
            }
            return t0;
        }
        uint32_t uniformInt(uint64_t n) {
            const uint64_t upper = 0x100000000ull;
            const uint64_t keep = upper - (upper % n);
            uint64_t c = 0;
            do {
                c = randomU32();
            } while (c >= keep);
            return static_cast<uint32_t>(c % n);
        }
    } prng(seed);

    uint32_t bound = 128;
    if (seed < kNumCases / 4) {
        bound = 8;
    } else if (seed < kNumCases / 2) {
        bound = 16;
    } else if (seed < 3 * (kNumCases / 4)) {
        bound = 32;
    }
    std::vector<uint32_t> data(numInputs);
    for (uint32_t x = 0; x < numInputs; ++x) {
        data[x] = prng.uniformInt(bound);
    }
    return data;
}

// Returns the subgroup invocation id of the requested shuffle.
int64_t getShuffledId(int64_t id, int64_t value, const std::string& op) {
    if (op == "subgroupShuffle") {
        return value;
    }
    if (op == "subgroupShuffleUp") {
        return id - value;
    }
    if (op == "subgroupShuffleDown") {
        return id + value;
    }
    return id ^ value; // subgroupShuffleXor
}

// Checks results of compute passes.
std::optional<std::string> checkCompute(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    const std::string& op,
    uint32_t numInvs,
    const std::function<bool(uint32_t, uint32_t)>& filter) {
    std::map<uint32_t, std::vector<int64_t>> mapping;
    for (uint32_t inv = 0; inv < numInvs; ++inv) {
        const uint32_t id = metadata[inv];
        const uint32_t subgroupUniqueId = metadata[inv + numInvs];
        auto it = mapping.find(subgroupUniqueId);
        if (it == mapping.end()) {
            it = mapping.emplace(subgroupUniqueId, std::vector<int64_t>(128, -1)).first;
        }
        it->second[id] = inv;
    }

    for (uint32_t inv = 0; inv < numInvs; ++inv) {
        const uint32_t id = metadata[inv];
        const uint32_t subgroupUniqueId = metadata[inv + numInvs];
        auto it = mapping.find(subgroupUniqueId);
        const std::vector<int64_t>& subInvIdToInvIdx = it->second;

        const uint32_t res = output[inv];
        const uint32_t size = output[inv + numInvs];

        if (!filter(id, size)) {
            continue;
        }

        uint32_t inputValue = input[inv];
        if (op != "subgroupShuffle") {
            const int firstSubgroupInvId = 0;
            inputValue = input[subInvIdToInvIdx[firstSubgroupInvId]];
        }

        const int64_t shuffledTargetId = getShuffledId(id, inputValue, op);
        if (shuffledTargetId < 0 || shuffledTargetId >= 128 ||
            subInvIdToInvIdx[shuffledTargetId] == -1) {
            continue;
        }
        if (!filter(static_cast<uint32_t>(shuffledTargetId), size)) {
            continue;
        }
        if (static_cast<int64_t>(res) != subInvIdToInvIdx[shuffledTargetId]) {
            std::ostringstream msg;
            msg << "Invocation " << inv << ": unexpected result\n- expected: "
                << subInvIdToInvIdx[shuffledTargetId] << "\n-      got: " << res
                << "\n-      id = " << id << "\n-      size = " << size
                << "\n-      inputValue = " << inputValue
                << "\n-      shuffled_target_id = " << shuffledTargetId
                << "\n-      subgroup_unique_id = " << subgroupUniqueId;
            return msg.str();
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "compute,all_active")
    .desc("Test randomized inputs across many workgroup sizes")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumCases; ++x) {
            cases.emplace_back(x);
        }
        return u.combine("wgSize", wgSizes).combine("op", kOps()).beginSubcases().combine("case",
                                                                                           cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const std::string op = t.param<std::string>("op");
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::string selectValue = "input[lid]";
        if (op != "subgroupShuffle") {
            selectValue = "subgroupBroadcastFirst(input[lid])";
        }

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32, " << wgThreads << ">;\n\n"
             << "struct Output {\n"
             << "  res : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : Output;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_id : array<u32, " << wgThreads << ">\n"
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
             << "  metadata.id[lid] = id;\n"
             << "  metadata.subgroup_id[lid] = subgroupBroadcastFirst(lid + 1); // avoid 0\n\n"
             << "  output.size[lid] = subgroupSize;\n"
             << "  output.res[lid] = " << op << "(lid, " << selectValue << ");\n"
             << "}";

        const std::vector<uint32_t> inputArray =
            generateInputs(static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads);
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 2, inputArray,
            [inputArray, op, wgThreads](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, inputArray, op, wgThreads,
                                    [](uint32_t, uint32_t) { return true; });
            });
    });

CTS_TEST(g, "compute,split")
    .desc("Test randomized inputs across many workgroup sizes")
    .params([](ParamsBuilder u) {
        std::vector<Value> predicates;
        for (const sg::PredicateCase& c : sg::kPredicateCases()) {
            predicates.emplace_back(c.name);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("predicate", predicates)
            .combine("op", kOps())
            .beginSubcases()
            .combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::PredicateCase& testcase =
            sg::predicateCaseByName(t.param<std::string>("predicate"));
        const std::string op = t.param<std::string>("op");
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::string value = "input[lidx]";
        if (op != "subgroupShuffle") {
            value = "subgroupBroadcastFirst(input[lidx])";
        }

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32, " << wgThreads << ">;\n\n"
             << "struct Output {\n"
             << "  res : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : Output;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_id : array<u32, " << wgThreads << ">\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lidx : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n"
             << "  _ = input[0];\n"
             << "  metadata.id[lidx] = id;\n"
             << "  // Made from lidx but not lidx to avoid value confusion.\n"
             << "  var fake_unique_id = lidx + 1000;\n"
             << "  metadata.subgroup_id[lidx] = subgroupBroadcastFirst(fake_unique_id);\n\n"
             << "  output.size[lidx] = subgroupSize;\n"
             << "  let value = " << value << ";\n"
             << "  if " << testcase.cond << " {\n"
             << "    output.res[lidx] = " << op << "(lidx, value);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        std::vector<uint32_t> inputArray(wgThreads);
        for (uint32_t x = 0; x < wgThreads; ++x) {
            inputArray[x] = x % 128;
        }
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 2, inputArray,
            [inputArray, op, wgThreads, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, inputArray, op, wgThreads, filter);
            });
    });

// ---------------------------------------------------------------------------
// data_types
// ---------------------------------------------------------------------------

std::optional<std::string> checkDataTypes(
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    const std::string& op,
    int64_t id,
    const bt::Type& type) {
    if (requiresF16(type) && !type.isVector()) {
        for (int i = 0; i < 4; ++i) {
            const int64_t index = getShuffledId(i, id, op);
            if (index < 0 || index >= 4) {
                continue;
            }
            const int64_t expectIdx = index / 2;
            const bool expectShift = (index % 2) == 1;
            uint32_t expect = input[expectIdx];
            if (expectShift) {
                expect >>= 16;
            }
            expect &= 0xffffu;

            const int resIdx = i / 2;
            const bool resShift = (i % 2) == 1;
            uint32_t res = output[resIdx];
            if (resShift) {
                res >>= 16;
            }
            res &= 0xffffu;

            if (res != expect) {
                std::ostringstream msg;
                msg << i << ": incorrect result\n- expected: " << expect << "\n-      got: " << res;
                return msg.str();
            }
        }
    } else {
        uint32_t uints = 1;
        if (type.isVector()) {
            uints = type.width == 3 ? 4u : static_cast<uint32_t>(type.width);
            if (requiresF16(type)) {
                uints = uints / 2;
            }
        }
        for (int i = 0; i < 4; ++i) {
            for (uint32_t j = 0; j < uints; ++j) {
                const int64_t index = getShuffledId(i, id, op);
                if (index < 0 || index >= 4) {
                    continue;
                }
                const uint32_t expect = input[index * uints + j];
                const uint32_t res = output[i * uints + j];
                if (res != expect) {
                    std::ostringstream msg;
                    msg << (uints * i + j) << ": incorrect result\n- expected: " << expect
                        << "\n-      got: " << res;
                    return msg.str();
                }
            }
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "data_types")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const bt::Type& ty : kTypes()) {
            types.emplace_back(ty.toString());
        }
        std::vector<Value> ids = {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(2)),
                                  Value(int64_t(3))};
        return u.combine("op", kOps()).combine("type", types).beginSubcases().combine("id", ids);
    })
    .fn([](sg::SubgroupTest& t) {
        const sg::WGSize wgSize{4, 1, 1};
        const bt::Type type = typeByName(t.param<std::string>("type"));
        skipIfNoSubgroups(t);
        if (requiresF16(type)) {
            skipIfNoF16(t);
        }
        const std::string op = t.param<std::string>("op");
        const int64_t id = t.param<int64_t>("id");

        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;";
        }
        std::ostringstream wgsl;
        wgsl << "\n" << enables << "\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<" << type.toString() << ">;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<" << type.toString() << ">;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : array<u32>; // unused\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << ") {\n"
             << "  _ = metadata[0];\n\n"
             << "  output[id] = " << op << "(input[id], " << id << ");\n"
             << "}";

        const std::vector<uint32_t> inputData = generateTypedInputs(type);
        uint32_t uintsPerOutput = 1;
        if (type.isVector()) {
            uintsPerOutput = type.width == 3 ? 4u : static_cast<uint32_t>(type.width);
            if (requiresF16(type)) {
                uintsPerOutput = uintsPerOutput / 2;
            }
        }
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, inputData,
            [inputData, op, id, type](
                const std::vector<uint32_t>& /*metadata*/,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkDataTypes(output, inputData, op, id, type);
            });
    });

// ---------------------------------------------------------------------------
// fragment
// ---------------------------------------------------------------------------

std::optional<std::string> checkFragment(
    const std::vector<uint32_t>& data,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height,
    int64_t shuffleId,
    const std::string& op) {
    if (width < 3 || height < 3) {
        std::ostringstream msg;
        msg << "Insufficient framebuffer size [" << width << "w x " << height
            << "h]. Minimum is [3w x 3h].";
        return msg.str();
    }
    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;
    auto coordToIndex = [&](uint32_t row, uint32_t col) {
        return static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
    };

    std::map<uint32_t, std::vector<int64_t>> mapping;
    for (uint32_t row = 0; row + 1 < height; ++row) {
        for (uint32_t col = 0; col + 1 < width; ++col) {
            const size_t offset = coordToIndex(row, col);
            const uint32_t id = data[offset + 1];
            const uint32_t subgroupId = data[offset + 2];
            auto it = mapping.find(subgroupId);
            if (it == mapping.end()) {
                it = mapping.emplace(subgroupId, std::vector<int64_t>(128, -1)).first;
            }
            it->second[id] = col + row * width;
        }
    }

    for (uint32_t row = 0; row + 1 < height; ++row) {
        for (uint32_t col = 0; col + 1 < width; ++col) {
            const size_t offset = coordToIndex(row, col);
            const uint32_t res = data[offset];
            const uint32_t id = data[offset + 1];
            const uint32_t subgroupId = data[offset + 2];

            auto it = mapping.find(subgroupId);
            const std::vector<int64_t>& subgroupMapping = it->second;

            const int64_t index = getShuffledId(id, shuffleId, op);
            if (index < 0 || index >= 128 || subgroupMapping[index] == -1) {
                continue;
            }
            const int64_t shuffleLinear = subgroupMapping[index];
            const uint32_t shuffleRow = static_cast<uint32_t>(shuffleLinear / width);
            const uint32_t shuffleCol = static_cast<uint32_t>(shuffleLinear % width);
            if (shuffleRow == height - 1 || shuffleCol == width - 1) {
                continue;
            }
            if (static_cast<int64_t>(res) != subgroupMapping[index]) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect results:\n- expected: "
                    << subgroupMapping[index] << "\n-      got: " << res;
                return msg.str();
            }
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "fragment")
    .desc("Test shuffles in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<Value> ids = {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(2)),
                                  Value(int64_t(3))};
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rgba32uint"))}}};
        return u.combine("size", sizes)
            .beginSubcases()
            .combine("op", kOps())
            .combine("id", ids)
            .combineWithParams(formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const std::string op = t.param<std::string>("op");
        const int64_t id = t.param<int64_t>("id");
        const WGPUTextureFormat format = WGPUTextureFormat_RGBA32Uint;

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, 1>; // unused\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << "  @builtin(subgroup_invocation_id) id : u32,\n"
                 << ") -> @location(0) vec4u {\n"
                 << "  _ = inputs[0];\n\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n"
                 << "  let subgroup_id = subgroupBroadcastFirst(linear + 1);\n\n"
                 << "  let x_in_range = u32(pos.x) < (" << size[0] << " - 1);\n"
                 << "  let y_in_range = u32(pos.y) < (" << size[1] << " - 1);\n"
                 << "  let in_range = x_in_range && y_in_range;\n\n"
                 << "  return vec4u(" << op << "(linear, " << id << "), id, subgroup_id, linear);\n"
                 << "}";

        const std::vector<uint32_t> inputData{0};
        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], inputData, sg::FragmentInputKind::U32,
            [size, id, op](
                const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkFragment(data, format, size[0], size[1], id, op);
            });
    });

} // namespace
