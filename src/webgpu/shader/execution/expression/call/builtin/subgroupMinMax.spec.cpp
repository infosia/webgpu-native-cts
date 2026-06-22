// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupMinMax.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupMin and subgroupMax.
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
namespace fp = cts::expression::fp;

namespace {

TestGroup<sg::SubgroupTest> g = MakeTestGroup<sg::SubgroupTest>(
    "shader,execution,expression,call,builtin,subgroupMinMax",
    "Execution tests for subgroupMin and subgroupMax.");

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

// kOps = ['subgroupMin', 'subgroupMax']
std::vector<Value> kOps() {
    return {Value(std::string("subgroupMin")), Value(std::string("subgroupMax"))};
}

const std::vector<bt::Type>& kDataTypes() {
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
    for (const bt::Type& ty : kDataTypes()) {
        if (ty.toString() == name) {
            return ty;
        }
    }
    return bt::scalar(bt::ScalarKind::U32);
}
bool requiresF16(const bt::Type& ty) { return ty.kind == bt::ScalarKind::F16; }

// generateScalarValues: 4 32-bit bit patterns of interesting values per scalar type.
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

// Identity (kValue) for f16/f32 min/max. Implementations may assume infinities
// are absent, so positive/negative max value is used instead of infinities.
double identity(const std::string& op, const std::string& type) {
    if (op == "subgroupMin") {
        return type == "f16" ? fp::f16PositiveMax() : 3.4028234663852886e+38; // f32.positive.max
    }
    return type == "f16" ? fp::f16NegativeMin() : -3.4028234663852886e+38; // f32.negative.min
}
sg::FPInterval intervalGen(const std::string& op, sg::FPKind kind, double x, double y) {
    if (op == "subgroupMin") {
        return fp::minInterval(kind, x, y);
    }
    return fp::maxInterval(kind, x, y);
}

CTS_TEST(g, "fp_accuracy")
    .desc("Tests the accuracy of floating-point min/max.")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (int64_t x = 0; x < sg::kNumCases; ++x) {
            cases.emplace_back(x);
        }
        std::vector<Value> wgSizes = {Value(sg::wgSizeToString({sg::kStride, 1, 1})),
                                      Value(sg::wgSizeToString({sg::kStride / 2, 2, 1}))};
        return u.combine("case", cases)
            .combine("type", {Value(std::string("f32")), Value(std::string("f16"))})
            .combine("op", kOps())
            .combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string typeName = t.param<std::string>("type");
        if (typeName == "f16") {
            skipIfNoF16(t);
        }
        const std::string op = t.param<std::string>("op");
        const sg::FPKind kind = typeName == "f16" ? sg::FPKind::F16 : sg::FPKind::F32;
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        sg::runAccuracyTest(t, static_cast<uint32_t>(t.param<int64_t>("case")), wgSize, op, kind,
                            identity(op, typeName),
                            [op, kind](double x, double y) { return intervalGen(op, kind, x, y); });
    });

// Checks subgroupMin/Max for allowed data types.
std::optional<std::string> checkDataTypes(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const bt::Type& type) {
    if (requiresF16(type) && !type.isVector()) {
        const uint32_t expected = metadata[0];
        const uint32_t expectF16 = expected & 0xffffu;
        for (int i = 0; i < 4; ++i) {
            const int index = i / 2;
            const bool shift = (i % 2) == 1;
            uint32_t res = output[index];
            if (shift) {
                res >>= 16;
            }
            res &= 0xffffu;
            if (res != expectF16) {
                std::ostringstream msg;
                msg << "Invocation " << i << ": incorrect results\n- expected: " << std::hex
                    << expectF16 << "\n-      got: " << res;
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
        for (uint32_t i = 0; i < 4; ++i) {
            for (uint32_t j = 0; j < uints; ++j) {
                const uint32_t expect = metadata[j];
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
    .desc("Test allowed data types")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const bt::Type& ty : kDataTypes()) {
            types.emplace_back(ty.toString());
        }
        std::vector<Value> idx = {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(2)),
                                  Value(int64_t(3))};
        return u.combine("op", kOps())
            .combine("type", types)
            .beginSubcases()
            .combine("idx1", idx)
            .combine("idx2", idx)
            .combine("idx1Id", idx);
    })
    .fn([](sg::SubgroupTest& t) {
        const sg::WGSize wgSize{4, 1, 1};
        const bt::Type type = typeByName(t.param<std::string>("type"));
        skipIfNoSubgroups(t);
        if (requiresF16(type)) {
            skipIfNoF16(t);
        }
        const std::string op = t.param<std::string>("op");
        const int64_t idx1 = t.param<int64_t>("idx1");
        const int64_t idx2 = t.param<int64_t>("idx2");
        const int64_t idx1Id = t.param<int64_t>("idx1Id");

        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;";
        }
        const std::string minMax = op == "subgroupMin" ? "min" : "max";
        std::ostringstream wgsl;
        wgsl << "\n" << enables << "\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<" << type.toString() << ">;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<" << type.toString() << ">;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : array<" << type.toString() << ">;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << ") {\n"
             << "  let value = select(input[" << idx2 << "], input[" << idx1 << "], id == " << idx1Id
             << ");\n"
             << "  output[id] = " << op << "(value);\n\n"
             << "  if (id == 0) {\n"
             << "    metadata[0] = " << minMax << "(input[" << idx1 << "], input[" << idx2 << "]);\n"
             << "  }\n"
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
            [type](const std::vector<uint32_t>& metadata,
                   const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkDataTypes(metadata, output, type);
            });
    });

