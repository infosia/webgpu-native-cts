// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupBroadcast.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupBroadcast and subgroupBroadcastFirst
//
// Note: There is a lack of portability for non-uniform execution so these tests
// restrict themselves to uniform control flow.
// Note: There is no guaranteed mapping between subgroup_invocation_id and
// local_invocation_index. Tests should avoid assuming there is.

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

WGPUStringView sv(std::string_view text) { return WGPUStringView{text.data(), text.size()}; }

TestGroup<sg::SubgroupTest> g = MakeTestGroup<sg::SubgroupTest>(
    "shader,execution,expression,call,builtin,subgroupBroadcast",
    "Execution tests for subgroupBroadcast and subgroupBroadcastFirst");

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

struct SubgroupSizes {
    uint32_t minSize;
    uint32_t maxSize;
};
SubgroupSizes getSubgroupSizes(sg::SubgroupTest& t) {
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuDeviceGetAdapterInfo(t.device(), &info) != WGPUStatus_Success) {
        t.fail("wgpuDeviceGetAdapterInfo failed");
    }
    const SubgroupSizes sizes{info.subgroupMinSize, info.subgroupMaxSize};
    wgpuAdapterInfoFreeMembers(info);
    return sizes;
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

// Checks the results of the data types test.
std::optional<std::string> checkDataTypes(
    const std::vector<uint32_t>& output,
    const std::vector<uint32_t>& input,
    uint32_t id,
    const bt::Type& type) {
    if (requiresF16(type) && !type.isVector()) {
        for (int i = 0; i < 4; ++i) {
            const uint32_t expectIdx = id / 2;
            const bool expectShift = (id % 2) == 1;
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
        for (uint32_t i = 0; i < 4; ++i) {
            for (uint32_t j = 0; j < uints; ++j) {
                const uint32_t expect = input[id * uints + j];
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
    .desc("Tests broadcast of data types")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const bt::Type& ty : kDataTypes()) {
            types.emplace_back(ty.toString());
        }
        std::vector<Value> ids = {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(2)),
                                  Value(int64_t(3))};
        return u.combine("type", types).beginSubcases().combine("id", ids);
    })
    .fn([](sg::SubgroupTest& t) {
        const sg::WGSize wgSize{4, 1, 1};
        const bt::Type type = typeByName(t.param<std::string>("type"));
        skipIfNoSubgroups(t);
        if (requiresF16(type)) {
            skipIfNoF16(t);
        }
        const int64_t id = t.param<int64_t>("id");

        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;\n";
        }
        std::string broadcast;
        if (id == 0) {
            broadcast = "subgroupBroadcastFirst(input[id])";
        } else {
            broadcast = "subgroupBroadcast(input[id], " + std::to_string(id) + ")";
        }

        std::ostringstream wgsl;
        wgsl << "\n" << enables << "\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage, read_write> input : array<" << type.toString() << ">;\n\n"
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
             << "  output[id] = " << broadcast << ";\n"
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
            [inputData, id, type](
                const std::vector<uint32_t>& /*metadata*/,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkDataTypes(output, inputData, static_cast<uint32_t>(id), type);
            });
    });

