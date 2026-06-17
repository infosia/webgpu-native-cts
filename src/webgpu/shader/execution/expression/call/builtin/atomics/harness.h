// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/harness.ts
//   @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#ifndef CTS_WEBGPU_SHADER_EXECUTION_EXPRESSION_CALL_BUILTIN_ATOMICS_HARNESS_H_
#define CTS_WEBGPU_SHADER_EXECUTION_EXPRESSION_CALL_BUILTIN_ATOMICS_HARNESS_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

namespace cts::atomics {

inline constexpr int kWorkgroupSizes[] = {1, 2, 32, 64};
inline constexpr int kDispatchSizes[] = {1, 4, 8, 16};
inline constexpr int kOnlyWorkgroupSizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};

enum class ScalarType { U32, I32 };

inline const char* scalarTypeName(ScalarType type) {
    switch (type) {
        case ScalarType::U32:
            return "u32";
        case ScalarType::I32:
            return "i32";
    }
    std::abort();
}

inline ScalarType scalarTypeFromParam(const std::string& value) {
    if (value == "u32") {
        return ScalarType::U32;
    }
    if (value == "i32") {
        return ScalarType::I32;
    }
    std::abort();
}

struct MapId {
    std::uint32_t (*f)(std::uint32_t id, std::uint32_t max);
    std::string (*wgsl)(std::uint32_t max, ScalarType scalarType);
};

inline uint32_t mapIdPassthroughF(uint32_t id, uint32_t) {
    return id;
}

inline std::string mapIdPassthroughWgsl(uint32_t, ScalarType scalarType) {
    const std::string type = scalarTypeName(scalarType);
    return "fn map_id(id: " + type + ") -> " + type + " { return id; }";
}

inline uint32_t mapIdRemapF(uint32_t id, uint32_t max) {
    return ((id * 14957u) ^ ((id * 26561u) >> 2u)) % max;
}

inline std::string mapIdRemapWgsl(uint32_t max, ScalarType scalarType) {
    const std::string type = scalarTypeName(scalarType);
    return "fn map_id(id: " + type + ") -> " + type +
           " { return ((id * 14957) ^ ((id * 26561) >> 2)) % " + std::to_string(max) + "; }";
}

inline const MapId kMapIdPassthrough{mapIdPassthroughF, mapIdPassthroughWgsl};
inline const MapId kMapIdRemap{mapIdRemapF, mapIdRemapWgsl};

inline const MapId& mapIdFromParam(const std::string& value) {
    if (value == "passthrough") {
        return kMapIdPassthrough;
    }
    if (value == "remap") {
        return kMapIdRemap;
    }
    std::abort();
}

inline std::vector<Value> workgroupSizeValues() {
    std::vector<Value> values;
    for (int value : kWorkgroupSizes) {
        values.emplace_back(value);
    }
    return values;
}

inline std::vector<Value> dispatchSizeValues() {
    std::vector<Value> values;
    for (int value : kDispatchSizes) {
        values.emplace_back(value);
    }
    return values;
}

inline std::vector<Value> onlyWorkgroupSizeValues() {
    std::vector<Value> values;
    for (int value : kOnlyWorkgroupSizes) {
        values.emplace_back(value);
    }
    return values;
}

inline ParamsBuilder basicParams(ParamsBuilder u) {
    return u.combine("workgroupSize", workgroupSizeValues())
        .combine("dispatchSize", dispatchSizeValues())
        .combine("scalarType", {"u32", "i32"});
}

inline ParamsBuilder mapIdParams(ParamsBuilder u) {
    return u.combine("workgroupSize", workgroupSizeValues())
        .combine("dispatchSize", dispatchSizeValues())
        .combine("mapId", {"passthrough", "remap"})
        .combine("scalarType", {"u32", "i32"});
}

inline ParamsBuilder onlyWorkgroupParams(ParamsBuilder u) {
    return u.combine("workgroupSize", onlyWorkgroupSizeValues()).combine("scalarType", {"u32", "i32"});
}

