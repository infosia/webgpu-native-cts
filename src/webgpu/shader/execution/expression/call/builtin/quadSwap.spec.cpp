// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/quadSwap.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for quadSwapX, quadSwapY, and quadSwapDiagonal.
//
// Note: There is a lack of portability for non-uniform execution so these tests
// restrict themselves to uniform control flow.
// Note: There is no guaranteed mapping between subgroup_invocation_id and
// local_invocation_index. Tests should avoid assuming there is.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
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
    "shader,execution,expression,call,builtin,quadSwap",
    "Execution tests for quadSwapX, quadSwapY, and quadSwapDiagonal");

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

bool requiresF16(const bt::Type& type) { return type.kind == bt::ScalarKind::F16; }

bt::Type typeByName(const std::string& name) {
    for (const bt::Type& ty : bt::kConcreteNumericScalarsAndVectors()) {
        if (ty.toString() == name) {
            return ty;
        }
    }
    return bt::scalar(bt::ScalarKind::U32);
}

// kOps = ['quadSwapX', 'quadSwapY', 'quadSwapDiagonal']
std::vector<Value> kOps() {
    return {Value(std::string("quadSwapX")), Value(std::string("quadSwapY")),
            Value(std::string("quadSwapDiagonal"))};
}

// Returns the swapped quad invocation id for the given op.
uint32_t swapIndex(uint32_t index, const std::string& op) {
    if (op == "quadSwapX") {
        return index ^ 1u;
    }
    if (op == "quadSwapY") {
        return index ^ 2u;
    }
    return index ^ 3u; // quadSwapDiagonal
}

// generateScalarValues(type) from subgroup_util.ts (kBit values).
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
            std::abort();
    }
}

std::vector<uint32_t> generateTypedInputs(const bt::Type& type) {
    const std::array<uint32_t, 4> scalarValues = generateScalarValues(type);
    const uint32_t elements = type.isVector() ? static_cast<uint32_t>(type.width) : 1u;
    return sg::generateTypedInputs(scalarValues, elements, requiresF16(type));
}

// Checks the results of data types test.
std::optional<std::string> checkDataTypes(
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    const std::string& op,
    const bt::Type& type) {
    if (requiresF16(type) && !type.isVector()) {
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t swapIdx = swapIndex(i, op);

            const uint32_t expectIdx = swapIdx / 2;
            const bool expectShift = swapIdx % 2 == 1;
            uint32_t expect = input[expectIdx];
            if (expectShift) {
                expect >>= 16;
            }
            expect &= 0xffffu;

            const uint32_t resIdx = i / 2;
            const bool resShift = i % 2 == 1;
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
                uints /= 2;
            }
        }
        for (uint32_t i = 0; i < 4; ++i) {
            for (uint32_t j = 0; j < uints; ++j) {
                const uint32_t expect = input[swapIndex(i, op) * uints + j];
                const uint32_t res = output[i * uints + j];
                if (res != expect) {
                    std::ostringstream msg;
                    msg << uints * i + j << ": incorrect result\n- expected: " << expect
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
        for (const bt::Type& ty : bt::kConcreteNumericScalarsAndVectors()) {
            types.emplace_back(ty.toString());
        }
        return u.combine("type", types).beginSubcases().combine("op", kOps());
    })
    .fn([](sg::SubgroupTest& t) {
        const sg::WGSize wgSize{4, 1, 1};
        const bt::Type type = typeByName(t.param<std::string>("type"));
        const std::string op = t.param<std::string>("op");
        skipIfNoSubgroups(t);
        if (requiresF16(type)) {
            skipIfNoF16(t);
        }

        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;";
        }
        std::ostringstream wgsl;
        wgsl << "\n"
             << enables << "\n\n"
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
             << "  // Force usage\n"
             << "  _ = metadata[0];\n\n"
             << "  output[id] = " << op << "(input[id]);\n"
             << "}";

        const std::vector<uint32_t> inputData = generateTypedInputs(type);
        uint32_t uintsPerOutput = 1;
        if (type.isVector()) {
            uintsPerOutput = type.width == 3 ? 4u : static_cast<uint32_t>(type.width);
            if (requiresF16(type)) {
                uintsPerOutput /= 2;
            }
        }
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, inputData,
            [inputData, op, type](
                const std::vector<uint32_t>& /*metadata*/,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkDataTypes(output, inputData, op, type);
            });
    });