CTS_TEST(g, "workgroup_uniform_load")
    .desc("Tests a workgroup uniform load equivalent")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        std::vector<Value> inputIds = {Value(int64_t(1)), Value(int64_t(2)), Value(int64_t(3))};
        return u.combine("wgSize", wgSizes)
            .beginSubcases()
            .combine("inputId", inputIds)
            .combine("first", {Value(false), Value(true)});
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const int64_t inputId = t.param<int64_t>("inputId");
        const bool first = t.param<bool>("first");
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        const WGPULimits limits = t.getLimits();
        if (limits.maxComputeInvocationsPerWorkgroup < wgThreads ||
            limits.maxComputeWorkgroupSizeX < wgSize[0] ||
            limits.maxComputeWorkgroupSizeY < wgSize[1] ||
            limits.maxComputeWorkgroupSizeZ < wgSize[2]) {
            t.skip("Workgroup size too large");
        }

        const std::string broadcast =
            first ? "subgroupBroadcastFirst(v)" : "subgroupBroadcast(v, 0)";

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n\n"
             << "var<workgroup> wgmem : u32;\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage, read> inputs : array<u32>;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(@builtin(subgroup_invocation_id) id : u32,\n"
             << "        @builtin(local_invocation_index) lid : u32) {\n"
             << "  if (lid == " << inputId << ") {\n"
             << "    wgmem = inputs[lid];\n"
             << "  }\n"
             << "  workgroupBarrier();\n"
             << "  var v = 0u;\n"
             << "  if (id == 0) {\n"
             << "    v = wgmem;\n"
             << "  }\n"
             << "  v = " << broadcast << ";\n"
             << "  output[lid] = v;\n"
             << "}";

        const std::array<uint32_t, 4> values{1, 13, 33, 125};
        WGPUBuffer inputBuffer = t.makeBufferWithContents(
            values.data(), values.size() * sizeof(uint32_t),
            static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage));

        const std::vector<uint32_t> outputInit(wgThreads, 0);
        WGPUBuffer outputBuffer = t.makeBufferWithContents(
            outputInit.data(), outputInit.size() * sizeof(uint32_t),
            static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                                         WGPUBufferUsage_Storage));

        WGPUShaderModule module = t.createShaderModuleTracked(wgsl.str());
        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr;
        pipeDesc.compute.module = module;
        pipeDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        std::array<WGPUBindGroupEntry, 2> entries{};
        for (uint32_t i = 0; i < 2; ++i) {
            entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            entries[i].binding = i;
            entries[i].buffer = i == 0 ? inputBuffer : outputBuffer;
            entries[i].offset = 0;
            entries[i].size = WGPU_WHOLE_SIZE;
        }
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = entries.size();
        bgDesc.entries = entries.data();
        WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        std::vector<uint32_t> expect(wgThreads, values[inputId]);
        t.expectGPUBufferValuesEqual(outputBuffer, expect.data(), expect.size() * sizeof(uint32_t));
    });

constexpr uint32_t kUnattemptedBroadcast = 555u << 16;

// Checks the results of broadcast in compute shaders.
std::optional<std::string> checkCompute(
    const std::vector<uint32_t>& metadata,
    const std::vector<uint32_t>& output,
    uint32_t numInvs,
    int64_t broadcast, // -1 => 'first'
    const std::function<bool(uint32_t, uint32_t)>& filter) {
    int64_t broadcastedId = broadcast;
    if (broadcast < 0) {
        const uint32_t size = metadata[numInvs];
        for (uint32_t i = 0; i < size; ++i) {
            if (filter(i, size)) {
                broadcastedId = i;
                break;
            }
        }
    }

    std::map<uint32_t, uint32_t> mapping;
    std::map<uint32_t, uint32_t> sizes;
    for (uint32_t i = 0; i < numInvs; ++i) {
        const uint32_t id = metadata[i];
        const uint32_t size = metadata[i + numInvs];

        const uint32_t res = output[i];
        const uint32_t upper = res & 0xffff0000u;
        if (upper == kUnattemptedBroadcast) {
            const uint32_t lower = res & 0xffffu;
            if (broadcastedId < static_cast<int64_t>(lower)) {
                std::ostringstream msg;
                msg << "Invocation " << i << ": expected a valid broadcast:\n-       broadcast id: "
                    << id << "\n- real subgroup size: " << lower;
                return msg.str();
            }
            continue;
        }

        if (filter(id, size)) {
            mapping[res] += 1;
            if (broadcastedId == static_cast<int64_t>(id)) {
                sizes[res] = size;
                if (res != i) {
                    std::ostringstream msg;
                    msg << "Invocation " << i << ": incorrect result:\n- expected: " << i
                        << "\n-      got: " << res;
                    return msg.str();
                }
            }
        } else {
            if (res != sg::kDataSentinel) {
                std::ostringstream msg;
                msg << "Invocation " << i << ": unexpected write (" << res << ")";
                return msg.str();
            }
        }
    }

    for (const auto& kv : mapping) {
        const uint32_t id = kv.first;
        const uint32_t seen = kv.second;
        auto it = sizes.find(id);
        const uint32_t size = it == sizes.end() ? 0u : it->second;
        if (size < seen) {
            std::ostringstream msg;
            msg << "Unexpected number of invocations for subgroup " << id << "\n- expected: " << size
                << "\n-      got: " << seen;
            return msg.str();
        }
    }
    return std::nullopt;
}

std::vector<Value> kBroadcastIds() {
    return {Value(int64_t(0)),  Value(int64_t(1)),  Value(int64_t(2)),  Value(int64_t(3)),
            Value(int64_t(7)),  Value(int64_t(13)), Value(int64_t(25)), Value(int64_t(46))};
}

