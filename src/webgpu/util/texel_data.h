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
    // Port of upstream texel_data.ts `bitsToULPFromZero(numberToBits(...))` for a single
    // component: encodes `value` through this format's encoding of the component at
    // `index` (0=R .. 3=A) and returns its signed distance from zero in encoding steps
    // ("ULP from zero"). unorm/uint map to the encoded integer, snorm/sint sign-extend
    // (snorm clamps -max-1 to -max), float formats count normal-number steps with
    // subnormals flushed to zero. Note: rgb9e5ufloat encodes each component
    // independently (5-bit exponent / 9-bit mantissa), matching upstream numberToBits,
    // not the shared-exponent pack used by this struct's numberToBits().
    int64_t ulpFromZero(uint32_t index, double value) const;
};

const TexelRepresentation& texelRepresentation(WGPUTextureFormat format);
bool texelComponentEqual(double actual, double expected, double maxDiff);

} // namespace cts
