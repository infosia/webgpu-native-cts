// Ported from gpuweb/cts src/webgpu/api/validation/queue/writeTexture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Test group — upstream fixture is AllFeaturesMaxLimitsGPUTest.
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,writeTexture",
    "Tests writeTexture validation.");

// ---------------------------------------------------------------------------
// Helper: build a minimal WGPUTextureDescriptor for writeTexture tests.
// Format: bgra8unorm, 16x16, CopyDst | RenderAttachment.
// The sampleCount parameter lets sample_count test override it.
// ---------------------------------------------------------------------------
static WGPUTextureDescriptor makeTexDesc(uint32_t sampleCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size.width             = 16;
    desc.size.height            = 16;
    desc.size.depthOrArrayLayers = 1;
    desc.format                 = WGPUTextureFormat_BGRA8Unorm;
    desc.sampleCount            = sampleCount;
    desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;
    return desc;
}

// ---------------------------------------------------------------------------
// texture_state
// Test that the texture used for GPUQueue.writeTexture() must be valid.
// Tests calling writeTexture with {valid, invalid, destroyed} texture.
// ---------------------------------------------------------------------------
CTS_TEST(g, "texture_state")
    .desc(
        "Test that the texture used for GPUQueue.writeTexture() must be valid. Tests calling "
        "writeTexture with {valid, invalid, destroyed} texture.")
    .params([](ParamsBuilder u) {
        return u.combine("textureState", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state = parseResourceState(t.param<std::string>("textureState"));

        // The upstream vtu.createTextureWithState uses a default descriptor with
        // all common usages. We use a concrete descriptor that satisfies writeTexture
        // (CopyDst) and is renderable (needed for invalid-state creation path).
        WGPUTextureDescriptor desc = makeTexDesc(1);
        WGPUTexture texture = t.createTextureWithState(state, desc);

        std::vector<uint8_t> data(16, 0);
        WGPUExtent3D size = {1, 1, 1};

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow  = 256; // row pitch must be >= 256 or WGPU_COPY_STRIDE_UNDEFINED
        layout.rowsPerImage = 1;

        const bool isValid = (state == ResourceState::Valid);
        t.expectValidationError([&] {
            wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &size);
        }, !isValid);
    });

// ---------------------------------------------------------------------------
// usages
// Tests calling writeTexture with the texture missed COPY_DST usage.
//   - texture {with, without} COPY_DST usage
// ---------------------------------------------------------------------------
CTS_TEST(g, "usages")
    .desc(
        "Tests calling writeTexture with the texture missed COPY_DST usage. "
        "texture {with, without} COPY_DST usage.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"usage", static_cast<int64_t>(WGPUTextureUsage_CopyDst)},
                        {"_valid", true}},
            ParamRecord{{"usage", static_cast<int64_t>(WGPUTextureUsage_StorageBinding)},
                        {"_valid", false}},
            ParamRecord{{"usage", static_cast<int64_t>(
                             WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc)},
                        {"_valid", false}},
            ParamRecord{{"usage", static_cast<int64_t>(
                             WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst)},
                        {"_valid", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(t.param<int64_t>("usage"));
        const bool valid = t.param<bool>("_valid");

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width             = 16;
        desc.size.height            = 16;
        desc.size.depthOrArrayLayers = 1;
        desc.format                 = WGPUTextureFormat_RGBA8Unorm;
        desc.sampleCount            = 1;
        desc.usage                  = usage;
        WGPUTexture texture = t.createTextureTracked(desc);

        std::vector<uint8_t> data(16, 0);
        WGPUExtent3D size = {1, 1, 1};

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow  = 256;
        layout.rowsPerImage = 1;

        t.expectValidationError([&] {
            wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &size);
        }, !valid);
    });

// ---------------------------------------------------------------------------
// sample_count
// Test that a validation error is generated if sample count is not 1.
// ---------------------------------------------------------------------------
CTS_TEST(g, "sample_count")
    .desc(
        "Test that the texture sample count. Check that a validation error is generated if sample "
        "count is not 1.")
    .params([](ParamsBuilder u) {
        return u.combine("sampleCount", {Value(1), Value(4)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));

        WGPUTextureDescriptor desc = makeTexDesc(sampleCount);
        WGPUTexture texture = t.createTextureTracked(desc);

        std::vector<uint8_t> data(16, 0);
        WGPUExtent3D size = {1, 1, 1};

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow  = 256;
        layout.rowsPerImage = 1;

        const bool isValid = (sampleCount == 1);
        t.expectValidationError([&] {
            wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &size);
        }, !isValid);
    });

// ---------------------------------------------------------------------------
// texture,device_mismatch
// Tests writeTexture cannot be called with a texture created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "texture,device_mismatch")
    .desc("Tests writeTexture cannot be called with a texture created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width             = 16;
        desc.size.height            = 16;
        desc.size.depthOrArrayLayers = 1;
        desc.format                 = WGPUTextureFormat_BGRA8Unorm;
        desc.sampleCount            = 1;
        desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;

        WGPUTexture texture = wgpuDeviceCreateTexture(sourceDevice, &desc);

        std::vector<uint8_t> data(16, 0);
        WGPUExtent3D size = {1, 1, 1};

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow  = 256;
        layout.rowsPerImage = 1;

        t.expectValidationError([&] {
            wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &size);
        }, mismatched);

        if (texture != nullptr) { wgpuTextureRelease(texture); }
    });

} // namespace
