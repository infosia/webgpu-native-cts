// Ported from gpuweb/cts src/webgpu/shader/execution/shader_io/workgroup_size.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations):
// - Upstream's `checkResults` returns Error|undefined; here it returns
//   std::optional<std::string> to match expectGPUBufferValuesPassCheck's
//   callback signature.
// - `t.skipIf(cond, msg)` is not a harness method; ported as `if (cond) { t.skip(msg); }`.
// - `makeBufferWithContents` with a zero-filled Uint32Array is ported as
//   `createBufferTracked` with Storage|CopySrc|CopyDst usage (WebGPU zero-initialises
//   storage buffers by default); the upstream Uint32Array is all zeros anyway.
// - `readGPUBufferRangeTyped` / `expectOK` is replaced with
//   `expectGPUBufferValuesPassCheck`, which reads back and invokes the check
//   inline, faithfully replicating the upstream validation logic.
// - `createComputePipeline` (JS sync) maps to `createComputePipelineTracked`.

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,shader_io,workgroup_size",
    "Test that workgroup size is set correctly");

static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// Faithful port of upstream checkResults().
// Returns an error string on failure, nullopt on success.
std::optional<std::string> checkResults(
    uint32_t sizeX,
    uint32_t sizeY,
    uint32_t sizeZ,
    uint32_t numWGs,
    const uint8_t* rawData,
    size_t rawLen)
{
    const uint32_t totalInvocations = sizeX * sizeY * sizeZ;
    // Each workgroup writes 4 u32 values: x, y, z, total.
    const size_t requiredBytes = static_cast<size_t>(numWGs) * 4u * sizeof(uint32_t);
    if (rawLen < requiredBytes) {
        std::ostringstream msg;
        msg << "buffer too small: " << rawLen << " < " << requiredBytes;
        return msg.str();
    }
    for (uint32_t i = 0; i < numWGs; ++i) {
        uint32_t wgx_data = 0;
        uint32_t wgy_data = 0;
        uint32_t wgz_data = 0;
        uint32_t total_data = 0;
        std::memcpy(&wgx_data,   rawData + (4u * i + 0u) * sizeof(uint32_t), sizeof(uint32_t));
        std::memcpy(&wgy_data,   rawData + (4u * i + 1u) * sizeof(uint32_t), sizeof(uint32_t));
        std::memcpy(&wgz_data,   rawData + (4u * i + 2u) * sizeof(uint32_t), sizeof(uint32_t));
        std::memcpy(&total_data, rawData + (4u * i + 3u) * sizeof(uint32_t), sizeof(uint32_t));

        // Upstream error messages use the variable name on the wrong side
        // (e.g. "expected: ${wgx_data}" / "got: ${sizeX}") — ported faithfully
        // to preserve identical error text.
        if (wgx_data != sizeX) {
            std::ostringstream msg;
            msg << "Incorrect workgroup size x dimension for wg " << i << ":\n";
            msg << "- expected: " << wgx_data << "\n";
            msg << "- got:      " << sizeX;
            return msg.str();
        }
        if (wgy_data != sizeY) {
            std::ostringstream msg;
            msg << "Incorrect workgroup size y dimension for wg " << i << ":\n";
            msg << "- expected: " << wgy_data << "\n";
            msg << "- got:      " << sizeY;
            return msg.str();
        }
        if (wgz_data != sizeZ) {
            std::ostringstream msg;
            msg << "Incorrect workgroup size y dimension for wg " << i << ":\n";
            msg << "- expected: " << wgz_data << "\n";
            msg << "- got:      " << sizeZ;
            return msg.str();
        }
        if (total_data != totalInvocations) {
            std::ostringstream msg;
            msg << "Incorrect workgroup total invocations for wg " << i << ":\n";
            msg << "- expected: " << total_data << "\n";
            msg << "- got:      " << totalInvocations;
            return msg.str();
        }
    }
    return std::nullopt;
}

