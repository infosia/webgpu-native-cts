// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupAdd.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupAdd, subgroupExclusiveAdd, and subgroupInclusiveAdd
//
// Note: There is a lack of portability for non-uniform execution so these tests
// restrict themselves to uniform control flow.
// Note: There is no guaranteed mapping between subgroup_invocation_id and
// local_invocation_index. Tests should avoid assuming there is.

#include <cmath>
#include <cstdint>
#include <cstring>
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
    "shader,execution,expression,call,builtin,subgroupAdd",
    "Execution tests for subgroupAdd, subgroupExclusiveAdd, and subgroupInclusiveAdd");

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

constexpr int kIdentity = 0;

// kConcreteNumericScalarsAndVectors order:
//   integers (i32, vec2i..vec4i, u32, vec2u..vec4u),
//   f16 (f16, vec2h..vec4h), f32 (f32, vec2f..vec4f).
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

// kOperations = ['subgroupAdd', 'subgroupExclusiveAdd', 'subgroupInclusiveAdd']
std::vector<Value> kOperations() {
    return {Value(std::string("subgroupAdd")), Value(std::string("subgroupExclusiveAdd")),
            Value(std::string("subgroupInclusiveAdd"))};
}

// Float bit reinterpretation helpers.
float bitsToF32(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
uint32_t f32ToBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// f16 (round-to-nearest-even) encode/decode used by the data_types path.
uint16_t f32ToF16Bits(double value) {
    float f = static_cast<float>(value);
    uint32_t x = f32ToBits(f);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFFu) == 0xFFu) {
        return static_cast<uint16_t>(sign | 0x7C00u | (mant != 0u ? 0x200u : 0u));
    }
    if (exp >= 0x1F) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<uint16_t>(sign);
        }
        mant |= 0x800000u;
        const int32_t shift = 14 - exp;
        uint32_t result = mant >> shift;
        const uint32_t roundBit = 1u << (shift - 1);
        if ((mant & roundBit) && ((mant & (roundBit - 1)) || (result & 1u))) {
            result += 1;
        }
        return static_cast<uint16_t>(sign | result);
    }
    uint16_t half = static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
    const uint32_t roundBit = 1u << 12;
    if ((mant & roundBit) && ((mant & (roundBit - 1)) || (half & 1u))) {
        half += 1;
    }
    return half;
}
double f16BitsToF32(uint16_t h) {
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    uint32_t exp = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    uint32_t mant = static_cast<uint32_t>(h) & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                exp -= 1;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1F) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    return static_cast<double>(bitsToF32(bits));
}

// Checks subgroup additions (the data_types compute path).
std::optional<std::string> checkAddition(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const bt::Type& type,
    const std::string& operation,
    uint32_t expectedFillValue) {
    uint32_t numEles = 1;
    if (type.isVector()) {
        numEles = static_cast<uint32_t>(type.width);
    }
    const bt::Type scalarTy = bt::scalarTypeOf(type);
    const size_t half = metadata.size() / 2;
    const size_t expectedOffset = operation == "subgroupAdd" ? 0 : half;
    for (size_t i = 0; i < half; ++i) {
        double expected = static_cast<double>(metadata[i + expectedOffset]);
        if (operation == "subgroupInclusiveAdd") {
            expected += expectedFillValue;
        }
        for (uint32_t j = 0; j < numEles; ++j) {
            size_t idx = i * numEles + j;
            const bool isOdd = (idx & 0x1u) != 0;
            if (scalarTy.kind == bt::ScalarKind::F16) {
                idx = idx / 2;
            }
            double val;
            const uint32_t raw = output[idx];
            if (scalarTy.kind == bt::ScalarKind::F32) {
                val = static_cast<double>(bitsToF32(raw));
            } else if (scalarTy.kind == bt::ScalarKind::F16) {
                uint32_t bits = raw;
                if (isOdd) {
                    bits >>= 16;
                }
                val = f16BitsToF32(static_cast<uint16_t>(bits & 0xFFFFu));
            } else {
                val = static_cast<double>(raw);
            }
            if (expected != val) {
                std::ostringstream msg;
                msg << "Invocation " << i << ", component " << j << ": incorrect result\n- expected: "
                    << expected << "\n-      got: " << val;
                return msg.str();
            }
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "fp_accuracy")
    .desc("Tests the accuracy of floating-point addition.")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (int64_t x = 0; x < sg::kNumCases; ++x) {
            cases.emplace_back(x);
        }
        std::vector<Value> wgSizes = {Value(sg::wgSizeToString({sg::kStride, 1, 1})),
                                      Value(sg::wgSizeToString({sg::kStride / 2, 2, 1}))};
        return u.combine("case", cases)
            .combine("type", {Value(std::string("f32")), Value(std::string("f16"))})
            .combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string typeName = t.param<std::string>("type");
        if (typeName == "f16") {
            skipIfNoF16(t);
        }
        const sg::FPKind kind = typeName == "f16" ? sg::FPKind::F16 : sg::FPKind::F32;
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        sg::runAccuracyTest(
            t, static_cast<uint32_t>(t.param<int64_t>("case")), wgSize, "subgroupAdd", kind,
            kIdentity, [kind](double x, double y) { return fp::additionInterval(kind, x, y); });
    });