CTS_TEST(g, "compute,all_active")
    .desc("Test broadcasts in compute shaders with all active invocations")
    .params([](ParamsBuilder u) {
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("wgSize", wgSizes).beginSubcases().combine("id", kBroadcastIds());
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const int64_t id = t.param<int64_t>("id");
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        std::string broadcast;
        if (id == 0) {
            broadcast = "subgroupBroadcastFirst(input[lid])";
        } else {
            broadcast = "subgroupBroadcast(input[lid], " + std::to_string(id) + ")";
        }

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata: Metadata;\n\n"
             << "fn ballotSize() -> u32 {\n"
             << "  let b = subgroupBallot(true);\n"
             << "  let count = countOneBits(b);\n"
             << "  return count.x + count.y + count.z + count.w;\n"
             << "}\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.size[lid] = subgroupSize;\n\n"
             << "  let realSize = ballotSize();\n"
             << "  if " << id << " < realSize {\n"
             << "    output[lid] = " << broadcast << ";\n"
             << "  } else {\n"
             << "    output[lid] = " << kUnattemptedBroadcast << "u | realSize;\n"
             << "  }\n"
             << "}";

        std::vector<uint32_t> inputData(wgThreads);
        for (uint32_t x = 0; x < wgThreads; ++x) {
            inputData[x] = x;
        }
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 1, inputData,
            [wgThreads, id](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, wgThreads, id,
                                    [](uint32_t, uint32_t) { return true; });
            });
    });

CTS_TEST(g, "compute,split")
    .desc("Test broadcasts with only some active invocations")
    .params([](ParamsBuilder u) {
        std::vector<Value> predicates;
        for (const sg::PredicateCase& c : sg::kPredicateCases()) {
            if (c.name == "upper_half") {
                continue; // This case would be UB.
            }
            predicates.emplace_back(c.name);
        }
        std::vector<Value> wgSizes;
        for (const sg::WGSize& s : sg::kWGSizes()) {
            wgSizes.emplace_back(sg::wgSizeToString(s));
        }
        return u.combine("predicate", predicates)
            .beginSubcases()
            .combine("id", kBroadcastIds())
            .combine("wgSize", wgSizes);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::PredicateCase& testcase =
            sg::predicateCaseByName(t.param<std::string>("predicate"));
        const int64_t id = t.param<int64_t>("id");
        const sg::WGSize wgSize = sg::wgSizeFromString(t.param<std::string>("wgSize"));
        const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

        const SubgroupSizes sizes = getSubgroupSizes(t);
        for (uint32_t size = sizes.minSize; size <= sizes.maxSize; size *= 2) {
            if (!testcase.filter(static_cast<uint32_t>(id), size)) {
                t.skip("Skipping potential undefined behavior");
            }
        }

        std::string broadcast;
        if (id == 0) {
            broadcast = "subgroupBroadcastFirst(input[lid])";
        } else {
            broadcast = "subgroupBroadcast(input[lid], " + std::to_string(id) + ")";
        }

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata: Metadata;\n\n"
             << "fn ballotSize() -> u32 {\n"
             << "  let b = subgroupBallot(true);\n"
             << "  let count = countOneBits(b);\n"
             << "  return count.x + count.y + count.z + count.w;\n"
             << "}\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.size[lid] = subgroupSize;\n\n"
             << "  let realSize = ballotSize();\n"
             << "  if " << id << " < realSize {\n"
             << "    if " << testcase.cond << " {\n"
             << "      output[lid] = " << broadcast << ";\n"
             << "    } else {\n"
             << "      return;\n"
             << "    }\n"
             << "  } else {\n"
             << "    output[lid] = " << kUnattemptedBroadcast << "u | realSize;\n"
             << "  }\n"
             << "}";

        std::vector<uint32_t> inputData(wgThreads);
        for (uint32_t x = 0; x < wgThreads; ++x) {
            inputData[x] = x;
        }
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 1, inputData,
            [wgThreads, id, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, wgThreads, id, filter);
            });
    });