// Returns randomized integers in [0, 2**30).
std::vector<uint32_t> generateInputData(uint32_t seed, uint32_t num) {
    // PRNG via the engine's runAccuracyTest seed semantics is internal; replicate
    // the TinyMT here for the integer compute/fragment cases.
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
    std::vector<uint32_t> data(num);
    for (uint32_t x = 0; x < num; ++x) {
        data[x] = prng.uniformInt(1u << 30);
    }
    return data;
}

// Checks results from compute shaders.
std::optional<std::string> checkCompute(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    uint32_t numInvs,
    const std::string& op,
    const std::function<bool(uint32_t, uint32_t)>& filter) {
    const uint32_t ident = op == "subgroupMin" ? 0x7fffffffu : 0u;
    std::map<uint32_t, uint32_t> expected;
    for (uint32_t i = 0; i < numInvs; ++i) {
        const uint32_t id = metadata[i];
        const uint32_t subgroupId = metadata[numInvs + i];
        const uint32_t size = output[numInvs + i];
        if (!filter(id, size)) {
            continue;
        }
        auto it = expected.find(subgroupId);
        uint32_t e = it == expected.end() ? ident : it->second;
        e = op == "subgroupMin" ? std::min(e, input[i]) : std::max(e, input[i]);
        expected[subgroupId] = e;
    }
    for (uint32_t i = 0; i < numInvs; ++i) {
        const uint32_t id = metadata[i];
        const uint32_t subgroupId = metadata[numInvs + i];
        const uint32_t size = output[numInvs + i];
        if (!filter(id, size)) {
            continue;
        }
        const uint32_t res = output[i];
        auto it = expected.find(subgroupId);
        const uint32_t e = it == expected.end() ? ident : it->second;
        if (res != e) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": incorrect result\n- expected: " << e
                << "\n-      got: " << res;
            return msg.str();
        }
    }
    return std::nullopt;
}

constexpr uint32_t kNumRandomCases = 15;

CTS_TEST(g, "compute,all_active")
    .desc(
        "Test subgroupMin/Max in compute shader with all active invocations and varied workgroup "
        "sizes")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumRandomCases; ++x) {
            cases.emplace_back(x);
        }
        return u.combine("op", kOps()).combine("wgSize", wgSizes).beginSubcases().combine("case",
                                                                                          cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string op = t.param<std::string>("op");
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
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
             << "  output.res[lid] = " << op << "(input[lid]);\n"
             << "}";

        const std::vector<uint32_t> inputData =
            generateInputData(static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads);
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 2, inputData,
            [inputData, wgThreads, op](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, inputData, wgThreads, op,
                                    [](uint32_t, uint32_t) { return true; });
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
        for (int64_t x = 0; x < kNumRandomCases; ++x) {
            cases.emplace_back(x);
        }
        return u.combine("op", kOps())
            .combine("predicate", predicates)
            .beginSubcases()
            .combine("wgSize", wgSizes)
            .combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string op = t.param<std::string>("op");
        const sg::PredicateCase& testcase =
            sg::predicateCaseByName(t.param<std::string>("predicate"));
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
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
             << "  if " << testcase.cond << " {\n"
             << "    output.res[lid] = " << op << "(input[lid]);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        const std::vector<uint32_t> inputData =
            generateInputData(static_cast<uint32_t>(t.param<int64_t>("case")), wgThreads);
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 2, inputData,
            [inputData, wgThreads, op, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, inputData, wgThreads, op, filter);
            });
    });

// Checks min/max ops results from a fragment shader.
std::optional<std::string> checkFragment(
    const std::vector<uint32_t>& data,
    const std::vector<uint32_t>& input,
    const std::string& op,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height) {
    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;

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
        return std::nullopt;
    }

    const uint32_t ident = op == "subgroupMin" ? 0x7fffffffu : 0u;
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
            uint32_t v = it == expected.end() ? ident : it->second;
            const uint32_t in = input[row * width + col];
            v = op == "subgroupMin" ? std::min(v, in) : std::max(v, in);
            expected[subgroupId] = v;
        }
    }

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t res = data[offset];
            const uint32_t subgroupId = data[offset + 1];
            if (subgroupId == 0) {
                continue;
            }
            auto bit = inBounds.find(subgroupId);
            const bool subgroupInBounds = bit == inBounds.end() ? true : bit->second;
            if (!subgroupInBounds) {
                continue;
            }
            auto it = expected.find(subgroupId);
            const uint32_t expectedV = it == expected.end() ? ident : it->second;
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

CTS_TEST(g, "fragment")
    .desc("Test subgroupMin/Max in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<Value> cases;
        for (int64_t x = 0; x < kNumRandomCases; ++x) {
            cases.emplace_back(x);
        }
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rg32uint"))}}};
        return u.combine("size", sizes)
            .combine("op", kOps())
            .beginSubcases()
            .combine("case", cases)
            .combineWithParams(formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string op = t.param<std::string>("op");
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const WGPUTextureFormat format = WGPUTextureFormat_RG32Uint;
        const uint32_t numInputs = size[0] * size[1];

        const uint32_t subgroupMinSize = sg::getSubgroupSizes(t).minSize;

        const uint32_t innerTexels = (size[0] - 1) * (size[1] - 1);
        if (innerTexels < subgroupMinSize) {
            t.skip("Too few texels to be reliable");
        }

        const std::vector<uint32_t> inputData =
            generateInputData(static_cast<uint32_t>(t.param<int64_t>("case")), numInputs);
        const std::string ident = op == "subgroupMin" ? "0x7fffffff" : "0";

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, " << inputData.size() << ">;\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << ") -> @location(0) vec2u {\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n"
                 << "  let subgroup_id = subgroupBroadcastFirst(linear + 1);\n\n"
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
                return checkFragment(data, inputData, op, format, size[0], size[1]);
            });
    });

} // namespace