CTS_TEST(g, "data_types")
    .desc("Tests subgroup addition for valid data types")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const bt::Type& ty : kDataTypes()) {
            if (ty.isVector() && ty.width == 3) {
                continue;
            }
            types.emplace_back(ty.toString());
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("type", types)
            .beginSubcases()
            .combine("wgSize", wgSizes)
            .combine("operation", kOperations());
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const bt::Type type = typeByName(t.param<std::string>("type"));
        if (requiresF16(type)) {
            skipIfNoF16(t);
        }
        uint32_t numEles = 1;
        if (type.isVector()) {
            numEles = static_cast<uint32_t>(type.width);
        }
        const bt::Type scalarType = bt::scalarTypeOf(type);
        const std::string operation = t.param<std::string>("operation");
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;\n";
        }

        std::ostringstream wgsl;
        wgsl << "\n" << enables << "\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<" << type.toString() << ">;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<" << type.toString() << ">;\n\n"
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
             << ") {\n"
             << "  let ballot = subgroupBallot(true);\n"
             << "  var size = countOneBits(ballot.x);\n"
             << "  size += countOneBits(ballot.y);\n"
             << "  size += countOneBits(ballot.z);\n"
             << "  size += countOneBits(ballot.w);\n"
             << "  metadata.subgroup_size[lid] = size;\n\n"
             << "  metadata.subgroup_invocation_id[lid] = id;\n\n"
             << "  outputs[lid] = " << operation << "(inputs[lid]);\n"
             << "}";

        const uint32_t expectedFillValue = 1;
        uint32_t fillValue = expectedFillValue;
        uint32_t numUints = wgThreads * numEles;
        if (scalarType.kind == bt::ScalarKind::F32) {
            fillValue = f32ToBits(1.0f);
        } else if (scalarType.kind == bt::ScalarKind::F16) {
            const uint32_t f16 = f32ToF16Bits(1.0);
            fillValue = f16 | (f16 << 16);
            numUints = (numUints + 1) / 2;
        }
        const std::vector<uint32_t> inputData(numUints, fillValue);
        sg::runComputeTest(
            t, wgsl.str(), wgSize, numUints, inputData,
            [type, operation](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkAddition(metadata, output, type, operation, expectedFillValue);
            });
    });

