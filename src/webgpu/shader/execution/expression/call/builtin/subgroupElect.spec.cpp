// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupElect.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupElect.

#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/expression/call/builtin/subgroup_util.h"

using namespace cts;
namespace sg = cts::subgroups;

namespace {

TestGroup<sg::SubgroupTest> g = MakeTestGroup<sg::SubgroupTest>(
    "shader,execution,expression,call,builtin,subgroupElect",
    "Execution tests for subgroupElect");

// Skips the case unless the device advertises the Subgroups feature.
void skipIfNoSubgroups(sg::SubgroupTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
        t.skip("subgroups feature not available");
    }
}

// Reads the device's subgroupMinSize / subgroupMaxSize from adapter info.
struct SubgroupSizes {
    uint32_t minSize;
    uint32_t maxSize;
};
SubgroupSizes getSubgroupSizes(sg::SubgroupTest& t) {
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    const WGPUStatus status = wgpuDeviceGetAdapterInfo(t.device(), &info);
    if (status != WGPUStatus_Success) {
        t.fail("wgpuDeviceGetAdapterInfo failed");
    }
    const SubgroupSizes sizes{info.subgroupMinSize, info.subgroupMaxSize};
    wgpuAdapterInfoFreeMembers(info);
    return sizes;
}

// Checks subgroupElect compute shader results.
//
// metadata: id in first half, subgroup_size in second half.
// output: elect results.
// filter: determines active invocations.
std::optional<std::string> checkCompute(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    const std::function<bool(uint32_t id, uint32_t size)>& filter) {
    const uint32_t size = metadata[output.size()];
    uint32_t elected = 129;
    for (uint32_t i = 0; i < 128; i++) {
        if (filter(i, size)) {
            elected = i;
            break;
        }
    }

    for (size_t i = 0; i < output.size(); i++) {
        const uint32_t res = output[i];
        const uint32_t id = metadata[i];
        uint32_t expected = sg::kDataSentinel;
        if (filter(id, size)) {
            expected = elected == id ? 1u : 0u;
        }
        if (res != expected) {
            std::ostringstream msg;
            msg << "Invocation " << i << ": incorrect result\n- expected: " << expected
                << "\n-      got: " << res;
            return msg.str();
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "compute,all_active")
    .desc("Test subgroupElect in compute shader with all active invocations")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<u32>; // unused\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << ") {\n"
             << "  // Force usage.\n"
             << "  _ = inputs[0];\n\n"
             << "  let e = subgroupElect();\n"
             << "  outputs[lid] = select(0u, 1u, e);\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.size[lid] = subgroupSize;\n"
             << "}";

        const uint32_t uintsPerOutput = 1;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, std::vector<uint32_t>{0},
            [](const std::vector<uint32_t>& metadata,
               const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output,
                                    [](uint32_t /*id*/, uint32_t /*size*/) { return true; });
            });
    });

CTS_TEST(g, "compute,split")
    .desc("Test subgroupElect in compute shader with partially active invocations")
    .params([](ParamsBuilder u) {
        std::vector<Value> predicates;
        for (const sg::PredicateCase& c : sg::kPredicateCases()) {
            predicates.emplace_back(c.name);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("predicate", predicates).beginSubcases().combine("wgSize", wgSizes);
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
             << "var<storage> inputs : array<u32>; // unused\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << ") {\n"
             << "  // Force usage.\n"
             << "  _ = inputs[0];\n\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.size[lid] = subgroupSize;\n"
             << "  if " << testcase.cond << " {\n"
             << "    let e = subgroupElect();\n"
             << "    outputs[lid] = select(0u, 1u, e);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        const uint32_t uintsPerOutput = 1;
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, std::vector<uint32_t>{0},
            [filter](const std::vector<uint32_t>& metadata,
                     const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, filter);
            });
    });

CTS_TEST(g, "compute,each_invocation")
    .desc("Test subgroupElect in compute shader to elect each possible invocation")
    .params([](ParamsBuilder u) {
        std::vector<Value> ids;
        for (int64_t x = 0; x < 128; ++x) {
            ids.emplace_back(x);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("id", ids).beginSubcases().combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const uint32_t paramId = static_cast<uint32_t>(t.param<int64_t>("id"));
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        const SubgroupSizes sizes = getSubgroupSizes(t);
        if (sizes.maxSize <= paramId) {
            t.skip("No invocation selected");
        }

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> inputs : array<u32>; // unused\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> outputs : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata : Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << ") {\n"
             << "  // Force usage.\n"
             << "  _ = inputs[0];\n\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.size[lid] = subgroupSize;\n"
             << "  if id >= " << paramId << " {\n"
             << "    let e = subgroupElect();\n"
             << "    outputs[lid] = select(0u, 1u, e);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        const uint32_t uintsPerOutput = 1;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, uintsPerOutput, std::vector<uint32_t>{0},
            [paramId](const std::vector<uint32_t>& metadata,
                      const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, [paramId](uint32_t id, uint32_t /*size*/) {
                    return id >= paramId;
                });
            });
    });

// Checks subgroupElect results from a fragment shader.
//
// Avoids subgroups in the last row or column to skip potential helper invocations.
// data layout (per texel): component 0 = result, 1 = subgroup_invocation_id,
// 2 = generated subgroup id.
std::optional<std::string> checkFragment(
    const std::vector<uint32_t>& data,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height) {
    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;

    // Determine if a subgroup should be included in the checks.
    std::unordered_map<uint32_t, bool> inBounds;
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
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
        // This variant would not reliably test behavior.
        return std::nullopt;
    }

    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
            const uint32_t subgroupId = data[offset + 2];
            if (subgroupId == 0) {
                std::ostringstream msg;
                msg << "Internal error: helper invocation at (" << col << ", " << row << ")";
                return msg.str();
            }
            auto it = inBounds.find(subgroupId);
            const bool subgroupInBounds = it == inBounds.end() ? true : it->second;
            if (!subgroupInBounds) {
                continue;
            }

            const uint32_t res = data[offset];
            const uint32_t id = data[offset + 1];
            const uint32_t expected = id == 0 ? 0x55555555u : 0xaaaaaaaau;
            if (res != expected) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect result\n- expected: 0x"
                    << std::hex << expected << "\n-      got: 0x" << res;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "fragment")
    .desc("Tests subgroupElect in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rgba32uint"))}}};
        return u.combine("size", sizes).beginSubcases().combineWithParams(formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const WGPUTextureFormat format = WGPUTextureFormat_RGBA32Uint;

        const SubgroupSizes sizes = getSubgroupSizes(t);
        const uint32_t innerTexels = (size[0] - 1) * (size[1] - 1);
        if (innerTexels < sizes.minSize) {
            t.skip("Too few texels to be reliable");
        }

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> inputs : array<vec4u, 1>; // unused\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << "  @builtin(subgroup_invocation_id) id : u32,\n"
                 << ") -> @location(0) vec4u {\n"
                 << "  // Force usage\n"
                 << "  _ = inputs[0];\n\n"
                 << "  // Generate a subgroup id based on linearized position, avoid 0.\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n"
                 << "  let subgroup_id = subgroupBroadcastFirst(linear + 1);\n\n"
                 << "  let e = subgroupElect();\n"
                 << "  let res = select(0xaaaaaaaau, 0x55555555u, e);\n"
                 << "  return vec4u(res, id, subgroup_id, 0);\n"
                 << "}";

        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], std::vector<uint32_t>{0},
            sg::FragmentInputKind::U32,
            [size](const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkFragment(data, WGPUTextureFormat_RGBA32Uint, size[0], size[1]);
            });
    });

} // namespace
