// Ported from gpuweb/cts src/webgpu/util/texture/texel_data.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "webgpu/util/texel_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "webgpu/texture_format.h"

namespace cts {
namespace {

uint32_t componentIndex(TexelComponent component) {
    return static_cast<uint32_t>(component);
}

uint32_t bitMask(uint32_t bits) {
    return bits == 32 ? 0xffffffffu : ((1u << bits) - 1u);
}

int32_t signExtend(uint32_t value, uint32_t bits) {
    const uint32_t signBit = 1u << (bits - 1);
    const uint32_t mask = bitMask(bits);
    value &= mask;
    return static_cast<int32_t>((value ^ signBit) - signBit);
}

double normalizedIntegerAsFloat(int32_t integer, uint32_t bits, bool signedValue) {
    if (signedValue) {
        const int32_t max = (1 << (bits - 1)) - 1;
        if (integer == -max - 1) {
            integer = -max;
        }
        return static_cast<double>(integer) / static_cast<double>(max);
    }
    const uint32_t max = bitMask(bits);
    return static_cast<double>(static_cast<uint32_t>(integer)) / static_cast<double>(max);
}

uint32_t floatAsNormalizedInteger(double value, uint32_t bits, bool signedValue) {
    const double scale = signedValue ? static_cast<double>((1 << (bits - 1)) - 1) : static_cast<double>(bitMask(bits));
    const int64_t rounded = static_cast<int64_t>(std::llround(value * scale));
    return static_cast<uint32_t>(rounded) & bitMask(bits);
}

double gammaDecompress(double value) {
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return std::pow((value + 0.055) / 1.055, 2.4);
}

double gammaCompress(double value) {
    if (value <= 0.0031308) {
        return 12.92 * value;
    }
    return 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double f32FromBits(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t f32ToBits(double value) {
    const float f = static_cast<float>(value);
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

double floatBitsToNumber(uint32_t bits, uint32_t signedBit, uint32_t exponentBits, uint32_t mantissaBits, int32_t bias) {
    const uint32_t nonSignBits = exponentBits + mantissaBits;
    const uint32_t exponentMask = ((1u << exponentBits) - 1u) << mantissaBits;
    const uint32_t mantissaMask = (1u << mantissaBits) - 1u;
    const bool negative = signedBit != 0 && ((bits & (1u << nonSignBits)) != 0);
    const uint32_t exponent = (bits & exponentMask) >> mantissaBits;
    const uint32_t mantissa = bits & mantissaMask;
    if (exponent == ((1u << exponentBits) - 1u)) {
        if (mantissa != 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    double value = 0.0;
    if (exponent == 0) {
        value = std::ldexp(static_cast<double>(mantissa) / static_cast<double>(1u << mantissaBits), 1 - bias);
    } else {
        value = std::ldexp(1.0 + static_cast<double>(mantissa) / static_cast<double>(1u << mantissaBits),
                           static_cast<int32_t>(exponent) - bias);
    }
    return negative ? -value : value;
}

uint32_t numberToFloatBits(double value, uint32_t signedBit, uint32_t exponentBits, uint32_t mantissaBits, int32_t bias) {
    if (std::isnan(value)) {
        return (((1u << exponentBits) - 1u) << mantissaBits) | ((1u << mantissaBits) - 1u);
    }
    const uint32_t sign = value < 0.0 && signedBit != 0 ? 1u : 0u;
    value = std::fabs(value);
    if (value == 0.0) {
        return sign << (exponentBits + mantissaBits);
    }
    if (std::isinf(value)) {
        return (sign << (exponentBits + mantissaBits)) | (((1u << exponentBits) - 1u) << mantissaBits);
    }
    int exponent = 0;
    const double normalized = std::frexp(value, &exponent) * 2.0;
    --exponent;
    const int32_t biased = exponent + bias;
    if (biased <= 0) {
        return sign << (exponentBits + mantissaBits);
    }
    const uint32_t maxExponent = (1u << exponentBits) - 1u;
    if (static_cast<uint32_t>(biased) >= maxExponent) {
        return (sign << (exponentBits + mantissaBits)) | (maxExponent << mantissaBits);
    }
    const double mantissaFloat = (normalized - 1.0) * static_cast<double>(1u << mantissaBits);
    const uint32_t mantissa = static_cast<uint32_t>(mantissaFloat) & ((1u << mantissaBits) - 1u);
    return (sign << (exponentBits + mantissaBits)) | (static_cast<uint32_t>(biased) << mantissaBits) | mantissa;
}

// Port of upstream conversion.ts `floatBitsToNormalULPFromZero`: the signed distance of
// a float bit pattern from zero, counting steps between normal numbers. Subnormal values
// flush to 0 (so 0 is one ULP from the minimum normal number); +0 and -0 are both 0.
// Upstream asserts on infinity/NaN bit patterns; here they map monotonically past the
// maximum finite value instead, so callers must reject non-finite values themselves.
int64_t floatBitsToNormalULPFromZero(uint32_t bits, uint32_t signedBit, uint32_t exponentBits, uint32_t mantissaBits) {
    const uint32_t maskSign = signedBit << (exponentBits + mantissaBits);
    const uint32_t maskExpt = ((1u << exponentBits) - 1u) << mantissaBits;
    const uint32_t maskMant = (1u << mantissaBits) - 1u;
    const int64_t sign = (bits & maskSign) != 0 ? -1 : 1;
    const uint32_t rest = bits & (maskExpt | maskMant);
    const bool subnormalOrZero = (bits & maskExpt) == 0;
    // The first normal number is maskMant+1, so subtract maskMant to make
    // minNormal - zero = 1 ULP.
    const int64_t absUlpFromZero = subnormalOrZero
        ? 0
        : static_cast<int64_t>(rest) - static_cast<int64_t>(maskMant);
    return sign * absUlpFromZero;
}

double rgb9e5BitsToNumber(uint32_t bits) {
    const uint32_t exponent = bits >> 9;
    const uint32_t mantissa = bits & 0x1ffu;
    return static_cast<double>(mantissa) * std::pow(2.0, static_cast<int32_t>(exponent) - 15 - 9);
}

uint32_t packRGB9E5UFloat(double r, double g, double b) {
    constexpr uint32_t mantissaBits = 9;
    constexpr uint32_t maxExponent = 31;
    constexpr int32_t exponentBias = 15;
    const double sharedExponentMax =
        (static_cast<double>((1u << mantissaBits) - 1u) / static_cast<double>(1u << mantissaBits))
        * std::pow(2.0, static_cast<int32_t>(maxExponent) - exponentBias);
    const double red = std::clamp(r, 0.0, sharedExponentMax);
    const double green = std::clamp(g, 0.0, sharedExponentMax);
    const double blue = std::clamp(b, 0.0, sharedExponentMax);
    const double maxComponent = std::max({red, green, blue});
    const double log2Max = maxComponent == 0.0 ? -std::numeric_limits<double>::infinity() : std::log2(maxComponent);
    const int32_t exponentSharedP =
        std::max(-exponentBias - 1, static_cast<int32_t>(std::floor(log2Max))) + 1 + exponentBias;
    const double maxSFloat =
        maxComponent / std::pow(2.0, exponentSharedP - exponentBias - static_cast<int32_t>(mantissaBits)) + 0.5;
    const uint32_t maxS = static_cast<uint32_t>(std::floor(maxSFloat));
    const int32_t exponentShared = maxS == (1u << mantissaBits) ? exponentSharedP + 1 : exponentSharedP;
    const double scalar = 1.0 / std::pow(2.0, exponentShared - exponentBias - static_cast<int32_t>(mantissaBits));
    const uint32_t redS = static_cast<uint32_t>(std::floor(red * scalar + 0.5));
    const uint32_t greenS = static_cast<uint32_t>(std::floor(green * scalar + 0.5));
    const uint32_t blueS = static_cast<uint32_t>(std::floor(blue * scalar + 0.5));
    return (static_cast<uint32_t>(exponentShared) << 27u)
        | ((blueS & 0x1ffu) << 18u)
        | ((greenS & 0x1ffu) << 9u)
        | (redS & 0x1ffu);
}

bool componentsAreByteAligned(const TexelRepresentation& repr) {
    for (TexelComponent component : repr.componentOrder) {
        if (repr.bitLengths[componentIndex(component)] % 8 != 0) {
            return false;
        }
    }
    return true;
}

TexelRepresentation makeRepr(
    WGPUTextureFormat format,
    std::vector<TexelComponent> order,
    std::array<uint32_t, 4> bitLengths,
    std::array<ComponentDataType, 4> dataTypes,
    uint32_t bytesPerBlock) {
    return TexelRepresentation{format, std::move(order), bitLengths, dataTypes, bytesPerBlock};
}

TexelRepresentation makeSimple(
    WGPUTextureFormat format,
    std::vector<TexelComponent> order,
    uint32_t bits,
    ComponentDataType type,
    uint32_t bytesPerBlock) {
    std::array<uint32_t, 4> bitLengths = {0, 0, 0, 0};
    std::array<ComponentDataType, 4> dataTypes = {
        ComponentDataType::Uint,
        ComponentDataType::Uint,
        ComponentDataType::Uint,
        ComponentDataType::Uint,
    };
    for (TexelComponent component : order) {
        bitLengths[componentIndex(component)] = bits;
        dataTypes[componentIndex(component)] = type;
    }
    return makeRepr(format, std::move(order), bitLengths, dataTypes, bytesPerBlock);
}

TexelRepresentation makePackedRgb10a2(WGPUTextureFormat format, ComponentDataType type) {
    return makeRepr(
        format,
        {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A},
        {10, 10, 10, 2},
        {type, type, type, type},
        4);
}

TexelRepresentation makeRg11b10() {
    return makeRepr(
        WGPUTextureFormat_RG11B10Ufloat,
        {TexelComponent::R, TexelComponent::G, TexelComponent::B},
        {11, 11, 10, 0},
        {ComponentDataType::Ufloat, ComponentDataType::Ufloat, ComponentDataType::Ufloat, ComponentDataType::Uint},
        4);
}

TexelRepresentation makeRgb9e5() {
    return makeRepr(
        WGPUTextureFormat_RGB9E5Ufloat,
        {TexelComponent::R, TexelComponent::G, TexelComponent::B},
        {14, 14, 14, 0},
        {ComponentDataType::Ufloat, ComponentDataType::Ufloat, ComponentDataType::Ufloat, ComponentDataType::Uint},
        4);
}

} // namespace

std::vector<uint8_t> TexelRepresentation::packBits(const TexelBits& bits) const {
    if (format == WGPUTextureFormat_RGB9E5Ufloat) {
        const uint32_t exponent = (bits.values[0] >> 9) & 0x1fu;
        const uint32_t word = (bits.values[0] & 0x1ffu)
            | ((bits.values[1] & 0x1ffu) << 9)
            | ((bits.values[2] & 0x1ffu) << 18)
            | (exponent << 27);
        return {
            static_cast<uint8_t>(word & 0xffu),
            static_cast<uint8_t>((word >> 8) & 0xffu),
            static_cast<uint8_t>((word >> 16) & 0xffu),
            static_cast<uint8_t>((word >> 24) & 0xffu),
        };
    }

    if (componentsAreByteAligned(*this)) {
        std::vector<uint8_t> bytes(bytesPerBlock);
        uint32_t byteOffset = 0;
        for (TexelComponent component : componentOrder) {
            const uint32_t index = componentIndex(component);
            const uint32_t byteLength = bitLengths[index] / 8;
            const uint32_t value = bits.values[index];
            for (uint32_t i = 0; i < byteLength; ++i) {
                bytes[byteOffset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xffu);
            }
            byteOffset += byteLength;
        }
        return bytes;
    }

    uint64_t word = 0;
    uint32_t bitOffset = 0;
    for (TexelComponent component : componentOrder) {
        const uint32_t index = componentIndex(component);
        const uint32_t bitLength = bitLengths[index];
        word |= (static_cast<uint64_t>(bits.values[index]) & bitMask(bitLength)) << bitOffset;
        bitOffset += bitLength;
    }
    std::vector<uint8_t> bytes(bytesPerBlock);
    for (uint32_t i = 0; i < bytesPerBlock; ++i) {
        bytes[i] = static_cast<uint8_t>((word >> (i * 8)) & 0xffu);
    }
    return bytes;
}

TexelBits TexelRepresentation::unpackBits(const uint8_t* data, size_t len) const {
    if (len < bytesPerBlock) {
        throw std::runtime_error("texel data too short");
    }

    TexelBits bits;
    if (format == WGPUTextureFormat_RGB9E5Ufloat) {
        uint32_t encoded = 0;
        for (uint32_t i = 0; i < bytesPerBlock; ++i) {
            encoded |= static_cast<uint32_t>(data[i]) << (i * 8);
        }
        const uint32_t exponent = (encoded >> 27) & 0x1fu;
        bits.values[0] = (exponent << 9) | (encoded & 0x1ffu);
        bits.values[1] = (exponent << 9) | ((encoded >> 9) & 0x1ffu);
        bits.values[2] = (exponent << 9) | ((encoded >> 18) & 0x1ffu);
        return bits;
    }

    if (componentsAreByteAligned(*this)) {
        uint32_t byteOffset = 0;
        for (TexelComponent component : componentOrder) {
            const uint32_t index = componentIndex(component);
            const uint32_t byteLength = bitLengths[index] / 8;
            uint32_t value = 0;
            for (uint32_t i = 0; i < byteLength; ++i) {
                value |= static_cast<uint32_t>(data[byteOffset + i]) << (i * 8);
            }
            bits.values[index] = value;
            byteOffset += byteLength;
        }
        return bits;
    }

    uint64_t word = 0;
    for (uint32_t i = 0; i < bytesPerBlock; ++i) {
        word |= static_cast<uint64_t>(data[i]) << (i * 8);
    }

    uint32_t bitOffset = 0;
    for (TexelComponent component : componentOrder) {
        const uint32_t index = componentIndex(component);
        const uint32_t bitLength = bitLengths[index];
        bits.values[index] = static_cast<uint32_t>((word >> bitOffset) & bitMask(bitLength));
        bitOffset += bitLength;
    }
    return bits;
}

TexelComponents TexelRepresentation::bitsToNumber(const TexelBits& bits) const {
    TexelComponents numbers;
    for (TexelComponent component : componentOrder) {
        const uint32_t index = componentIndex(component);
        const uint32_t bitLength = bitLengths[index];
        const uint32_t value = bits.values[index];
        switch (dataTypes[index]) {
            case ComponentDataType::Uint:
                numbers.values[index] = value;
                break;
            case ComponentDataType::Sint:
                numbers.values[index] = signExtend(value, bitLength);
                break;
            case ComponentDataType::Unorm: {
                const double decoded = normalizedIntegerAsFloat(static_cast<int32_t>(value), bitLength, false);
                const bool srgb = format == WGPUTextureFormat_RGBA8UnormSrgb || format == WGPUTextureFormat_BGRA8UnormSrgb;
                numbers.values[index] = srgb && component != TexelComponent::A ? gammaDecompress(decoded) : decoded;
                break;
            }
            case ComponentDataType::Snorm:
                numbers.values[index] = normalizedIntegerAsFloat(signExtend(value, bitLength), bitLength, true);
                break;
            case ComponentDataType::Float:
                numbers.values[index] = bitLength == 16 ? floatBitsToNumber(value, 1, 5, 10, 15) : f32FromBits(value);
                break;
            case ComponentDataType::Ufloat:
                if (format == WGPUTextureFormat_RG11B10Ufloat) {
                    numbers.values[index] = bitLength == 11
                        ? floatBitsToNumber(value, 0, 5, 6, 15)
                        : floatBitsToNumber(value, 0, 5, 5, 15);
                } else {
                    numbers.values[index] = rgb9e5BitsToNumber(value);
                }
                break;
        }
    }
    return numbers;
}

TexelBits TexelRepresentation::numberToBits(const TexelComponents& numbers) const {
    TexelBits bits;
    if (format == WGPUTextureFormat_RGB9E5Ufloat) {
        const uint32_t encoded = packRGB9E5UFloat(numbers.values[0], numbers.values[1], numbers.values[2]);
        const uint32_t exponent = (encoded >> 27u) & 0x1fu;
        bits.values[0] = (exponent << 9u) | (encoded & 0x1ffu);
        bits.values[1] = (exponent << 9u) | ((encoded >> 9u) & 0x1ffu);
        bits.values[2] = (exponent << 9u) | ((encoded >> 18u) & 0x1ffu);
        return bits;
    }

    for (TexelComponent component : componentOrder) {
        const uint32_t index = componentIndex(component);
        const uint32_t bitLength = bitLengths[index];
        double value = numbers.values[index];
        switch (dataTypes[index]) {
            case ComponentDataType::Uint:
            case ComponentDataType::Sint:
                bits.values[index] = static_cast<uint32_t>(static_cast<int64_t>(value)) & bitMask(bitLength);
                break;
            case ComponentDataType::Unorm:
                if ((format == WGPUTextureFormat_RGBA8UnormSrgb || format == WGPUTextureFormat_BGRA8UnormSrgb)
                    && component != TexelComponent::A) {
                    value = gammaCompress(value);
                }
                bits.values[index] = floatAsNormalizedInteger(value, bitLength, false);
                break;
            case ComponentDataType::Snorm:
                bits.values[index] = floatAsNormalizedInteger(value, bitLength, true);
                break;
            case ComponentDataType::Float:
                bits.values[index] = bitLength == 16 ? numberToFloatBits(value, 1, 5, 10, 15) : f32ToBits(value);
                break;
            case ComponentDataType::Ufloat:
                if (format == WGPUTextureFormat_RG11B10Ufloat) {
                    bits.values[index] = bitLength == 11
                        ? numberToFloatBits(value, 0, 5, 6, 15)
                        : numberToFloatBits(value, 0, 5, 5, 15);
                } else {
                    bits.values[index] = numberToFloatBits(value, 0, 5, 9, 15);
                }
                break;
        }
    }
    return bits;
}

int64_t TexelRepresentation::ulpFromZero(uint32_t index, double value) const {
    const uint32_t bitLength = bitLengths[index];
    switch (dataTypes[index]) {
        case ComponentDataType::Uint:
            // numberToBits is the masked integer value; bitsToULPFromZero is identity.
            return static_cast<int64_t>(static_cast<uint32_t>(static_cast<int64_t>(value)) & bitMask(bitLength));
        case ComponentDataType::Sint:
            // numberToBits masks; bitsToULPFromZero sign-extends back.
            return signExtend(static_cast<uint32_t>(static_cast<int64_t>(value)) & bitMask(bitLength), bitLength);
        case ComponentDataType::Unorm: {
            double v = value;
            if ((format == WGPUTextureFormat_RGBA8UnormSrgb || format == WGPUTextureFormat_BGRA8UnormSrgb)
                && index != componentIndex(TexelComponent::A)) {
                v = gammaCompress(v);
            }
            // Unsigned-normalized ULP space is the encoded integer itself.
            return floatAsNormalizedInteger(v, bitLength, false);
        }
        case ComponentDataType::Snorm: {
            // Signed-normalized: sign-extend and clamp -max-1 to -max (both encode -1.0).
            const int64_t maxValue = (1 << (bitLength - 1)) - 1;
            const uint32_t bits = floatAsNormalizedInteger(value, bitLength, true);
            return std::max<int64_t>(-maxValue, signExtend(bits, bitLength));
        }
        case ComponentDataType::Float:
            return bitLength == 16
                ? floatBitsToNormalULPFromZero(numberToFloatBits(value, 1, 5, 10, 15), 1, 5, 10)
                : floatBitsToNormalULPFromZero(f32ToBits(value), 1, 8, 23);
        case ComponentDataType::Ufloat:
            if (format == WGPUTextureFormat_RG11B10Ufloat) {
                return bitLength == 11
                    ? floatBitsToNormalULPFromZero(numberToFloatBits(value, 0, 5, 6, 15), 0, 5, 6)
                    : floatBitsToNormalULPFromZero(numberToFloatBits(value, 0, 5, 5, 15), 0, 5, 5);
            }
            // rgb9e5ufloat: upstream numberToBits encodes each component independently as
            // a 5-exponent/9-mantissa ufloat (no shared exponent), so mirror that here
            // rather than reusing this struct's shared-exponent numberToBits().
            return floatBitsToNormalULPFromZero(numberToFloatBits(value, 0, 5, 9, 15), 0, 5, 9);
    }
    return 0;
}

const TexelRepresentation& texelRepresentation(WGPUTextureFormat format) {
    static const std::vector<TexelRepresentation> reprs = {
        makeSimple(WGPUTextureFormat_R8Unorm, {TexelComponent::R}, 8, ComponentDataType::Unorm, 1),
        makeSimple(WGPUTextureFormat_R8Snorm, {TexelComponent::R}, 8, ComponentDataType::Snorm, 1),
        makeSimple(WGPUTextureFormat_R8Uint, {TexelComponent::R}, 8, ComponentDataType::Uint, 1),
        makeSimple(WGPUTextureFormat_R8Sint, {TexelComponent::R}, 8, ComponentDataType::Sint, 1),
        makeSimple(WGPUTextureFormat_RG8Unorm, {TexelComponent::R, TexelComponent::G}, 8, ComponentDataType::Unorm, 2),
        makeSimple(WGPUTextureFormat_RG8Snorm, {TexelComponent::R, TexelComponent::G}, 8, ComponentDataType::Snorm, 2),
        makeSimple(WGPUTextureFormat_RG8Uint, {TexelComponent::R, TexelComponent::G}, 8, ComponentDataType::Uint, 2),
        makeSimple(WGPUTextureFormat_RG8Sint, {TexelComponent::R, TexelComponent::G}, 8, ComponentDataType::Sint, 2),
        makeSimple(WGPUTextureFormat_RGBA8Unorm, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 8, ComponentDataType::Unorm, 4),
        makeSimple(WGPUTextureFormat_RGBA8UnormSrgb, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 8, ComponentDataType::Unorm, 4),
        makeSimple(WGPUTextureFormat_RGBA8Snorm, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 8, ComponentDataType::Snorm, 4),
        makeSimple(WGPUTextureFormat_RGBA8Uint, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 8, ComponentDataType::Uint, 4),
        makeSimple(WGPUTextureFormat_RGBA8Sint, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 8, ComponentDataType::Sint, 4),
        makeSimple(WGPUTextureFormat_BGRA8Unorm, {TexelComponent::B, TexelComponent::G, TexelComponent::R, TexelComponent::A}, 8, ComponentDataType::Unorm, 4),
        makeSimple(WGPUTextureFormat_BGRA8UnormSrgb, {TexelComponent::B, TexelComponent::G, TexelComponent::R, TexelComponent::A}, 8, ComponentDataType::Unorm, 4),
        makeSimple(WGPUTextureFormat_R16Unorm, {TexelComponent::R}, 16, ComponentDataType::Unorm, 2),
        makeSimple(WGPUTextureFormat_R16Snorm, {TexelComponent::R}, 16, ComponentDataType::Snorm, 2),
        makeSimple(WGPUTextureFormat_R16Uint, {TexelComponent::R}, 16, ComponentDataType::Uint, 2),
        makeSimple(WGPUTextureFormat_R16Sint, {TexelComponent::R}, 16, ComponentDataType::Sint, 2),
        makeSimple(WGPUTextureFormat_R16Float, {TexelComponent::R}, 16, ComponentDataType::Float, 2),
        makeSimple(WGPUTextureFormat_RG16Unorm, {TexelComponent::R, TexelComponent::G}, 16, ComponentDataType::Unorm, 4),
        makeSimple(WGPUTextureFormat_RG16Snorm, {TexelComponent::R, TexelComponent::G}, 16, ComponentDataType::Snorm, 4),
        makeSimple(WGPUTextureFormat_RG16Uint, {TexelComponent::R, TexelComponent::G}, 16, ComponentDataType::Uint, 4),
        makeSimple(WGPUTextureFormat_RG16Sint, {TexelComponent::R, TexelComponent::G}, 16, ComponentDataType::Sint, 4),
        makeSimple(WGPUTextureFormat_RG16Float, {TexelComponent::R, TexelComponent::G}, 16, ComponentDataType::Float, 4),
        makeSimple(WGPUTextureFormat_RGBA16Unorm, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 16, ComponentDataType::Unorm, 8),
        makeSimple(WGPUTextureFormat_RGBA16Snorm, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 16, ComponentDataType::Snorm, 8),
        makeSimple(WGPUTextureFormat_RGBA16Uint, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 16, ComponentDataType::Uint, 8),
        makeSimple(WGPUTextureFormat_RGBA16Sint, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 16, ComponentDataType::Sint, 8),
        makeSimple(WGPUTextureFormat_RGBA16Float, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 16, ComponentDataType::Float, 8),
        makeSimple(WGPUTextureFormat_R32Uint, {TexelComponent::R}, 32, ComponentDataType::Uint, 4),
        makeSimple(WGPUTextureFormat_R32Sint, {TexelComponent::R}, 32, ComponentDataType::Sint, 4),
        makeSimple(WGPUTextureFormat_R32Float, {TexelComponent::R}, 32, ComponentDataType::Float, 4),
        makeSimple(WGPUTextureFormat_RG32Uint, {TexelComponent::R, TexelComponent::G}, 32, ComponentDataType::Uint, 8),
        makeSimple(WGPUTextureFormat_RG32Sint, {TexelComponent::R, TexelComponent::G}, 32, ComponentDataType::Sint, 8),
        makeSimple(WGPUTextureFormat_RG32Float, {TexelComponent::R, TexelComponent::G}, 32, ComponentDataType::Float, 8),
        makeSimple(WGPUTextureFormat_RGBA32Uint, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 32, ComponentDataType::Uint, 16),
        makeSimple(WGPUTextureFormat_RGBA32Sint, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 32, ComponentDataType::Sint, 16),
        makeSimple(WGPUTextureFormat_RGBA32Float, {TexelComponent::R, TexelComponent::G, TexelComponent::B, TexelComponent::A}, 32, ComponentDataType::Float, 16),
        makePackedRgb10a2(WGPUTextureFormat_RGB10A2Uint, ComponentDataType::Uint),
        makePackedRgb10a2(WGPUTextureFormat_RGB10A2Unorm, ComponentDataType::Unorm),
        makeRg11b10(),
        makeRgb9e5(),
    };

    for (const TexelRepresentation& repr : reprs) {
        if (repr.format == format) {
            return repr;
        }
    }
    throw std::runtime_error("unsupported texel representation");
}

bool texelComponentEqual(double actual, double expected, double maxDiff) {
    return (std::isnan(actual) && std::isnan(expected))
        || (actual == expected)
        || std::fabs(actual - expected) <= maxDiff;
}

} // namespace cts
