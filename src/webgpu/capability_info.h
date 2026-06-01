#pragma once

#include <array>
#include <cstdlib>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cts/webgpu.h"

namespace cts {

constexpr uint64_t kBufferSizeAlignment = 4;
constexpr std::array<WGPUBufferUsage, 10> kBufferUsages = {
    WGPUBufferUsage_MapRead,
    WGPUBufferUsage_MapWrite,
    WGPUBufferUsage_CopySrc,
    WGPUBufferUsage_CopyDst,
    WGPUBufferUsage_Index,
    WGPUBufferUsage_Vertex,
    WGPUBufferUsage_Uniform,
    WGPUBufferUsage_Storage,
    WGPUBufferUsage_Indirect,
    WGPUBufferUsage_QueryResolve,
};
constexpr WGPUBufferUsage kSomeBogusBufferUsage = 0x40000000;
constexpr WGPUBufferUsage kAllBufferUsageBits =
    WGPUBufferUsage_MapRead |
    WGPUBufferUsage_MapWrite |
    WGPUBufferUsage_CopySrc |
    WGPUBufferUsage_CopyDst |
    WGPUBufferUsage_Index |
    WGPUBufferUsage_Vertex |
    WGPUBufferUsage_Uniform |
    WGPUBufferUsage_Storage |
    WGPUBufferUsage_Indirect |
    WGPUBufferUsage_QueryResolve;
constexpr uint64_t kMaxSafeMultipleOf8 = 9007199254740984ULL;

constexpr std::array<WGPUTextureUsage, 6> kTextureUsages = {
    WGPUTextureUsage_CopySrc,
    WGPUTextureUsage_CopyDst,
    WGPUTextureUsage_TextureBinding,
    WGPUTextureUsage_StorageBinding,
    WGPUTextureUsage_RenderAttachment,
    WGPUTextureUsage_TransientAttachment,
};
constexpr WGPUTextureUsage kAllTextureUsages =
    WGPUTextureUsage_CopySrc |
    WGPUTextureUsage_CopyDst |
    WGPUTextureUsage_TextureBinding |
    WGPUTextureUsage_StorageBinding |
    WGPUTextureUsage_RenderAttachment |
    WGPUTextureUsage_TransientAttachment;
constexpr WGPUTextureUsage kSomeBogusTextureUsage = 0x40000000;

constexpr bool isValidTextureUsageCombination(WGPUTextureUsage usage) {
    if (usage == 0) return false;
    if (usage & WGPUTextureUsage_TransientAttachment)
        return usage == (WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment);
    return (usage & ~kAllTextureUsages) == 0;
}

inline const std::vector<WGPUTextureUsage> kValidCombinationsOfOneOrTwoTextureUsages = [] {
    std::vector<WGPUTextureUsage> combinations;
    for (WGPUTextureUsage usage0 : kTextureUsages) {
        for (WGPUTextureUsage usage1 : kTextureUsages) {
            if (usage0 > usage1) {
                continue;
            }
            const WGPUTextureUsage usage = usage0 | usage1;
            if (isValidTextureUsageCombination(usage)) {
                combinations.push_back(usage);
            }
        }
    }
    return combinations;
}();

inline constexpr WGPUShaderStage kValidStagesAll =
    WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
inline constexpr WGPUShaderStage kValidStagesStorageWrite =
    WGPUShaderStage_Fragment | WGPUShaderStage_Compute;

inline constexpr std::array<WGPUShaderStage, 8> kShaderStageCombinations = {
    WGPUShaderStage_None,
    WGPUShaderStage_Vertex,
    WGPUShaderStage_Fragment,
    WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
    WGPUShaderStage_Compute,
    WGPUShaderStage_Vertex | WGPUShaderStage_Compute,
    WGPUShaderStage_Fragment | WGPUShaderStage_Compute,
    WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute,
};

inline constexpr std::array<WGPUBufferBindingType, 3> kBufferBindingTypes = {
    WGPUBufferBindingType_Uniform,
    WGPUBufferBindingType_Storage,
    WGPUBufferBindingType_ReadOnlyStorage,
};

inline uint32_t bufferTypeMaxDynamicBuffersLimit(const WGPULimits& limits, WGPUBufferBindingType type) {
    switch (type) {
        case WGPUBufferBindingType_Uniform:
            return limits.maxDynamicUniformBuffersPerPipelineLayout;
        case WGPUBufferBindingType_Storage:
        case WGPUBufferBindingType_ReadOnlyStorage:
            return limits.maxDynamicStorageBuffersPerPipelineLayout;
        default:
            std::abort();
    }
}

inline uint32_t bufferTypePerStageComputeLimit(const WGPULimits& limits, WGPUBufferBindingType type) {
    switch (type) {
        case WGPUBufferBindingType_Uniform:
            return limits.maxUniformBuffersPerShaderStage;
        case WGPUBufferBindingType_Storage:
        case WGPUBufferBindingType_ReadOnlyStorage:
            return limits.maxStorageBuffersPerShaderStage;
        default:
            std::abort();
    }
}

inline constexpr std::array<WGPUSamplerBindingType, 3> kSamplerBindingTypes = {
    WGPUSamplerBindingType_Filtering,
    WGPUSamplerBindingType_NonFiltering,
    WGPUSamplerBindingType_Comparison,
};

inline constexpr std::array<WGPUTextureSampleType, 5> kTextureSampleTypes = {
    WGPUTextureSampleType_Float,
    WGPUTextureSampleType_UnfilterableFloat,
    WGPUTextureSampleType_Depth,
    WGPUTextureSampleType_Sint,
    WGPUTextureSampleType_Uint,
};

inline constexpr std::array<WGPUStorageTextureAccess, 3> kStorageTextureAccessValues = {
    WGPUStorageTextureAccess_ReadOnly,
    WGPUStorageTextureAccess_ReadWrite,
    WGPUStorageTextureAccess_WriteOnly,
};

// Binding entry params use stable scalar string keys instead of upstream's object JSON serialization.
inline constexpr std::array<std::string_view, 11> kAllBindingEntryKeys = {
    "buffer_uniform",
    "buffer_storage",
    "buffer_read-only-storage",
    "sampler_comparison",
    "sampler_filtering",
    "sampler_non-filtering",
    "texture_ms-false",
    "texture_ms-true",
    "storageTexture_write-only",
    "storageTexture_read-only",
    "storageTexture_read-write",
};

inline std::vector<std::string_view> allBindingEntries(bool) {
    return std::vector<std::string_view>(kAllBindingEntryKeys.begin(), kAllBindingEntryKeys.end());
}

inline bool entryKeyIsBuffer(std::string_view key) {
    return key == "buffer_uniform"
        || key == "buffer_storage"
        || key == "buffer_read-only-storage";
}

inline WGPUBufferBindingType entryKeyBufferType(std::string_view key) {
    if (key == "buffer_uniform") return WGPUBufferBindingType_Uniform;
    if (key == "buffer_storage") return WGPUBufferBindingType_Storage;
    if (key == "buffer_read-only-storage") return WGPUBufferBindingType_ReadOnlyStorage;
    std::abort();
}

inline bool entryKeyIsStorageTexture(std::string_view key) {
    return key == "storageTexture_write-only"
        || key == "storageTexture_read-only"
        || key == "storageTexture_read-write";
}

inline WGPUShaderStage validStagesForEntryKey(std::string_view key) {
    if (key == "buffer_storage"
        || key == "storageTexture_write-only"
        || key == "storageTexture_read-write") {
        return kValidStagesStorageWrite;
    }
    return kValidStagesAll;
}

inline WGPUBindGroupLayoutEntry bglEntryFromKey(std::string_view key) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    if (entryKeyIsBuffer(key)) {
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = entryKeyBufferType(key);
    } else if (key == "sampler_comparison") {
        entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry.sampler.type = WGPUSamplerBindingType_Comparison;
    } else if (key == "sampler_filtering") {
        entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry.sampler.type = WGPUSamplerBindingType_Filtering;
    } else if (key == "sampler_non-filtering") {
        entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry.sampler.type = WGPUSamplerBindingType_NonFiltering;
    } else if (key == "texture_ms-false" || key == "texture_ms-true") {
        entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entry.texture.multisampled = key == "texture_ms-true" ? WGPU_TRUE : WGPU_FALSE;
    } else if (entryKeyIsStorageTexture(key)) {
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        if (key == "storageTexture_write-only") {
            entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        } else if (key == "storageTexture_read-only") {
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
        } else {
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
        }
        entry.storageTexture.format = WGPUTextureFormat_R32Float;
    } else {
        std::abort();
    }
    return entry;
}

} // namespace cts
