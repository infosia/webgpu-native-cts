#pragma once

#include <array>
#include <cstdint>

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

} // namespace cts
