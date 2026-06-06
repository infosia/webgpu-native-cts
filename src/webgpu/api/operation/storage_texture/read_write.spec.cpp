// Ported from gpuweb/cts src/webgpu/api/operation/storage_texture/read_write.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,storage_texture,read_write",
    "Read-write storage texture operation tests.");

constexpr uint32_t kWidth = 16;
constexpr uint32_t kHeight = 8;
constexpr uint32_t kTexelCount = kWidth * kHeight;
constexpr uint32_t kComponentSize = 4;
constexpr uint32_t kBytesPerRow = kWidth * kComponentSize;

struct FormatInfo {
    WGPUTextureFormat textureFormat = WGPUTextureFormat_Undefined;
    std::string wgslFormat;
};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

FormatInfo formatInfo(std::string_view format) {
    if (format == "r32uint") {
        return FormatInfo{WGPUTextureFormat_R32Uint, "r32uint"};
    }
    if (format == "r32sint") {
        return FormatInfo{WGPUTextureFormat_R32Sint, "r32sint"};
    }
    if (format == "r32float") {
        return FormatInfo{WGPUTextureFormat_R32Float, "r32float"};
    }
    std::abort();
}

void writeTexelBytes(std::string_view format, uint32_t texelIndex, uint8_t* dst) {
    const uint32_t baseValue = 2 * texelIndex + 1;
    if (format == "r32uint") {
        const uint32_t value = baseValue % 256;
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    if (format == "r32sint") {
        const int32_t value = (texelIndex & 1 ? 1 : -1) * static_cast<int32_t>(baseValue);
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    if (format == "r32float") {
        const float value = static_cast<float>(baseValue) / 10.0f;
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    std::abort();
}

std::vector<uint8_t> inputTextureData(std::string_view format) {
    std::vector<uint8_t> bytes(kTexelCount * kComponentSize);
    for (uint32_t i = 0; i < kTexelCount; ++i) {
        writeTexelBytes(format, i, bytes.data() + static_cast<size_t>(i) * kComponentSize);
    }
    return bytes;
}

std::string computeShader(const FormatInfo& info) {
    return R"(
@group(0) @binding(0) var rwTexture: texture_storage_2d<)" + info.wgslFormat + R"(, read_write>;

@compute @workgroup_size(16, 8, 1)
fn main(@builtin(local_invocation_id) invocationID: vec3u) {
  let dimension = textureDimensions(rwTexture);
  let initialValue = textureLoad(rwTexture,
      vec2u(dimension.x - 1u - invocationID.x, dimension.y - 1u - invocationID.y));
  textureBarrier();
  textureStore(rwTexture, invocationID.xy, initialValue);
}
)";
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createStorageTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kWidth, kHeight, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return t.createTextureTracked(desc);
}

void uploadTextureData(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const std::vector<uint8_t>& data) {
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = kBytesPerRow;
    layout.rowsPerImage = kHeight;
    t.queueWriteTexture(texture, WGPUExtent3D{kWidth, kHeight, 1}, layout, data.data(), data.size());
}

WGPUBindGroupLayout createBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Compute;
    entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
    entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    entry.storageTexture.format = format;
    entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout, const FormatInfo& info) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(computeShader(info));
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = stringView("main");
    return t.createComputePipelineTracked(desc);
}

WGPUBindGroup createBindGroup(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout layout, WGPUTextureView textureView) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = textureView;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectMirroredTextureData(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, std::string_view format) {
    const uint32_t readbackBytesPerRow = static_cast<uint32_t>(alignTo(kWidth * kComponentSize, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(readbackBytesPerRow) * (kHeight - 1) + static_cast<uint64_t>(kWidth) * kComponentSize,
        kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, readbackBytesPerRow, WGPUExtent3D{kWidth, kHeight, 1});
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            std::array<uint8_t, kComponentSize> expected = {};
            for (uint32_t y = 0; y < kHeight; ++y) {
                for (uint32_t x = 0; x < kWidth; ++x) {
                    const uint64_t offset = static_cast<uint64_t>(y) * readbackBytesPerRow
                        + static_cast<uint64_t>(x) * kComponentSize;
                    if (offset + kComponentSize > len) {
                        std::ostringstream message;
                        message << "storage texture readback offset out of range: " << offset;
                        return message.str();
                    }
                    const uint32_t expectedIndex = (kHeight - 1 - y) * kWidth + (kWidth - 1 - x);
                    writeTexelBytes(format, expectedIndex, expected.data());
                    for (uint32_t byte = 0; byte < kComponentSize; ++byte) {
                        if (actual[offset + byte] != expected[byte]) {
                            std::ostringstream message;
                            message << "storage texture mismatch at (" << x << ", " << y << ") byte "
                                    << byte << ": expected " << static_cast<int>(expected[byte])
                                    << ", got " << static_cast<int>(actual[offset + byte]);
                            return message.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void runBasic(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string formatParam = t.param<std::string>("format");
    t.expect(t.param<std::string>("shaderStage") == "compute", "T38 ports compute only");
    t.expect(t.param<std::string>("textureDimension") == "2d", "T38 ports 2d only");
    t.expect(t.param<int64_t>("depthOrArrayLayers") == 1, "T38 ports one layer only");

    const FormatInfo info = formatInfo(formatParam);
    WGPUTexture texture = createStorageTexture(t, info.textureFormat);
    uploadTextureData(t, texture, inputTextureData(formatParam));
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView textureView = t.createViewTracked(texture, viewDesc);

    WGPUBindGroupLayout bindGroupLayout = createBindGroupLayout(t, info.textureFormat);
    WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
    WGPUComputePipeline pipeline = createPipeline(t, pipelineLayout, info);
    WGPUBindGroup bindGroup = createBindGroup(t, bindGroupLayout, textureView);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    submit(t, encoder);

    expectMirroredTextureData(t, texture, formatParam);
}

CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("format", {Value("r32uint"), Value("r32sint"), Value("r32float")})
            .combine("shaderStage", {Value("compute")})
            .combine("textureDimension", {Value("2d")})
            .combine("depthOrArrayLayers", {1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBasic(t);
    });

} // namespace
