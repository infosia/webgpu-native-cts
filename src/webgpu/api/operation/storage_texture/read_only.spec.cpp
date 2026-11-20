// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/storage_texture/read_only.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
//   - kPossibleStorageTextureFormats is kStorageTextureFormats + bgra8unorm +
//     kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly (format_info.ts order).
//   - The `isCompatibility` limit skips are dropped: this port never runs compatibility mode.
//   - 'auto' layouts are written out as the equivalent explicit bind group layouts.
//   - Typed-array element stores (Uint8Array, Int16Array, Float16Array, ...) are emulated per
//     element; Float16Array stores round to nearest even (float16BitsFromNumber).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
    "api,operation,storage_texture,read_only",
    "\nTests for the behavior of read-only storage textures.\n\n"
    "TODO:\n"
    "- Test mipmap level > 0\n"
    "- Test resource usage transitions with read-only storage textures\n");

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

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

// Element types of the JS typed arrays upstream uses for texel and output data.
enum class ElementType { U8, I8, U16, I16, U32, I32, F16, F32 };

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

// Emulates `typedArray[index] = value` (integer stores wrap modulo 2^bits like ToUint8/ToInt8/...).
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

enum class FormatType { Uint, Sint, Float };

// getTextureFormatType for the storage formats ('float' and 'unfilterable-float' fold together).
FormatType formatType(WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    if (id.find("uint") != std::string_view::npos) {
        return FormatType::Uint;
    }
    if (id.find("sint") != std::string_view::npos) {
        return FormatType::Sint;
    }
    return FormatType::Float;
}

uint32_t getComponentCountForFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_R32Uint:
            return 1;
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RG32Uint:
            return 2;
        case WGPUTextureFormat_RG11B10Ufloat:
            return 3;
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
            return 4;
        default:
            std::abort();
    }
}

// getTypedArrayBufferViewForTexelData.
ElementType texelDataElementType(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
            return ElementType::U32;
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Uint:
            return ElementType::U8;
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_RG16Uint:
            return ElementType::U16;
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_RG11B10Ufloat:
            return ElementType::I32;
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Sint:
            return ElementType::I8;
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Snorm:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RG16Sint:
            return ElementType::I16;
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RGBA32Float:
            return ElementType::F32;
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RGBA16Float:
            return ElementType::F16;
        default:
            std::abort();
    }
}

// getTypedArrayBufferForOutputBufferData.
ElementType outputElementType(WGPUTextureFormat format) {
    switch (formatType(format)) {
        case FormatType::Uint:
            return ElementType::U32;
        case FormatType::Sint:
            return ElementType::I32;
        case FormatType::Float:
            return ElementType::F32;
    }
    std::abort();
}

