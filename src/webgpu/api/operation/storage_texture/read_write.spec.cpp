// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/storage_texture/read_write.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
//   - kPossibleStorageTextureFormats is kStorageTextureFormats + bgra8unorm +
//     kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly (format_info.ts order).
//   - This port never runs compatibility mode, so the `isCompatibility` limit skip is dropped and
//     kWidth is always 16.
//   - 'auto' layouts are written out as the equivalent explicit bind group layouts.
//   - Typed-array element stores/loads are emulated per element (Float16Array via
//     float16BitsFromNumber / float16BitsToNumber).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,storage_texture,read_write",
    "\nTests for the behavior of read-write storage textures.\n\n"
    "TODO:\n"
    "- Test resource usage transitions with read-write storage textures\n");

std::vector<Value> possibleStorageTextureFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kStorageTextureFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    values.emplace_back(std::string(textureFormatIdentifier(WGPUTextureFormat_BGRA8Unorm)));
    for (WGPUTextureFormat format : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

// Element types of the JS typed arrays from F.getTypedArrayBuffer.
enum class ElementType { U8, I8, U16, I16, U32, I32, F16, F32 };

ElementType typedArrayElementType(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8Uint:
            return ElementType::U8;
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RGBA8Sint:
            return ElementType::I8;
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_RGBA16Uint:
            return ElementType::U16;
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RGBA16Sint:
            return ElementType::I16;
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RGBA16Float:
            return ElementType::F16;
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RGBA32Sint:
            return ElementType::I32;
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_RGBA32Uint:
            return ElementType::U32;
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_RGBA32Float:
            return ElementType::F32;
        default:
            // Upstream `unreachable()`: only the read-write capable formats get here.
            std::abort();
    }
}

uint32_t elementSize(ElementType type) {
    switch (type) {
        case ElementType::U8:
        case ElementType::I8:
            return 1;
        case ElementType::U16:
        case ElementType::I16:
        case ElementType::F16:
            return 2;
        case ElementType::U32:
        case ElementType::I32:
        case ElementType::F32:
            return 4;
    }
    std::abort();
}

// Emulates `typedArray[index] = value`.
void storeElement(std::vector<uint8_t>& bytes, ElementType type, size_t index, double value) {
    uint8_t* dst = bytes.data() + index * elementSize(type);
    switch (type) {
        case ElementType::U8:
        case ElementType::I8: {
            const uint8_t v = static_cast<uint8_t>(static_cast<int64_t>(value));
            std::memcpy(dst, &v, sizeof(v));
            return;
        }
        case ElementType::U16:
        case ElementType::I16: {
            const uint16_t v = static_cast<uint16_t>(static_cast<int64_t>(value));
            std::memcpy(dst, &v, sizeof(v));
            return;
        }
        case ElementType::U32:
        case ElementType::I32: {
            const uint32_t v = static_cast<uint32_t>(static_cast<int64_t>(value));
            std::memcpy(dst, &v, sizeof(v));
            return;
        }
        case ElementType::F16: {
            const uint16_t v = float16BitsFromNumber(value);
            std::memcpy(dst, &v, sizeof(v));
            return;
        }
        case ElementType::F32: {
            const float v = static_cast<float>(value);
            std::memcpy(dst, &v, sizeof(v));
            return;
        }
    }
}

// Emulates `typedArray[index]`.
double loadElement(const std::vector<uint8_t>& bytes, ElementType type, size_t index) {
    const uint8_t* src = bytes.data() + index * elementSize(type);
    switch (type) {
        case ElementType::U8:
            return src[0];
        case ElementType::I8:
            return static_cast<int8_t>(src[0]);
        case ElementType::U16: {
            uint16_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return v;
        }
        case ElementType::I16: {
            int16_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return v;
        }
        case ElementType::U32: {
            uint32_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return v;
        }
        case ElementType::I32: {
            int32_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return v;
        }
        case ElementType::F16: {
            uint16_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return float16BitsToNumber(v);
        }
        case ElementType::F32: {
            float v = 0.0f;
            std::memcpy(&v, src, sizeof(v));
            return v;
        }
    }
    std::abort();
}