// ============================================================
// Test: workgroup_size
// ============================================================
CTS_TEST(g, "workgroup_size")
    .desc("Test workgroup size is set correctly")
    .params([](ParamsBuilder u) {
        return u
            .combine("wgx", {1, 3, 4, 8, 11, 16, 51, 64, 128, 256})
            .combine("wgy", {1, 3, 4, 8, 16, 51, 64, 256})
            .combine("wgz", {1, 3, 11, 16, 128, 256})
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t wgx = t.param<int>("wgx");
        const uint32_t wgy = t.param<int>("wgy");
        const uint32_t wgz = t.param<int>("wgz");

        const WGPULimits limits = t.getLimits();

        if (wgx > limits.maxComputeWorkgroupSizeX) {
            t.skip(std::string("workgroup size x: ") + std::to_string(wgx) +
                   " > limit: " + std::to_string(limits.maxComputeWorkgroupSizeX));
        }
        if (wgy > limits.maxComputeWorkgroupSizeY) {
            t.skip(std::string("workgroup size x: ") + std::to_string(wgy) +
                   " > limit: " + std::to_string(limits.maxComputeWorkgroupSizeY));
        }
        if (wgz > limits.maxComputeWorkgroupSizeZ) {
            t.skip(std::string("workgroup size x: ") + std::to_string(wgz) +
                   " > limit: " + std::to_string(limits.maxComputeWorkgroupSizeZ));
        }
        const uint32_t totalInvocations = wgx * wgy * wgz;
        if (totalInvocations > limits.maxComputeInvocationsPerWorkgroup) {
            t.skip(std::string("workgroup size: ") + std::to_string(totalInvocations) +
                   " > limit: " + std::to_string(limits.maxComputeInvocationsPerWorkgroup));
        }

        // Build the shader code string: wrap the first literal in std::string so
        // the concatenation with std::to_string() is a std::string operation,
        // not a (disallowed) raw C-string pointer-arithmetic expression.
        const std::string code =
            std::string("\nstruct Values {\n"
            "  x : atomic<u32>,\n"
            "  y : atomic<u32>,\n"
            "  z : atomic<u32>,\n"
            "  total : atomic<u32>,\n"
            "}\n"
            "\n"
            "@group(0) @binding(0)\n"
            "var<storage, read_write> v : array<Values>;\n"
            "\n"
            "@compute @workgroup_size(") +
            std::to_string(wgx) + ", " +
            std::to_string(wgy) + ", " +
            std::to_string(wgz) +
            ")\n"
            "fn main(@builtin(local_invocation_id) lid : vec3u,\n"
            "        @builtin(workgroup_id) wgid : vec3u) {\n"
            "  atomicMax(&v[wgid.x].x, lid.x + 1);\n"
            "  atomicMax(&v[wgid.x].y, lid.y + 1);\n"
            "  atomicMax(&v[wgid.x].z, lid.z + 1);\n"
            "  atomicAdd(&v[wgid.x].total, 1);\n"
            "}";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        // layout:auto (null)
        pipeDesc.compute.module     = shaderModule;
        pipeDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        const uint32_t numWorkgroups = (totalInvocations < 256u) ? 5u : 3u;
        const uint64_t bufferSize    = static_cast<uint64_t>(numWorkgroups) * 4u * sizeof(uint32_t);

        // Zero-initialised storage buffer (WebGPU guarantees zero-init for
        // storage buffers; output never pre-filled with expected values).
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = bufferSize;
        bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = buffer;
        bgEntry.offset  = 0;
        bgEntry.size    = bufferSize;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, numWorkgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        // Capture by value (wgx/wgy/wgz/numWorkgroups are small PODs).
        t.expectGPUBufferValuesPassCheck(
            buffer,
            [wgx, wgy, wgz, numWorkgroups](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                return checkResults(wgx, wgy, wgz, numWorkgroups, actual, len);
            },
            /*srcByteOffset=*/0,
            static_cast<size_t>(bufferSize));
    });