inline uint32_t bits(int32_t value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline std::string svString(std::string_view text) {
    return std::string(text);
}

inline WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

inline WGPUComputePipeline createComputePipelineAuto(GpuTest& t, const std::string& wgsl) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

struct BufferBinding {
    WGPUBuffer buffer = nullptr;
    uint64_t size = 0;
};

inline WGPUBindGroup makeAutoBindGroup(
    GpuTest& t,
    WGPUComputePipeline pipeline,
    const std::vector<BufferBinding>& buffers) {
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::vector<WGPUBindGroupEntry> entries(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = static_cast<uint32_t>(i);
        entries[i].buffer = buffers[i].buffer;
        entries[i].offset = 0;
        entries[i].size = buffers[i].size;
    }

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = bgl;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(desc);
    wgpuBindGroupLayoutRelease(bgl);
    return bindGroup;
}

inline void runComputePassX(GpuTest& t, WGPUComputePipeline pipeline, WGPUBindGroup bindGroup, uint32_t x) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, x, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);
}

inline WGPUBuffer makeStorageBuffer(GpuTest& t, uint64_t elements) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = elements * sizeof(uint32_t);
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

inline WGPUBuffer makeFilledStorageBuffer(GpuTest& t, const std::vector<uint32_t>& values) {
    return t.makeBufferWithContents(
        values.data(),
        values.size() * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
}

inline WGPUBuffer makeFilledStorageBuffer(GpuTest& t, uint64_t elements, uint32_t value) {
    return makeFilledStorageBuffer(t, std::vector<uint32_t>(static_cast<size_t>(elements), value));
}

inline std::vector<uint32_t> readBackU32(GpuTest& t, WGPUBuffer buffer, size_t count) {
    std::vector<uint32_t> out(count, 0);
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&out, count](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < count * sizeof(uint32_t)) {
                return std::string("readback buffer too small");
            }
            std::memcpy(out.data(), actual, count * sizeof(uint32_t));
            return std::nullopt;
        },
        0,
        count * sizeof(uint32_t));
    return out;
}

inline std::string joinValues(const std::vector<uint32_t>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

inline void expectOneOf(GpuTest& t, uint32_t value, const std::vector<uint32_t>& expected, const std::string& label) {
    if (std::find(expected.begin(), expected.end(), value) == expected.end()) {
        t.fail("Unexpected value in " + label + ": " + std::to_string(value) +
               ", expected one of: " + joinValues(expected));
    }
}

inline void expectSortedEqual(GpuTest& t, std::vector<uint32_t> actual, std::vector<uint32_t> expected) {
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    if (actual != expected) {
        t.fail("sorted values mismatch: actual " + joinValues(actual) + ", expected " + joinValues(expected));
    }
}

inline void validateCompareExchangeWeakAdvanced(
    GpuTest& t,
    const std::vector<uint32_t>& oldValues,
    const std::vector<uint32_t>& exchangedValues,
    uint32_t numInvocations,
    uint32_t numWrites) {
    const uint32_t defaultValue = 99999999u;
    const std::array<uint32_t, 2> pingPongValues = {24u, 68u};
    for (uint32_t w = 0; w < numWrites; ++w) {
        const size_t offset = static_cast<size_t>(w) * numInvocations;
        uint32_t exchangeCount = 0;
        uint32_t exchangeIndex = 0;
        for (uint32_t i = 0; i < numInvocations; ++i) {
            if (exchangedValues[offset + i] == 1u) {
                exchangeCount++;
                exchangeIndex = i;
            } else if (exchangedValues[offset + i] != 0u) {
                t.fail("unexpected exchanged value: " + std::to_string(exchangedValues[offset + i]));
            }
        }

        if (exchangeCount == 0) {
            for (uint32_t i = 0; i < numInvocations; ++i) {
                if (oldValues[offset + i] != defaultValue) {
                    t.fail("spurious failure expected only default old_values for write " + std::to_string(w));
                }
            }
            continue;
        }

        if (exchangeCount != 1) {
            t.fail("more than one invocation exchanged its value for write " + std::to_string(w));
        }

        const uint32_t oldValue = pingPongValues[w % 2u];
        if (oldValues[offset + exchangeIndex] != oldValue) {
            t.fail("old_values exchanged slot mismatch for write " + std::to_string(w));
        }
        for (uint32_t i = 0; i < numInvocations; ++i) {
            if (i == exchangeIndex) {
                continue;
            }
            const uint32_t value = oldValues[offset + i];
            if (value != pingPongValues[0] && value != pingPongValues[1]) {
                t.fail("old_values contains non ping-pong value for write " + std::to_string(w));
            }
        }
    }
}

inline void runStorageVariableTest(
    GpuTest& t,
    int workgroupSize,
    int dispatchSize,
    uint32_t bufferNumElements,
    uint32_t initValue,
    const std::string& op,
    const std::vector<uint32_t>& expected,
    ScalarType scalarType,
    const std::string& extra = "") {
    if (expected.size() != bufferNumElements) {
        t.fail("'expected' buffer size is incorrect");
    }
    const std::string type = scalarTypeName(scalarType);
    const std::string wgsl =
        "@group(0) @binding(0)\n"
        "var<storage, read_write> output : array<atomic<" + type + ">>;\n"
        "\n"
        "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
        "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
        "  let id = " + type + "(global_invocation_id[0]);\n"
        "  " + op + ";\n"
        "}\n" +
        extra + "\n";

    WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
    WGPUBuffer outputBuffer = makeFilledStorageBuffer(t, bufferNumElements, initValue);
    WGPUBindGroup bindGroup =
        makeAutoBindGroup(t, pipeline, {{outputBuffer, static_cast<uint64_t>(bufferNumElements) * sizeof(uint32_t)}});
    runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size() * sizeof(uint32_t));
}

