// Ported from gpuweb/cts src/webgpu/api/operation/adapter/requestAdapter.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes (deviations from upstream, documented inline as well):
//
//  1. Fixture: upstream uses the plain `Fixture` and calls
//     getGPU(t.rec).requestAdapter(...). Each test creates its own private
//     WGPUInstance -> adapter -> device (OwnedDeviceContext, RAII cleanup, same
//     pattern as api/operation/buffers/map.spec.cpp and
//     api/validation/error_scope.spec.cpp). The harness readback helpers pump
//     cache().instance and would hang on a private-instance device, so
//     testAdapter() does its own map-read via cts::bufferMapSync(ctx.instance, ...)
//     against the private instance.
//
//  2. requestAdapter_invalid_featureLevel: in JS, featureLevel is a string and
//     invalid string values ('cor', 'Core', 'compatability', '', ' ') must
//     return null. The C WebGPU API models featureLevel as the enum
//     WGPUFeatureLevel, so an invalid string simply cannot be expressed — there
//     is no C analog for passing an invalid featureLevel value. The valid
//     featureLevel cases (undefined/core/compatibility) ARE portable and run
//     testAdapter(); the invalid string cases keep their upstream query
//     identity (param value mirrors the upstream string) but are recorded as
//     skipped with a reason, since the request cannot be made. File is partial.
//
//  3. adapter.info.isFallbackAdapter has no field in the standard webgpu.h
//     WGPUAdapterInfo struct, so the upstream
//     `t.expect(adapter.info.isFallbackAdapter === true)` check for
//     forceFallbackAdapter===true cannot be asserted portably. When a fallback
//     adapter is returned we still run testAdapter() for functionality; the
//     fallback-ness assertion is omitted with this note.
//
//  4. Link safety: adapter-info would be queried via wgpuAdapterGetInfo on the
//     owned adapter only if needed; this file does not require it.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// Upstream uses the plain Fixture (getGPU(t.rec).requestAdapter). We use the
// base Fixture (NOT GpuTest) so the test bodies follow the same
// lifecycle-fixture pattern as api/validation/error_scope.spec.cpp and
// api/operation/buffers/map.spec.cpp (private instance/adapter/device, never the
// shared harness device). Using GpuTest here would be incorrect: GpuTest::init()
// eagerly creates the shared cached device on the shared harness adapter, which
// the C API treats as single-use (one device per adapter). That would "consume"
// the shared adapter and cascade-fail any later AllFeaturesMaxLimitsGpuTest case
// in the same process ("adapter is consumed"). The base Fixture has an empty
// init(), so these tests touch only their own private instance.
TestGroup<Fixture> g = MakeTestGroup<Fixture>(
    "api,operation,adapter,requestAdapter",
    R"(
Tests for GPU.requestAdapter.

Test all possible options to requestAdapter.
default, low-power, and high performance should all always return adapters.
forceFallbackAdapter may or may not return an adapter.
invalid featureLevel values should not return an adapter.

GPU.requestAdapter can technically return null for any reason
but we need test functionality so the test requires an adapter except
when forceFallbackAdapter is true.

The test runs simple compute shader is run that fills a buffer with consecutive
values and then checks the result to test the adapter for basic functionality.
)");

// ---------------------------------------------------------------------------
// Owned instance/adapter/device with RAII cleanup. The whole point of these
// tests is adapter/device lifecycle, so they must never touch the shared
// harness device. Released in reverse order on scope exit (fail()/skip() throw).
// ---------------------------------------------------------------------------

struct OwnedDeviceContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;

    OwnedDeviceContext() = default;
    OwnedDeviceContext(const OwnedDeviceContext&) = delete;
    OwnedDeviceContext& operator=(const OwnedDeviceContext&) = delete;

    ~OwnedDeviceContext() {
        if (device != nullptr) {
            wgpuDeviceRelease(device);
        }
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
        }
    }
};

constexpr const char* kComputeShader = R"(
struct Buffer { data: array<u32>, };

@group(0) @binding(0) var<storage, read_write> buffer: Buffer;
@compute @workgroup_size(1u) fn main(
    @builtin(global_invocation_id) id: vec3<u32>) {
  buffer.data[id.x] = id.x + 1230000u;
}
)";

