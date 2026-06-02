// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/basic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,operation,command_buffer,basic",
    "Basic command buffer operation tests.");

void submitCommandBuffer(GpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUBuffer createCopyBuffer(GpuTest& t, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

WGPUTexture createCopyTexture(GpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Uint;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return t.createTextureTracked(desc);
}

CTS_TEST(g, "empty")
    .fn([](GpuTest& t) {
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        submitCommandBuffer(t, encoder);
    });

CTS_TEST(g, "b2t2b")
    .fn([](GpuTest& t) {
        const uint32_t data = 0x01020304;
        WGPUBuffer src = t.makeBufferWithContents(&data, sizeof(data), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        WGPUBuffer dst = createCopyBuffer(t, sizeof(data));
        WGPUTexture mid = createCopyTexture(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        t.copyBufferToTexture(encoder, src, 256, mid, WGPUExtent3D{1, 1, 1});
        t.copyTextureToBuffer(encoder, mid, dst, 256, WGPUExtent3D{1, 1, 1});
        submitCommandBuffer(t, encoder);

        t.expectGPUBufferValuesEqual(dst, &data, sizeof(data));
    });

CTS_TEST(g, "b2t2t2b")
    .fn([](GpuTest& t) {
        const uint32_t data = 0x01020304;
        WGPUBuffer src = t.makeBufferWithContents(&data, sizeof(data), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        WGPUBuffer dst = createCopyBuffer(t, sizeof(data));
        WGPUTexture mid1 = createCopyTexture(t);
        WGPUTexture mid2 = createCopyTexture(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        t.copyBufferToTexture(encoder, src, 256, mid1, WGPUExtent3D{1, 1, 1});
        t.copyTextureToTexture(encoder, mid1, mid2, WGPUExtent3D{1, 1, 1});
        t.copyTextureToBuffer(encoder, mid2, dst, 256, WGPUExtent3D{1, 1, 1});
        submitCommandBuffer(t, encoder);

        t.expectGPUBufferValuesEqual(dst, &data, sizeof(data));
    });

} // namespace