struct StorageTextureInfo {
    WGPUTexture texture = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depthOrArrayLayers = 0;
};

// F.getInitialData.
std::vector<uint8_t> getInitialData(const StorageTextureInfo& storageTexture) {
    const WGPUTextureFormat format = storageTexture.format;
    const uint32_t bytesPerBlock = getBlockInfoForTextureFormat(format).bytesPerBlock;
    const uint32_t width = storageTexture.width;
    const uint32_t height = storageTexture.height;
    const uint32_t depthOrArrayLayers = storageTexture.depthOrArrayLayers;
    std::vector<uint8_t> initialData(
        static_cast<size_t>(bytesPerBlock) * width * height * depthOrArrayLayers, 0);
    const ElementType type = typedArrayElementType(format);
    const auto set = [&](size_t index, double value) { storeElement(initialData, type, index, value); };

    for (uint32_t z = 0; z < depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t index = static_cast<size_t>(z) * width * height + static_cast<size_t>(y) * width + x;
                const int64_t baseValue = 2 * static_cast<int64_t>(index) + 1;
                const int64_t sign = (index & 1) != 0 ? 1 : -1;
                switch (format) {
                    case WGPUTextureFormat_R8Unorm:
                    case WGPUTextureFormat_R8Uint:
                        set(index, static_cast<double>(baseValue % 256));
                        break;
                    case WGPUTextureFormat_R8Sint:
                        set(index, static_cast<double>((baseValue % 256) - 128));
                        break;
                    case WGPUTextureFormat_RGBA8Unorm:
                    case WGPUTextureFormat_RGBA8Uint:
                        set(index * 4 + 0, static_cast<double>((baseValue * 1) % 256));
                        set(index * 4 + 1, static_cast<double>((baseValue * 2) % 256));
                        set(index * 4 + 2, static_cast<double>((baseValue * 3) % 256));
                        set(index * 4 + 3, 255);
                        break;
                    case WGPUTextureFormat_RGBA8Sint:
                        set(index * 4 + 0, static_cast<double>(((baseValue * 1) % 256) - 128));
                        set(index * 4 + 1, static_cast<double>(((baseValue * 2) % 256) - 128));
                        set(index * 4 + 2, static_cast<double>(((baseValue * 3) % 256) - 128));
                        set(index * 4 + 3, 127);
                        break;
                    case WGPUTextureFormat_R16Uint:
                        set(index, static_cast<double>(baseValue % 65536));
                        break;
                    case WGPUTextureFormat_R16Sint:
                        set(index, static_cast<double>((baseValue % 65536) - 32768));
                        break;
                    case WGPUTextureFormat_R16Float:
                        set(index, static_cast<double>(baseValue));
                        break;
                    case WGPUTextureFormat_RGBA16Uint:
                        set(index * 4 + 0, static_cast<double>((baseValue * 1) % 65536));
                        set(index * 4 + 1, static_cast<double>((baseValue * 2) % 65536));
                        set(index * 4 + 2, static_cast<double>((baseValue * 3) % 65536));
                        set(index * 4 + 3, 65535);
                        break;
                    case WGPUTextureFormat_RGBA16Sint:
                        set(index * 4 + 0, static_cast<double>(((baseValue * 1) % 65536) - 32768));
                        set(index * 4 + 1, static_cast<double>(((baseValue * 2) % 65536) - 32768));
                        set(index * 4 + 2, static_cast<double>(((baseValue * 3) % 65536) - 32768));
                        set(index * 4 + 3, 32767);
                        break;
                    case WGPUTextureFormat_RGBA16Float:
                        set(index * 4 + 0, static_cast<double>(baseValue * 1));
                        set(index * 4 + 1, static_cast<double>(baseValue * 2));
                        set(index * 4 + 2, static_cast<double>(baseValue * 3));
                        set(index * 4 + 3, 1.0);
                        break;
                    case WGPUTextureFormat_R32Sint:
                        set(index, static_cast<double>(sign * baseValue));
                        break;
                    case WGPUTextureFormat_R32Uint:
                        set(index, static_cast<double>(baseValue));
                        break;
                    case WGPUTextureFormat_R32Float:
                        set(index, static_cast<double>(baseValue) / 10.0);
                        break;
                    case WGPUTextureFormat_RGBA32Uint:
                        set(index * 4 + 0, static_cast<double>(baseValue * 1));
                        set(index * 4 + 1, static_cast<double>(baseValue * 2));
                        set(index * 4 + 2, static_cast<double>(baseValue * 3));
                        set(index * 4 + 3, static_cast<double>(baseValue));
                        break;
                    case WGPUTextureFormat_RGBA32Sint:
                        set(index * 4 + 0, static_cast<double>(sign * baseValue * 1));
                        set(index * 4 + 1, static_cast<double>(sign * baseValue * 2));
                        set(index * 4 + 2, static_cast<double>(sign * baseValue * 3));
                        set(index * 4 + 3, static_cast<double>(baseValue));
                        break;
                    case WGPUTextureFormat_RGBA32Float:
                        set(index * 4 + 0, static_cast<double>(baseValue * 1) / 10.0);
                        set(index * 4 + 1, static_cast<double>(baseValue * 2) / 10.0);
                        set(index * 4 + 2, static_cast<double>(baseValue * 3) / 10.0);
                        set(index * 4 + 3, 1.0);
                        break;
                    default:
                        break;
                }
            }
        }
    }
    return initialData;
}