WGPUStringView sv(const char* value) {
    return WGPUStringView{value, value == nullptr ? 0 : std::strlen(value)};
}

// Mirrors the upstream testAdapter(): create a device on the given adapter, run
// a compute shader that fills a buffer with id.x + kOffset, and verify the
// result via copy + map-read. Uses the private instance (ctx.instance) for the
// map-read so it does not pump the shared harness instance.
void testAdapter(Fixture& t, OwnedDeviceContext& ctx) {
    if (ctx.adapter == nullptr) {
        t.fail("Failed to get adapter.");
    }

    WGPUDeviceDescriptor deviceDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
    DeviceResult device = requestDeviceSync(ctx.instance, ctx.adapter, &deviceDesc);
    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        t.fail("Failed to get device: " + device.message);
    }
    ctx.device = device.device;
    WGPUQueue queue = wgpuDeviceGetQueue(ctx.device);

    const uint32_t kOffset = 1230000;
    const uint32_t kNumElements = 64;
    const uint64_t kBufferSize = static_cast<uint64_t>(kNumElements) * 4;

    // Compute pipeline (layout: 'auto').
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = sv(kComputeShader);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(ctx.device, &moduleDesc);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // layout: 'auto'
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(ctx.device, &pipelineDesc);

    // Storage buffer written by the shader.
    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = kBufferSize;
    bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(ctx.device, &bufferDesc);

    // Result buffer (map-readable).
    WGPUBufferDescriptor resultDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    resultDesc.size = kBufferSize;
    resultDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer resultBuffer = wgpuDeviceCreateBuffer(ctx.device, &resultDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = kBufferSize;
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device, nullptr);
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, kNumElements, 1, 1);
    wgpuComputePassEncoderEnd(pass);

    wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, 0, resultBuffer, 0, kBufferSize);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &commandBuffer);

    // Map-read the result on the private instance.
    const WGPUMapAsyncStatus mapStatus = bufferMapSync(
        ctx.instance, resultBuffer, WGPUMapMode_Read, 0, static_cast<size_t>(kBufferSize));
    if (mapStatus != WGPUMapAsyncStatus_Success) {
        t.fail("Failed to map result buffer.");
    }
    const void* mapped =
        wgpuBufferGetConstMappedRange(resultBuffer, 0, static_cast<size_t>(kBufferSize));
    if (mapped == nullptr) {
        t.fail("getMappedRange returned null.");
    }

    std::vector<uint32_t> actual(kNumElements, 0);
    std::memcpy(actual.data(), mapped, static_cast<size_t>(kBufferSize));
    wgpuBufferUnmap(resultBuffer);

    bool ok = true;
    for (uint32_t i = 0; i < kNumElements; ++i) {
        if (actual[i] != i + kOffset) {
            ok = false;
            break;
        }
    }
    t.expect(ok, "compute pipeline ran");

    wgpuBindGroupRelease(bindGroup);
    wgpuBufferRelease(resultBuffer);
    wgpuBufferRelease(buffer);
    wgpuComputePipelineRelease(pipeline);
    wgpuShaderModuleRelease(module);
    wgpuQueueRelease(queue);
    wgpuCommandBufferRelease(commandBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuComputePassEncoderRelease(pass);
    // ctx.device is released by OwnedDeviceContext's destructor.
}

std::vector<Value> powerPreferenceModes() {
    // undefined, 'low-power', 'high-performance'
    return {Value::undef(), Value("low-power"), Value("high-performance")};
}

std::vector<Value> forceFallbackOptions() {
    // undefined, false, true
    return {Value::undef(), Value(false), Value(true)};
}

// validFeatureLevels followed by invalidFeatureLevels, mirroring upstream order.
std::vector<Value> featureLevelOptions() {
    return {
        // valid: undefined, 'core', 'compatibility'
        Value::undef(),
        Value("core"),
        Value("compatibility"),
        // invalid strings
        Value("cor"),
        Value("Core"),
        Value("compatability"),
        Value(""),
        Value(" "),
    };
}

