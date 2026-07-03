// Ported from gpuweb/cts src/webgpu/api/operation/device/all_limits_and_features.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
//  - Upstream uses a custom GPUTestSubcaseBatchState
//    (AllLimitsAndFeaturesGPUTestSubcaseBatchState) that requests a device whose
//    requiredLimits are set to *all* the adapter's max limits and whose
//    requiredFeatures are *all* the adapter's features. This is a device/adapter
//    lifecycle test, so the port creates a PRIVATE WGPUInstance -> adapter ->
//    device (OwnedDeviceContext, the pattern from
//    api/operation/buffers/map.spec.cpp / api/validation/error_scope.spec.cpp)
//    with RAII cleanup. It never touches the shared harness device and never
//    uses harness readback helpers (none are needed: the test only reads back
//    limits/features).
//  - JS `for (const key in adapter.limits)` has no C analog (no struct
//    introspection). WGPULimits is a flat struct of named numeric fields, so the
//    port compares every named limit field of device.limits against
//    adapter.limits via wgpuAdapterGetLimits / wgpuDeviceGetLimits. The set of
//    fields mirrors the WGPULimits struct in webgpu.h (all 32 numeric members;
//    nextInChain is not a limit). The per-limit failure message mirrors the
//    upstream `device.limits.<key> (<dl>) === adapter.limits.<key> (<al>)` text.
//  - JS `for (const feature of t.adapter.features)` -> enumerate the adapter
//    feature set via wgpuAdapterGetFeatures (WGPUSupportedFeatures), then assert
//    each is present on the device via wgpuDeviceHasFeature. Mirrors the upstream
//    `device has feature: <feature>` expectation.
//  - Link safety: the adapter/device come from a private instance the test owns,
//    so adapter limits/features are read directly off that adapter (no
//    wgpuDeviceGetAdapterInfo, which yawgpu may not export).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// Owns a private instance/adapter/device, released in reverse order on scope
// exit (fail()/skip() throw, so RAII is required). This test requests a device
// with all the adapter's features and all the adapter's max limits, so it must
// not reuse the shared harness adapter/device.
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

// Base Fixture (NOT GpuTest): GpuTest::init() eagerly creates the shared cached
// device on the shared harness adapter, which the C API treats as single-use
// (one device per adapter). That would consume the shared adapter and
// cascade-fail later AllFeaturesMaxLimitsGpuTest cases ("adapter is consumed").
// This test only ever uses its own private instance/adapter/device.
TestGroup<Fixture> g = MakeTestGroup<Fixture>(
    "api,operation,device,all_limits_and_features",
    "Test you can request an device with all features and limits");