struct FormatRange {
    double min;
    double max;
};

FormatRange getFormatRange(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8Uint:
            return {0, 255};
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RGBA8Sint:
            return {-128, 127};
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_RGBA16Uint:
            return {0, 65535};
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RGBA16Sint:
            return {-32768, 32767};
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_RGBA32Uint:
            return {0, 4294967295.0};
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RGBA32Sint:
            return {-2147483648.0, 2147483647.0};
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RGBA16Float:
            return {-65504, 65504};
        default:
            return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
}

// F.getExpectedData.
std::vector<uint8_t> getExpectedData(
    std::string_view shaderStage,
    const StorageTextureInfo& storageTexture,
    const std::vector<uint8_t>& initialData) {
    const WGPUTextureFormat format = storageTexture.format;
    const uint32_t bytesPerBlock = getBlockInfoForTextureFormat(format).bytesPerBlock;
    const uint32_t width = storageTexture.width;
    const uint32_t height = storageTexture.height;
    const uint32_t depthOrArrayLayers = storageTexture.depthOrArrayLayers;
    const uint32_t bytesPerRowAlignment = (bytesPerBlock * width + 255u) / 256u * 256u;
    const uint32_t itemsPerRow = bytesPerRowAlignment / bytesPerBlock;

    std::vector<uint8_t> expectedData(
        static_cast<size_t>(bytesPerRowAlignment) * (height * depthOrArrayLayers - 1)
            + static_cast<size_t>(bytesPerBlock) * width,
        0);
    const ElementType type = typedArrayElementType(format);
    const bool isRGBAChannel = textureFormatIdentifier(format).substr(0, 4) == "rgba";
    const FormatRange range = getFormatRange(format);
    const uint32_t channels = isRGBAChannel ? 4 : 1;

    for (uint32_t z = 0; z < depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t expectedIndex = static_cast<size_t>(z) * itemsPerRow * height
                    + static_cast<size_t>(y) * itemsPerRow + x;
                if (shaderStage == "compute") {
                    // In the compute shader we flip the texture along the diagonal.
                    const size_t initialIndex =
                        static_cast<size_t>(depthOrArrayLayers - 1 - z) * width * height
                        + static_cast<size_t>(height - 1 - y) * width + (width - 1 - x);
                    for (uint32_t c = 0; c < channels; ++c) {
                        storeElement(expectedData, type, expectedIndex * channels + c,
                                     loadElement(initialData, type, initialIndex * channels + c));
                    }
                } else {
                    // In the fragment shader we double the original texel value of the read-write
                    // storage texture.
                    const size_t initialIndex = static_cast<size_t>(z) * width * height
                        + static_cast<size_t>(y) * width + x;
                    for (uint32_t c = 0; c < channels; ++c) {
                        double value = loadElement(initialData, type, initialIndex * channels + c) * 2;
                        value = std::max(range.min, std::min(range.max, value));
                        storeElement(expectedData, type, expectedIndex * channels + c, value);
                    }
                }
            }
        }
    }
    return expectedData;
}