CTS_TEST(g, "broadcastFirst,split")
    .desc("Test broadcastFirst with only some active invocations")
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
        wgsl << "\nenable subgroups;\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage> input : array<u32>;\n\n"
             << "struct Metadata {\n"
             << "  id : array<u32, " << wgThreads << ">,\n"
             << "  size : array<u32, " << wgThreads << ">,\n"
             << "}\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<u32>;\n\n"
             << "@group(0) @binding(2)\n"
             << "var<storage, read_write> metadata: Metadata;\n\n"
             << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
             << ")\n"
             << "fn main(\n"
             << "  @builtin(local_invocation_index) lid : u32,\n"
             << "  @builtin(subgroup_invocation_id) id : u32,\n"
             << "  @builtin(subgroup_size) subgroupSize : u32,\n"
             << ") {\n"
             << "  metadata.id[lid] = id;\n"
             << "  metadata.size[lid] = subgroupSize;\n\n"
             << "  if " << testcase.cond << " {\n"
             << "    output[lid] = subgroupBroadcastFirst(input[lid]);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        std::vector<uint32_t> inputData(wgThreads);
        for (uint32_t x = 0; x < wgThreads; ++x) {
            inputData[x] = x;
        }
        const std::function<bool(uint32_t, uint32_t)> filter = testcase.filter;
        sg::runComputeTest(
            t, wgsl.str(), wgSize, 1, inputData,
            [wgThreads, filter](
                const std::vector<uint32_t>& metadata,
                const std::vector<uint32_t>& output) -> std::optional<std::string> {
                return checkCompute(metadata, output, wgThreads, -1, filter);
            });
    });

// Check broadcasts in fragment shaders.
std::optional<std::string> checkFragment(
    const std::vector<uint32_t>& data,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t broadcast) {
    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;
    auto coordToIndex = [&](uint32_t row, uint32_t col) {
        return static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;
    };

    std::map<uint32_t, bool> inBounds;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = coordToIndex(row, col);
            const uint32_t res = data[offset];
            auto it = inBounds.find(res);
            bool bound = it == inBounds.end() ? true : it->second;
            bound = bound && row < height - 1 && col < height - 1;
            inBounds[res] = bound;
        }
    }

    std::map<uint32_t, uint32_t> seen;
    std::map<uint32_t, uint32_t> sizes;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset = coordToIndex(row, col);
            const uint32_t res = data[offset];
            auto bit = inBounds.find(res);
            const bool bound = bit == inBounds.end() ? true : bit->second;
            if (!bound) {
                continue;
            }
            const uint32_t id = data[offset + 1];
            const uint32_t size = data[offset + 2];
            seen[res] += 1;
            if (id == broadcast) {
                const uint32_t linear = row * width + col;
                if (res != linear) {
                    std::ostringstream msg;
                    msg << "Row " << row << ", col " << col << ": incorrect broadcast\n- expected: "
                        << linear << "\n-      got: " << res;
                    return msg.str();
                }
                sizes[res] = size;
            }
        }
    }

    for (const auto& kv : inBounds) {
        const uint32_t id = kv.first;
        if (kv.second) {
            auto it = sizes.find(id);
            const uint32_t size = it == sizes.end() ? 0u : it->second;
            const uint32_t seenCount = it == sizes.end() ? 0u : it->second;
            if (size < seenCount) {
                std::ostringstream msg;
                msg << "Unexpected number of invocations for subgroup " << id
                    << "\n- expected: " << size << "\n-      got: " << seenCount;
                return msg.str();
            }
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "fragment")
    .desc("Test broadcast in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<Value> ids = {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(2)),
                                  Value(int64_t(3))};
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rgba32uint"))}}};
        return u.combine("size", sizes).beginSubcases().combine("id", ids).combineWithParams(
            formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const int64_t id = t.param<int64_t>("id");
        const WGPUTextureFormat format = WGPUTextureFormat_RGBA32Uint;

        const uint32_t innerTexels = (size[0] - 1) * (size[1] - 1);
        const SubgroupSizes subgroupSizes = getSubgroupSizes(t);
        if (innerTexels < subgroupSizes.maxSize) {
            t.skip("Too few texels to be reliable");
        }

        std::string broadcast;
        if (id == 0) {
            broadcast = "subgroupBroadcastFirst(input[linear].x)";
        } else {
            broadcast = "subgroupBroadcast(input[linear].x, " + std::to_string(id) + ")";
        }
        const uint32_t texels = size[0] * size[1];
        std::vector<uint32_t> inputData(texels);
        for (uint32_t x = 0; x < texels; ++x) {
            inputData[x] = x;
        }

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "@group(0) @binding(0)\n"
                 << "var<uniform> input : array<vec4u, " << inputData.size() << ">;\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << "  @builtin(subgroup_invocation_id) id : u32,\n"
                 << "  @builtin(subgroup_size) size : u32,\n"
                 << ") -> @location(0) vec4u {\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << size[0] << ";\n\n"
                 << "  return vec4u(" << broadcast << ", id, size, linear);\n"
                 << "}";

        sg::runFragmentTest(
            t, format, fsShader.str(), size[0], size[1], inputData, sg::FragmentInputKind::U32,
            [size, id](const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkFragment(data, format, size[0], size[1], static_cast<uint32_t>(id));
            });
    });

} // namespace
