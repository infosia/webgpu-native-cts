// Ported from gpuweb/cts src/webgpu/api/validation/texture/bgra8unorm_storage.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Partial port: tests 3 and 4 (canvas context configure) are .unimplemented() — GPUCanvasContext is a Web API with no native equivalent.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,texture,bgra8unorm_storage",
    "Tests for capabilities added by bgra8unorm-storage flag.");

// Test that it is valid to create bgra8unorm texture with STORAGE usage iff the feature
// bgra8unorm-storage is enabled. Note, the createTexture test suite covers the validation cases
// where this feature is not enabled, which are skipped here.
CTS_TEST(g, "create_texture")
    .desc(R"(
Test that it is valid to create bgra8unorm texture with STORAGE usage iff the feature
bgra8unorm-storage is enabled. Note, the createTexture test suite covers the validation cases where
this feature is not enabled, which are skipped here.
)")
    .fn([](GpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_BGRA8UnormStorage) == 0) {
            t.skip("bgra8unorm-storage feature is not available on this device");
        }

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = 1;
        desc.size.height = 1;
        desc.size.depthOrArrayLayers = 1;
        desc.format = WGPUTextureFormat_BGRA8Unorm;
        desc.usage = WGPUTextureUsage_StorageBinding;
        t.createTextureTracked(desc);
    });

// Test that it is valid to create GPUBindGroupLayout that uses bgra8unorm as storage texture
// format iff the feature bgra8unorm-storage is enabled. Note, the createBindGroupLayout test
// suite covers the validation cases where this feature is not enabled, which are skipped here.
CTS_TEST(g, "create_bind_group_layout")
    .desc(R"(
Test that it is valid to create GPUBindGroupLayout that uses bgra8unorm as storage texture format
iff the feature bgra8unorm-storage is enabled. Note, the createBindGroupLayout test suite covers the
validation cases where this feature is not enabled, which are skipped here.
)")
    .fn([](GpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_BGRA8UnormStorage) == 0) {
            t.skip("bgra8unorm-storage feature is not available on this device");
        }

        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entry.storageTexture.format = WGPUTextureFormat_BGRA8Unorm;

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        t.createBindGroupLayoutTracked(bglDesc);
    });

// Upstream test 3: configure_storage_usage_on_canvas_context_without_bgra8unorm_storage
// GPUCanvasContext and canvas APIs have no native C equivalent; cannot be ported faithfully.
CTS_TEST(g, "configure_storage_usage_on_canvas_context_without_bgra8unorm_storage")
    .desc(R"(
Test that it is invalid to configure a GPUCanvasContext to 'GPUStorageBinding' usage with
'bgra8unorm' format on a GPUDevice with 'bgra8unorm-storage' disabled.
)")
    .unimplemented("GPUCanvasContext is a Web API with no native WebGPU C equivalent");

// Upstream test 4: configure_storage_usage_on_canvas_context_with_bgra8unorm_storage
// GPUCanvasContext and canvas APIs have no native C equivalent; cannot be ported faithfully.
CTS_TEST(g, "configure_storage_usage_on_canvas_context_with_bgra8unorm_storage")
    .desc(R"(
Test that it is valid to configure a GPUCanvasContext with GPUStorageBinding usage and a GPUDevice
with 'bgra8unorm-storage' enabled.
)")
    .unimplemented("GPUCanvasContext is a Web API with no native WebGPU C equivalent");

} // namespace