WGPUTextureViewDimension storageViewDimension(const StorageTextureInfo& rwTexture) {
    switch (rwTexture.dimension) {
        case WGPUTextureDimension_1D:
            return WGPUTextureViewDimension_1D;
        case WGPUTextureDimension_2D:
            return rwTexture.depthOrArrayLayers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
        case WGPUTextureDimension_3D:
            return WGPUTextureViewDimension_3D;
        default:
            std::abort();
    }
}

// The explicit form of the 'auto' layout of the pipelines below.
WGPUBindGroup createRWBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    const StorageTextureInfo& rwTexture,
    WGPUShaderStage visibility,
    WGPUPipelineLayout* outPipelineLayout) {
    WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0;
    layoutEntry.visibility = visibility;
    layoutEntry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    layoutEntry.storageTexture.format = rwTexture.format;
    layoutEntry.storageTexture.viewDimension = storageViewDimension(rwTexture);
    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    WGPUBindGroupLayout bindGroupLayout = t.createBindGroupLayoutTracked(layoutDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
    *outPipelineLayout = t.createPipelineLayoutTracked(pipelineLayoutDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = t.createViewTracked(rwTexture.texture, viewDesc);
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = bindGroupLayout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// F.recordCommandsToTransform.
void recordCommandsToTransform(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view shaderStage,
    WGPUCommandEncoder commandEncoder,
    const StorageTextureInfo& rwTexture) {
    std::string declaration;
    switch (rwTexture.dimension) {
        case WGPUTextureDimension_1D:
            declaration = "texture_storage_1d";
            break;
        case WGPUTextureDimension_2D:
            declaration = rwTexture.depthOrArrayLayers > 1 ? "texture_storage_2d_array" : "texture_storage_2d";
            break;
        default:
            declaration = "texture_storage_3d";
            break;
    }
    const std::string textureDeclaration = "\n    @group(0) @binding(0) var rwTexture: " + declaration + "<"
        + std::string(textureFormatIdentifier(rwTexture.format)) + ", read_write>;\n    ";

    if (shaderStage == "fragment") {
        const std::string vertexShader = R"(
        @vertex
        fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f {
            var pos = array(
              vec2f(-1.0, -1.0),
              vec2f(-1.0,  1.0),
              vec2f( 1.0, -1.0),
              vec2f(-1.0,  1.0),
              vec2f( 1.0, -1.0),
              vec2f( 1.0,  1.0));
            return vec4f(pos[VertexIndex], 0.0, 1.0);
        }
        )";
        std::string textureLoadStoreCoord;
        switch (rwTexture.dimension) {
            case WGPUTextureDimension_1D:
                textureLoadStoreCoord = "textureCoord.x";
                break;
            case WGPUTextureDimension_2D:
                textureLoadStoreCoord = rwTexture.depthOrArrayLayers > 1 ? "textureCoord, z" : "textureCoord";
                break;
            default:
                textureLoadStoreCoord = "vec3u(textureCoord, z)";
                break;
        }
        std::ostringstream fragmentShader;
        fragmentShader << "\n        " << textureDeclaration << "\n"
                       << "        @fragment\n"
                       << "        fn main(@builtin(position) fragCoord: vec4f) -> @location(0) vec4f {\n"
                       << "          let textureCoord = vec2u(fragCoord.xy);\n\n"
                       << "          for (var z = 0u; z < " << rwTexture.depthOrArrayLayers << "; z++) {\n"
                       << "            let initialValue = textureLoad(rwTexture, " << textureLoadStoreCoord << ");\n"
                       << "            let outputValue = initialValue * 2;\n"
                       << "            textureStore(rwTexture, " << textureLoadStoreCoord << ", outputValue);\n"
                       << "          }\n\n"
                       << "          return vec4f(0.0, 1.0, 0.0, 1.0);\n"
                       << "        }\n"
                       << "        ";

        WGPUPipelineLayout pipelineLayout = nullptr;
        WGPUBindGroup bindGroup = createRWBindGroup(t, rwTexture, WGPUShaderStage_Fragment, &pipelineLayout);

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = t.createShaderModuleTracked(fragmentShader.str());
        fragment.entryPoint = stringView("main");
        fragment.targetCount = 1;
        fragment.targets = &target;
        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = t.createShaderModuleTracked(vertexShader);
        pipelineDesc.vertex.entryPoint = stringView("main");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.fragment = &fragment;
        WGPURenderPipeline renderPipeline = t.createRenderPipelineTracked(pipelineDesc);

        WGPUTextureDescriptor placeholderDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        placeholderDesc.size = WGPUExtent3D{rwTexture.width, rwTexture.height, 1};
        placeholderDesc.usage = WGPUTextureUsage_RenderAttachment;
        placeholderDesc.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUTexture placeholderColorTexture = t.createTextureTracked(placeholderDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = t.createViewTracked(placeholderColorTexture, viewDesc);
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        colorAttachment.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder renderPassEncoder = t.beginRenderPassTracked(commandEncoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(renderPassEncoder, renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(renderPassEncoder, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPassEncoder);
    } else if (shaderStage == "compute") {
        std::string textureLoadCoord;
        std::string textureStoreCoord;
        switch (rwTexture.dimension) {
            case WGPUTextureDimension_1D:
                textureLoadCoord = "dimension - 1u - invocationID.x";
                textureStoreCoord = "invocationID.x";
                break;
            case WGPUTextureDimension_2D:
                textureLoadCoord = rwTexture.depthOrArrayLayers > 1
                    ? "vec2u(dimension.x - 1u - invocationID.x, dimension.y - 1u - invocationID.y),\n"
                      "                   textureNumLayers(rwTexture) - 1u - invocationID.z"
                    : "vec2u(dimension.x - 1u - invocationID.x, dimension.y - 1u - invocationID.y)";
                textureStoreCoord =
                    rwTexture.depthOrArrayLayers > 1 ? "invocationID.xy, invocationID.z" : "invocationID.xy";
                break;
            default:
                textureLoadCoord =
                    "\n              vec3u(dimension.x - 1u - invocationID.x, dimension.y - 1u - invocationID.y,\n"
                    "                    dimension.z - 1u - invocationID.z)";
                textureStoreCoord = "invocationID";
                break;
        }

        std::ostringstream computeShader;
        computeShader << "\n        " << textureDeclaration << "\n"
                      << "        @compute\n"
                      << "        @workgroup_size(" << rwTexture.width << ", " << rwTexture.height << ", "
                      << rwTexture.depthOrArrayLayers << ")\n"
                      << "        fn main(@builtin(local_invocation_id) invocationID: vec3u) {\n"
                      << "          let dimension = textureDimensions(rwTexture);\n\n"
                      << "          let initialValue = textureLoad(rwTexture, " << textureLoadCoord << ");\n"
                      << "          textureBarrier();\n\n"
                      << "          textureStore(rwTexture, " << textureStoreCoord << ", initialValue);\n"
                      << "        }";

        WGPUPipelineLayout pipelineLayout = nullptr;
        WGPUBindGroup bindGroup = createRWBindGroup(t, rwTexture, WGPUShaderStage_Compute, &pipelineLayout);
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = t.createShaderModuleTracked(computeShader.str());
        WGPUComputePipeline computePipeline = t.createComputePipelineTracked(pipelineDesc);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder computePassEncoder = t.beginComputePassTracked(commandEncoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(computePassEncoder, computePipeline);
        wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, 1, 1, 1);
        wgpuComputePassEncoderEnd(computePassEncoder);
    } else {
        std::abort();
    }
}

WGPUTextureDimension parseDimension(std::string_view dimension) {
    if (dimension == "1d") {
        return WGPUTextureDimension_1D;
    }
    if (dimension == "2d") {
        return WGPUTextureDimension_2D;
    }
    if (dimension == "3d") {
        return WGPUTextureDimension_3D;
    }
    std::abort();
}

CTS_TEST(g, "basic")
    .desc(
        "The basic functionality tests for read-write storage textures. In the test we read data from\n"
        "    the read-write storage texture, do transforms and write the data back to the read-write storage\n"
        "    texture. textureBarrier() is also called in the tests using compute pipelines.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormatValues())
            .combine("shaderStage", {Value("fragment"), Value("compute")})
            .combine("textureDimension", {Value("1d"), Value("2d"), Value("3d")})
            .combine("depthOrArrayLayers", {1, 2})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "textureDimension")) == "1d"
                         && valueAs<int64_t>(*findParam(p, "depthOrArrayLayers")) > 1);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string shaderStage = t.param<std::string>("shaderStage");
        const WGPUTextureDimension textureDimension = parseDimension(t.param<std::string>("textureDimension"));
        const uint32_t depthOrArrayLayers = static_cast<uint32_t>(t.param<int64_t>("depthOrArrayLayers"));
        t.skipIfTextureFormatNotSupported(format);
        if (!t.isTextureFormatUsableWithStorageAccessMode(format, WGPUStorageTextureAccess_ReadWrite)) {
            t.skip("Texture with " + std::string(textureFormatIdentifier(format))
                   + " is not usable as a storage texture with access read-write");
        }

        constexpr uint32_t kWidth = 16;
        const uint32_t height = textureDimension == WGPUTextureDimension_1D ? 1 : 8;
        const WGPUExtent3D textureSize = WGPUExtent3D{kWidth, height, depthOrArrayLayers};
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.format = format;
        textureDesc.dimension = textureDimension;
        textureDesc.size = textureSize;
        textureDesc.usage =
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding;
        StorageTextureInfo storageTexture;
        storageTexture.texture = t.createTextureTracked(textureDesc);
        storageTexture.format = format;
        storageTexture.dimension = textureDimension;
        storageTexture.width = kWidth;
        storageTexture.height = height;
        storageTexture.depthOrArrayLayers = depthOrArrayLayers;

        const uint32_t bytesPerBlock = getBlockInfoForColorTextureFormat(format).bytesPerBlock;
        const std::vector<uint8_t> initialData = getInitialData(storageTexture);
        WGPUTexelCopyBufferLayout writeLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        writeLayout.offset = 0;
        writeLayout.bytesPerRow = bytesPerBlock * kWidth;
        writeLayout.rowsPerImage = height;
        t.queueWriteTexture(storageTexture.texture, textureSize, writeLayout, initialData.data(), initialData.size());

        WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();

        recordCommandsToTransform(t, shaderStage, commandEncoder, storageTexture);

        const std::vector<uint8_t> expectedData = getExpectedData(shaderStage, storageTexture, initialData);
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = expectedData.size();
        bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer readbackBuffer = t.createBufferTracked(bufferDesc);
        const uint32_t bytesPerRow = (bytesPerBlock * kWidth + 255u) / 256u * 256u;

        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = storageTexture.texture;
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = readbackBuffer;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = height;
        wgpuCommandEncoderCopyTextureToBuffer(commandEncoder, &source, &destination, &textureSize);
        WGPUCommandBuffer commandBuffer = t.finishTracked(commandEncoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Upstream compares as Uint32Array / Int32Array / Uint8Array / Float32Array over the whole
        // buffer; the expected values are never NaN or -0, so element equality is byte equality
        // (row padding is zero in both the zero-initialized readback buffer and the expectation).
        t.expectGPUBufferValuesEqual(readbackBuffer, expectedData.data(), expectedData.size());
    });

} // namespace