inline void runWorkgroupVariableTest(
    GpuTest& t,
    int workgroupSize,
    int dispatchSize,
    uint32_t wgNumElements,
    uint32_t initValue,
    const std::string& op,
    const std::vector<uint32_t>& expected,
    ScalarType scalarType,
    const std::string& extra = "") {
    if (expected.size() != static_cast<size_t>(wgNumElements) * dispatchSize) {
        t.fail("'expected' buffer size is incorrect");
    }
    const std::string type = scalarTypeName(scalarType);
    const uint32_t outputElements = wgNumElements * static_cast<uint32_t>(dispatchSize);
    const std::string wgsl =
        "var<workgroup> wg: array<atomic<" + type + ">, " + std::to_string(wgNumElements) + ">;\n"
        "\n"
        "@group(0) @binding(0)\n"
        "var<storage, read_write> output: array<" + type + ", " + std::to_string(outputElements) + ">;\n"
        "\n"
        "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
        "fn main(\n"
        "  @builtin(local_invocation_index) local_invocation_index: u32,\n"
        "  @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
        "  let id = " + type + "(local_invocation_index);\n"
        "  let global_id = " + type + "(workgroup_id.x * " + std::to_string(wgNumElements) +
        " + local_invocation_index);\n"
        "  if (local_invocation_index == 0) {\n"
        "    for (var i = 0u; i < " + std::to_string(wgNumElements) + "; i++) {\n"
        "      atomicStore(&wg[i], bitcast<" + type + ">(" + std::to_string(initValue) + "u));\n"
        "    }\n"
        "  }\n"
        "  workgroupBarrier();\n"
        "  " + op + ";\n"
        "  workgroupBarrier();\n"
        "  if (local_invocation_index == 0) {\n"
        "    for (var i = 0u; i < " + std::to_string(wgNumElements) + "; i++) {\n"
        "      output[(workgroup_id.x * " + std::to_string(wgNumElements) + ") + i] = atomicLoad(&wg[i]);\n"
        "    }\n"
        "  }\n"
        "}\n" +
        extra + "\n";

    WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
    WGPUBuffer outputBuffer = makeStorageBuffer(t, outputElements);
    WGPUBindGroup bindGroup =
        makeAutoBindGroup(t, pipeline, {{outputBuffer, static_cast<uint64_t>(outputElements) * sizeof(uint32_t)}});
    runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size() * sizeof(uint32_t));
}

} // namespace cts::atomics

#endif // CTS_WEBGPU_SHADER_EXECUTION_EXPRESSION_CALL_BUILTIN_ATOMICS_HARNESS_H_
