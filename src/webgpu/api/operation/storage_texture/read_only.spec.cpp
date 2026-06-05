// Ported from gpuweb/cts src/webgpu/api/operation/storage_texture/read_only.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,storage_texture,read_only",
    "Read-only storage texture operation tests.");

constexpr uint32_t kWidth = 16;
constexpr uint32_t kHeight = 8;
constexpr uint32_t kTexelCount = kWidth * kHeight;
constexpr uint32_t kComponentSize = 4;
constexpr uint32_t kComponentsPerTexel = 4;
constexpr uint32_t kBytesPerRow = kWidth * kComponentSize;
constexpr uint64_t kOutputBufferSize = kTexelCount * kComponentsPerTexel * kComponentSize;

struct FormatInfo {
    WGPUTextureFormat textureFormat = WGPUTextureFormat_Undefined;
    std::string wgslFormat;
    std::string outputType;
};

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

FormatInfo formatInfo(std::string_view format) {
    if (format == "r32uint") {
        return FormatInfo{WGPUTextureFormat_R32Uint, "r32uint", "vec4u"};
    }
    if (format == "r32sint") {
        return FormatInfo{WGPUTextureFormat_R32Sint, "r32sint", "vec4i"};
    }
    if (format == "r32float") {
        return FormatInfo{WGPUTextureFormat_R32Float, "r32float", "vec4f"};
    }
    std::abort();
}

std::vector<uint8_t> inputTextureData(std::string_view format) {
    std::vector<uint8_t> bytes(kTexelCount * kComponentSize);
    for (uint32_t i = 0; i < kTexelCount; ++i) {
        if (format == "r32uint") {
            const uint32_t value = (4 * i + 1) % 256;
            std::memcpy(bytes.data() + i * kComponentSize, &value, sizeof(value));
        } else if (format == "r32sint") {
            const int32_t value = (i & 1 ? 1 : -1) * static_cast<int32_t>(4 * i + 1);
            std::memcpy(bytes.data() + i * kComponentSize, &value, sizeof(value));
        } else if (format == "r32float") {
            const float value = static_cast<float>(4 * i + 1) / 10.0f;
            std::memcpy(bytes.data() + i * kComponentSize, &value, sizeof(value));
        } else {
            std::abort();
        }
    }
    return bytes;
}

std::vector<uint8_t> expectedOutputData(std::string_view format) {
    std::vector<uint8_t> bytes(static_cast<size_t>(kOutputBufferSize));
    for (uint32_t i = 0; i < kTexelCount; ++i) {
        const size_t base = static_cast<size_t>(i) * kComponentsPerTexel * kComponentSize;
        if (format == "r32uint") {
            const std::array<uint32_t, 4> value = {{(4 * i + 1) % 256, 0, 0, 1}};
            std::memcpy(bytes.data() + base, value.data(), value.size() * sizeof(uint32_t));
        } else if (format == "r32sint") {
            const int32_t texelValue = (i & 1 ? 1 : -1) * static_cast<int32_t>(4 * i + 1);
            const std::array<int32_t, 4> value = {{texelValue, 0, 0, 1}};
            std::memcpy(bytes.data() + base, value.data(), value.size() * sizeof(int32_t));
        } else if (format == "r32float") {
            const float texelValue = static_cast<float>(4 * i + 1) / 10.0f;
            const std::array<float, 4> value = {{texelValue, 0.0f, 0.0f, 1.0f}};
            std::memcpy(bytes.data() + base, value.data(), value.size() * sizeof(float));
        } else {
            std::abort();
        }
    }
    return bytes;
}

std::string computeShader(const FormatInfo& info) {
    return R"(
@group(0) @binding(0) var readOnlyTexture: texture_storage_2d<)" + info.wgslFormat + R"(, read>;
@group(0) @binding(1) var<storage, read_write> outputBuffer : array<)" + info.outputType + R"(>;

@compute @workgroup_size(16, 8, 1)
fn main(@builtin(local_invocation_id) invocationID: vec3u,
        @builtin(local_invocation_index) invocationIndex: u32) {
  let initialValue = textureLoad(readOnlyTexture, vec2u(invocationID.x, invocationID.y));
  outputBuffer[invocationIndex] = initialValue;
}
)";
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

WGPUBuffer createOutputBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = kOutputBufferSize;
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

void uploadTextureData(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const std::vector<uint8_t>& data) {
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = kBytesPerRow;
    layout.rowsPerImage = kHeight;
    t.queueWriteTexture(texture, WGPUExtent3D{kWidth, kHeight, 1}, layout, data.data(), data.size());
}

WGPUBindGroupLayout createBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
    entries[0].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    entries[0].storageTexture.format = format;
    entries[0].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[1].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
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

WGPUBindGroup createBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUTextureView textureView,
    WGPUBuffer outputBuffer) {
    std::array<WGPUBindGroupEntry, 2> entries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].textureView = textureView;
    entries[1].binding = 1;
    entries[1].buffer = outputBuffer;
    entries[1].offset = 0;
    entries[1].size = kOutputBufferSize;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void runBasic(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string formatParam = t.param<std::string>("format");
    t.expect(t.param<std::string>("shaderStage") == "compute", "T37 ports compute only");
    t.expect(t.param<std::string>("dimension") == "2d", "T37 ports 2d only");
    t.expect(t.param<int64_t>("depthOrArrayLayers") == 1, "T37 ports one layer only");

    const FormatInfo info = formatInfo(formatParam);
    WGPUTexture texture = createStorageTexture(t, info.textureFormat);
    uploadTextureData(t, texture, inputTextureData(formatParam));
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView textureView = t.createViewTracked(texture, viewDesc);
    WGPUBuffer outputBuffer = createOutputBuffer(t);

    WGPUBindGroupLayout bindGroupLayout = createBindGroupLayout(t, info.textureFormat);
    WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
    WGPUComputePipeline pipeline = createPipeline(t, pipelineLayout, info);
    WGPUBindGroup bindGroup = createBindGroup(t, bindGroupLayout, textureView, outputBuffer);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    submit(t, encoder);

    const std::vector<uint8_t> expected = expectedOutputData(formatParam);
    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size());
}

CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("format", {Value("r32uint"), Value("r32sint"), Value("r32float")})
            .combine("shaderStage", {Value("compute")})
            .combine("dimension", {Value("2d")})
            .combine("depthOrArrayLayers", {1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBasic(t);
    });

} // namespace
