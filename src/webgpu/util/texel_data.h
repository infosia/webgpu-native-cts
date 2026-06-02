// Ported from gpuweb/cts src/webgpu/util/texture/texel_data.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cts/webgpu.h"

namespace cts {

enum class TexelComponent {
    R = 0,
    G = 1,
    B = 2,
    A = 3,
};

enum class ComponentDataType {
    Uint,
    Sint,
    Unorm,
    Snorm,
    Float,
    Ufloat,
};

struct TexelComponents {
    std::array<double, 4> values = {0.0, 0.0, 0.0, 0.0};
};

struct TexelBits {
    std::array<uint32_t, 4> values = {0, 0, 0, 0};
};

struct TexelRepresentation {
    WGPUTextureFormat format;
    std::vector<TexelComponent> componentOrder;
    std::array<uint32_t, 4> bitLengths = {0, 0, 0, 0};
    std::array<ComponentDataType, 4> dataTypes = {
        ComponentDataType::Uint,
        ComponentDataType::Uint,
        ComponentDataType::Uint,
        ComponentDataType::Uint,
    };
    uint32_t bytesPerBlock = 0;

    std::vector<uint8_t> packBits(const TexelBits& bits) const;
    TexelBits unpackBits(const uint8_t* data, size_t len) const;
    TexelComponents bitsToNumber(const TexelBits& bits) const;
    TexelBits numberToBits(const TexelComponents& numbers) const;
};

const TexelRepresentation& texelRepresentation(WGPUTextureFormat format);
bool texelComponentEqual(double actual, double expected, double maxDiff);

} // namespace cts