// Checks quad swaps in compute shaders. Assumes that quads are linear within a subgroup.
std::optional<std::string> checkSwapCompute(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const std::string& op,
    const std::function<bool(uint32_t id, uint32_t size)>& filter) {
    const size_t bound = output.size() / 2;
    for (size_t i = 0; i < bound; ++i) {
        const uint32_t subgroupId = output[bound + i];
        const uint32_t id = metadata[i];
        const uint32_t size = metadata[bound + i];
        if (!filter(id, size)) {
            if (output[i] != sg::kDataSentinel) {
                std::ostringstream msg;
                msg << "Unexpected write for invocation " << i;
                return msg.str();
            }
            continue;
        }

        const uint32_t quadId = id / 4;
        const uint32_t quadIndex = id % 4;
        bool found = false;
        for (size_t j = 0; j < bound; ++j) {
            const uint32_t otherId = metadata[j];
            const uint32_t otherQuadId = otherId / 4;
            const uint32_t otherQuadIndex = otherId % 4;
            const uint32_t otherSubgroupId = output[bound + j];
            if (subgroupId == otherSubgroupId && quadId == otherQuadId &&
                quadIndex == swapIndex(otherQuadIndex, op)) {
                found = true;
                if (output[i] != j) {
                    std::ostringstream msg;
                    msg << "Invocation " << i << ": incorrect result\n- expected: " << j
                        << "\n-      got: " << output[i];
                    return msg.str();
                }
                break;
            }
        }
        if (!found) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": failed to find swapped result";
            return msg.str();
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "compute,all_active")
    .desc(
        "Tests swaps with all active invocations\n\nQuad operations require a full quad so workgroup "
        "sizes are limited to multiples of 4.\n  ")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("wgSize", wgSizes)
            .filter([](const ParamRecord& p) {
                const sg::WGSize s = sg::wgSizeFromString(valueAs<std::string>(*findParam(p, "wgSize")));
                const uint32_t wgThreads = s[0] * s[1] * s[2];
                return wgThreads % 4 == 0;
            })
            .beginSubcases()
            .combine("op", kOps());
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const std::string op = t.param<std::string>("op");
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : u32; // unused\n\n"
             << "struct Output {\n"
             << "  results : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : Output;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_size : array<u32, " << wgThreads << ">,\n"
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
             << "  // Force usage\n"
             << "  _ = inputs;\n\n"
             << "  let b = " << op << "(lid);\n"
             << "  output.results[lid] = b;\n"
             << "  output.subgroup_size[lid] = subgroupBroadcastFirst(lid + 1);\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.subgroup_size[lid] = subgroupSize;\n"
             << "}";

        const uint32_t uintsPerOutput = 2;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, std::vector<uint32_t>{0},
            [op](const std::vector<uint32_t>& metadata,
                 const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkSwapCompute(metadata, output, op,
                                        [](uint32_t /*id*/, uint32_t /*size*/) { return true; });
            });
    });

