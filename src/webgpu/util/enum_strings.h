#pragma once

#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cts/test.h"
#include "webgpu/texture_format.h"

namespace cts {

inline std::string_view textureDimensionIdentifier(WGPUTextureDimension dimension) {
    switch (dimension) {
        case WGPUTextureDimension_1D:
            return "1d";
        case WGPUTextureDimension_2D:
            return "2d";
        case WGPUTextureDimension_3D:
            return "3d";
        default:
            std::abort();
    }
}

inline WGPUTextureDimension parseTextureDimension(std::string_view identifier) {
    if (identifier == "1d") {
        return WGPUTextureDimension_1D;
    }
    if (identifier == "2d") {
        return WGPUTextureDimension_2D;
    }
    if (identifier == "3d") {
        return WGPUTextureDimension_3D;
    }
    std::abort();
}

inline std::string_view textureViewDimensionIdentifier(WGPUTextureViewDimension dimension) {
    switch (dimension) {
        case WGPUTextureViewDimension_1D:
            return "1d";
        case WGPUTextureViewDimension_2D:
            return "2d";
        case WGPUTextureViewDimension_2DArray:
            return "2d-array";
        case WGPUTextureViewDimension_Cube:
            return "cube";
        case WGPUTextureViewDimension_CubeArray:
            return "cube-array";
        case WGPUTextureViewDimension_3D:
            return "3d";
        default:
            std::abort();
    }
}

inline WGPUTextureViewDimension parseTextureViewDimension(std::string_view identifier) {
    if (identifier == "1d") {
        return WGPUTextureViewDimension_1D;
    }
    if (identifier == "2d") {
        return WGPUTextureViewDimension_2D;
    }
    if (identifier == "2d-array") {
        return WGPUTextureViewDimension_2DArray;
    }
    if (identifier == "cube") {
        return WGPUTextureViewDimension_Cube;
    }
    if (identifier == "cube-array") {
        return WGPUTextureViewDimension_CubeArray;
    }
    if (identifier == "3d") {
        return WGPUTextureViewDimension_3D;
    }
    std::abort();
}

inline std::string_view textureAspectIdentifier(WGPUTextureAspect aspect) {
    switch (aspect) {
        case WGPUTextureAspect_All:
            return "all";
        case WGPUTextureAspect_DepthOnly:
            return "depth-only";
        case WGPUTextureAspect_StencilOnly:
            return "stencil-only";
        default:
            std::abort();
    }
}

inline WGPUTextureAspect parseTextureAspect(std::string_view identifier) {
    if (identifier == "all") {
        return WGPUTextureAspect_All;
    }
    if (identifier == "depth-only") {
        return WGPUTextureAspect_DepthOnly;
    }
    if (identifier == "stencil-only") {
        return WGPUTextureAspect_StencilOnly;
    }
    std::abort();
}

inline std::string_view bufferBindingTypeIdentifier(WGPUBufferBindingType type) {
    switch (type) {
        case WGPUBufferBindingType_Uniform:
            return "uniform";
        case WGPUBufferBindingType_Storage:
            return "storage";
        case WGPUBufferBindingType_ReadOnlyStorage:
            return "read-only-storage";
        default:
            std::abort();
    }
}

inline WGPUBufferBindingType parseBufferBindingType(std::string_view identifier) {
    if (identifier == "uniform") {
        return WGPUBufferBindingType_Uniform;
    }
    if (identifier == "storage") {
        return WGPUBufferBindingType_Storage;
    }
    if (identifier == "read-only-storage") {
        return WGPUBufferBindingType_ReadOnlyStorage;
    }
    std::abort();
}

inline std::string_view storageTextureAccessIdentifier(WGPUStorageTextureAccess access) {
    switch (access) {
        case WGPUStorageTextureAccess_WriteOnly:
            return "write-only";
        case WGPUStorageTextureAccess_ReadOnly:
            return "read-only";
        case WGPUStorageTextureAccess_ReadWrite:
            return "read-write";
        default:
            std::abort();
    }
}

inline WGPUStorageTextureAccess parseStorageTextureAccess(std::string_view identifier) {
    if (identifier == "write-only") {
        return WGPUStorageTextureAccess_WriteOnly;
    }
    if (identifier == "read-only") {
        return WGPUStorageTextureAccess_ReadOnly;
    }
    if (identifier == "read-write") {
        return WGPUStorageTextureAccess_ReadWrite;
    }
    std::abort();
}

inline std::string_view textureSampleTypeIdentifier(WGPUTextureSampleType sampleType) {
    switch (sampleType) {
        case WGPUTextureSampleType_Float:
            return "float";
        case WGPUTextureSampleType_UnfilterableFloat:
            return "unfilterable-float";
        case WGPUTextureSampleType_Depth:
            return "depth";
        case WGPUTextureSampleType_Sint:
            return "sint";
        case WGPUTextureSampleType_Uint:
            return "uint";
        default:
            std::abort();
    }
}

inline WGPUTextureSampleType parseTextureSampleType(std::string_view identifier) {
    if (identifier == "float") {
        return WGPUTextureSampleType_Float;
    }
    if (identifier == "unfilterable-float") {
        return WGPUTextureSampleType_UnfilterableFloat;
    }
    if (identifier == "depth") {
        return WGPUTextureSampleType_Depth;
    }
    if (identifier == "sint") {
        return WGPUTextureSampleType_Sint;
    }
    if (identifier == "uint") {
        return WGPUTextureSampleType_Uint;
    }
    std::abort();
}

inline std::vector<Value> formatIdentifierValues(std::span<const WGPUTextureFormat> formats) {
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.push_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

} // namespace cts
