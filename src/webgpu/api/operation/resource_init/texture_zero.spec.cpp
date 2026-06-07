// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/texture_zero.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal port of uninitialized_texture_is_zero (rgba8unorm/r32uint, CopyToBuffer); the generic check_texture machinery + buffer.spec deferred.

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,resource_init,texture_zero",
    "Uninitialized texture subresources read back as zero (WebGPU lazy zero-init guarantee).");

constexpr uint32_t kSize = 4;
constexpr uint32_t kBytesPerTexel = 4; // both rgba8unorm and r32uint are 4 bytes/texel
constexpr uint32_t kBytesPerRow = 256; // align(4*4, 256) = 256

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUTextureFormat toTextureFormat(std::string_view name) {
    if (name == "rgba8unorm") { return WGPUTextureFormat_RGBA8Unorm; }
    if (name == "r32uint")    { return WGPUTextureFormat_R32Uint; }
    std::abort();
}

// Create a 4x4 2D texture with the given format.
// Usage: CopySrc | RenderAttachment (renderable, but never written — stays uninitialized).
WGPUTexture createUninitializedTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kSize, kSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// Copy the 4x4 texture to a readback buffer and verify all bytes are zero.
void verifyTextureIsZero(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    // Buffer must hold kSize rows; bytesPerRow = 256, so total = 256 * 4 = 1024 bytes.
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(kBytesPerRow) * kSize, kBufferCopyAlignment);

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = readback;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = kBytesPerRow;
    destination.layout.rowsPerImage = kSize;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUExtent3D copySize = WGPUExtent3D{kSize, kSize, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submit(t, encoder);

    // Check all 4x4 texels: for each (col, row), all 4 bytes at row*256 + col*4 must be 0.
    t.expectGPUBufferValuesPassCheck(
        readback,
        [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t row = 0; row < kSize; ++row) {
                for (uint32_t col = 0; col < kSize; ++col) {
                    const uint64_t offset =
                        static_cast<uint64_t>(row) * kBytesPerRow
                        + static_cast<uint64_t>(col) * kBytesPerTexel;
                    if (offset + kBytesPerTexel > len) {
                        std::ostringstream msg;
                        msg << "texel offset out of range at (" << col << ", " << row
                            << "): " << offset;
                        return msg.str();
                    }
                    for (uint32_t byte = 0; byte < kBytesPerTexel; ++byte) {
                        if (actual[offset + byte] != 0) {
                            std::ostringstream msg;
                            msg << "uninitialized texture byte is non-zero at ("
                                << col << ", " << row << ") byte " << byte
                                << ": got " << static_cast<int>(actual[offset + byte])
                                << ", expected 0";
                            return msg.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void runUninitializedTextureIsZero(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string formatName = t.param<std::string>("format");
    WGPUTextureFormat format = toTextureFormat(formatName);
    WGPUTexture texture = createUninitializedTexture(t, format);
    verifyTextureIsZero(t, texture);
}

CTS_TEST(g, "uninitialized_texture_is_zero")
    .params([](ParamsBuilder u) {
        return u.combine("format", {"rgba8unorm", "r32uint"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runUninitializedTextureIsZero(t);
    });

} // namespace