WGPUPowerPreference toPowerPreference(const std::string& mode) {
    if (mode == "low-power") {
        return WGPUPowerPreference_LowPower;
    }
    if (mode == "high-performance") {
        return WGPUPowerPreference_HighPerformance;
    }
    return WGPUPowerPreference_Undefined;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

CTS_TEST(g, "requestAdapter")
    .desc("request adapter with all possible options and check for basic functionality")
    .params([](ParamsBuilder u) {
        return u.combine("powerPreference", powerPreferenceModes())
            .combine("forceFallbackAdapter", forceFallbackOptions());
    })
    .fn([](Fixture& t) {
        const bool forceFallbackUndefined = t.paramIsUndefined("forceFallbackAdapter");
        const bool forceFallbackAdapter =
            forceFallbackUndefined ? false : t.param<bool>("forceFallbackAdapter");

        OwnedDeviceContext ctx;
        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("failed to create WGPUInstance");
        }

        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        if (!t.paramIsUndefined("powerPreference")) {
            options.powerPreference = toPowerPreference(t.param<std::string>("powerPreference"));
        }
        if (!forceFallbackUndefined) {
            options.forceFallbackAdapter = forceFallbackAdapter ? WGPU_TRUE : WGPU_FALSE;
        }

        AdapterResult adapter = requestAdapterSync(ctx.instance, &options);
        if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
            // Failing to create an adapter is only OK when forceFallbackAdapter is true.
            t.expect(forceFallbackAdapter, "no adapter returned and forceFallbackAdapter is not true");
            // Mark the test as skipped (as long as nothing else failed before this point).
            t.skip("No fallback adapter available");
        }
        ctx.adapter = adapter.adapter;

        // Upstream additionally asserts adapter.info.isFallbackAdapter === true when
        // forceFallbackAdapter === true. The standard webgpu.h WGPUAdapterInfo has no
        // isFallbackAdapter field, so that assertion is omitted (porting note 3).

        testAdapter(t, ctx);
    });

CTS_TEST(g, "requestAdapter_invalid_featureLevel")
    .desc("request adapter with invalid featureLevel string values return null")
    .params([](ParamsBuilder u) {
        return u.combine("featureLevel", featureLevelOptions());
    })
    .fn([](Fixture& t) {
        // The C WebGPU API models featureLevel as the WGPUFeatureLevel enum, so the
        // invalid string values ('cor', 'Core', 'compatability', '', ' ') cannot be
        // passed at all — there is no C analog for "return null for an invalid
        // featureLevel string". Only the valid featureLevels are portable
        // (porting note 2).
        const bool isUndefined = t.paramIsUndefined("featureLevel");
        const std::string featureLevel = isUndefined ? std::string() : t.param<std::string>("featureLevel");

        WGPUFeatureLevel level = WGPUFeatureLevel_Undefined;
        bool valid = true;
        if (isUndefined) {
            level = WGPUFeatureLevel_Undefined;
        } else if (featureLevel == "core") {
            level = WGPUFeatureLevel_Core;
        } else if (featureLevel == "compatibility") {
            level = WGPUFeatureLevel_Compatibility;
        } else {
            valid = false;
        }

        if (!valid) {
            t.skip("invalid featureLevel string values have no WGPUFeatureLevel enum analog");
        }

        OwnedDeviceContext ctx;
        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("failed to create WGPUInstance");
        }

        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.featureLevel = level;

        AdapterResult adapter = requestAdapterSync(ctx.instance, &options);
        if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
            t.fail("Failed to get adapter for a valid featureLevel.");
        }
        ctx.adapter = adapter.adapter;
        testAdapter(t, ctx);
    });

CTS_TEST(g, "requestAdapter_no_parameters")
    .desc("request adapter with no parameters")
    .fn([](Fixture& t) {
        OwnedDeviceContext ctx;
        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("failed to create WGPUInstance");
        }

        AdapterResult adapter = requestAdapterSync(ctx.instance, nullptr);
        if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
            t.fail("Failed to get adapter.");
        }
        ctx.adapter = adapter.adapter;
        testAdapter(t, ctx);
    });

} // namespace
