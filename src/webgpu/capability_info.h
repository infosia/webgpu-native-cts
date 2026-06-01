#pragma once

#include <algorithm>
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

inline constexpr std::array<WGPUShaderStage, 3> kShaderStages = {
    WGPUShaderStage_Vertex,
    WGPUShaderStage_Fragment,
    WGPUShaderStage_Compute,
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

enum class BGLPerStageLimitClass {
    UniformBuffer,
    StorageBuffer,
    Sampler,
    SampledTexture,
    ReadOnlyStorageTexture,
    WriteOnlyStorageTexture,
    ReadWriteStorageTexture,
};

inline BGLPerStageLimitClass bglEntryPerStageLimitClass(std::string_view key) {
    if (key == "buffer_uniform") return BGLPerStageLimitClass::UniformBuffer;
    if (key == "buffer_storage" || key == "buffer_read-only-storage") {
        return BGLPerStageLimitClass::StorageBuffer;
    }
    if (key == "sampler_comparison" || key == "sampler_filtering" || key == "sampler_non-filtering") {
        return BGLPerStageLimitClass::Sampler;
    }
    if (key == "texture_ms-false" || key == "texture_ms-true") {
        return BGLPerStageLimitClass::SampledTexture;
    }
    if (key == "storageTexture_read-only") return BGLPerStageLimitClass::ReadOnlyStorageTexture;
    if (key == "storageTexture_write-only") return BGLPerStageLimitClass::WriteOnlyStorageTexture;
    if (key == "storageTexture_read-write") return BGLPerStageLimitClass::ReadWriteStorageTexture;
    std::abort();
}

inline uint32_t compatibilityLimitOrFallback(uint32_t compatibilityLimit, uint32_t fallback) {
    return compatibilityLimit == WGPU_LIMIT_U32_UNDEFINED ? fallback : compatibilityLimit;
}

inline uint32_t getBindingLimitForClassAndStage(
    const WGPULimits& limits,
    const WGPUCompatibilityModeLimits& compatibilityLimits,
    BGLPerStageLimitClass limitClass,
    WGPUShaderStage stage) {
    switch (limitClass) {
        case BGLPerStageLimitClass::UniformBuffer:
            return limits.maxUniformBuffersPerShaderStage;
        case BGLPerStageLimitClass::Sampler:
            return limits.maxSamplersPerShaderStage;
        case BGLPerStageLimitClass::SampledTexture:
            return limits.maxSampledTexturesPerShaderStage;
        case BGLPerStageLimitClass::StorageBuffer:
            if (stage == WGPUShaderStage_Vertex) {
                return compatibilityLimitOrFallback(
                    compatibilityLimits.maxStorageBuffersInVertexStage,
                    limits.maxStorageBuffersPerShaderStage);
            }
            if (stage == WGPUShaderStage_Fragment) {
                return compatibilityLimitOrFallback(
                    compatibilityLimits.maxStorageBuffersInFragmentStage,
                    limits.maxStorageBuffersPerShaderStage);
            }
            return limits.maxStorageBuffersPerShaderStage;
        case BGLPerStageLimitClass::ReadOnlyStorageTexture:
        case BGLPerStageLimitClass::WriteOnlyStorageTexture:
        case BGLPerStageLimitClass::ReadWriteStorageTexture:
            if (stage == WGPUShaderStage_Vertex) {
                return compatibilityLimitOrFallback(
                    compatibilityLimits.maxStorageTexturesInVertexStage,
                    limits.maxStorageTexturesPerShaderStage);
            }
            if (stage == WGPUShaderStage_Fragment) {
                return compatibilityLimitOrFallback(
                    compatibilityLimits.maxStorageTexturesInFragmentStage,
                    limits.maxStorageTexturesPerShaderStage);
            }
            return limits.maxStorageTexturesPerShaderStage;
        default:
            std::abort();
    }
}

template <class Test>
uint32_t getBindingLimitForBindingType(
    const Test& t,
    WGPUShaderStage visibility,
    std::string_view key) {
    if (visibility == WGPUShaderStage_None) {
        return 0;
    }

    const WGPULimits limits = t.getLimits();
    const WGPUCompatibilityModeLimits compatibilityLimits = t.getCompatibilityModeLimits();
    const BGLPerStageLimitClass limitClass = bglEntryPerStageLimitClass(key);

    uint32_t result = WGPU_LIMIT_U32_UNDEFINED;
    for (WGPUShaderStage stage : kShaderStages) {
        if ((visibility & stage) == 0) {
            continue;
        }
        result = std::min(result, getBindingLimitForClassAndStage(limits, compatibilityLimits, limitClass, stage));
    }
    return result;
}

inline std::vector<std::string_view> pickExtraBindingTypesForPerStage(
    std::string_view maxedKey,
    bool extraTypeSame) {
    std::vector<std::string_view> result;
    const BGLPerStageLimitClass maxedClass = bglEntryPerStageLimitClass(maxedKey);
    if (extraTypeSame) {
        for (std::string_view key : allBindingEntries(false)) {
            if (bglEntryPerStageLimitClass(key) == maxedClass) {
                result.push_back(key);
            }
        }
        return result;
    }

    result.push_back(maxedClass == BGLPerStageLimitClass::Sampler
        ? std::string_view("texture_ms-false")
        : std::string_view("sampler_filtering"));
    return result;
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