CTS_TEST(g, "compute,split")
    .desc(
        "Tests swaps with all predicated invocations\n\nQuad operations require a full quad so "
        "workgroup sizes are limited to multiples of 4.\nQuad operations require a fully active quad "
        "to operate correctly so several of the\npredication filters are skipped.\n  ")
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
            .filter([](const ParamRecord& p) {
                const std::string pred = valueAs<std::string>(*findParam(p, "predicate"));
                return pred == "lower_half" || pred == "upper_half";
            })
            .combine("wgSize", wgSizes)
            .filter([](const ParamRecord& p) {
                const sg::WGSize s = sg::wgSizeFromString(valueAs<std::string>(*findParam(p, "wgSize")));
                const uint32_t wgThreads = s[0] * s[1] * s[2];
                return wgThreads % 4 == 0;
            })
            .beginSubcases()
            .combine("op", kOps());
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const std::string op = t.param<std::string>("op");
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];
        const sg::PredicateCase& testcase =
            sg::predicateCaseByName(t.param<std::string>("predicate"));

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : u32; // unused\n\n"
             << "struct Output {\n"
             << "  results : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : Output;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  subgroup_size : array<u32, " << wgThreads << ">,\n"
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
             << "  // Force usage\n"
             << "  _ = inputs;\n\n"
             << "  output.subgroup_size[lid] = subgroupBroadcastFirst(lid + 1);\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.subgroup_size[lid] = subgroupSize;\n\n"
             << "  if " << testcase.cond << " {\n"
             << "    let b = " << op << "(lid);\n"
             << "    output.results[lid] = b;\n"
             << "  }\n"
             << "}";

        const uint32_t uintsPerOutput = 2;
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, std::vector<uint32_t>{0},
            [op, filter](const std::vector<uint32_t>& metadata,
                         const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkSwapCompute(metadata, output, op, filter);
            });
    });

// Checks results of quad swaps in fragment shaders.
std::optional<std::string> checkFragment(
    const std::vector<uint32_t>& data,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height,
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

    // Iteration skips last row and column to avoid helper invocations because it is not
    // guaranteed whether or not they participate in the subgroup operation.
    for (uint32_t row = 0; row < height - 1; ++row) {
        for (uint32_t col = 0; col < width - 1; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;

            const bool rowIsOdd = row % 2 == 1;
            const bool colIsOdd = col % 2 == 1;

            // Skip checking quads that extend into potential helper invocations.
            const uint32_t maxRow = rowIsOdd ? row : row + 1;
            const uint32_t maxCol = colIsOdd ? col : col + 1;
            if (maxRow == height - 1 || maxCol == width - 1) {
                continue;
            }

            uint32_t expectRow = row;
            uint32_t expectCol = col;
            if (op == "quadSwapX") {
                expectCol = colIsOdd ? col - 1 : col + 1;
            } else if (op == "quadSwapY") {
                expectRow = rowIsOdd ? row - 1 : row + 1;
            } else { // quadSwapDiagonal
                expectRow = rowIsOdd ? row - 1 : row + 1;
                expectCol = colIsOdd ? col - 1 : col + 1;
            }

            const uint32_t rowOutput = data[offset + 1];
            const uint32_t colOutput = data[offset];
            if (expectRow != rowOutput) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect row results:\n- expected: "
                    << expectRow << "\n-      got: " << rowOutput;
                return msg.str();
            }

            if (expectCol != colOutput) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect col results:\n- expected: "
                    << expectRow << "\n-      got: " << colOutput;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "fragment,all_active")
    .desc("Tests quad swaps in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rgba32uint"))}}};
        return u.combine("size", sizes).beginSubcases().combine("op", kOps()).combineWithParams(
            formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const WGPUTextureFormat format = WGPUTextureFormat_RGBA32Uint;
        const std::string op = t.param<std::string>("op");

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, 1>; // unused\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << ") -> @location(0) vec4u {\n"
                 << "  // Force usage\n"
                 << "  _ = inputs[0];\n\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n\n"
                 << "  // Filter out possible helper invocations.\n"
                 << "  let x_in_range = u32(pos.x) < (" << size[0] << " - 1);\n"
                 << "  let y_in_range = u32(pos.y) < (" << size[1] << " - 1);\n"
                 << "  let in_range = x_in_range && y_in_range;\n\n"
                 << "  var x_swap = select(1001, u32(pos.x), in_range);\n"
                 << "  var y_swap = select(1001, u32(pos.y), in_range);\n\n"
                 << "  x_swap = " << op << "(x_swap);\n"
                 << "  y_swap = " << op << "(y_swap);\n\n"
                 << "  return vec4u(x_swap, y_swap, 0, 0);\n"
                 << "}";

        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], std::vector<uint32_t>{0},
            sg::FragmentInputKind::U32,
            [size, op](
                const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkFragment(data, WGPUTextureFormat_RGBA32Uint, size[0], size[1], op);
            });
    });

CTS_TEST(g, "fragment,split").unimplemented();

} // namespace
