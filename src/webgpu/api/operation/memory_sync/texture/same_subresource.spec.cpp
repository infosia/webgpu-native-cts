// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/texture/same_subresource.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal copy-based slice: read = t2b-copy, write = b2t-copy, boundary = command-buffer.
// Deferred: other read/write ops, queue-op/pass/encoder boundaries, TextureSyncTestHelper matrix.

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,memory_sync,texture,same_subresource",
    "Memory synchronization tests for texture: read before write, write before read, and write after write to the same subresource.");

// 1x1 rgba8unorm texture; bytesPerRow must be >= 256 (copy alignment).
constexpr uint32_t kBytesPerRow = 256;
constexpr uint32_t kRowsPerImage = 1;

// texelValue1 = green {0, 255, 0, 255}, texelValue2 = red {255, 0, 0, 255}
const std::array<uint8_t, 4> kTexelValue1 = {0, 255, 0, 255};
const std::array<uint8_t, 4> kTexelValue2 = {255, 0, 0, 255};

// Create a 1x1 rgba8unorm texture with COPY_DST | COPY_SRC usage.
WGPUTexture createTexture(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

// Create a 256-byte COPY_SRC buffer with the 4 texel bytes at offset 0.
WGPUBuffer makeSrcBuffer(AllFeaturesMaxLimitsGpuTest& t, const std::array<uint8_t, 4>& texel) {
    // Allocate 256 bytes, zero-fill, then copy the 4 texel bytes at offset 0.
    std::array<uint8_t, kBytesPerRow> data = {};
    std::memcpy(data.data(), texel.data(), texel.size());
    return t.makeBufferWithContents(data.data(), data.size(), WGPUBufferUsage_CopySrc);
}

// Create a 256-byte zero-initialised readback buffer (COPY_DST).
WGPUBuffer makeReadbackBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = kBytesPerRow;
    desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    return t.createBufferTracked(desc);
}

// Encode a b2t copy: srcBuffer[0..4] -> texture mip-0 origin-0.
void b2tCopy(WGPUCommandEncoder encoder, WGPUBuffer srcBuffer, WGPUTexture texture) {
    WGPUTexelCopyBufferInfo src = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    src.buffer = srcBuffer;
    src.layout.offset = 0;
    src.layout.bytesPerRow = kBytesPerRow;
    src.layout.rowsPerImage = kRowsPerImage;

    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = texture;
    dst.mipLevel = 0;
    dst.origin = WGPUOrigin3D{0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
    wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &copySize);
}

// Encode a t2b copy: texture mip-0 origin-0 -> dstBuffer[0..4].
void t2bCopy(WGPUCommandEncoder encoder, WGPUTexture texture, WGPUBuffer dstBuffer) {
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = texture;
    src.mipLevel = 0;
    src.origin = WGPUOrigin3D{0, 0, 0};
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = dstBuffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = kBytesPerRow;
    dst.layout.rowsPerImage = kRowsPerImage;

    WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);
}

// Verify the first 4 bytes of the buffer match the expected texel.
void expectTexelEquals(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, const std::array<uint8_t, 4>& expected) {
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&expected](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                return std::string("readback buffer is too small");
            }
            for (size_t i = 0; i < 4; ++i) {
                if (actual[i] != expected[i]) {
                    std::ostringstream msg;
                    msg << "texel byte " << i << ": expected " << static_cast<int>(expected[i])
                        << ", got " << static_cast<int>(actual[i]);
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        4);
}