// ============================================================
// Test: workgroup_size_override_exp
// ============================================================
CTS_TEST(g, "workgroup_size_override_exp")
    .desc("Test workgroup size can be set from an override expression")
    .params([](ParamsBuilder u) {
        return u
            .combine("override1", {1, 3, 4, 8, 11, 16, 51, 64, 128, 256})
            .combine("override2", {1, 2, 3, 4})
            .combine("override3", {1, 2, 4, 8})
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t override1 = t.param<int>("override1");
        const uint32_t override2 = t.param<int>("override2");
        const uint32_t override3 = t.param<int>("override3");

        // These expressions must mirror the WGSL workgroup_size attribute below.
        const uint32_t wgx = override1 + override2 + override3;
        const uint32_t wgy = override1 + override2 * override2;
        const uint32_t wgz = override1 + override3 * override3;

        const WGPULimits limits = t.getLimits();

        if (wgx > limits.maxComputeWorkgroupSizeX) {
            t.skip(std::string("workgroup size x: ") + std::to_string(wgx) +
                   " > limit: " + std::to_string(limits.maxComputeWorkgroupSizeX));
        }
        if (wgy > limits.maxComputeWorkgroupSizeY) {
            t.skip(std::string("workgroup size x: ") + std::to_string(wgy) +
                   " > limit: " + std::to_string(limits.maxComputeWorkgroupSizeY));
        }
        if (wgz > limits.maxComputeWorkgroupSizeZ) {
            t.skip(std::string("workgroup size x: ") + std::to_string(wgz) +
                   " > limit: " + std::to_string(limits.maxComputeWorkgroupSizeZ));
        }
        const uint32_t totalInvocations = wgx * wgy * wgz;
        if (totalInvocations > limits.maxComputeInvocationsPerWorkgroup) {
            t.skip(std::string("workgroup size: ") + std::to_string(totalInvocations) +
                   " > limit: " + std::to_string(limits.maxComputeInvocationsPerWorkgroup));
        }

        // The override identifiers in the shader match the upstream names exactly.
        constexpr std::string_view kCode =
            "\nstruct Values {\n"
            "  x : atomic<u32>,\n"
            "  y : atomic<u32>,\n"
            "  z : atomic<u32>,\n"
            "  total : atomic<u32>,\n"
            "}\n"
            "\n"
            "@group(0) @binding(0)\n"
            "var<storage, read_write> v : array<Values>;\n"
            "override override1: u32;\n"
            "override override2: u32;\n"
            "override override3: u32;\n"
            "\n"
            "@compute @workgroup_size(override1 + override2 + override3,\n"
            "  override1 + override2 * override2,\n"
            " override1 + override3 * override3)\n"
            "fn main(@builtin(local_invocation_id) lid : vec3u,\n"
            "        @builtin(workgroup_id) wgid : vec3u) {\n"
            "  atomicMax(&v[wgid.x].x, lid.x + 1);\n"
            "  atomicMax(&v[wgid.x].y, lid.y + 1);\n"
            "  atomicMax(&v[wgid.x].z, lid.z + 1);\n"
            "  atomicAdd(&v[wgid.x].total, 1);\n"
            "}";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(kCode);

        // Pipeline override constants: keep the array alive until
        // createComputePipelineTracked returns.
        std::array<WGPUConstantEntry, 3> constants;
        constants[0] = WGPU_CONSTANT_ENTRY_INIT;
        constants[0].key   = sv("override1");
        constants[0].value = static_cast<double>(override1);

        constants[1] = WGPU_CONSTANT_ENTRY_INIT;
        constants[1].key   = sv("override2");
        constants[1].value = static_cast<double>(override2);

        constants[2] = WGPU_CONSTANT_ENTRY_INIT;
        constants[2].key   = sv("override3");
        constants[2].value = static_cast<double>(override3);

        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        // layout:auto (null)
        pipeDesc.compute.module        = shaderModule;
        pipeDesc.compute.entryPoint    = sv("main");
        pipeDesc.compute.constantCount = constants.size();
        pipeDesc.compute.constants     = constants.data();
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        const uint32_t numWorkgroups = (totalInvocations < 256u) ? 5u : 3u;
        const uint64_t bufferSize    = static_cast<uint64_t>(numWorkgroups) * 4u * sizeof(uint32_t);

        // Zero-initialised storage buffer (output never pre-filled with expected values).
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = bufferSize;
        bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = buffer;
        bgEntry.offset  = 0;
        bgEntry.size    = bufferSize;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, numWorkgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        t.expectGPUBufferValuesPassCheck(
            buffer,
            [wgx, wgy, wgz, numWorkgroups](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                return checkResults(wgx, wgy, wgz, numWorkgroups, actual, len);
            },
            /*srcByteOffset=*/0,
            static_cast<size_t>(bufferSize));
    });

} // namespace
