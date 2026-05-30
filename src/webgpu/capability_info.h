#pragma once

#include <array>

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

} // namespace cts