// getOutputBufferWGSLType.
std::string outputBufferWGSLType(WGPUTextureFormat format) {
    switch (formatType(format)) {
        case FormatType::Uint:
            return "vec4u";
        case FormatType::Sint:
            return "vec4i";
        case FormatType::Float:
            return "vec4f";
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

// F.initTextureAndGetExpectedOutputBufferData: writes the texel data into the storage texture and
// returns the expected output buffer contents.
std::vector<uint8_t> initTextureAndGetExpectedOutputBufferData(
    AllFeaturesMaxLimitsGpuTest& t,
    const StorageTextureInfo& storageTexture) {
    const WGPUTextureFormat format = storageTexture.format;
    const uint32_t bytesPerBlock = getBlockInfoForColorTextureFormat(format).bytesPerBlock;
    const uint32_t width = storageTexture.width;
    const uint32_t height = storageTexture.height;
    const uint32_t depthOrArrayLayers = storageTexture.depthOrArrayLayers;
    const size_t texelCount = static_cast<size_t>(width) * height * depthOrArrayLayers;

    std::vector<uint8_t> texelData(bytesPerBlock * texelCount, 0);
    const ElementType texelType = texelDataElementType(format);
    const uint32_t componentCount = getComponentCountForFormat(format);
    std::vector<uint8_t> outputBufferData(4 * 4 * texelCount, 0);
    const ElementType outputType = outputElementType(format);

    const auto setData = [&](double texelValue, double outputValue, size_t texelDataIndex, uint32_t component,
                             uint32_t outputComponent) {
        storeElement(texelData, texelType, texelDataIndex * componentCount + component, texelValue);
        storeElement(outputBufferData, outputType, texelDataIndex * 4 + outputComponent, outputValue);
    };

    for (uint32_t z = 0; z < depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t texelDataIndex = static_cast<size_t>(z) * width * height
                    + static_cast<size_t>(y) * width + x;
                const int64_t index = static_cast<int64_t>(texelDataIndex);
                storeElement(outputBufferData, outputType, 4 * texelDataIndex, 0);
                storeElement(outputBufferData, outputType, 4 * texelDataIndex + 1, 0);
                storeElement(outputBufferData, outputType, 4 * texelDataIndex + 2, 0);
                storeElement(outputBufferData, outputType, 4 * texelDataIndex + 3, 1);
                // Packed formats store multiple color components within a single 32-bit integer.
                if (format == WGPUTextureFormat_RGB10A2Unorm || format == WGPUTextureFormat_RGB10A2Uint) {
                    const uint32_t texelValue = static_cast<uint32_t>(4 * index + 1);
                    const uint32_t r = texelValue % 1024;
                    const uint32_t gc = (texelValue * 2) % 1024;
                    const uint32_t b = (texelValue * 3) % 1024;
                    const uint32_t a = 3;
                    const uint32_t packedValue = (a << 30) | (b << 20) | (gc << 10) | r;
                    storeElement(texelData, texelType, texelDataIndex, packedValue);
                    if (format == WGPUTextureFormat_RGB10A2Unorm) {
                        storeElement(outputBufferData, outputType, texelDataIndex * 4, r / 1023.0);
                        storeElement(outputBufferData, outputType, texelDataIndex * 4 + 1, gc / 1023.0);
                        storeElement(outputBufferData, outputType, texelDataIndex * 4 + 2, b / 1023.0);
                        storeElement(outputBufferData, outputType, texelDataIndex * 4 + 3, a / 3.0);
                    } else {
                        storeElement(outputBufferData, outputType, texelDataIndex * 4, r);
                        storeElement(outputBufferData, outputType, texelDataIndex * 4 + 1, gc);
                        storeElement(outputBufferData, outputType, texelDataIndex * 4 + 2, b);
                        storeElement(outputBufferData, outputType, texelDataIndex * 4 + 3, a);
                    }
                } else if (format == WGPUTextureFormat_RG11B10Ufloat) {
                    const uint32_t kFloat11One = 0x3c0;
                    const uint32_t kFloat10Zero = 0;
                    const uint32_t r = kFloat11One;
                    const uint32_t gc = kFloat11One;
                    const uint32_t b = kFloat10Zero;
                    const uint32_t packedValue = (b << 22) | (gc << 11) | r;
                    storeElement(texelData, texelType, texelDataIndex, packedValue);
                    storeElement(outputBufferData, outputType, texelDataIndex * 4, 1.0);
                    storeElement(outputBufferData, outputType, texelDataIndex * 4 + 1, 1.0);
                    storeElement(outputBufferData, outputType, texelDataIndex * 4 + 2, 0);
                } else {
                    for (uint32_t component = 0; component < componentCount; ++component) {
                        const int64_t base = 4 * index + component + 1;
                        switch (format) {
                            case WGPUTextureFormat_R32Uint:
                            case WGPUTextureFormat_RG32Uint:
                            case WGPUTextureFormat_RGBA16Uint:
                            case WGPUTextureFormat_RGBA32Uint:
                            case WGPUTextureFormat_R8Uint:
                            case WGPUTextureFormat_RG8Uint:
                            case WGPUTextureFormat_RGBA8Uint: {
                                const double texelValue = static_cast<double>(base % 256);
                                setData(texelValue, texelValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R16Uint:
                            case WGPUTextureFormat_RG16Uint: {
                                const double texelValue = static_cast<double>(base % 65536);
                                setData(texelValue, texelValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R8Unorm:
                            case WGPUTextureFormat_RG8Unorm:
                            case WGPUTextureFormat_RGBA8Unorm: {
                                const double texelValue = static_cast<double>(base % 256);
                                setData(texelValue, texelValue / 255.0, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_BGRA8Unorm: {
                                const double texelValue = static_cast<double>(base % 256);
                                // BGRA -> RGBA
                                constexpr std::array<uint32_t, 4> kOutputComponent = {{2, 1, 0, 3}};
                                setData(texelValue, texelValue / 255.0, texelDataIndex, component,
                                        kOutputComponent[component]);
                                break;
                            }
                            case WGPUTextureFormat_R16Unorm:
                            case WGPUTextureFormat_RG16Unorm:
                            case WGPUTextureFormat_RGBA16Unorm: {
                                const double texelValue = static_cast<double>(base % 65536);
                                setData(texelValue, texelValue / 65535.0, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R32Sint:
                            case WGPUTextureFormat_RG32Sint:
                            case WGPUTextureFormat_RGBA16Sint:
                            case WGPUTextureFormat_RGBA32Sint: {
                                const double texelValue = static_cast<double>((index & 1 ? 1 : -1) * base);
                                setData(texelValue, texelValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R8Sint:
                            case WGPUTextureFormat_RG8Sint:
                            case WGPUTextureFormat_RGBA8Sint: {
                                const double texelValue = static_cast<double>((base % 256) - 128);
                                setData(texelValue, texelValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R16Sint:
                            case WGPUTextureFormat_RG16Sint: {
                                const double signedValue =
                                    static_cast<double>((index & 1 ? 1 : -1) * ((base % 65536) - 32768));
                                setData(signedValue, signedValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R8Snorm:
                            case WGPUTextureFormat_RG8Snorm:
                            case WGPUTextureFormat_RGBA8Snorm: {
                                const double texelValue = static_cast<double>((base % 256) - 128);
                                const double outputValue = std::max(texelValue / 127.0, -1.0);
                                setData(texelValue, outputValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R16Snorm:
                            case WGPUTextureFormat_RG16Snorm:
                            case WGPUTextureFormat_RGBA16Snorm: {
                                const double texelValue = static_cast<double>((base % 65536) - 32768);
                                const double outputValue = std::max(texelValue / 32767.0, -1.0);
                                setData(texelValue, outputValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R32Float:
                            case WGPUTextureFormat_RG32Float:
                            case WGPUTextureFormat_RGBA32Float: {
                                const double texelValue = static_cast<double>(base) / 10.0;
                                setData(texelValue, texelValue, texelDataIndex, component, component);
                                break;
                            }
                            case WGPUTextureFormat_R16Float:
                            case WGPUTextureFormat_RG16Float:
                            case WGPUTextureFormat_RGBA16Float: {
                                const double texelValue = static_cast<double>(base) / 10.0;
                                const double f16Value = float16BitsToNumber(float16BitsFromNumber(texelValue));
                                setData(texelValue, f16Value, texelDataIndex, component, component);
                                break;
                            }
                            default:
                                std::abort();
                        }
                    }
                }
            }
        }
    }

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = bytesPerBlock * width;
    layout.rowsPerImage = height;
    t.queueWriteTexture(
        storageTexture.texture,
        WGPUExtent3D{width, height, depthOrArrayLayers},
        layout,
        texelData.data(),
        texelData.size());

    return outputBufferData;
}

WGPUTextureViewDimension storageViewDimension(const StorageTextureInfo& storageTexture) {
    switch (storageTexture.dimension) {
        case WGPUTextureDimension_1D:
            return WGPUTextureViewDimension_1D;
        case WGPUTextureDimension_2D:
            return storageTexture.depthOrArrayLayers > 1 ? WGPUTextureViewDimension_2DArray
                                                         : WGPUTextureViewDimension_2D;
        case WGPUTextureDimension_3D:
            return WGPUTextureViewDimension_3D;
        default:
            std::abort();
    }
}

std::string textureDeclaration(const StorageTextureInfo& storageTexture) {
    std::string declaration;
    switch (storageTexture.dimension) {
        case WGPUTextureDimension_1D:
            declaration = "texture_storage_1d";
            break;
        case WGPUTextureDimension_2D:
            declaration = storageTexture.depthOrArrayLayers > 1 ? "texture_storage_2d_array" : "texture_storage_2d";
            break;
        case WGPUTextureDimension_3D:
            declaration = "texture_storage_3d";
            break;
        default:
            std::abort();
    }
    return "\n    @group(0) @binding(0) var readOnlyTexture: " + declaration + "<"
        + std::string(textureFormatIdentifier(storageTexture.format)) + ", read>;\n    ";
}

// The explicit form of the 'auto' layout of the pipelines below.
WGPUBindGroupLayout createBindGroupLayout(
    AllFeaturesMaxLimitsGpuTest& t,
    const StorageTextureInfo& storageTexture,
    WGPUShaderStage visibility,
    bool withOutputBuffer) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].visibility = visibility;
    entries[0].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
    entries[0].storageTexture.format = storageTexture.format;
    entries[0].storageTexture.viewDimension = storageViewDimension(storageTexture);

    entries[1].binding = 1;
    entries[1].visibility = visibility;
    entries[1].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = withOutputBuffer ? 2 : 1;
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPUBindGroup createBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUTextureView view,
    WGPUBuffer outputBuffer) {
    std::array<WGPUBindGroupEntry, 2> entries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].buffer = outputBuffer;
    entries[1].offset = 0;
    entries[1].size = WGPU_WHOLE_SIZE;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = outputBuffer != nullptr ? 2 : 1;
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

WGPURenderPipeline createRenderPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    std::string_view vertexEntryPoint,
    std::string_view fragmentEntryPoint,
    WGPUPrimitiveTopology topology) {
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA32Uint;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView(fragmentEntryPoint);
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView(vertexEntryPoint);
    desc.primitive.topology = topology;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPUTexture createPlaceholderColorTexture(AllFeaturesMaxLimitsGpuTest& t, const StorageTextureInfo& storageTexture) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{storageTexture.width, storageTexture.height * storageTexture.depthOrArrayLayers, 1};
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    desc.format = WGPUTextureFormat_RGBA32Uint;
    return t.createTextureTracked(desc);
}

void drawToPlaceholderAndCopy(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUCommandEncoder commandEncoder,
    const StorageTextureInfo& storageTexture,
    WGPURenderPipeline renderPipeline,
    WGPUBindGroup bindGroup,
    uint32_t vertexCount,
    WGPUBuffer outputBuffer) {
    WGPUTexture placeholderColorTexture = createPlaceholderColorTexture(t, storageTexture);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView placeholderView = t.createViewTracked(placeholderColorTexture, viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = placeholderView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder renderPassEncoder = t.beginRenderPassTracked(commandEncoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(renderPassEncoder, renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(renderPassEncoder, vertexCount, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPassEncoder);

    t.copyTextureToBuffer(
        commandEncoder,
        placeholderColorTexture,
        outputBuffer,
        256,
        WGPUExtent3D{storageTexture.width, storageTexture.height * storageTexture.depthOrArrayLayers, 1});
}

// F.doTransform.
void doTransform(
    AllFeaturesMaxLimitsGpuTest& t,
    const StorageTextureInfo& storageTexture,
    std::string_view shaderStage,
    WGPUBuffer outputBuffer) {
    const std::string declaration = textureDeclaration(storageTexture);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView storageView = t.createViewTracked(storageTexture.texture, viewDesc);
    const uint32_t width = storageTexture.width;
    const uint32_t height = storageTexture.height;
    const uint32_t depthOrArrayLayers = storageTexture.depthOrArrayLayers;

    WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();

    if (shaderStage == "compute") {
        std::string textureLoadCoord;
        switch (storageTexture.dimension) {
            case WGPUTextureDimension_1D:
                textureLoadCoord = "invocationID.x";
                break;
            case WGPUTextureDimension_2D:
                textureLoadCoord = depthOrArrayLayers > 1 ? "vec2u(invocationID.x, invocationID.y), invocationID.z"
                                                          : "vec2u(invocationID.x, invocationID.y)";
                break;
            default:
                textureLoadCoord = "invocationID";
                break;
        }

        std::ostringstream computeShader;
        computeShader << "\n      " << declaration << "\n      @group(0) @binding(1)\n"
                      << "      var<storage,read_write> outputBuffer : array<"
                      << outputBufferWGSLType(storageTexture.format) << ">;\n\n"
                      << "      @compute\n      @workgroup_size(\n        " << width << ", " << height << ", "
                      << depthOrArrayLayers << ")\n"
                      << "      fn main(\n"
                      << "        @builtin(local_invocation_id) invocationID: vec3u,\n"
                      << "        @builtin(local_invocation_index) invocationIndex: u32) {\n"
                      << "        let initialValue = textureLoad(readOnlyTexture, " << textureLoadCoord << ");\n"
                      << "        outputBuffer[invocationIndex] = initialValue;\n"
                      << "      }";

        WGPUBindGroupLayout bindGroupLayout =
            createBindGroupLayout(t, storageTexture, WGPUShaderStage_Compute, /*withOutputBuffer=*/true);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = t.createShaderModuleTracked(computeShader.str());
        WGPUComputePipeline computePipeline = t.createComputePipelineTracked(pipelineDesc);
        WGPUBindGroup bindGroup = createBindGroup(t, bindGroupLayout, storageView, outputBuffer);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder computePassEncoder = t.beginComputePassTracked(commandEncoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(computePassEncoder, computePipeline);
        wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, 1, 1, 1);
        wgpuComputePassEncoderEnd(computePassEncoder);
    } else if (shaderStage == "fragment") {
        std::string textureLoadCoord;
        switch (storageTexture.dimension) {
            case WGPUTextureDimension_1D:
                textureLoadCoord = "textureCoord.x";
                break;
            case WGPUTextureDimension_2D:
                textureLoadCoord = depthOrArrayLayers > 1 ? "textureCoord, coordZ" : "textureCoord";
                break;
            default:
                textureLoadCoord = "vec3u(textureCoord, coordZ)";
                break;
        }

        std::ostringstream shader;
        shader << "\n        " << declaration << "\n"
               << "        @fragment\n"
               << "        fn fs(@builtin(position) fragCoord: vec4f) -> @location(0) vec4u {\n"
               << "          let coordX = u32(fragCoord.x);\n"
               << "          let coordY = u32(fragCoord.y) % " << height << "u;\n"
               << "          let coordZ = u32(fragCoord.y) / " << height << "u;\n"
               << "          let textureCoord = vec2u(coordX, coordY);\n"
               << "          return bitcast<vec4u>(textureLoad(readOnlyTexture, " << textureLoadCoord << "));\n"
               << "        }\n\n"
               << "        @vertex\n"
               << "        fn vs(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {\n"
               << "            var pos = array(\n"
               << "              vec2f(-1.0,  3.0),\n"
               << "              vec2f( 3.0, -1.0),\n"
               << "              vec2f(-1.0, -1.0));\n"
               << "            return vec4f(pos[vertexIndex], 0.0, 1.0);\n"
               << "        }\n"
               << "        ";

        WGPUShaderModule module = t.createShaderModuleTracked(shader.str());
        WGPUBindGroupLayout bindGroupLayout =
            createBindGroupLayout(t, storageTexture, WGPUShaderStage_Fragment, /*withOutputBuffer=*/false);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPURenderPipeline renderPipeline =
            createRenderPipeline(t, pipelineLayout, module, "vs", "fs", WGPUPrimitiveTopology_TriangleList);
        WGPUBindGroup bindGroup = createBindGroup(t, bindGroupLayout, storageView, nullptr);

        // This is just so our buffer compare is the same as the compute stage.
        t.expect(width == 16, "width must be 16 because we require 256 bytesPerRow");
        drawToPlaceholderAndCopy(t, commandEncoder, storageTexture, renderPipeline, bindGroup, 3, outputBuffer);
    } else if (shaderStage == "vertex") {
        // We draw width by (height * depthOrArrayLayers) points via 'point-list' to a placeholder
        // color texture of the same size, using the vertex index to compute a coord in the source
        // texture and the same coord to compute where to render the point.
        std::string loadFromTextureWGSL;
        switch (storageTexture.dimension) {
            case WGPUTextureDimension_1D:
                loadFromTextureWGSL = "\n              output.vertex_out = textureLoad(readOnlyTexture, coordX);";
                break;
            case WGPUTextureDimension_2D:
                if (depthOrArrayLayers == 1) {
                    loadFromTextureWGSL =
                        "\n                output.vertex_out = textureLoad(readOnlyTexture, vec2u(coordX, coordY));";
                } else {
                    loadFromTextureWGSL =
                        "\n                output.vertex_out =\n"
                        "                  textureLoad(readOnlyTexture, vec2u(coordX, coordY), coordZ);";
                }
                break;
            default:
                loadFromTextureWGSL =
                    "\n              output.vertex_out = textureLoad(readOnlyTexture, vec3u(coordX, coordY, coordZ));";
                break;
        }

        std::ostringstream shader;
        shader << "\n        " << declaration << "\n"
               << "        struct VertexOutput {\n"
               << "          @builtin(position) my_pos: vec4f,\n"
               << "          @location(0) @interpolate(flat, either)\n"
               << "            vertex_out: " << outputBufferWGSLType(storageTexture.format) << ",\n"
               << "        }\n"
               << "        @vertex\n"
               << "        fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VertexOutput {\n"
               << "            var output : VertexOutput;\n"
               << "            let coordX = vertexIndex % " << width << "u;\n"
               << "            let coordY = vertexIndex / " << width << "u % " << height << "u;\n"
               << "            let coordZ = vertexIndex / " << width * height << "u;\n"
               << "            let writePos = vec2f(f32(coordX), f32(coordY + coordZ * " << height << "));\n"
               << "            let destSize = vec2f(\n"
               << "              " << width << ",\n"
               << "              " << height * depthOrArrayLayers << ");\n"
               << "            output.my_pos = vec4f((((writePos + 0.5) / destSize) * 2.0 - 1.0) * vec2f(1, -1), 0.0, 1.0);\n"
               << "            " << loadFromTextureWGSL << "\n"
               << "            return output;\n"
               << "        }\n"
               << "        @fragment\n"
               << "        fn fs_main(fragmentInput : VertexOutput) -> @location(0) vec4u {\n"
               << "          let v = fragmentInput.vertex_out;\n"
               << "          return bitcast<vec4u>(v);\n"
               << "        }\n"
               << "        ";

        WGPUShaderModule module = t.createShaderModuleTracked(shader.str());
        WGPUBindGroupLayout bindGroupLayout =
            createBindGroupLayout(t, storageTexture, WGPUShaderStage_Vertex, /*withOutputBuffer=*/false);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPURenderPipeline renderPipeline =
            createRenderPipeline(t, pipelineLayout, module, "vs_main", "fs_main", WGPUPrimitiveTopology_PointList);
        WGPUBindGroup bindGroup = createBindGroup(t, bindGroupLayout, storageView, nullptr);

        const uint32_t texelCount = width * height * depthOrArrayLayers;
        drawToPlaceholderAndCopy(t, commandEncoder, storageTexture, renderPipeline, bindGroup, texelCount, outputBuffer);
    } else {
        std::abort();
    }

    WGPUCommandBuffer commandBuffer = t.finishTracked(commandEncoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
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
        "The basic functionality tests for read-only storage textures. In the test we read data from\n"
        "    the read-only storage texture, write the data into an output storage buffer, and check if the\n"
        "    data in the output storage buffer is exactly what we expect.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormatValues())
            .combine("shaderStage", {Value("compute"), Value("vertex"), Value("fragment")})
            .combine("dimension", {Value("1d"), Value("2d"), Value("3d")})
            .combine("depthOrArrayLayers", {1, 2})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d"
                         && valueAs<int64_t>(*findParam(p, "depthOrArrayLayers")) > 1);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string shaderStage = t.param<std::string>("shaderStage");
        const WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        const uint32_t depthOrArrayLayers = static_cast<uint32_t>(t.param<int64_t>("depthOrArrayLayers"));
        t.skipIfTextureFormatNotSupported(format);
        if (!t.isTextureFormatUsableWithStorageAccessMode(format, WGPUStorageTextureAccess_ReadOnly)) {
            t.skip("Texture with " + std::string(textureFormatIdentifier(format))
                   + " is not usable as a storage texture with access read-only");
        }

        constexpr uint32_t kWidth = 16;
        const uint32_t height = dimension == WGPUTextureDimension_1D ? 1 : 8;
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.format = format;
        textureDesc.dimension = dimension;
        textureDesc.size = WGPUExtent3D{kWidth, height, depthOrArrayLayers};
        textureDesc.usage =
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding;
        StorageTextureInfo storageTexture;
        storageTexture.texture = t.createTextureTracked(textureDesc);
        storageTexture.format = format;
        storageTexture.dimension = dimension;
        storageTexture.width = kWidth;
        storageTexture.height = height;
        storageTexture.depthOrArrayLayers = depthOrArrayLayers;

        const std::vector<uint8_t> expectedData = initTextureAndGetExpectedOutputBufferData(t, storageTexture);

        const uint32_t bytesPerRow = 4 * 4 * kWidth;
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = static_cast<uint64_t>(bytesPerRow) * height * depthOrArrayLayers;
        bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        WGPUBuffer outputBuffer = t.createBufferTracked(bufferDesc);

        doTransform(t, storageTexture, shaderStage, outputBuffer);

        // Upstream compares as Uint32Array / Int32Array / Float32Array; the expected values are
        // never NaN or -0, so element equality is byte equality.
        t.expectGPUBufferValuesEqual(outputBuffer, expectedData.data(), expectedData.size());
    });

} // namespace