// rw: init texture = value1; cmdA = t2bCopy(tex -> readback); cmdB = b2tCopy(src2 -> tex);
//     submit [A, B]; expect readback == value1 (read saw the pre-write value).
CTS_TEST(g, "rw")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture texture = createTexture(t);
        WGPUBuffer srcBuffer2 = makeSrcBuffer(t, kTexelValue2);
        WGPUBuffer readback = makeReadbackBuffer(t);

        // Init texture to value1 via queueWriteTexture.
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = kBytesPerRow;
        layout.rowsPerImage = kRowsPerImage;
        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
        t.queueWriteTexture(texture, copySize, layout, kTexelValue1.data(), kTexelValue1.size());

        // cmdA: read texture -> readback
        WGPUCommandEncoder encA = t.createCommandEncoderTracked();
        t2bCopy(encA, texture, readback);
        WGPUCommandBuffer cmdA = t.finishTracked(encA);

        // cmdB: write src2 -> texture
        WGPUCommandEncoder encB = t.createCommandEncoderTracked();
        b2tCopy(encB, srcBuffer2, texture);
        WGPUCommandBuffer cmdB = t.finishTracked(encB);

        // Command-buffer boundary: both in one submit, A before B.
        std::array<WGPUCommandBuffer, 2> commandBuffers = {{cmdA, cmdB}};
        wgpuQueueSubmit(t.queue(), commandBuffers.size(), commandBuffers.data());

        // The read (cmdA) happens before the write (cmdB), so readback == value1.
        expectTexelEquals(t, readback, kTexelValue1);
    });

// wr: init texture = value1; cmdA = b2tCopy(src2 -> tex); cmdB = t2bCopy(tex -> readback);
//     submit [A, B]; expect readback == value2 (read saw the written value).
CTS_TEST(g, "wr")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture texture = createTexture(t);
        WGPUBuffer srcBuffer2 = makeSrcBuffer(t, kTexelValue2);
        WGPUBuffer readback = makeReadbackBuffer(t);

        // Init texture to value1.
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = kBytesPerRow;
        layout.rowsPerImage = kRowsPerImage;
        WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
        t.queueWriteTexture(texture, copySize, layout, kTexelValue1.data(), kTexelValue1.size());

        // cmdA: write src2 -> texture
        WGPUCommandEncoder encA = t.createCommandEncoderTracked();
        b2tCopy(encA, srcBuffer2, texture);
        WGPUCommandBuffer cmdA = t.finishTracked(encA);

        // cmdB: read texture -> readback
        WGPUCommandEncoder encB = t.createCommandEncoderTracked();
        t2bCopy(encB, texture, readback);
        WGPUCommandBuffer cmdB = t.finishTracked(encB);

        // Command-buffer boundary: both in one submit, A before B.
        std::array<WGPUCommandBuffer, 2> commandBuffers = {{cmdA, cmdB}};
        wgpuQueueSubmit(t.queue(), commandBuffers.size(), commandBuffers.data());

        // The write (cmdA) happens before the read (cmdB), so readback == value2.
        expectTexelEquals(t, readback, kTexelValue2);
    });

// ww: cmdA = b2tCopy(src1 -> tex); cmdB = b2tCopy(src2 -> tex); submit [A, B];
//     then t2bCopy(tex -> readback) in its own submit; expect readback == value2 (second write wins).
CTS_TEST(g, "ww")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTexture texture = createTexture(t);
        WGPUBuffer srcBuffer1 = makeSrcBuffer(t, kTexelValue1);
        WGPUBuffer srcBuffer2 = makeSrcBuffer(t, kTexelValue2);
        WGPUBuffer readback = makeReadbackBuffer(t);

        // cmdA: write src1 -> texture
        WGPUCommandEncoder encA = t.createCommandEncoderTracked();
        b2tCopy(encA, srcBuffer1, texture);
        WGPUCommandBuffer cmdA = t.finishTracked(encA);

        // cmdB: write src2 -> texture (overwrites value1)
        WGPUCommandEncoder encB = t.createCommandEncoderTracked();
        b2tCopy(encB, srcBuffer2, texture);
        WGPUCommandBuffer cmdB = t.finishTracked(encB);

        // Command-buffer boundary: both in one submit, A before B.
        std::array<WGPUCommandBuffer, 2> commandBuffers = {{cmdA, cmdB}};
        wgpuQueueSubmit(t.queue(), commandBuffers.size(), commandBuffers.data());

        // Readback in its own separate submit.
        WGPUCommandEncoder encR = t.createCommandEncoderTracked();
        t2bCopy(encR, texture, readback);
        WGPUCommandBuffer cmdR = t.finishTracked(encR);
        wgpuQueueSubmit(t.queue(), 1, &cmdR);

        // The second write (cmdB) wins, so readback == value2.
        expectTexelEquals(t, readback, kTexelValue2);
    });

} // namespace