CTS_TEST(g, "everything")
    .desc(
        R"(
Test we can request all features and limits.

It is expected that, even though this is generally not recommended, because
it is possible, make sure it works and continues to work going forward so that
changes to WebGPU do not break sites requesting everything.
)")
    .fn([](Fixture& t) {
        // This is a device-lifecycle test: it must request a device whose
        // requiredLimits are the adapter's max limits and whose requiredFeatures
        // are all the adapter's features. Use a private instance/adapter/device.
        OwnedDeviceContext ctx;

        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("failed to create WGPUInstance");
        }

        AdapterResult adapter = requestAdapterSync(ctx.instance, adapterOptions());
        if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
            t.fail("failed to request adapter: " + adapter.message);
        }
        ctx.adapter = adapter.adapter;

        // Gather the adapter's max limits (used as the device's requiredLimits).
        WGPULimits adapterLimits = WGPU_LIMITS_INIT;
        if (wgpuAdapterGetLimits(ctx.adapter, &adapterLimits) != WGPUStatus_Success) {
            t.fail("wgpuAdapterGetLimits failed");
        }

        // Gather all the adapter's features (used as the device's
        // requiredFeatures). Copy into an owned vector that outlives the
        // requestDevice call; free the supported-features members immediately.
        WGPUSupportedFeatures adapterFeatures = WGPU_SUPPORTED_FEATURES_INIT;
        wgpuAdapterGetFeatures(ctx.adapter, &adapterFeatures);
        std::vector<WGPUFeatureName> requiredFeatures(
            adapterFeatures.features, adapterFeatures.features + adapterFeatures.featureCount);
        wgpuSupportedFeaturesFreeMembers(adapterFeatures);

        // Request the device with all features and all (max) limits.
        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredFeatureCount = requiredFeatures.size();
        desc.requiredFeatures = requiredFeatures.empty() ? nullptr : requiredFeatures.data();
        desc.requiredLimits = &adapterLimits;
        DeviceResult device = requestDeviceSync(ctx.instance, ctx.adapter, &desc);
        if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
            t.fail("failed to request device with all features and limits: " + device.message);
        }
        ctx.device = device.device;

        // Test that all the limits on the device match the adapter.
        WGPULimits deviceLimits = WGPU_LIMITS_INIT;
        if (wgpuDeviceGetLimits(ctx.device, &deviceLimits) != WGPUStatus_Success) {
            t.fail("wgpuDeviceGetLimits failed");
        }

        // Mirror JS `for (const key in adapter.limits)` over the named WGPULimits
        // fields (nextInChain is not a limit). Each entry compares
        // device.limits.<key> === adapter.limits.<key> and reports both values.
        struct LimitField {
            const char* name;
            uint64_t deviceValue;
            uint64_t adapterValue;
        };
        const std::vector<LimitField> limitFields = {
            {"maxTextureDimension1D", deviceLimits.maxTextureDimension1D,
             adapterLimits.maxTextureDimension1D},
            {"maxTextureDimension2D", deviceLimits.maxTextureDimension2D,
             adapterLimits.maxTextureDimension2D},
            {"maxTextureDimension3D", deviceLimits.maxTextureDimension3D,
             adapterLimits.maxTextureDimension3D},
            {"maxTextureArrayLayers", deviceLimits.maxTextureArrayLayers,
             adapterLimits.maxTextureArrayLayers},
            {"maxBindGroups", deviceLimits.maxBindGroups, adapterLimits.maxBindGroups},
            {"maxBindGroupsPlusVertexBuffers", deviceLimits.maxBindGroupsPlusVertexBuffers,
             adapterLimits.maxBindGroupsPlusVertexBuffers},
            {"maxBindingsPerBindGroup", deviceLimits.maxBindingsPerBindGroup,
             adapterLimits.maxBindingsPerBindGroup},
            {"maxDynamicUniformBuffersPerPipelineLayout",
             deviceLimits.maxDynamicUniformBuffersPerPipelineLayout,
             adapterLimits.maxDynamicUniformBuffersPerPipelineLayout},
            {"maxDynamicStorageBuffersPerPipelineLayout",
             deviceLimits.maxDynamicStorageBuffersPerPipelineLayout,
             adapterLimits.maxDynamicStorageBuffersPerPipelineLayout},
            {"maxSampledTexturesPerShaderStage", deviceLimits.maxSampledTexturesPerShaderStage,
             adapterLimits.maxSampledTexturesPerShaderStage},
            {"maxSamplersPerShaderStage", deviceLimits.maxSamplersPerShaderStage,
             adapterLimits.maxSamplersPerShaderStage},
            {"maxStorageBuffersPerShaderStage", deviceLimits.maxStorageBuffersPerShaderStage,
             adapterLimits.maxStorageBuffersPerShaderStage},
            {"maxStorageTexturesPerShaderStage", deviceLimits.maxStorageTexturesPerShaderStage,
             adapterLimits.maxStorageTexturesPerShaderStage},
            {"maxUniformBuffersPerShaderStage", deviceLimits.maxUniformBuffersPerShaderStage,
             adapterLimits.maxUniformBuffersPerShaderStage},
            {"maxUniformBufferBindingSize", deviceLimits.maxUniformBufferBindingSize,
             adapterLimits.maxUniformBufferBindingSize},
            {"maxStorageBufferBindingSize", deviceLimits.maxStorageBufferBindingSize,
             adapterLimits.maxStorageBufferBindingSize},
            {"minUniformBufferOffsetAlignment", deviceLimits.minUniformBufferOffsetAlignment,
             adapterLimits.minUniformBufferOffsetAlignment},
            {"minStorageBufferOffsetAlignment", deviceLimits.minStorageBufferOffsetAlignment,
             adapterLimits.minStorageBufferOffsetAlignment},
            {"maxVertexBuffers", deviceLimits.maxVertexBuffers, adapterLimits.maxVertexBuffers},
            {"maxBufferSize", deviceLimits.maxBufferSize, adapterLimits.maxBufferSize},
            {"maxVertexAttributes", deviceLimits.maxVertexAttributes,
             adapterLimits.maxVertexAttributes},
            {"maxVertexBufferArrayStride", deviceLimits.maxVertexBufferArrayStride,
             adapterLimits.maxVertexBufferArrayStride},
            {"maxInterStageShaderVariables", deviceLimits.maxInterStageShaderVariables,
             adapterLimits.maxInterStageShaderVariables},
            {"maxColorAttachments", deviceLimits.maxColorAttachments,
             adapterLimits.maxColorAttachments},
            {"maxColorAttachmentBytesPerSample", deviceLimits.maxColorAttachmentBytesPerSample,
             adapterLimits.maxColorAttachmentBytesPerSample},
            {"maxComputeWorkgroupStorageSize", deviceLimits.maxComputeWorkgroupStorageSize,
             adapterLimits.maxComputeWorkgroupStorageSize},
            {"maxComputeInvocationsPerWorkgroup", deviceLimits.maxComputeInvocationsPerWorkgroup,
             adapterLimits.maxComputeInvocationsPerWorkgroup},
            {"maxComputeWorkgroupSizeX", deviceLimits.maxComputeWorkgroupSizeX,
             adapterLimits.maxComputeWorkgroupSizeX},
            {"maxComputeWorkgroupSizeY", deviceLimits.maxComputeWorkgroupSizeY,
             adapterLimits.maxComputeWorkgroupSizeY},
            {"maxComputeWorkgroupSizeZ", deviceLimits.maxComputeWorkgroupSizeZ,
             adapterLimits.maxComputeWorkgroupSizeZ},
            {"maxComputeWorkgroupsPerDimension", deviceLimits.maxComputeWorkgroupsPerDimension,
             adapterLimits.maxComputeWorkgroupsPerDimension},
            {"maxImmediateSize", deviceLimits.maxImmediateSize, adapterLimits.maxImmediateSize},
        };

        for (const LimitField& field : limitFields) {
            t.expect(
                field.deviceValue == field.adapterValue,
                std::string("device.limits.") + field.name + " (" +
                    std::to_string(field.deviceValue) + ") === adapter.limits." + field.name +
                    " (" + std::to_string(field.adapterValue) + ")");
        }

        // Test that all the adapter features are on the device.
        for (WGPUFeatureName feature : requiredFeatures) {
            t.expect(
                wgpuDeviceHasFeature(ctx.device, feature) != 0,
                std::string("device has feature: ") + std::to_string(static_cast<int>(feature)));
        }
    });

} // namespace