// Checks predicated additions.
std::optional<std::string> checkPredicatedAddition(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const std::string& operation,
    const std::function<bool(uint32_t, uint32_t)>& filter) {
    for (size_t i = 0; i < output.size(); ++i) {
        const uint32_t size = metadata[i];
        const uint32_t id = metadata[output.size() + i];
        uint32_t expected = 0;
        if (filter(id, size)) {
            const uint32_t bound = operation == "subgroupInclusiveAdd"
                                       ? id + 1
                                       : (operation == "subgroupAdd" ? size : id);
            for (uint32_t j = 0; j < bound; ++j) {
                if (filter(j, size)) {
                    expected += j;
                }
            }
        } else {
            expected = sg::kDataSentinel;
        }
        if (expected != output[i]) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": incorrect result\n- expected: " << expected
                << "\n-      got: " << output[i];
            return msg.str();
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "compute,split")
    .desc("Tests that only active invocations contribute to the operation")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (const sg::PredicateCase& c : sg::kPredicateCases()) {
            cases.emplace_back(c.name);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("case", cases)
            .beginSubcases()
            .combine("operation", kOperations())
            .combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::PredicateCase& testcase = sg::predicateCaseByName(t.param<std::string>("case"));
        const std::string operation = t.param<std::string>("operation");
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
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
             << ") {\n"
             << "  _ = input[0];\n\n"
             << "  let ballot = subgroupBallot(true);\n"
             << "  var subgroupSize = countOneBits(ballot.x);\n"
             << "  subgroupSize += countOneBits(ballot.y);\n"
             << "  subgroupSize += countOneBits(ballot.z);\n"
             << "  subgroupSize += countOneBits(ballot.w);\n"
             << "  metadata.subgroup_size[lid] = subgroupSize;\n\n"
             << "  metadata.subgroup_invocation_id[lid] = id;\n\n"
             << "  if " << testcase.cond << " {\n"
             << "    outputs[lid] = " << operation << "(id);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        const std::vector<uint32_t> inputData{0};
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 1, inputData,
            [operation, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkPredicatedAddition(metadata, output, operation, filter);
            });
    });

// Max subgroup size is 128.
constexpr uint32_t kMaxSize = 128;

// Checks subgroup addition results in fragment shaders.
std::optional<std::string> checkFragment(
    const std::vector<uint32_t>& data,
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
            const uint32_t subgroupId = data[offset + 2];
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

    std::map<uint32_t, std::vector<uint32_t>> expected;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t subgroupId = data[offset + 2];
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
            const uint32_t id = data[offset + 1];
            auto it = expected.find(subgroupId);
            if (it == expected.end()) {
                it = expected.emplace(subgroupId, std::vector<uint32_t>(kMaxSize, kIdentity)).first;
            }
            it->second[id] = row * width + col;
        }
    }

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t subgroupId = data[offset + 2];
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
            const uint32_t res = data[offset];
            const uint32_t id = data[offset + 1];
            auto it = expected.find(subgroupId);
            const std::vector<uint32_t> v =
                it == expected.end() ? std::vector<uint32_t>(kMaxSize, kIdentity) : it->second;
            const uint32_t bound = op == "subgroupAdd"
                                       ? kMaxSize
                                       : (op == "subgroupInclusiveAdd" ? id + 1 : id);
            uint32_t expect = kIdentity;
            for (uint32_t i = 0; i < bound; ++i) {
                expect += v[i];
            }
            if (res != expect) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect results\n- expected: "
                    << expect << "\n-      got: " << res;
                return msg.str();
            }
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "fragment")
    .desc("Test subgroup additions in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rgba32uint"))}}};
        return u.combine("op", kOperations())
            .combine("size", sizes)
            .beginSubcases()
            .combineWithParams(formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const std::string op = t.param<std::string>("op");
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const WGPUTextureFormat format = WGPUTextureFormat_RGBA32Uint;

        const uint32_t subgroupMinSize = sg::getSubgroupSizes(t).minSize;

        const uint32_t innerTexels = (size[0] - 1) * (size[1] - 1);
        if (innerTexels < subgroupMinSize) {
            t.skip("Too few texels to be reliable");
        }

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, 1>;\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << "  @builtin(subgroup_invocation_id) id : u32\n"
                 << ") -> @location(0) vec4u {\n"
                 << "  _ = inputs[0];\n\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n"
                 << "  let subgroup_id = subgroupBroadcastFirst(linear + 1);\n\n"
                 << "  let x_in_range = u32(pos.x) < (" << size[0] << " - 1);\n"
                 << "  let y_in_range = u32(pos.y) < (" << size[1] << " - 1);\n"
                 << "  let in_range = x_in_range && y_in_range;\n\n"
                 << "  let value = select(" << kIdentity << ", linear, in_range);\n"
                 << "  return vec4u(" << op << "(value), id, subgroup_id, 0);\n"
                 << "}";

        const std::vector<uint32_t> inputData{0};
        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], inputData, sg::FragmentInputKind::U32,
            [op, size](const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkFragment(data, op, format, size[0], size[1]);
            });
    });

} // namespace
