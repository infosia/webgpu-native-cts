// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/texture_utils.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_layout.h"

namespace cts::texture_utils {
namespace {

constexpr uint32_t kCallCount = 50;
constexpr uint32_t kMipLevelCount = 3;
constexpr uint32_t kMipLevelWeightSteps = 64;

std::vector<Value> values(std::initializer_list<const char*> input) {
    std::vector<Value> out;
    out.reserve(input.size());
    for (const char* value : input) {
        out.emplace_back(value);
    }
    return out;
}

std::vector<Value> formatValues(const auto& formats) {
    std::vector<Value> out;
    out.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        out.emplace_back(std::string(textureFormatInfo(format).identifier));
    }
    return out;
}

std::string paramString(const ParamRecord& record, std::string_view key) {
    const Value* value = findParam(record, key);
    return value == nullptr ? std::string() : valueAs<std::string>(*value);
}

WGPUTextureFormat paramFormat(const ParamRecord& record) {
    return parseTextureFormat(paramString(record, "format"));
}

bool isCompressedFloatFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_BC6HRGBUfloat || format == WGPUTextureFormat_BC6HRGBFloat;
}

bool isDepthFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).hasDepth;
}

bool isStencilOnlyFormat(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    return info.hasStencil && !info.hasDepth;
}

bool isUncompressed(WGPUTextureFormat format) {
    return textureFormatInfo(format).formatClass == TextureFormatClass::Uncompressed;
}

bool isColorFloatLikeFormat(WGPUTextureFormat format) {
    if (isDepthFormat(format) || isStencilOnlyFormat(format)) {
        return false;
    }
    const std::string_view id = textureFormatInfo(format).identifier;
    return id.find("uint") == std::string_view::npos && id.find("sint") == std::string_view::npos;
}

bool isPossiblyFilterableAsTextureF32(WGPUTextureFormat format) {
    if (isDepthFormat(format)) {
        return true;
    }
    if (!isColorFloatLikeFormat(format)) {
        return false;
    }
    return std::string_view(textureFormatInfo(format).identifier).find("32float") == std::string_view::npos;
}

bool containsFormatToken(WGPUTextureFormat format, std::string_view token) {
    return std::string_view(textureFormatInfo(format).identifier).find(token) != std::string_view::npos;
}

bool endsWithFormatToken(WGPUTextureFormat format, std::string_view token) {
    const std::string_view id = textureFormatInfo(format).identifier;
    return id.size() >= token.size() && id.substr(id.size() - token.size()) == token;
}

double maxFractionalDiffForTextureFormat(WGPUTextureFormat format) {
    if (isCompressedTextureFormat(format)) {
        if (containsFormatToken(format, "snorm")) {
            return 20.0 / 128.0;
        }
        if (containsFormatToken(format, "unorm")) {
            return 20.0 / 255.0;
        }
    }
    if (containsFormatToken(format, "depth")) {
        return 3.0 / 100.0;
    }
    if (containsFormatToken(format, "8unorm")) {
        return 7.0 / 255.0;
    }
    if (containsFormatToken(format, "2unorm")) {
        return 13.0 / 512.0;
    }
    if (containsFormatToken(format, "unorm")) {
        return 7.0 / 255.0;
    }
    if (containsFormatToken(format, "8snorm")) {
        return 7.9 / 128.0;
    }
    if (containsFormatToken(format, "snorm")) {
        return 7.9 / 128.0;
    }
    if (endsWithFormatToken(format, "ufloat")) {
        return 156.249;
    }
    if (endsWithFormatToken(format, "float")) {
        return 44.0;
    }
    return 0.0;
}

double comparisonToleranceForFormat(WGPUTextureFormat format, std::string_view filter) {
    constexpr double kIncrement9Tolerance = 0.035;
    if (isCompressedTextureFormat(format) && filter == "linear") {
        return maxFractionalDiffForTextureFormat(format);
    }
    return kIncrement9Tolerance;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

uint32_t alignToU32(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1u) / alignment) * alignment;
}

uint32_t gcdU32(uint32_t a, uint32_t b) {
    while (b != 0) {
        const uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

uint32_t lcmU32(uint32_t a, uint32_t b) {
    return a / gcdU32(a, b) * b;
}

WGPUAddressMode addressModeFromShort(const std::string& mode) {
    if (mode == "repeat") {
        return WGPUAddressMode_Repeat;
    }
    if (mode == "mirror") {
        return WGPUAddressMode_MirrorRepeat;
    }
    return WGPUAddressMode_ClampToEdge;
}

WGPUFilterMode filterModeFromString(const std::string& mode) {
    return mode == "linear" ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
}

WGPUSamplerBindingType samplerBindingType(bool isDepth, const std::string& filt, bool comparisonSampler) {
    if (comparisonSampler) {
        return WGPUSamplerBindingType_Comparison;
    }
    if (isDepth) {
        return WGPUSamplerBindingType_NonFiltering;
    }
    return filt == "linear" ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
}

WGPUTextureSampleType textureSampleType(bool isDepth, const std::string& filt) {
    if (isDepth) {
        return WGPUTextureSampleType_Depth;
    }
    return filt == "linear" ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
}

WGPUCompareFunction compareFunctionFromString(const std::string& compare) {
    if (compare == "never") return WGPUCompareFunction_Never;
    if (compare == "less") return WGPUCompareFunction_Less;
    if (compare == "equal") return WGPUCompareFunction_Equal;
    if (compare == "less-equal") return WGPUCompareFunction_LessEqual;
    if (compare == "greater") return WGPUCompareFunction_Greater;
    if (compare == "not-equal") return WGPUCompareFunction_NotEqual;
    if (compare == "greater-equal") return WGPUCompareFunction_GreaterEqual;
    if (compare == "always") return WGPUCompareFunction_Always;
    return WGPUCompareFunction_Undefined;
}

WGPUShaderStage stageVisibility(const std::string& stage) {
    if (stage == "compute") {
        return WGPUShaderStage_Compute;
    }
    if (stage == "vertex") {
        return WGPUShaderStage_Vertex;
    }
    return WGPUShaderStage_Fragment;
}

uint32_t hashU32(std::initializer_list<uint32_t> valuesIn) {
    uint32_t h = 2166136261u;
    for (uint32_t v : valuesIn) {
        h ^= v;
        h *= 16777619u;
    }
    return h;
}

double implicitMipLevelForCall(uint32_t index, uint32_t mipLevelCount) {
    if (mipLevelCount <= 1) {
        return 0.0;
    }
    static constexpr std::array<double, 6> kLevels = {{0.20, 0.80, 1.20, 1.80, 0.35, 1.65}};
    return std::min(kLevels[index % kLevels.size()], static_cast<double>(mipLevelCount - 1u));
}

double rand01(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return static_cast<double>(hashU32({a, b, c, d})) / 4294967296.0;
}

double lerp(double a, double b, double t) {
    return a * (1.0 - t) + b * t;
}

double channelValue(WGPUTextureFormat format, uint32_t x, uint32_t y, uint32_t z, uint32_t mip, uint32_t channel) {
    const std::string_view id = textureFormatInfo(format).identifier;
    const bool signedFormat = id.find("snorm") != std::string_view::npos;
    const double v = rand01(x + 1u + z * 13u, y + 3u, mip + 5u, channel + 7u);
    return signedFormat ? v * 2.0 - 1.0 : v;
}

double depthValue(uint32_t mip, uint32_t layer) {
    return 0.125 + 0.125 * static_cast<double>((mip * 7u + layer * 3u) % 6u);
}

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SampleCall {
    Vec3 coords;
    double level = 0.0;
    Vec3 derivativeMult{1.0, 0.0, 0.0};
    Vec3 ddx{0.0, 0.0, 0.0};
    Vec3 ddy{0.0, 0.0, 0.0};
    double bias = 0.0;
    double depthRef = 0.0;
    std::optional<std::array<int32_t, 3>> offset;
    std::optional<uint32_t> arrayIndex;
};

struct MipData {
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUExtent3D size = WGPUExtent3D{1, 1, 1};
    TextureCopyLayout layout;
    std::vector<uint8_t> data;
};

struct MipMixWeights {
    std::array<double, kMipLevelWeightSteps + 1> nearest = {};
    std::array<double, kMipLevelWeightSteps + 1> linear = {};
};

struct SampleParamsData {
    std::array<float, 4> coords = {};
    std::array<float, 4> ddx = {};
    std::array<float, 4> ddy = {};
    std::array<float, 4> derivativeMult = {};
    std::array<float, 4> scalars = {};
    std::array<uint32_t, 4> indices = {};
};

static_assert(sizeof(SampleParamsData) == 96);

enum class SampleKind {
    Sampled1D,
    Sampled2D,
    Sampled2DLodClamp,
    Sampled2DArray,
    Sampled3D,
    Sampled3DLodClamp,
    SampledCube,
    SampledCubeLodClamp,
    SampledCubeArray,
    Depth2D,
    Depth2DArray,
    DepthCube,
    DepthCubeArray,
};

struct TextureCase {
    SampleKind kind = SampleKind::Sampled2D;
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTextureDimension textureDimension = WGPUTextureDimension_2D;
    WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D;
    WGPUExtent3D baseSize = WGPUExtent3D{8, 8, 1};
    uint32_t mipLevelCount = kMipLevelCount;
    uint32_t baseMipLevel = 0;
    double lodMinClamp = 0.0;
    double lodMaxClamp = 32.0;
    bool isDepth = false;
    bool useOffset = false;
    bool useArrayIndex = false;
    std::string arrayIndexType = "i32";
    std::string levelType = "f32";
    std::string builtin = "textureSampleLevel";
    std::string stage = "compute";
    std::string filt = "nearest";
    std::string samplePoints = "texel-centre";
    std::string compare = "less";
    WGPUAddressMode modeU = WGPUAddressMode_ClampToEdge;
    WGPUAddressMode modeV = WGPUAddressMode_ClampToEdge;
    WGPUAddressMode modeW = WGPUAddressMode_ClampToEdge;
    bool comparisonSampler = false;
    bool baseClampToEdge = false;
};

bool isCubeKind(SampleKind kind) {
    return kind == SampleKind::SampledCube || kind == SampleKind::SampledCubeLodClamp
        || kind == SampleKind::SampledCubeArray || kind == SampleKind::DepthCube
        || kind == SampleKind::DepthCubeArray;
}

uint32_t componentCount(WGPUTextureFormat format) {
    const TexelRepresentation& repr = texelRepresentation(format);
    return static_cast<uint32_t>(repr.componentOrder.size());
}

std::vector<MipData> makeTextureData(
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevelCount,
    bool constantPerMip) {
    std::vector<MipData> mips;
    mips.reserve(mipLevelCount);
    const TexelRepresentation& repr = texelRepresentation(format);
    const uint32_t components = componentCount(format);
    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        MipData mipData;
        mipData.format = format;
        mipData.layout = getTextureCopyLayout(format, dimension, baseSize, mip);
        mipData.size = mipData.layout.mipSize;
        mipData.data.assign(static_cast<size_t>(mipData.layout.byteLength), 0);
        for (uint32_t z = 0; z < mipData.size.depthOrArrayLayers; ++z) {
            for (uint32_t y = 0; y < mipData.size.height; ++y) {
                for (uint32_t x = 0; x < mipData.size.width; ++x) {
                    TexelComponents comps;
                    for (uint32_t c = 0; c < 4; ++c) {
                        comps.values[c] = constantPerMip ? channelValue(format, 0, 0, 0, mip, c)
                                                         : channelValue(format, x, y, z, mip, c);
                    }
                    if (components < 4) {
                        comps.values[3] = 1.0;
                    }
                    const std::vector<uint8_t> bytes = repr.packBits(repr.numberToBits(comps));
                    const uint64_t offset = (static_cast<uint64_t>(z) * mipData.layout.rowsPerImage
                                                + static_cast<uint64_t>(y))
                            * mipData.layout.bytesPerRow
                        + static_cast<uint64_t>(x) * repr.bytesPerBlock;
                    std::memcpy(mipData.data.data() + offset, bytes.data(), bytes.size());
                }
            }
        }
        mips.push_back(std::move(mipData));
    }
    return mips;
}

WGPUExtent3D adjustedBaseSizeForFormat(const TextureCase& c) {
    WGPUExtent3D size = c.baseSize;
    if (!isCompressedTextureFormat(c.format)) {
        return size;
    }
    const TextureBlockInfo info = getBlockInfoForTextureFormat(c.format);
    // Keep all three tested mip levels block-aligned. For cube/cube-array,
    // the square side must satisfy both block dimensions.
    const uint32_t widthMultiple = info.blockWidth << (c.mipLevelCount - 1u);
    const uint32_t heightMultiple = info.blockHeight << (c.mipLevelCount - 1u);
    const uint32_t squareMultiple = lcmU32(widthMultiple, heightMultiple);
    size.width = alignToU32(std::max(size.width, widthMultiple), widthMultiple);
    if (c.textureDimension != WGPUTextureDimension_1D) {
        size.height = alignToU32(std::max(size.height, heightMultiple), heightMultiple);
    }
    if (c.viewDimension == WGPUTextureViewDimension_Cube || c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        const uint32_t largest = std::max(size.width, size.height);
        const uint32_t side = alignToU32(largest, squareMultiple);
        size.width = side;
        size.height = side;
    }
    return size;
}

std::vector<MipData> makeCompressedTextureData(WGPUTextureFormat format, WGPUTextureDimension dimension, WGPUExtent3D baseSize, uint32_t mipLevelCount) {
    std::vector<MipData> mips;
    mips.reserve(mipLevelCount);
    const bool astc = isASTCTextureFormat(format);
    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        MipData mipData;
        mipData.format = format;
        mipData.layout = getTextureCopyLayout(format, dimension, baseSize, mip);
        mipData.size = mipData.layout.mipSize;
        mipData.data.assign(static_cast<size_t>(mipData.layout.byteLength), 0);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        const uint32_t blocksAcross = std::max(1u, (mipData.size.width + info.blockWidth - 1u) / info.blockWidth);
        const uint32_t blocksDown = std::max(1u, (mipData.size.height + info.blockHeight - 1u) / info.blockHeight);
        for (uint32_t z = 0; z < mipData.size.depthOrArrayLayers; ++z) {
            for (uint32_t by = 0; by < blocksDown; ++by) {
                for (uint32_t bx = 0; bx < blocksAcross; ++bx) {
                    const size_t offset = (static_cast<size_t>(z) * mipData.layout.rowsPerImage + by)
                            * mipData.layout.bytesPerRow
                        + static_cast<size_t>(bx) * info.bytesPerBlock;
                    uint32_t byte = 0;
                    if (astc) {
                        static constexpr std::array<uint8_t, 8> kAstcVoidExtentHeader = {{0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
                        for (; byte < kAstcVoidExtentHeader.size(); ++byte) {
                            mipData.data[offset + byte] = kAstcVoidExtentHeader[byte];
                        }
                    }
                    for (; byte < info.bytesPerBlock; ++byte) {
                        mipData.data[offset + byte] = static_cast<uint8_t>(hashU32({bx, by, z + mip * 17u, byte + static_cast<uint32_t>(format)}));
                    }
                }
            }
        }
        mips.push_back(std::move(mipData));
    }
    return mips;
}

TexelComponents texelAt(const MipData& mip, WGPUTextureFormat format, int32_t x, int32_t y, int32_t z) {
    const int32_t maxX = static_cast<int32_t>(mip.size.width) - 1;
    const int32_t maxY = static_cast<int32_t>(mip.size.height) - 1;
    const int32_t maxZ = static_cast<int32_t>(mip.size.depthOrArrayLayers) - 1;
    const uint32_t cx = static_cast<uint32_t>(std::clamp(x, 0, maxX));
    const uint32_t cy = static_cast<uint32_t>(std::clamp(y, 0, maxY));
    const uint32_t cz = static_cast<uint32_t>(std::clamp(z, 0, maxZ));
    TexelViewConfig config;
    config.bytesPerRow = mip.layout.bytesPerRow;
    config.rowsPerImage = mip.layout.rowsPerImage;
    config.subrectSize = mip.size;
    return TexelView::fromTextureDataByReference(format, mip.data.data(), mip.data.size(), config).color(cx, cy, cz);
}

std::array<double, 4> resultTexel(TexelComponents comps, WGPUTextureFormat format) {
    std::array<double, 4> out = {0.0, 0.0, 0.0, 1.0};
    for (TexelComponent component : texelRepresentation(format).componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        out[index] = comps.values[index];
    }
    return out;
}

std::array<double, 4> mixTexel(const std::array<double, 4>& a, const std::array<double, 4>& b, double t) {
    return {
        lerp(a[0], b[0], t),
        lerp(a[1], b[1], t),
        lerp(a[2], b[2], t),
        lerp(a[3], b[3], t),
    };
}

double applyAddressToTexelCoord(double coord, uint32_t size, WGPUAddressMode mode) {
    const double s = static_cast<double>(size);
    switch (mode) {
        case WGPUAddressMode_ClampToEdge:
            return std::clamp(coord, 0.0, s - 1.0);
        case WGPUAddressMode_Repeat:
            return coord - std::floor(coord / s) * s;
        case WGPUAddressMode_MirrorRepeat: {
            const int32_t n = static_cast<int32_t>(std::floor(coord / s));
            const double v = coord - static_cast<double>(n) * s;
            return (n & 1) != 0 ? s - v - 1.0 : v;
        }
        default:
            return std::clamp(coord, 0.0, s - 1.0);
    }
}

double calibratedMixWeight(const std::array<double, kMipLevelWeightSteps + 1>& weights, double mipLevel) {
    const double frac = mipLevel - std::floor(mipLevel);
    const double scaled = frac * static_cast<double>(kMipLevelWeightSteps);
    const uint32_t lower = static_cast<uint32_t>(std::floor(scaled));
    const uint32_t upper = static_cast<uint32_t>(std::ceil(scaled));
    return lerp(weights[lower], weights[upper], scaled - std::floor(scaled));
}

Vec3 normalizedCubeToFaceCoord(Vec3 coord) {
    const double len = std::sqrt(coord.x * coord.x + coord.y * coord.y + coord.z * coord.z);
    const Vec3 r = len > 0.0 ? Vec3{coord.x / len, coord.y / len, coord.z / len} : Vec3{1.0, 0.0, 0.0};
    const double ax = std::abs(r.x);
    const double ay = std::abs(r.y);
    const double az = std::abs(r.z);
    Vec3 out;
    uint32_t layer = 0;
    if (ax > ay && ax > az) {
        const bool negX = r.x < 0.0;
        out.x = negX ? r.z : -r.z;
        out.y = -r.y;
        out.z = ax;
        layer = negX ? 1u : 0u;
    } else if (ay > az) {
        const bool negY = r.y < 0.0;
        out.x = r.x;
        out.y = negY ? -r.z : r.z;
        out.z = ay;
        layer = negY ? 3u : 2u;
    } else {
        const bool negZ = r.z < 0.0;
        out.x = negZ ? -r.x : r.x;
        out.y = -r.y;
        out.z = az;
        layer = negZ ? 5u : 4u;
    }
    return Vec3{(out.x / out.z + 1.0) * 0.5, (out.y / out.z + 1.0) * 0.5, static_cast<double>(layer)};
}

Vec3 normalized3DTextureCoordToCubeCoord(Vec3 uvLayer) {
    static constexpr std::array<std::array<double, 9>, 6> kFaceUVMatrices = {{
        {{0.0, 0.0, -2.0, 0.0, -2.0, 0.0, 1.0, 1.0, 1.0}},
        {{0.0, 0.0, 2.0, 0.0, -2.0, 0.0, -1.0, 1.0, -1.0}},
        {{2.0, 0.0, 0.0, 0.0, 0.0, 2.0, -1.0, 1.0, -1.0}},
        {{2.0, 0.0, 0.0, 0.0, 0.0, -2.0, -1.0, -1.0, 1.0}},
        {{2.0, 0.0, 0.0, 0.0, -2.0, 0.0, -1.0, 1.0, 1.0}},
        {{-2.0, 0.0, 0.0, 0.0, -2.0, 0.0, 1.0, 1.0, -1.0}},
    }};
    const uint32_t face = std::min(5u, static_cast<uint32_t>(std::floor(uvLayer.z * 6.0)));
    const auto& m = kFaceUVMatrices[face];
    Vec3 out{
        uvLayer.x * m[0] + uvLayer.y * m[3] + m[6],
        uvLayer.x * m[1] + uvLayer.y * m[4] + m[7],
        uvLayer.x * m[2] + uvLayer.y * m[5] + m[8],
    };
    const double len = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
    return len > 0.0 ? Vec3{out.x / len, out.y / len, out.z / len} : Vec3{1.0, 0.0, 0.0};
}

std::array<int32_t, 3> wrapFaceCoordToCubeFaceAtEdgeBoundaries(uint32_t mipLevelSize, int32_t x, int32_t y, int32_t z) {
    const int32_t arrayBase = (z / 6) * 6;
    const int32_t face = z - arrayBase;
    const Vec3 normalizedCoord{
        (static_cast<double>(x) + 0.5) / static_cast<double>(mipLevelSize),
        (static_cast<double>(y) + 0.5) / static_cast<double>(mipLevelSize),
        (static_cast<double>(face) + 0.5) / 6.0,
    };
    const Vec3 cubeCoord = normalized3DTextureCoordToCubeCoord(normalizedCoord);
    const Vec3 faceCoord = normalizedCubeToFaceCoord(cubeCoord);
    return {
        static_cast<int32_t>(std::floor(faceCoord.x * static_cast<double>(mipLevelSize))),
        static_cast<int32_t>(std::floor(faceCoord.y * static_cast<double>(mipLevelSize))),
        arrayBase + static_cast<int32_t>(std::floor((faceCoord.z + 0.5) / 6.0 * 6.0)),
    };
}

std::array<double, 4> loadAddressed(
    const MipData& mip,
    WGPUTextureFormat format,
    int32_t x,
    int32_t y,
    int32_t z,
    const TextureCase& c) {
    if (isCubeKind(c.kind)) {
        const std::array<int32_t, 3> wrapped = wrapFaceCoordToCubeFaceAtEdgeBoundaries(mip.size.width, x, y, z);
        x = wrapped[0];
        y = wrapped[1];
        z = wrapped[2];
    } else {
        const WGPUAddressMode modeU = c.baseClampToEdge ? WGPUAddressMode_ClampToEdge : c.modeU;
        const WGPUAddressMode modeV = c.baseClampToEdge ? WGPUAddressMode_ClampToEdge : c.modeV;
        const WGPUAddressMode modeW = c.baseClampToEdge ? WGPUAddressMode_ClampToEdge : c.modeW;
        x = static_cast<int32_t>(std::floor(applyAddressToTexelCoord(static_cast<double>(x), mip.size.width, modeU)));
        y = static_cast<int32_t>(std::floor(applyAddressToTexelCoord(static_cast<double>(y), mip.size.height, modeV)));
        if (c.textureDimension == WGPUTextureDimension_3D) {
            z = static_cast<int32_t>(std::floor(applyAddressToTexelCoord(static_cast<double>(z), mip.size.depthOrArrayLayers, modeW)));
        }
    }
    const WGPUTextureFormat texelFormat = mip.format == WGPUTextureFormat_Undefined ? format : mip.format;
    return resultTexel(texelAt(mip, texelFormat, x, y, z), texelFormat);
}

std::array<double, 4> sampleColorOneMip(const MipData& mip, WGPUTextureFormat format, SampleCall call, const TextureCase& c) {
    if (isCubeKind(c.kind)) {
        const Vec3 faceCoord = normalizedCubeToFaceCoord(call.coords);
        const uint32_t arrayBase = c.useArrayIndex ? (*call.arrayIndex) * 6u : 0u;
        call.coords = Vec3{faceCoord.x, faceCoord.y, 0.0};
        call.arrayIndex = arrayBase + static_cast<uint32_t>(faceCoord.z);
    }

    const int32_t ox = call.offset ? (*call.offset)[0] : 0;
    const int32_t oy = call.offset ? (*call.offset)[1] : 0;
    const int32_t oz = call.offset ? (*call.offset)[2] : 0;
    const int32_t layer = call.arrayIndex ? static_cast<int32_t>(*call.arrayIndex) : 0;

    if (filterModeFromString(c.filt) == WGPUFilterMode_Nearest) {
        const int32_t x = static_cast<int32_t>(std::floor(call.coords.x * static_cast<double>(mip.size.width))) + ox;
        const int32_t y = static_cast<int32_t>(std::floor(call.coords.y * static_cast<double>(mip.size.height))) + oy;
        const int32_t z = c.textureDimension == WGPUTextureDimension_3D
            ? static_cast<int32_t>(std::floor(call.coords.z * static_cast<double>(mip.size.depthOrArrayLayers))) + oz
            : layer;
        return loadAddressed(mip, format, x, y, z, c);
    }

    const double u = call.coords.x * static_cast<double>(mip.size.width) - 0.5 + static_cast<double>(ox);
    const double v = call.coords.y * static_cast<double>(mip.size.height) - 0.5 + static_cast<double>(oy);
    const int32_t x0 = static_cast<int32_t>(std::floor(u));
    const int32_t y0 = static_cast<int32_t>(std::floor(v));
    const double tx = u - std::floor(u);
    const double ty = v - std::floor(v);
    if (c.textureDimension != WGPUTextureDimension_3D) {
        const std::array<double, 4> c00 = loadAddressed(mip, format, x0, y0, layer, c);
        const std::array<double, 4> c10 = loadAddressed(mip, format, x0 + 1, y0, layer, c);
        const std::array<double, 4> c01 = loadAddressed(mip, format, x0, y0 + 1, layer, c);
        const std::array<double, 4> c11 = loadAddressed(mip, format, x0 + 1, y0 + 1, layer, c);
        return mixTexel(mixTexel(c00, c10, tx), mixTexel(c01, c11, tx), ty);
    }

    const double w = call.coords.z * static_cast<double>(mip.size.depthOrArrayLayers) - 0.5 + static_cast<double>(oz);
    const int32_t z0 = static_cast<int32_t>(std::floor(w));
    const double tz = w - std::floor(w);
    const std::array<double, 4> zA = mixTexel(
        mixTexel(loadAddressed(mip, format, x0, y0, z0, c), loadAddressed(mip, format, x0 + 1, y0, z0, c), tx),
        mixTexel(loadAddressed(mip, format, x0, y0 + 1, z0, c), loadAddressed(mip, format, x0 + 1, y0 + 1, z0, c), tx),
        ty);
    const std::array<double, 4> zB = mixTexel(
        mixTexel(loadAddressed(mip, format, x0, y0, z0 + 1, c), loadAddressed(mip, format, x0 + 1, y0, z0 + 1, c), tx),
        mixTexel(loadAddressed(mip, format, x0, y0 + 1, z0 + 1, c), loadAddressed(mip, format, x0 + 1, y0 + 1, z0 + 1, c), tx),
        ty);
    return mixTexel(zA, zB, tz);
}

std::array<double, 4> softwareSampleColor(
    const std::vector<MipData>& mips,
    WGPUTextureFormat format,
    const SampleCall& call,
    const TextureCase& c,
    const MipMixWeights& weights) {
    const double clampedLevel = std::clamp(call.level, c.lodMinClamp, c.lodMaxClamp);
    const double level = std::clamp(clampedLevel, 0.0, static_cast<double>(c.mipLevelCount - c.baseMipLevel - 1u));
    const uint32_t baseMip = c.baseMipLevel + static_cast<uint32_t>(std::floor(level));
    if (filterModeFromString(c.filt) == WGPUFilterMode_Nearest) {
        const double mix = calibratedMixWeight(weights.nearest, level);
        const uint32_t mip = std::min<uint32_t>(baseMip + (mix >= 0.5 ? 1u : 0u), c.mipLevelCount - 1u);
        return sampleColorOneMip(mips[mip], format, call, c);
    }
    const uint32_t mip0 = baseMip;
    const uint32_t mip1 = std::min<uint32_t>(baseMip + 1u, c.mipLevelCount - 1u);
    const double mix = calibratedMixWeight(weights.linear, level);
    return mixTexel(sampleColorOneMip(mips[mip0], format, call, c), sampleColorOneMip(mips[mip1], format, call, c), mix);
}

std::array<double, 4> softwareSampleDepth(const SampleCall& call, const TextureCase& c, const MipMixWeights& weights) {
    uint32_t layer = call.arrayIndex.value_or(0u);
    if (isCubeKind(c.kind)) {
        const Vec3 faceCoord = normalizedCubeToFaceCoord(call.coords);
        layer = (c.useArrayIndex ? (*call.arrayIndex) * 6u : 0u) + static_cast<uint32_t>(faceCoord.z);
    }
    const double clampedLevel = std::clamp(call.level, c.lodMinClamp, c.lodMaxClamp);
    const double level = std::clamp(clampedLevel, 0.0, static_cast<double>(c.mipLevelCount - 1u));
    const uint32_t baseMip = std::min<uint32_t>(static_cast<uint32_t>(std::floor(level)), c.mipLevelCount - 1u);
    const double mix = calibratedMixWeight(weights.nearest, level);
    const uint32_t mip = std::min<uint32_t>(baseMip + (mix >= 0.5 ? 1u : 0u), c.mipLevelCount - 1u);
    return {depthValue(mip, layer), 0.0, 0.0, 1.0};
}

double applyCompareToDepth(double src, WGPUCompareFunction compare, double ref) {
    switch (compare) {
        case WGPUCompareFunction_Never:
            return 0.0;
        case WGPUCompareFunction_Less:
            return ref < src ? 1.0 : 0.0;
        case WGPUCompareFunction_Equal:
            return ref == src ? 1.0 : 0.0;
        case WGPUCompareFunction_LessEqual:
            return ref <= src ? 1.0 : 0.0;
        case WGPUCompareFunction_Greater:
            return ref > src ? 1.0 : 0.0;
        case WGPUCompareFunction_NotEqual:
            return ref != src ? 1.0 : 0.0;
        case WGPUCompareFunction_GreaterEqual:
            return ref >= src ? 1.0 : 0.0;
        case WGPUCompareFunction_Always:
            return 1.0;
        default:
            return 0.0;
    }
}

double cubeDepthCompareTap(
    uint32_t mipLevel,
    uint32_t mipLevelSize,
    int32_t x,
    int32_t y,
    int32_t layer,
    WGPUCompareFunction compare,
    double depthRef) {
    const std::array<int32_t, 3> wrapped = wrapFaceCoordToCubeFaceAtEdgeBoundaries(mipLevelSize, x, y, layer);
    return applyCompareToDepth(depthValue(mipLevel, static_cast<uint32_t>(wrapped[2])), compare, depthRef);
}

std::array<double, 4> sampleDepthCompareCubeOneMip(
    const SampleCall& call,
    const TextureCase& c,
    uint32_t mipLevel,
    WGPUCompareFunction compare) {
    const WGPUExtent3D mipSize = physicalMipSize(c.baseSize, c.textureDimension, mipLevel);
    const Vec3 faceCoord = normalizedCubeToFaceCoord(call.coords);
    const int32_t arrayBase = c.useArrayIndex ? static_cast<int32_t>(*call.arrayIndex) * 6 : 0;
    const int32_t layer = arrayBase + static_cast<int32_t>(faceCoord.z);

    if (filterModeFromString(c.filt) == WGPUFilterMode_Nearest) {
        const int32_t x = static_cast<int32_t>(std::floor(faceCoord.x * static_cast<double>(mipSize.width)));
        const int32_t y = static_cast<int32_t>(std::floor(faceCoord.y * static_cast<double>(mipSize.height)));
        const double result = cubeDepthCompareTap(mipLevel, mipSize.width, x, y, layer, compare, call.depthRef);
        return {result, 0.0, 0.0, 1.0};
    }

    const double u = faceCoord.x * static_cast<double>(mipSize.width) - 0.5;
    const double v = faceCoord.y * static_cast<double>(mipSize.height) - 0.5;
    const int32_t x0 = static_cast<int32_t>(std::floor(u));
    const int32_t y0 = static_cast<int32_t>(std::floor(v));
    const double tx = u - std::floor(u);
    const double ty = v - std::floor(v);
    const double c00 = cubeDepthCompareTap(mipLevel, mipSize.width, x0, y0, layer, compare, call.depthRef);
    const double c10 = cubeDepthCompareTap(mipLevel, mipSize.width, x0 + 1, y0, layer, compare, call.depthRef);
    const double c01 = cubeDepthCompareTap(mipLevel, mipSize.width, x0, y0 + 1, layer, compare, call.depthRef);
    const double c11 = cubeDepthCompareTap(mipLevel, mipSize.width, x0 + 1, y0 + 1, layer, compare, call.depthRef);
    const double result = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
    return {result, 0.0, 0.0, 1.0};
}

std::array<double, 4> softwareSampleDepthCompare(const SampleCall& call, const TextureCase& c, const MipMixWeights& weights) {
    uint32_t layer = call.arrayIndex.value_or(0u);
    if (isCubeKind(c.kind)) {
        const Vec3 faceCoord = normalizedCubeToFaceCoord(call.coords);
        layer = (c.useArrayIndex ? (*call.arrayIndex) * 6u : 0u) + static_cast<uint32_t>(faceCoord.z);
    }
    const double clampedLevel = std::clamp(call.level, c.lodMinClamp, c.lodMaxClamp);
    const double level = std::clamp(clampedLevel, 0.0, static_cast<double>(c.mipLevelCount - c.baseMipLevel - 1u));
    const uint32_t baseMip = c.baseMipLevel + static_cast<uint32_t>(std::floor(level));
    const WGPUCompareFunction compare = compareFunctionFromString(c.compare);
    if (isCubeKind(c.kind)) {
        if (filterModeFromString(c.filt) == WGPUFilterMode_Nearest) {
            const double mix = calibratedMixWeight(weights.nearest, level);
            const uint32_t mip = std::min<uint32_t>(baseMip + (mix >= 0.5 ? 1u : 0u), c.mipLevelCount - 1u);
            return sampleDepthCompareCubeOneMip(call, c, mip, compare);
        }
        const uint32_t mip0 = baseMip;
        const uint32_t mip1 = std::min<uint32_t>(baseMip + 1u, c.mipLevelCount - 1u);
        const double mix = calibratedMixWeight(weights.linear, level);
        return mixTexel(sampleDepthCompareCubeOneMip(call, c, mip0, compare), sampleDepthCompareCubeOneMip(call, c, mip1, compare), mix);
    }
    if (filterModeFromString(c.filt) == WGPUFilterMode_Nearest) {
        const double mix = calibratedMixWeight(weights.nearest, level);
        const uint32_t mip = std::min<uint32_t>(baseMip + (mix >= 0.5 ? 1u : 0u), c.mipLevelCount - 1u);
        const double result = applyCompareToDepth(depthValue(mip, layer), compare, call.depthRef);
        return {result, 0.0, 0.0, 1.0};
    }
    const uint32_t mip0 = baseMip;
    const uint32_t mip1 = std::min<uint32_t>(baseMip + 1u, c.mipLevelCount - 1u);
    const double mix = calibratedMixWeight(weights.linear, level);
    const double a = applyCompareToDepth(depthValue(mip0, layer), compare, call.depthRef);
    const double b = applyCompareToDepth(depthValue(mip1, layer), compare, call.depthRef);
    return {lerp(a, b, mix), 0.0, 0.0, 1.0};
}

std::string wgslFloat(double value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(8);
    out << value;
    return out.str();
}

std::string textureType(const TextureCase& c) {
    const char* suffix = c.isDepth ? "" : "<f32>";
    switch (c.kind) {
        case SampleKind::Sampled1D:
            return std::string("texture_1d") + suffix;
        case SampleKind::Sampled2D:
        case SampleKind::Sampled2DLodClamp:
            return std::string("texture_2d") + suffix;
        case SampleKind::Sampled2DArray:
            return std::string("texture_2d_array") + suffix;
        case SampleKind::Sampled3D:
        case SampleKind::Sampled3DLodClamp:
            return std::string("texture_3d") + suffix;
        case SampleKind::SampledCube:
        case SampleKind::SampledCubeLodClamp:
            return std::string("texture_cube") + suffix;
        case SampleKind::SampledCubeArray:
            return std::string("texture_cube_array") + suffix;
        case SampleKind::Depth2D:
            return "texture_depth_2d";
        case SampleKind::Depth2DArray:
            return "texture_depth_2d_array";
        case SampleKind::DepthCube:
            return "texture_depth_cube";
        case SampleKind::DepthCubeArray:
            return "texture_depth_cube_array";
    }
    return "texture_2d<f32>";
}

std::string coordParamExpr(std::string_view param, const TextureCase& c) {
    std::ostringstream out;
    switch (c.kind) {
        case SampleKind::Sampled1D:
            out << param << ".coords.x";
            break;
        case SampleKind::Sampled2D:
        case SampleKind::Sampled2DLodClamp:
        case SampleKind::Sampled2DArray:
        case SampleKind::Depth2D:
        case SampleKind::Depth2DArray:
            out << param << ".coords.xy";
            break;
        default:
            out << param << ".coords.xyz";
            break;
    }
    return out.str();
}

std::string derivativeMultParamExpr(std::string_view param, const TextureCase& c) {
    std::ostringstream out;
    if (c.textureDimension == WGPUTextureDimension_1D) {
        out << param << ".derivativeMult.x";
    } else if (c.textureDimension == WGPUTextureDimension_3D || isCubeKind(c.kind)) {
        out << param << ".derivativeMult.xyz";
    } else {
        out << param << ".derivativeMult.xy";
    }
    return out.str();
}

std::string gradientParamExpr(std::string_view param, std::string_view field, const TextureCase& c) {
    std::ostringstream out;
    if (c.textureDimension == WGPUTextureDimension_3D || isCubeKind(c.kind)) {
        out << param << "." << field << ".xyz";
    } else {
        out << param << "." << field << ".xy";
    }
    return out.str();
}

std::string coordParamExprForSample(std::string_view param, const TextureCase& c) {
    if ((c.builtin != "textureSample" && c.builtin != "textureSampleBias" && c.builtin != "textureSampleCompare") || c.stage != "fragment") {
        return coordParamExpr(param, c);
    }
    return "(" + coordParamExpr(param, c) + " + derivativeBase * " + derivativeMultParamExpr(param, c) + ")";
}

std::array<int32_t, 3> offsetForCallIndex(uint32_t i) {
    return {
        static_cast<int32_t>(i % 3u) - 1,
        static_cast<int32_t>((i / 3u) % 3u) - 1,
        static_cast<int32_t>((i / 9u) % 3u) - 1,
    };
}

std::string offsetExprForCallIndex(uint32_t i, const TextureCase& c) {
    if (!c.useOffset) {
        return {};
    }
    const std::array<int32_t, 3> offset = offsetForCallIndex(i);
    std::ostringstream out;
    if (c.textureDimension == WGPUTextureDimension_3D) {
        out << ", vec3i(" << offset[0] << ", " << offset[1] << ", " << offset[2] << ")";
    } else {
        out << ", vec2i(" << offset[0] << ", " << offset[1] << ")";
    }
    return out.str();
}

std::string levelParamExpr(std::string_view param, const TextureCase& c) {
    if (c.levelType == "u32") {
        return "u32(" + std::string(param) + ".scalars.x)";
    }
    if (c.levelType == "i32") {
        return "i32(" + std::string(param) + ".scalars.x)";
    }
    return std::string(param) + ".scalars.x";
}

std::string arrayIndexParamExpr(std::string_view param, const TextureCase& c) {
    if (c.arrayIndexType == "u32") {
        return std::string(param) + ".indices.x";
    }
    return "i32(" + std::string(param) + ".indices.x)";
}

std::string sampleExprFromParams(std::string_view param, const TextureCase& c, std::string_view offsetExpr = {}) {
    std::ostringstream expr;
    const bool substituteLevel = (c.builtin == "textureSample" || c.builtin == "textureSampleBias" || c.builtin == "textureSampleCompare") && c.stage != "fragment";
    std::string builtin = substituteLevel ? "textureSampleLevel" : c.builtin;
    if (substituteLevel && c.builtin == "textureSampleCompare") {
        builtin = "textureSampleCompareLevel";
    }
    expr << builtin << "(tex, samp, " << coordParamExprForSample(param, c);
    if (c.useArrayIndex) {
        expr << ", " << arrayIndexParamExpr(param, c);
    }
    if (builtin == "textureSampleCompare" || builtin == "textureSampleCompareLevel") {
        expr << ", " << param << ".scalars.z";
    } else if (builtin == "textureSampleGrad") {
        expr << ", " << gradientParamExpr(param, "ddx", c) << ", " << gradientParamExpr(param, "ddy", c);
    } else if (builtin == "textureSampleBias") {
        expr << ", " << param << ".scalars.y";
    } else if (builtin == "textureSampleLevel") {
        expr << ", " << levelParamExpr(param, c);
    }
    expr << offsetExpr << ")";
    if (c.isDepth) {
        return "vec4f(" + expr.str() + ", 0.0, 0.0, 1.0)";
    }
    return expr.str();
}

bool usesUnconditionalCallSelection(const TextureCase& c) {
    if ((c.builtin == "textureSample" || c.builtin == "textureSampleBias" || c.builtin == "textureSampleCompare") && c.stage == "fragment") {
        return true;
    }
    return c.builtin == "textureSampleGrad" && c.useArrayIndex;
}

void writeSampleParamsHeader(std::ostringstream& wgsl) {
    wgsl << "struct SampleParams {\n"
         << "  coords: vec4f,\n"
         << "  ddx: vec4f,\n"
         << "  ddy: vec4f,\n"
         << "  derivativeMult: vec4f,\n"
         << "  scalars: vec4f,\n"
         << "  indices: vec4u,\n"
         << "};\n"
         << "@group(0) @binding(3) var<storage, read> params: array<SampleParams>;\n";
}

std::string buildComputeWgsl(uint32_t callCount, const TextureCase& c) {
    std::ostringstream wgsl;
    wgsl << "// texture-sampling structural shader; stage=" << c.stage << "; filter=" << c.filt << "\n"
         << "@group(0) @binding(0) var tex: " << textureType(c) << ";\n"
         << "@group(0) @binding(1) var samp: " << (c.comparisonSampler ? "sampler_comparison" : "sampler") << ";\n"
         << "@group(0) @binding(2) var<storage, read_write> out: array<vec4f>;\n"
         << "const derivativeBase = vec4f(0.0);\n";
    writeSampleParamsHeader(wgsl);
    wgsl
         << "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
         << "  let i = id.x;\n"
         << "  let p = params[i];\n";
    if (usesUnconditionalCallSelection(c)) {
        wgsl << "  var result = vec4f(0.0);\n";
        for (uint32_t i = 0; i < callCount; ++i) {
            wgsl << "  let call" << i << " = " << sampleExprFromParams("p", c, offsetExprForCallIndex(i, c)) << ";\n"
                 << "  result = select(result, call" << i << ", i == " << i << "u);\n";
        }
        wgsl << "  out[i] = result;\n";
    } else if (c.useOffset) {
        for (uint32_t i = 0; i < callCount; ++i) {
            wgsl << "  if (i == " << i << "u) { out[" << i << "] = "
                 << sampleExprFromParams("p", c, offsetExprForCallIndex(i, c)) << "; }\n";
        }
    } else {
        wgsl << "  out[i] = " << sampleExprFromParams("p", c) << ";\n";
    }
    wgsl << "}\n";
    return wgsl.str();
}

std::string buildRenderWgsl(uint32_t callCount, const TextureCase& c) {
    std::ostringstream wgsl;
    const std::string derivativeType = c.textureDimension == WGPUTextureDimension_1D
        ? "f32"
        : (c.textureDimension == WGPUTextureDimension_3D || isCubeKind(c.kind) ? "vec3f" : "vec2f");
    const std::string zeroDerivative = c.textureDimension == WGPUTextureDimension_1D
        ? "0.0"
        : (c.textureDimension == WGPUTextureDimension_3D || isCubeKind(c.kind) ? "vec3f(0.0)" : "vec2f(0.0)");
    wgsl << "// texture-sampling structural shader; stage=" << c.stage << "; filter=" << c.filt << "\n"
         << "@group(0) @binding(0) var tex: " << textureType(c) << ";\n"
         << "@group(0) @binding(1) var samp: " << (c.comparisonSampler ? "sampler_comparison" : "sampler") << ";\n"
         << "const unusedOutputBinding = 2u;\n";
    writeSampleParamsHeader(wgsl);
    wgsl
         << "struct VOut {\n"
         << "  @builtin(position) pos: vec4f,\n"
         << "  @location(0) @interpolate(flat, either) ndx: u32,\n"
         << "  @location(1) @interpolate(flat, either) result: vec4f,\n"
         << "};\n"
         << "fn callTexture(i: u32, derivativeBase: " << derivativeType << ") -> vec4f {\n"
         << "  let p = params[i];\n";
    if (usesUnconditionalCallSelection(c)) {
        wgsl << "  var result = vec4f(0.0);\n";
        for (uint32_t i = 0; i < callCount; ++i) {
            wgsl << "  let call" << i << " = " << sampleExprFromParams("p", c, offsetExprForCallIndex(i, c)) << ";\n"
                 << "  result = select(result, call" << i << ", i == " << i << "u);\n";
        }
        wgsl << "  return result;\n";
    } else if (c.useOffset) {
        for (uint32_t i = 0; i < callCount; ++i) {
            wgsl << "  if (i == " << i << "u) { return "
                 << sampleExprFromParams("p", c, offsetExprForCallIndex(i, c)) << "; }\n";
        }
        wgsl << "  return vec4f(0.0);\n";
    } else {
        wgsl << "  return " << sampleExprFromParams("p", c) << ";\n";
    }
    wgsl << "}\n"
         << "fn pixelPos(vertexIndex: u32, instanceIndex: u32) -> vec4f {\n"
         << "  let width = " << callCount << ".0;\n"
         << "  let x0 = -1.0 + 2.0 * f32(instanceIndex) / width;\n"
         << "  let x1 = -1.0 + 2.0 * f32(instanceIndex + 1u) / width;\n"
         << "  let p = array(vec2f(x0, 3.0), vec2f(x1, -1.0), vec2f(x0, -1.0));\n"
         << "  return vec4f(p[vertexIndex], 0.0, 1.0);\n"
         << "}\n";
    if (c.stage == "vertex") {
        wgsl << "@vertex fn vsMain(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VOut {\n"
             << "  return VOut(pixelPos(vertexIndex, instanceIndex), instanceIndex, callTexture(instanceIndex, " << zeroDerivative << "));\n"
             << "}\n"
             << "@fragment fn fsMain(v: VOut) -> @location(0) vec4u { return bitcast<vec4u>(v.result); }\n";
    } else {
        wgsl << "@vertex fn vsMain(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VOut {\n"
             << "  return VOut(pixelPos(vertexIndex, instanceIndex), instanceIndex, vec4f(0.0));\n"
             << "}\n"
             << "@fragment fn fsMain(v: VOut) -> @location(0) vec4u {\n";
        if (c.textureDimension == WGPUTextureDimension_1D) {
            wgsl << "  let derivativeBase = (v.pos.x - 0.5 - f32(v.ndx)) / f32(textureDimensions(tex));\n";
        } else if (c.textureDimension == WGPUTextureDimension_3D) {
            wgsl << "  let derivativeBase = vec3f(v.pos.xy - 0.5 - vec2f(f32(v.ndx), 0.0), 0.0) / vec3f(textureDimensions(tex));\n";
        } else if (isCubeKind(c.kind)) {
            wgsl << "  let derivativeBase = (v.pos.xyx - 0.5 - vec3f(f32(v.ndx), 0.0, f32(v.ndx))) / vec3f(vec2f(textureDimensions(tex)), 1.0);\n";
        } else {
            wgsl << "  let derivativeBase = (v.pos.xy - 0.5 - vec2f(f32(v.ndx), 0.0)) / vec2f(textureDimensions(tex));\n";
        }
        wgsl << "  return bitcast<vec4u>(callTexture(v.ndx, derivativeBase));\n"
             << "}\n";
    }
    return wgsl.str();
}

std::vector<SampleCall> generateCalls(const TextureCase& c) {
    std::vector<SampleCall> calls;
    const bool cubeEdges = c.samplePoints == "cube-edges";
    const uint32_t n = cubeEdges ? 24u : kCallCount;
    calls.reserve(n);
    static constexpr std::array<Vec3, 24> cubeEdgeCoords = {{
        {1, -1.01, 0}, {1, 1.01, 0}, {1, 0, -1.01}, {1, 0, 1.01},
        {-1, -1.01, 0}, {-1, 1.01, 0}, {-1, 0, -1.01}, {-1, 0, 1.01},
        {-1.01, 1, 0}, {1.01, 1, 0}, {0, 1, -1.01}, {0, 1, 1.01},
        {-1.01, -1, 0}, {1.01, -1, 0}, {0, -1, -1.01}, {0, -1, 1.01},
        {-1.01, 0, 1}, {1.01, 0, 1}, {0, -1.01, 1}, {0, 1.01, 1},
        {-1.01, 0, -1}, {1.01, 0, -1}, {0, -1.01, -1}, {0, 1.01, -1},
    }};
    for (uint32_t i = 0; i < n; ++i) {
        SampleCall call;
        if (cubeEdges) {
            call.coords = cubeEdgeCoords[i];
        } else if (c.samplePoints == "texel-centre") {
            const uint32_t x = i % c.baseSize.width;
            const uint32_t y = (i / c.baseSize.width) % std::max(1u, c.baseSize.height);
            const uint32_t z = (i / (c.baseSize.width * std::max(1u, c.baseSize.height)))
                % std::max(1u, c.baseSize.depthOrArrayLayers);
            call.coords = Vec3{
                (static_cast<double>(x) + 0.5) / static_cast<double>(c.baseSize.width),
                (static_cast<double>(y) + 0.5) / static_cast<double>(std::max(1u, c.baseSize.height)),
                (static_cast<double>(z) + 0.5) / static_cast<double>(std::max(1u, c.baseSize.depthOrArrayLayers)),
            };
            if (isCubeKind(c.kind)) {
                call.coords = Vec3{call.coords.x * 2.0 - 1.0, call.coords.y * 2.0 - 1.0, 1.0};
            }
        } else if (isCubeKind(c.kind)) {
            const double f = static_cast<double>(i + 1u) / static_cast<double>(std::max(2u, n) - 1u);
            const double r = 1.5 * f;
            const double theta = 2.0 * 2.0 * 3.14159265358979323846 * f;
            const double phi = 2.0 * 1.3 * 3.14159265358979323846 * f;
            call.coords = Vec3{std::cos(theta) * std::sin(phi) * r, std::cos(phi) * r, std::sin(theta) * std::sin(phi) * r};
        } else {
            const double r = 0.13 + static_cast<double>(i % 17u) * 0.061;
            const double theta = static_cast<double>(i) * 2.399963229728653;
            call.coords = Vec3{0.5 + std::cos(theta) * r, 0.5 + std::sin(theta) * r, 0.0};
        }
        const bool gradientBuiltin = c.builtin == "textureSample" || c.builtin == "textureSampleGrad"
            || c.builtin == "textureSampleBias" || c.builtin == "textureSampleCompare";
        call.level = gradientBuiltin
            ? implicitMipLevelForCall(i, c.mipLevelCount)
            : (c.levelType == "f32" ? static_cast<double>(i % 5u) * 0.5 : static_cast<double>(i % c.mipLevelCount));
        if (c.builtin == "textureSampleCompareLevel" || c.builtin == "textureSampleBaseClampToEdge") {
            call.level = 0.0;
        }
        if (gradientBuiltin) {
            call.derivativeMult = Vec3{std::pow(2.0, call.level), 0.0, 0.0};
            const double dx = std::pow(2.0, call.level) / static_cast<double>(std::max(1u, c.baseSize.width));
            call.ddx = Vec3{dx, 0.0, 0.0};
            call.ddy = Vec3{0.0, 0.0, 0.0};
        }
        if (c.comparisonSampler) {
            static constexpr std::array<double, 8> kDepthRefs = {{0.01, 0.18, 0.31, 0.44, 0.57, 0.70, 0.83, 0.96}};
            call.depthRef = kDepthRefs[(i + static_cast<uint32_t>(compareFunctionFromString(c.compare))) % kDepthRefs.size()];
        }
        if (c.builtin == "textureSampleBias") {
            static constexpr std::array<double, 6> kBiases = {{-1.0, 0.5, 1.25, -0.75, 0.0, 1.0}};
            call.bias = kBiases[i % kBiases.size()];
            call.level = std::clamp(call.level + std::clamp(call.bias, -16.0, 15.99), 0.0, static_cast<double>(c.mipLevelCount - 1u));
        }
        if (c.useOffset) {
            call.offset = offsetForCallIndex(i);
        }
        if (c.useArrayIndex) {
            const uint32_t arrayCount = c.viewDimension == WGPUTextureViewDimension_CubeArray
                ? c.baseSize.depthOrArrayLayers / 6u
                : c.baseSize.depthOrArrayLayers;
            call.arrayIndex = arrayCount == 0 ? 0u : i % arrayCount;
        }
        calls.push_back(call);
    }
    return calls;
}

std::vector<SampleParamsData> makeSampleParamsData(const std::vector<SampleCall>& calls) {
    std::vector<SampleParamsData> out;
    out.reserve(calls.size());
    for (const SampleCall& call : calls) {
        SampleParamsData data;
        data.coords = {
            static_cast<float>(call.coords.x),
            static_cast<float>(call.coords.y),
            static_cast<float>(call.coords.z),
            0.0f,
        };
        data.ddx = {
            static_cast<float>(call.ddx.x),
            static_cast<float>(call.ddx.y),
            static_cast<float>(call.ddx.z),
            0.0f,
        };
        data.ddy = {
            static_cast<float>(call.ddy.x),
            static_cast<float>(call.ddy.y),
            static_cast<float>(call.ddy.z),
            0.0f,
        };
        data.derivativeMult = {
            static_cast<float>(call.derivativeMult.x),
            static_cast<float>(call.derivativeMult.y),
            static_cast<float>(call.derivativeMult.z),
            0.0f,
        };
        data.scalars = {
            static_cast<float>(call.level),
            static_cast<float>(call.bias),
            static_cast<float>(call.depthRef),
            0.0f,
        };
        data.indices = {
            call.arrayIndex.value_or(0u),
            0u,
            0u,
            0u,
        };
        out.push_back(data);
    }
    return out;
}

WGPUBindGroupLayout createBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t, const TextureCase& c) {
    std::array<WGPUBindGroupLayoutEntry, 3> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    const WGPUShaderStage visibility = stageVisibility(c.stage);
    entries[0].binding = 0;
    entries[0].visibility = visibility;
    entries[0].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[0].texture.sampleType = textureSampleType(c.isDepth, c.filt);
    entries[0].texture.viewDimension = c.viewDimension;
    entries[1].binding = 1;
    entries[1].visibility = visibility;
    entries[1].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    entries[1].sampler.type = samplerBindingType(c.isDepth, c.filt, c.comparisonSampler);
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[2].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = c.stage == "compute" ? entries.size() : 2;
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroupLayout createCachedBindGroupLayout(WGPUDevice device, const TextureCase& c) {
    std::array<WGPUBindGroupLayoutEntry, 4> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    const WGPUShaderStage visibility = stageVisibility(c.stage);
    entries[0].binding = 0;
    entries[0].visibility = visibility;
    entries[0].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[0].texture.sampleType = textureSampleType(c.isDepth, c.filt);
    entries[0].texture.viewDimension = c.viewDimension;
    entries[1].binding = 1;
    entries[1].visibility = visibility;
    entries[1].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    entries[1].sampler.type = samplerBindingType(c.isDepth, c.filt, c.comparisonSampler);
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[2].buffer.type = WGPUBufferBindingType_Storage;
    entries[3].binding = 3;
    entries[3].visibility = visibility;
    entries[3].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return wgpuDeviceCreateBindGroupLayout(device, &desc);
}

struct SamplingPipelineBundle {
    WGPUShaderModule module = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
};

SamplingPipelineBundle createSamplingPipelineBundle(WGPUDevice device, const std::string& wgsl, const TextureCase& c) {
    SamplingPipelineBundle bundle;

    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = stringView(wgsl);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    bundle.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    bundle.bindGroupLayout = createCachedBindGroupLayout(device, c);
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bundle.bindGroupLayout;
    bundle.pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    if (c.stage == "compute") {
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = bundle.pipelineLayout;
        pipelineDesc.compute.module = bundle.module;
        pipelineDesc.compute.entryPoint = stringView("main");
        bundle.computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    } else {
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA32Uint;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = bundle.module;
        fragment.entryPoint = stringView("fsMain");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        WGPURenderPipelineDescriptor renderDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        renderDesc.layout = bundle.pipelineLayout;
        renderDesc.vertex.module = bundle.module;
        renderDesc.vertex.entryPoint = stringView("vsMain");
        renderDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        renderDesc.multisample.count = 1;
        renderDesc.fragment = &fragment;
        bundle.renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderDesc);
    }
    return bundle;
}

const SamplingPipelineBundle& samplingPipelineForDevice(AllFeaturesMaxLimitsGpuTest& t, const std::string& wgsl, const TextureCase& c) {
    static std::unordered_map<WGPUDevice, std::unordered_map<std::string, SamplingPipelineBundle>> cache;
    const WGPUDevice device = t.device();
    auto& deviceCache = cache[device];
    auto it = deviceCache.find(wgsl);
    if (it == deviceCache.end()) {
        it = deviceCache.emplace(wgsl, createSamplingPipelineBundle(device, wgsl, c)).first;
    }
    return it->second;
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUBindGroupLayout createMipWeightBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    std::array<WGPUBindGroupLayoutEntry, 4> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    entries[1].sampler.type = WGPUSamplerBindingType_NonFiltering;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Compute;
    entries[3].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[3].buffer.type = WGPUBufferBindingType_Storage;
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

MipMixWeights queryMipLevelMixWeightsForDevice(AllFeaturesMaxLimitsGpuTest& t) {
    static constexpr uint64_t outputSize = static_cast<uint64_t>(kMipLevelWeightSteps + 1u) * 4u * sizeof(float);
    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = WGPUExtent3D{2, 2, 1};
    textureDesc.mipLevelCount = 2;
    textureDesc.sampleCount = 1;
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.format = WGPUTextureFormat_R8Unorm;
    textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    const std::array<uint8_t, 4> mip0 = {{0, 0, 0, 0}};
    WGPUTexelCopyBufferLayout mip0Layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    mip0Layout.bytesPerRow = 2;
    mip0Layout.rowsPerImage = 2;
    t.queueWriteTexture(texture, WGPUExtent3D{2, 2, 1}, mip0Layout, mip0.data(), mip0.size(), 0);

    const std::array<uint8_t, 1> mip1 = {{255}};
    WGPUTexelCopyBufferLayout mip1Layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    mip1Layout.bytesPerRow = 1;
    mip1Layout.rowsPerImage = 1;
    t.queueWriteTexture(texture, WGPUExtent3D{1, 1, 1}, mip1Layout, mip1.data(), mip1.size(), 1);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.format = WGPUTextureFormat_R8Unorm;
    viewDesc.mipLevelCount = 2;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUSamplerDescriptor nearestDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    nearestDesc.minFilter = WGPUFilterMode_Nearest;
    nearestDesc.magFilter = WGPUFilterMode_Nearest;
    nearestDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    WGPUSampler nearestSampler = t.createSamplerTracked(nearestDesc);
    WGPUSamplerDescriptor linearDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    linearDesc.minFilter = WGPUFilterMode_Linear;
    linearDesc.magFilter = WGPUFilterMode_Linear;
    linearDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    WGPUSampler linearSampler = t.createSamplerTracked(linearDesc);

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = outputSize;
    bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(bufferDesc);

    const std::string wgsl =
        "@group(0) @binding(0) var tex: texture_2d<f32>;\n"
        "@group(0) @binding(1) var smpNearest: sampler;\n"
        "@group(0) @binding(2) var smpLinear: sampler;\n"
        "@group(0) @binding(3) var<storage, read_write> result: array<vec4f>;\n"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
        "  let mipLevel = f32(id.x) / 64.0;\n"
        "  result[id.x] = vec4f(textureSampleLevel(tex, smpNearest, vec2f(0.5), mipLevel).r,\n"
        "                       textureSampleLevel(tex, smpLinear, vec2f(0.5), mipLevel).r, 0.0, 0.0);\n"
        "}\n";
    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUBindGroupLayout bgl = createMipWeightBindGroupLayout(t);
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(pipelineLayoutDesc);
    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = stringView("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    std::array<WGPUBindGroupEntry, 4> bgEntries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = view;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = nearestSampler;
    bgEntries[2].binding = 2;
    bgEntries[2].sampler = linearSampler;
    bgEntries[3].binding = 3;
    bgEntries[3].buffer = outputBuffer;
    bgEntries[3].size = outputSize;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = bgl;
    bindGroupDesc.entryCount = bgEntries.size();
    bindGroupDesc.entries = bgEntries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, kMipLevelWeightSteps + 1u, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    submit(t, encoder);

    MipMixWeights weights;
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < static_cast<size_t>(outputSize)) {
                return std::string("mip weight output buffer too small");
            }
            const auto* floats = reinterpret_cast<const float*>(actual);
            for (uint32_t i = 0; i <= kMipLevelWeightSteps; ++i) {
                weights.nearest[i] = std::clamp(static_cast<double>(floats[i * 4u]), 0.0, 1.0);
                weights.linear[i] = std::clamp(static_cast<double>(floats[i * 4u + 1u]), 0.0, 1.0);
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(outputSize));
    return weights;
}

const MipMixWeights& mipMixWeightsForDevice(AllFeaturesMaxLimitsGpuTest& t) {
    static std::unordered_map<WGPUDevice, MipMixWeights> cache;
    const WGPUDevice device = t.device();
    auto it = cache.find(device);
    if (it == cache.end()) {
        it = cache.emplace(device, queryMipLevelMixWeightsForDevice(t)).first;
    }
    return it->second;
}

void uploadColorTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const TextureCase& c, const std::vector<MipData>& mips) {
    for (uint32_t mip = 0; mip < c.mipLevelCount; ++mip) {
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = mips[mip].layout.bytesPerRow;
        layout.rowsPerImage = mips[mip].layout.rowsPerImage;
        t.queueWriteTexture(texture, mips[mip].size, layout, mips[mip].data.data(), mips[mip].data.size(), mip, WGPUOrigin3D{0, 0, 0});
    }
}

std::string materializeCoordWGSL(const TextureCase& c) {
    if (c.viewDimension == WGPUTextureViewDimension_1D) {
        return "f32(id.x) + 0.5";
    }
    if (c.viewDimension == WGPUTextureViewDimension_3D) {
        return "vec3f((f32(id.x) + 0.5) / dims.x, (f32(id.y) + 0.5) / dims.y, (f32(id.z) + 0.5) / dims.z)";
    }
    if (c.viewDimension == WGPUTextureViewDimension_Cube || c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        return "cubeCoord(id.x, id.y, id.z)";
    }
    return "vec2f((f32(id.x) + 0.5) / dims.x, (f32(id.y) + 0.5) / dims.y)";
}

std::string buildMaterializeWgsl(const TextureCase& c, WGPUExtent3D mipSize) {
    std::ostringstream wgsl;
    wgsl << "@group(0) @binding(0) var tex: " << textureType(c) << ";\n"
         << "@group(0) @binding(1) var samp: sampler;\n"
         << "@group(0) @binding(2) var<storage, read_write> out: array<vec4f>;\n"
         << "const dims = vec3f(" << wgslFloat(static_cast<double>(mipSize.width)) << ", "
         << wgslFloat(static_cast<double>(std::max(1u, mipSize.height))) << ", "
         << wgslFloat(static_cast<double>(std::max(1u, mipSize.depthOrArrayLayers))) << ");\n";
    if (c.viewDimension == WGPUTextureViewDimension_Cube || c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        wgsl << "fn faceCoord(x: u32, y: u32, face: u32) -> vec3f {\n"
             << "  let u = (f32(x) + 0.5) / dims.x;\n"
             << "  let v = (f32(y) + 0.5) / dims.y;\n"
             << "  if (face == 0u) { return normalize(vec3f(1.0, 1.0 - 2.0 * v, 1.0 - 2.0 * u)); }\n"
             << "  if (face == 1u) { return normalize(vec3f(-1.0, 1.0 - 2.0 * v, 2.0 * u - 1.0)); }\n"
             << "  if (face == 2u) { return normalize(vec3f(2.0 * u - 1.0, 1.0, 2.0 * v - 1.0)); }\n"
             << "  if (face == 3u) { return normalize(vec3f(2.0 * u - 1.0, -1.0, 1.0 - 2.0 * v)); }\n"
             << "  if (face == 4u) { return normalize(vec3f(2.0 * u - 1.0, 1.0 - 2.0 * v, 1.0)); }\n"
             << "  return normalize(vec3f(1.0 - 2.0 * u, 1.0 - 2.0 * v, -1.0));\n"
             << "}\n"
             << "fn cubeCoord(x: u32, y: u32, z: u32) -> vec3f { return faceCoord(x, y, z % 6u); }\n";
    }
    wgsl << "@compute @workgroup_size(4, 4, 1) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
         << "  if (id.x >= " << mipSize.width << "u || id.y >= " << std::max(1u, mipSize.height)
         << "u || id.z >= " << std::max(1u, mipSize.depthOrArrayLayers) << "u) { return; }\n"
         << "  let ndx = (id.z * " << std::max(1u, mipSize.height) << "u + id.y) * " << mipSize.width << "u + id.x;\n";
    if (c.viewDimension == WGPUTextureViewDimension_1D) {
        wgsl << "  out[ndx] = textureLoad(tex, id.x, 0);\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_2D) {
        wgsl << "  out[ndx] = textureLoad(tex, id.xy, 0);\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_2DArray) {
        wgsl << "  out[ndx] = textureLoad(tex, id.xy, id.z, 0);\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_3D) {
        wgsl << "  out[ndx] = textureLoad(tex, id.xyz, 0);\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", id.z / 6u, 0.0);\n";
    } else {
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", 0.0);\n";
    }
    wgsl << "}\n";
    return wgsl.str();
}

std::vector<MipData> materializeCompressedTexels(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const TextureCase& c) {
    std::vector<MipData> out;
    out.reserve(c.mipLevelCount);
    TextureCase materializeCase = c;
    materializeCase.stage = "compute";
    materializeCase.filt = "nearest";
    if (c.viewDimension == WGPUTextureViewDimension_Cube || c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        materializeCase.kind = SampleKind::Sampled2DArray;
        materializeCase.viewDimension = WGPUTextureViewDimension_2DArray;
        materializeCase.useArrayIndex = true;
    }

    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

    for (uint32_t mip = 0; mip < c.mipLevelCount; ++mip) {
        MipData mipData;
        mipData.format = WGPUTextureFormat_RGBA32Float;
        mipData.size = physicalMipSize(c.baseSize, c.textureDimension, mip);
        mipData.layout.bytesPerBlock = 16;
        mipData.layout.mipSize = mipData.size;
        mipData.layout.minBytesPerRow = mipData.size.width * 16u;
        mipData.layout.bytesPerRow = mipData.layout.minBytesPerRow;
        mipData.layout.rowsPerImage = std::max(1u, mipData.size.height);
        mipData.layout.byteLength = static_cast<uint64_t>(mipData.layout.bytesPerRow)
            * mipData.layout.rowsPerImage * std::max(1u, mipData.size.depthOrArrayLayers);
        mipData.data.assign(static_cast<size_t>(mipData.layout.byteLength), 0);

        const uint64_t outputSize = mipData.layout.byteLength;
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = outputSize;
        bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(bufferDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = materializeCase.viewDimension;
        viewDesc.format = c.format;
        viewDesc.baseMipLevel = mip;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        WGPUShaderModule module = t.createShaderModuleTracked(buildMaterializeWgsl(materializeCase, mipData.size));
        WGPUBindGroupLayout bgl = createBindGroupLayout(t, materializeCase);
        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &bgl;
        WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(pipelineLayoutDesc);
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = stringView("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        std::array<WGPUBindGroupEntry, 3> bgEntries = {{
            WGPU_BIND_GROUP_ENTRY_INIT,
            WGPU_BIND_GROUP_ENTRY_INIT,
            WGPU_BIND_GROUP_ENTRY_INIT,
        }};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = view;
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = sampler;
        bgEntries[2].binding = 2;
        bgEntries[2].buffer = outputBuffer;
        bgEntries[2].size = outputSize;
        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = bgl;
        bindGroupDesc.entryCount = bgEntries.size();
        bindGroupDesc.entries = bgEntries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass,
            (mipData.size.width + 3u) / 4u,
            (std::max(1u, mipData.size.height) + 3u) / 4u,
            std::max(1u, mipData.size.depthOrArrayLayers));
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        submit(t, encoder);

        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < static_cast<size_t>(outputSize)) {
                    return std::string("compressed materialization buffer too small");
                }
                std::memcpy(mipData.data.data(), actual, static_cast<size_t>(outputSize));
                return std::nullopt;
            },
            0,
            static_cast<size_t>(outputSize));
        out.push_back(std::move(mipData));
    }
    return out;
}

void initializeDepthTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const TextureCase& c) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    for (uint32_t mip = 0; mip < c.mipLevelCount; ++mip) {
        const WGPUExtent3D mipSize = physicalMipSize(c.baseSize, c.textureDimension, mip);
        for (uint32_t layer = 0; layer < mipSize.depthOrArrayLayers; ++layer) {
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.format = c.format;
            viewDesc.baseMipLevel = mip;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = layer;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            WGPUTextureView view = t.createViewTracked(texture, viewDesc);

            WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            ds.view = view;
            ds.depthReadOnly = WGPU_FALSE;
            ds.depthLoadOp = WGPULoadOp_Clear;
            ds.depthStoreOp = WGPUStoreOp_Store;
            ds.depthClearValue = depthValue(mip, layer);
            if (textureFormatInfo(c.format).hasStencil) {
                ds.stencilReadOnly = WGPU_FALSE;
                ds.stencilLoadOp = WGPULoadOp_Clear;
                ds.stencilStoreOp = WGPUStoreOp_Store;
                ds.stencilClearValue = 0;
            }
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.depthStencilAttachment = &ds;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    }
    submit(t, encoder);
}

void executeCase(AllFeaturesMaxLimitsGpuTest& t, TextureCase c) {
    t.skipIfTextureFormatNotSupported(c.format);
    c.baseSize = adjustedBaseSizeForFormat(c);
    if (c.textureDimension == WGPUTextureDimension_3D && isBCTextureFormat(c.format)
        && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TextureCompressionBCSliced3D)) {
        t.skip("3D BC compressed textures require texture-compression-bc-sliced-3d");
    }
    if (c.textureDimension == WGPUTextureDimension_3D && isASTCTextureFormat(c.format)
        && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TextureCompressionASTCSliced3D)) {
        t.skip("3D ASTC compressed textures require texture-compression-astc-sliced-3d");
    }
    if (c.viewDimension == WGPUTextureViewDimension_Cube || c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        t.skipIfTextureViewDimensionNotSupported(c.viewDimension);
    }
    if (c.textureDimension == WGPUTextureDimension_1D) {
        t.skipIfTextureFormatAndDimensionNotCompatible(c.format, WGPUTextureDimension_1D);
    }
    if (c.textureDimension == WGPUTextureDimension_3D) {
        t.skipIfTextureFormatAndDimensionNotCompatible(c.format, WGPUTextureDimension_3D);
    }

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = c.baseSize;
    textureDesc.mipLevelCount = c.mipLevelCount;
    textureDesc.sampleCount = 1;
    textureDesc.dimension = c.textureDimension;
    textureDesc.format = c.format;
    textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    if (c.isDepth) {
        textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    }
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    const bool constantForCube = isCubeKind(c.kind);
    std::vector<MipData> mips;
    if (c.isDepth) {
        initializeDepthTexture(t, texture, c);
    } else if (isCompressedTextureFormat(c.format)) {
        std::vector<MipData> compressedMips = makeCompressedTextureData(c.format, c.textureDimension, c.baseSize, c.mipLevelCount);
        uploadColorTexture(t, texture, c, compressedMips);
        mips = materializeCompressedTexels(t, texture, c);
    } else {
        mips = makeTextureData(c.format, c.textureDimension, c.baseSize, c.mipLevelCount, constantForCube);
        uploadColorTexture(t, texture, c, mips);
    }

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.dimension = c.viewDimension;
    viewDesc.format = c.isDepth ? WGPUTextureFormat_Undefined : c.format;
    viewDesc.baseMipLevel = c.baseMipLevel;
    viewDesc.mipLevelCount = c.mipLevelCount - c.baseMipLevel;
    viewDesc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    if (c.isDepth) {
        viewDesc.aspect = WGPUTextureAspect_DepthOnly;
    }
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.addressModeU = c.modeU;
    samplerDesc.addressModeV = c.modeV;
    samplerDesc.addressModeW = c.modeW;
    samplerDesc.minFilter = filterModeFromString(c.filt);
    samplerDesc.magFilter = filterModeFromString(c.filt);
    samplerDesc.mipmapFilter = c.filt == "linear" ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = static_cast<float>(c.lodMinClamp);
    samplerDesc.lodMaxClamp = static_cast<float>(c.lodMaxClamp);
    if (c.comparisonSampler) {
        samplerDesc.compare = compareFunctionFromString(c.compare);
    }
    WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

    const std::vector<SampleCall> calls = generateCalls(c);
    const MipMixWeights& mipWeights = mipMixWeightsForDevice(t);
    std::vector<std::array<double, 4>> expected;
    expected.reserve(calls.size());
    for (const SampleCall& call : calls) {
        if (c.comparisonSampler) {
            expected.push_back(softwareSampleDepthCompare(call, c, mipWeights));
        } else if (c.isDepth) {
            expected.push_back(softwareSampleDepth(call, c, mipWeights));
        } else if (c.baseClampToEdge) {
            expected.push_back(sampleColorOneMip(mips[0], c.format, call, c));
        } else {
            expected.push_back(softwareSampleColor(mips, c.format, call, c, mipWeights));
        }
    }

    const std::string wgsl = c.stage == "compute"
        ? buildComputeWgsl(static_cast<uint32_t>(calls.size()), c)
        : buildRenderWgsl(static_cast<uint32_t>(calls.size()), c);
    const SamplingPipelineBundle& pipelineBundle = samplingPipelineForDevice(t, wgsl, c);

    const std::vector<SampleParamsData> sampleParams = makeSampleParamsData(calls);
    const uint64_t sampleParamsSize = static_cast<uint64_t>(sampleParams.size()) * sizeof(SampleParamsData);
    WGPUBufferDescriptor sampleParamsDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    sampleParamsDesc.size = sampleParamsSize;
    sampleParamsDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    WGPUBuffer sampleParamsBuffer = t.createBufferTracked(sampleParamsDesc);
    t.queueWriteBuffer(sampleParamsBuffer, 0, sampleParams.data(), static_cast<size_t>(sampleParamsSize));

    const uint64_t outputSize = static_cast<uint64_t>(calls.size()) * 4u * sizeof(float);
    const uint32_t renderBytesPerRow = alignToU32(static_cast<uint32_t>(calls.size()) * 16u, 256u);
    const uint64_t bufferSize = c.stage == "compute" ? outputSize : renderBytesPerRow;
    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = bufferSize;
    bufferDesc.usage = c.stage == "compute"
        ? WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc
        : WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(bufferDesc);

    std::array<WGPUBindGroupEntry, 4> bgEntries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = view;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = sampler;
    bgEntries[2].binding = 2;
    bgEntries[2].buffer = outputBuffer;
    bgEntries[2].size = outputSize;
    bgEntries[3].binding = 3;
    bgEntries[3].buffer = sampleParamsBuffer;
    bgEntries[3].size = sampleParamsSize;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = pipelineBundle.bindGroupLayout;
    bindGroupDesc.entryCount = bgEntries.size();
    bindGroupDesc.entries = bgEntries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    if (c.stage == "compute") {
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipelineBundle.computePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, static_cast<uint32_t>(calls.size()), 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    } else {
        WGPUTextureDescriptor targetDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        targetDesc.size = WGPUExtent3D{static_cast<uint32_t>(calls.size()), 1, 1};
        targetDesc.mipLevelCount = 1;
        targetDesc.sampleCount = 1;
        targetDesc.dimension = WGPUTextureDimension_2D;
        targetDesc.format = WGPUTextureFormat_RGBA32Uint;
        targetDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture target = t.createTextureTracked(targetDesc);
        WGPUTextureViewDescriptor targetViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        targetViewDesc.dimension = WGPUTextureViewDimension_2D;
        targetViewDesc.format = WGPUTextureFormat_RGBA32Uint;
        WGPUTextureView targetView = t.createViewTracked(target, targetViewDesc);

        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = targetView;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipelineBundle.renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, static_cast<uint32_t>(calls.size()), 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        t.copyTextureToBuffer(encoder, target, outputBuffer, renderBytesPerRow, targetDesc.size);
    }
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < static_cast<size_t>(outputSize)) {
                return std::string("textureSampleLevel output buffer too small");
            }
            const double tolerance = comparisonToleranceForFormat(c.format, c.filt);
            for (size_t i = 0; i < calls.size(); ++i) {
                for (size_t component = 0; component < 4; ++component) {
                    const size_t byteOffset = c.stage == "compute"
                        ? (i * 4u + component) * sizeof(float)
                        : i * 16u + component * sizeof(float);
                    float gotValue = 0.0f;
                    std::memcpy(&gotValue, actual + byteOffset, sizeof(float));
                    const double diff = std::abs(static_cast<double>(gotValue) - expected[i][component]);
                    if (diff > tolerance) {
                        std::ostringstream msg;
                        msg << "textureSampleLevel mismatch at call " << i << " component " << component
                            << ": expected " << expected[i][component] << ", got " << gotValue
                            << ", diff " << diff << ", format " << textureFormatInfo(c.format).identifier
                            << ", tolerance " << tolerance << ", stage " << c.stage;
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(outputSize));
}

WGPUExtent3D baseSize(uint32_t width, uint32_t height, uint32_t depth) {
    return WGPUExtent3D{width, height, depth};
}

TextureCase baseSampledCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind) {
    TextureCase c;
    c.kind = kind;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.stage = t.param<std::string>("stage");
    c.filt = t.param<std::string>("filt");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.mipLevelCount = kMipLevelCount;
    return c;
}

TextureCase baseTextureSampleSampledCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind) {
    TextureCase c;
    c.kind = kind;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.stage = "fragment";
    c.filt = t.param<std::string>("filt");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.mipLevelCount = kMipLevelCount;
    c.builtin = "textureSample";
    return c;
}

TextureCase baseTextureSampleGradSampledCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind) {
    TextureCase c = baseSampledCase(t, kind);
    c.builtin = "textureSampleGrad";
    return c;
}

TextureCase baseTextureSampleBiasSampledCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind) {
    TextureCase c = baseTextureSampleSampledCase(t, kind);
    c.builtin = "textureSampleBias";
    return c;
}

TextureCase baseDepthCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind) {
    TextureCase c;
    c.kind = kind;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.stage = t.param<std::string>("stage");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.filt = "nearest";
    c.isDepth = true;
    c.levelType = t.param<std::string>("L");
    c.mipLevelCount = kMipLevelCount;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    return c;
}

TextureCase baseTextureSampleDepthCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind) {
    TextureCase c;
    c.kind = kind;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.stage = "fragment";
    c.samplePoints = t.param<std::string>("samplePoints");
    c.filt = "nearest";
    c.isDepth = true;
    c.builtin = "textureSample";
    c.mipLevelCount = kMipLevelCount;
    return c;
}

TextureCase baseTextureSampleCompareCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind, std::string stage, std::string builtin) {
    TextureCase c;
    c.kind = kind;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.stage = std::move(stage);
    c.samplePoints = t.param<std::string>("samplePoints");
    c.filt = t.param<std::string>("filt");
    c.compare = t.param<std::string>("compare");
    c.isDepth = true;
    c.comparisonSampler = true;
    c.builtin = std::move(builtin);
    c.mipLevelCount = c.builtin == "textureSampleCompare" ? kMipLevelCount : kMipLevelCount;
    return c;
}

TextureCase baseTextureSampleBaseClampToEdgeCase(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c;
    c.kind = SampleKind::Sampled2D;
    c.format = WGPUTextureFormat_RGBA8Unorm;
    c.stage = t.param<std::string>("stage");
    c.filt = t.param<std::string>("filt");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.builtin = "textureSampleBaseClampToEdge";
    c.baseClampToEdge = true;
    c.baseSize = baseSize(8, 8, 1);
    c.mipLevelCount = kMipLevelCount;
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    return c;
}

constexpr uint32_t kMetadataMaxMipsForTest = 3;
constexpr uint32_t kMetadataMaxSamplesForTest = 4;
constexpr uint32_t kMetadataNumLayers = 36;

struct MetadataQueryCase {
    std::string stage = "compute";
    std::string textureType;
    std::string valueExpr;
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTextureDimension textureDimension = WGPUTextureDimension_2D;
    WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D;
    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding;
    WGPUTextureSampleType sampleType = WGPUTextureSampleType_Float;
    bool storageTexture = false;
    WGPUStorageTextureAccess storageAccess = WGPUStorageTextureAccess_BindingNotUsed;
    WGPUExtent3D size = WGPUExtent3D{1, 1, 1};
    uint32_t sampleCount = 1;
    uint32_t textureMipCount = 1;
    uint32_t baseMipLevel = 0;
    uint32_t viewMipCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    std::array<uint32_t, 4> expected = {0, 0, 0, 0};
    uint32_t expectedCount = 1;
};

struct MetadataPipelineBundle {
    WGPUShaderModule module = nullptr;
    WGPUBindGroupLayout textureBindGroupLayout = nullptr;
    WGPUBindGroupLayout resultBindGroupLayout = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
};

uint32_t paramU32(const ParamRecord& record, std::string_view key) {
    return static_cast<uint32_t>(valueAs<int>(*findParam(record, key)));
}

uint32_t paramU32(const Fixture& t, std::string_view key) {
    return static_cast<uint32_t>(t.param<int>(key));
}

std::string viewDimensionParam(const ParamRecord& record) {
    return paramString(record, "dimensions");
}

WGPUTextureViewDimension parseViewDimension(std::string_view value) {
    if (value == "1d") return WGPUTextureViewDimension_1D;
    if (value == "2d") return WGPUTextureViewDimension_2D;
    if (value == "2d-array") return WGPUTextureViewDimension_2DArray;
    if (value == "3d") return WGPUTextureViewDimension_3D;
    if (value == "cube") return WGPUTextureViewDimension_Cube;
    if (value == "cube-array") return WGPUTextureViewDimension_CubeArray;
    return WGPUTextureViewDimension_Undefined;
}

WGPUTextureDimension textureDimensionForView(WGPUTextureViewDimension dimension) {
    switch (dimension) {
        case WGPUTextureViewDimension_1D:
            return WGPUTextureDimension_1D;
        case WGPUTextureViewDimension_3D:
            return WGPUTextureDimension_3D;
        default:
            return WGPUTextureDimension_2D;
    }
}

std::string wgslDimensionName(WGPUTextureViewDimension dimension) {
    switch (dimension) {
        case WGPUTextureViewDimension_1D:
            return "1d";
        case WGPUTextureViewDimension_2D:
            return "2d";
        case WGPUTextureViewDimension_2DArray:
            return "2d_array";
        case WGPUTextureViewDimension_3D:
            return "3d";
        case WGPUTextureViewDimension_Cube:
            return "cube";
        case WGPUTextureViewDimension_CubeArray:
            return "cube_array";
        default:
            return "2d";
    }
}

WGPUTextureAspect parseAspect(std::string_view value) {
    if (value == "depth-only") return WGPUTextureAspect_DepthOnly;
    if (value == "stencil-only") return WGPUTextureAspect_StencilOnly;
    return WGPUTextureAspect_All;
}

std::vector<Value> formatAspects(WGPUTextureFormat format) {
    if (isDepthTextureFormat(format) && isStencilTextureFormat(format)) {
        return values({"depth-only", "stencil-only"});
    }
    return values({"all"});
}

std::vector<Value> formatSamples(WGPUTextureFormat format) {
    if (textureFormatInfo(format).multisample) {
        return {Value(1), Value(static_cast<int>(kMetadataMaxSamplesForTest))};
    }
    return {Value(1)};
}

bool dimensionsValidForStorage(WGPUTextureViewDimension dimension) {
    return dimension == WGPUTextureViewDimension_1D || dimension == WGPUTextureViewDimension_2D
        || dimension == WGPUTextureViewDimension_2DArray || dimension == WGPUTextureViewDimension_3D;
}

std::vector<Value> viewDimensionsFor(WGPUTextureFormat format, uint32_t samples) {
    if (samples > 1) {
        return values({"2d"});
    }
    std::vector<Value> out;
    for (const char* dim : {"1d", "2d", "2d-array", "3d", "cube", "cube-array"}) {
        const WGPUTextureViewDimension view = parseViewDimension(dim);
        if (textureFormatAndDimensionPossiblyCompatible(textureDimensionForView(view), format)) {
            out.emplace_back(dim);
        }
    }
    return out;
}

std::vector<Value> metadataMipCounts(WGPUTextureFormat, WGPUTextureViewDimension dimension, uint32_t samples) {
    if (samples != 1 || textureDimensionForView(dimension) == WGPUTextureDimension_1D) {
        return {Value(1)};
    }
    return {Value(1), Value(static_cast<int>(kMetadataMaxMipsForTest))};
}

std::vector<Value> metadataBaseMipLevels(uint32_t textureMipCount) {
    std::vector<Value> out;
    for (uint32_t i = 0; i < textureMipCount; ++i) {
        out.emplace_back(static_cast<int>(i));
    }
    return out;
}

std::vector<Value> metadataDimensionsLevels(uint32_t samples, uint32_t textureMipCount, uint32_t baseMipLevel) {
    if (samples > 1) {
        return {Value::undef()};
    }
    std::vector<Value> out;
    out.push_back(Value::undef());
    for (uint32_t i = 0; i < textureMipCount - baseMipLevel; ++i) {
        out.emplace_back(static_cast<int>(i));
    }
    return out;
}

uint32_t alignForMetadata(uint32_t value, uint32_t alignment) {
    return alignToU32(value, alignment);
}

WGPUExtent3D metadataTestSize(WGPUTextureViewDimension dimension, WGPUTextureFormat format, uint32_t mipLevel) {
    constexpr uint32_t kMinLen = 1u << kMetadataMaxMipsForTest;
    constexpr uint32_t kNumCubeFaces = 6;
    const TextureFormatInfo& info = textureFormatInfo(format);
    const uint32_t bw = info.blockWidth;
    const uint32_t bh = info.blockHeight;
    switch (dimension) {
        case WGPUTextureViewDimension_1D: {
            const uint32_t w = alignForMetadata(kMinLen, bw) * 2u;
            return WGPUExtent3D{w, 1, 1};
        }
        case WGPUTextureViewDimension_2D: {
            const uint32_t w = alignForMetadata(kMinLen, bw) * 2u;
            const uint32_t h = alignForMetadata(kMinLen, bh) * 3u;
            return WGPUExtent3D{w, h, 1};
        }
        case WGPUTextureViewDimension_2DArray: {
            const uint32_t w = alignForMetadata(kMinLen, bw) * 4u;
            const uint32_t h = alignForMetadata(kMinLen, bh) * 3u;
            return WGPUExtent3D{w, h, 4};
        }
        case WGPUTextureViewDimension_3D: {
            const uint32_t w = alignForMetadata(kMinLen, bw) * 2u;
            const uint32_t h = alignForMetadata(kMinLen, bh) * 3u;
            const uint32_t d = kMinLen * 4u;
            return WGPUExtent3D{w, h, d >> mipLevel};
        }
        case WGPUTextureViewDimension_Cube: {
            const uint32_t l = alignForMetadata(kMinLen, bw) * alignForMetadata(kMinLen, bh) * 3u;
            return WGPUExtent3D{l, l, kNumCubeFaces};
        }
        case WGPUTextureViewDimension_CubeArray: {
            const uint32_t l = alignForMetadata(kMinLen, bw) * alignForMetadata(kMinLen, bh) * 4u;
            return WGPUExtent3D{l, l, kNumCubeFaces * 3u};
        }
        default:
            return WGPUExtent3D{1, 1, 1};
    }
}

std::array<uint32_t, 4> expectedDimensions(WGPUTextureViewDimension dimension, WGPUExtent3D size, uint32_t mip) {
    const uint32_t w = size.width >> mip;
    const uint32_t h = std::max(1u, size.height) >> mip;
    const uint32_t d = std::max(1u, size.depthOrArrayLayers) >> mip;
    switch (dimension) {
        case WGPUTextureViewDimension_1D:
            return {w, 0, 0, 0};
        case WGPUTextureViewDimension_3D:
            return {w, h, d, 0};
        default:
            return {w, h, 0, 0};
    }
}

uint32_t dimensionReturnCount(WGPUTextureViewDimension dimension) {
    if (dimension == WGPUTextureViewDimension_1D) return 1;
    if (dimension == WGPUTextureViewDimension_3D) return 3;
    return 2;
}

bool formatLooksUint(WGPUTextureFormat format) {
    return std::string_view(textureFormatInfo(format).identifier).find("uint") != std::string_view::npos;
}

bool formatLooksSint(WGPUTextureFormat format) {
    return std::string_view(textureFormatInfo(format).identifier).find("sint") != std::string_view::npos;
}

WGPUTextureSampleType metadataSampleType(WGPUTextureFormat format, WGPUTextureAspect aspect, bool depthTextureType, uint32_t samples) {
    if (aspect == WGPUTextureAspect_StencilOnly || isStencilOnlyFormat(format)) {
        return WGPUTextureSampleType_Uint;
    }
    if (depthTextureType) {
        return WGPUTextureSampleType_Depth;
    }
    if (isDepthTextureFormat(format)) {
        return WGPUTextureSampleType_UnfilterableFloat;
    }
    if (formatLooksUint(format)) {
        return WGPUTextureSampleType_Uint;
    }
    if (formatLooksSint(format)) {
        return WGPUTextureSampleType_Sint;
    }
    if (samples > 1 || !isPossiblyFilterableAsTextureF32(format)) {
        return WGPUTextureSampleType_UnfilterableFloat;
    }
    return WGPUTextureSampleType_Float;
}

std::string sampledWGSLTypeFor(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    if (aspect == WGPUTextureAspect_StencilOnly || isStencilOnlyFormat(format) || formatLooksUint(format)) {
        return "u32";
    }
    if (formatLooksSint(format)) {
        return "i32";
    }
    return "f32";
}

std::string storageAccessWGSL(WGPUStorageTextureAccess access) {
    if (access == WGPUStorageTextureAccess_ReadOnly) return "read";
    if (access == WGPUStorageTextureAccess_ReadWrite) return "read_write";
    return "write";
}

WGPUStorageTextureAccess parseStorageAccessParam(std::string_view value) {
    if (value == "read" || value == "read-only") return WGPUStorageTextureAccess_ReadOnly;
    if (value == "read_write" || value == "read-write") return WGPUStorageTextureAccess_ReadWrite;
    return WGPUStorageTextureAccess_WriteOnly;
}

std::string metadataCastWGSL(uint32_t expectedCount) {
    if (expectedCount == 1) return "vec4u(getValue(), 0u, 0u, 0u)";
    if (expectedCount == 2) return "vec4u(getValue(), 0u, 0u)";
    if (expectedCount == 3) return "vec4u(getValue(), 0u)";
    return "getValue()";
}

std::string buildMetadataWgsl(const MetadataQueryCase& c) {
    std::ostringstream wgsl;
    const std::string outputType = c.expectedCount == 1
        ? "u32"
        : ("vec" + std::to_string(c.expectedCount) + "u");
    wgsl << "@group(0) @binding(0) var texture : " << c.textureType << ";\n"
         << "fn getValue() -> " << outputType << " { return " << c.valueExpr << "; }\n"
         << "struct VOut {\n"
         << "  @builtin(position) pos: vec4f,\n"
         << "  @location(0) @interpolate(flat, either) ndx: u32,\n"
         << "  @location(1) @interpolate(flat, either) result: vec4u,\n"
         << "};\n";
    if (c.stage == "compute") {
        wgsl << "@group(1) @binding(0) var<storage, read_write> results: array<vec4u>;\n"
             << "@compute @workgroup_size(1) fn csCompute(@builtin(global_invocation_id) id: vec3u) {\n"
             << "  results[id.x] = " << metadataCastWGSL(c.expectedCount) << ";\n"
             << "}\n";
    } else if (c.stage == "vertex") {
        wgsl << "@vertex fn vsVertex(@builtin(vertex_index) vertex_index: u32, @builtin(instance_index) instance_index: u32) -> VOut {\n"
             << "  let positions = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));\n"
             << "  return VOut(vec4f(positions[vertex_index], 0, 1), instance_index, " << metadataCastWGSL(c.expectedCount) << ");\n"
             << "}\n"
             << "@fragment fn fsVertex(v: VOut) -> @location(0) vec4u { return v.result; }\n";
    } else {
        wgsl << "@vertex fn vsFragment(@builtin(vertex_index) vertex_index: u32, @builtin(instance_index) instance_index: u32) -> VOut {\n"
             << "  let positions = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));\n"
             << "  return VOut(vec4f(positions[vertex_index], 0, 1), instance_index, vec4u(0));\n"
             << "}\n"
             << "@fragment fn fsFragment(v: VOut) -> @location(0) vec4u { return " << metadataCastWGSL(c.expectedCount) << "; }\n";
    }
    return wgsl.str();
}

MetadataPipelineBundle createMetadataPipelineBundle(WGPUDevice device, const std::string& wgsl, const MetadataQueryCase& c) {
    MetadataPipelineBundle bundle;
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = stringView(wgsl);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    bundle.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    WGPUBindGroupLayoutEntry textureEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    textureEntry.binding = 0;
    textureEntry.visibility = stageVisibility(c.stage);
    if (c.storageTexture) {
        textureEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        textureEntry.storageTexture.access = c.storageAccess;
        textureEntry.storageTexture.format = c.format;
        textureEntry.storageTexture.viewDimension = c.viewDimension;
    } else {
        textureEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        textureEntry.texture.sampleType = c.sampleType;
        textureEntry.texture.viewDimension = c.viewDimension;
        textureEntry.texture.multisampled = c.sampleCount > 1 ? WGPU_TRUE : WGPU_FALSE;
    }
    WGPUBindGroupLayoutDescriptor textureLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    textureLayoutDesc.entryCount = 1;
    textureLayoutDesc.entries = &textureEntry;
    bundle.textureBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &textureLayoutDesc);

    std::array<WGPUBindGroupLayout, 2> layouts = {{bundle.textureBindGroupLayout, nullptr}};
    uint32_t layoutCount = 1;
    if (c.stage == "compute") {
        WGPUBindGroupLayoutEntry resultEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        resultEntry.binding = 0;
        resultEntry.visibility = WGPUShaderStage_Compute;
        resultEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        resultEntry.buffer.type = WGPUBufferBindingType_Storage;
        resultEntry.buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor resultLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        resultLayoutDesc.entryCount = 1;
        resultLayoutDesc.entries = &resultEntry;
        bundle.resultBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &resultLayoutDesc);
        layouts[1] = bundle.resultBindGroupLayout;
        layoutCount = 2;
    }

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = layoutCount;
    pipelineLayoutDesc.bindGroupLayouts = layouts.data();
    bundle.pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (c.stage == "compute") {
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = bundle.pipelineLayout;
        pipelineDesc.compute.module = bundle.module;
        pipelineDesc.compute.entryPoint = stringView("csCompute");
        bundle.computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    } else {
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA32Uint;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = bundle.module;
        fragment.entryPoint = stringView(c.stage == "vertex" ? "fsVertex" : "fsFragment");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        WGPURenderPipelineDescriptor renderDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        renderDesc.layout = bundle.pipelineLayout;
        renderDesc.vertex.module = bundle.module;
        renderDesc.vertex.entryPoint = stringView(c.stage == "vertex" ? "vsVertex" : "vsFragment");
        renderDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        renderDesc.multisample.count = 1;
        renderDesc.fragment = &fragment;
        bundle.renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderDesc);
    }
    return bundle;
}

std::string metadataPipelineKey(const std::string& wgsl, const MetadataQueryCase& c) {
    std::ostringstream key;
    key << wgsl
        << "\n// layout:"
        << " stage=" << c.stage
        << " view=" << static_cast<uint32_t>(c.viewDimension)
        << " sampleType=" << static_cast<uint32_t>(c.sampleType)
        << " multisampled=" << c.sampleCount
        << " storage=" << (c.storageTexture ? 1 : 0)
        << " storageAccess=" << static_cast<uint32_t>(c.storageAccess)
        << " storageFormat=" << static_cast<uint32_t>(c.storageTexture ? c.format : WGPUTextureFormat_Undefined);
    return key.str();
}

const MetadataPipelineBundle& metadataPipelineForDevice(AllFeaturesMaxLimitsGpuTest& t, const std::string& wgsl, const MetadataQueryCase& c) {
    static std::unordered_map<WGPUDevice, std::unordered_map<std::string, MetadataPipelineBundle>> cache;
    auto& deviceCache = cache[t.device()];
    const std::string key = metadataPipelineKey(wgsl, c);
    auto it = deviceCache.find(key);
    if (it == deviceCache.end()) {
        it = deviceCache.emplace(key, createMetadataPipelineBundle(t.device(), wgsl, c)).first;
    }
    return it->second;
}

void skipIfNoStorageTexturesInStage(AllFeaturesMaxLimitsGpuTest& t, const std::string& stage) {
    const WGPUCompatibilityModeLimits limits = t.getCompatibilityModeLimits();
    if (stage == "fragment" && limits.maxStorageTexturesInFragmentStage == 0) {
        t.skip("device does not support storage textures in fragment shaders");
    }
    if (stage == "vertex" && limits.maxStorageTexturesInVertexStage == 0) {
        t.skip("device does not support storage textures in vertex shaders");
    }
}

void skipIfMetadataTextureUnsupported(AllFeaturesMaxLimitsGpuTest& t, const MetadataQueryCase& c) {
    t.skipIfTextureFormatNotSupported(c.format);
    t.skipIfTextureViewDimensionNotSupported(c.viewDimension);
    t.skipIfTextureFormatAndDimensionNotCompatible(c.format, c.textureDimension);
    if (c.sampleCount > 1 && !t.isTextureFormatMultisampled(c.format)) {
        t.skip("texture format is not multisampled");
    }
    if ((c.usage & WGPUTextureUsage_RenderAttachment) != 0) {
        t.skipIfTextureFormatNotUsableAsRenderAttachment(c.format);
    }
    if (c.storageTexture) {
        skipIfNoStorageTexturesInStage(t, c.stage);
        if (!t.isTextureFormatUsableWithStorageAccessMode(c.format, c.storageAccess)) {
            t.skip("texture format is not usable with requested storage access mode");
        }
    }
    if (c.textureDimension == WGPUTextureDimension_3D && isBCTextureFormat(c.format)
        && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TextureCompressionBCSliced3D)) {
        t.skip("3D BC compressed textures require texture-compression-bc-sliced-3d");
    }
    if (c.textureDimension == WGPUTextureDimension_3D && isASTCTextureFormat(c.format)
        && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TextureCompressionASTCSliced3D)) {
        t.skip("3D ASTC compressed textures require texture-compression-astc-sliced-3d");
    }
}

void executeMetadataQuery(AllFeaturesMaxLimitsGpuTest& t, const MetadataQueryCase& c) {
    skipIfMetadataTextureUnsupported(t, c);

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = c.size;
    textureDesc.mipLevelCount = c.textureMipCount;
    textureDesc.sampleCount = c.sampleCount;
    textureDesc.dimension = c.textureDimension;
    textureDesc.format = c.format;
    textureDesc.usage = c.usage;
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.dimension = c.viewDimension;
    viewDesc.format = c.storageTexture ? c.format : WGPUTextureFormat_Undefined;
    viewDesc.aspect = c.aspect;
    viewDesc.baseMipLevel = c.baseMipLevel;
    viewDesc.mipLevelCount = c.viewMipCount;
    viewDesc.baseArrayLayer = c.baseArrayLayer;
    viewDesc.arrayLayerCount = c.arrayLayerCount;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUBufferDescriptor resultDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    resultDesc.size = 256;
    resultDesc.usage = c.stage == "compute"
        ? WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc
        : WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer resultBuffer = t.createBufferTracked(resultDesc);

    const std::string wgsl = buildMetadataWgsl(c);
    const MetadataPipelineBundle& pipeline = metadataPipelineForDevice(t, wgsl, c);

    WGPUBindGroupEntry textureEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    textureEntry.binding = 0;
    textureEntry.textureView = view;
    WGPUBindGroupDescriptor textureBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    textureBindGroupDesc.layout = pipeline.textureBindGroupLayout;
    textureBindGroupDesc.entryCount = 1;
    textureBindGroupDesc.entries = &textureEntry;
    WGPUBindGroup textureBindGroup = t.createBindGroupTracked(textureBindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    if (c.stage == "compute") {
        WGPUBindGroupEntry resultEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        resultEntry.binding = 0;
        resultEntry.buffer = resultBuffer;
        resultEntry.size = 16;
        WGPUBindGroupDescriptor resultBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        resultBindGroupDesc.layout = pipeline.resultBindGroupLayout;
        resultBindGroupDesc.entryCount = 1;
        resultBindGroupDesc.entries = &resultEntry;
        WGPUBindGroup resultBindGroup = t.createBindGroupTracked(resultBindGroupDesc);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline.computePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, textureBindGroup, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 1, resultBindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    } else {
        WGPUTextureDescriptor targetDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        targetDesc.size = WGPUExtent3D{1, 1, 1};
        targetDesc.mipLevelCount = 1;
        targetDesc.sampleCount = 1;
        targetDesc.dimension = WGPUTextureDimension_2D;
        targetDesc.format = WGPUTextureFormat_RGBA32Uint;
        targetDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture target = t.createTextureTracked(targetDesc);
        WGPUTextureViewDescriptor targetViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        targetViewDesc.dimension = WGPUTextureViewDimension_2D;
        targetViewDesc.format = WGPUTextureFormat_RGBA32Uint;
        WGPUTextureView targetView = t.createViewTracked(target, targetViewDesc);

        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = targetView;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline.renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, textureBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        t.copyTextureToBuffer(encoder, target, resultBuffer, 256, targetDesc.size);
    }
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        resultBuffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 16) {
                return std::string("metadata output buffer too small");
            }
            for (uint32_t i = 0; i < 4; ++i) {
                uint32_t got = 0;
                std::memcpy(&got, actual + i * sizeof(uint32_t), sizeof(uint32_t));
                if (got != c.expected[i]) {
                    std::ostringstream msg;
                    msg << "metadata query mismatch component " << i
                        << ": expected " << c.expected[i] << ", got " << got
                        << ", textureType " << c.textureType << ", stage " << c.stage;
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        16);
}

MetadataQueryCase dimensionsCase(AllFeaturesMaxLimitsGpuTest& t, bool depth, bool storage) {
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.viewDimension = parseViewDimension(t.param<std::string>("dimensions"));
    c.textureDimension = textureDimensionForView(c.viewDimension);
    c.aspect = parseAspect(t.param<std::string>("aspect"));
    c.sampleCount = t.hasParam("samples") ? paramU32(t, "samples") : 1u;
    c.textureMipCount = paramU32(t, "textureMipCount");
    c.baseMipLevel = paramU32(t, "baseMipLevel");
    const uint32_t queryLevel = (!t.hasParam("textureDimensionsLevel") || t.paramIsUndefined("textureDimensionsLevel"))
        ? 0u
        : paramU32(t, "textureDimensionsLevel");
    const uint32_t mip = c.baseMipLevel + queryLevel;
    c.size = metadataTestSize(c.viewDimension, c.format, 0);
    c.expected = expectedDimensions(c.viewDimension, c.size, mip);
    c.expectedCount = dimensionReturnCount(c.viewDimension);
    c.usage = c.sampleCount == 1
        ? WGPUTextureUsage_TextureBinding
        : WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    if (storage) {
        c.storageTexture = true;
        c.storageAccess = parseStorageAccessParam(t.param<std::string>("access"));
        c.usage = WGPUTextureUsage_StorageBinding;
        c.sampleType = WGPUTextureSampleType_BindingNotUsed;
        c.viewMipCount = 1;
        c.textureType = "texture_storage_" + wgslDimensionName(c.viewDimension) + "<"
            + std::string(textureFormatInfo(c.format).identifier) + ", "
            + storageAccessWGSL(c.storageAccess) + ">";
        c.valueExpr = "textureDimensions(texture)";
    } else if (depth) {
        const std::string base = c.sampleCount > 1 ? "texture_depth_multisampled" : "texture_depth";
        c.textureType = base + "_" + wgslDimensionName(c.viewDimension);
        c.sampleType = WGPUTextureSampleType_Depth;
        c.valueExpr = (!t.hasParam("textureDimensionsLevel") || t.paramIsUndefined("textureDimensionsLevel"))
            ? "textureDimensions(texture)"
            : ("textureDimensions(texture, " + std::to_string(queryLevel) + "u)");
    } else {
        const std::string base = c.sampleCount > 1 ? "texture_multisampled" : "texture";
        c.textureType = base + "_" + wgslDimensionName(c.viewDimension) + "<"
            + sampledWGSLTypeFor(c.format, c.aspect) + ">";
        c.sampleType = metadataSampleType(c.format, c.aspect, false, c.sampleCount);
        c.valueExpr = (!t.hasParam("textureDimensionsLevel") || t.paramIsUndefined("textureDimensionsLevel"))
            ? "textureDimensions(texture)"
            : ("textureDimensions(texture, " + std::to_string(queryLevel) + "u)");
    }
    return c;
}

} // namespace

std::vector<Value> shortShaderStages() {
    return values({"vertex", "fragment", "compute"});
}

std::vector<Value> allTextureFormats() {
    std::vector<Value> out = formatValues(kUncompressedTextureFormats);
    std::vector<Value> compressed = formatValues(kCompressedTextureFormats);
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
}

std::vector<Value> depthStencilFormats() {
    return formatValues(kDepthStencilFormats);
}

std::vector<Value> possibleStorageTextureFormats() {
    std::vector<WGPUTextureFormat> formats;
    formats.reserve(kStorageTextureFormats.size() + 1u + kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly.size());
    for (WGPUTextureFormat format : kStorageTextureFormats) {
        formats.push_back(format);
    }
    formats.push_back(WGPUTextureFormat_BGRA8Unorm);
    for (WGPUTextureFormat format : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        formats.push_back(format);
    }
    return formatValues(formats);
}

std::vector<Value> shortAddressModes() {
    return values({"clamp", "repeat", "mirror"});
}

std::vector<Value> samplePointMethods() {
    return values({"texel-centre", "spiral"});
}

std::vector<Value> cubeSamplePointMethods() {
    return values({"cube-edges", "texel-centre", "spiral"});
}

std::vector<Value> compareFunctions() {
    return values({"never", "less", "equal", "less-equal", "greater", "not-equal", "greater-equal", "always"});
}

bool isPotentiallyFilterableAndFillable(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    if (isStencilOnlyFormat(format) || isCompressedFloatFormat(format)) {
        return false;
    }
    return isDepthFormat(format) || isColorFloatLikeFormat(format);
}

bool isFilterNearestOrFormatPossiblyFilterableAsTextureF32(const ParamRecord& record) {
    return paramString(record, "filt") == "nearest" || isPossiblyFilterableAsTextureF32(paramFormat(record));
}

bool isDepthTextureFormatParam(const ParamRecord& record) {
    return isDepthFormat(paramFormat(record));
}

bool cubeEdgesOnlyForCube(const ParamRecord& record) {
    return paramString(record, "samplePoints") != "cube-edges" || paramString(record, "dim") != "3d";
}

bool cubeOffsetsUnsupported(const ParamRecord& record) {
    const Value* offset = findParam(record, "offset");
    return paramString(record, "dim") != "cube" || offset == nullptr || !valueAs<bool>(*offset);
}

bool isStage1Sampled2DSupported(const ParamRecord& record) {
    return isIncrement2SampledFormatSupported(record);
}

bool isIncrement2SampledFormatSupported(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    return isUncompressed(format) && isColorFloatLikeFormat(format);
}

bool isSampledColorTextureFormatParam(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    return isColorTextureFormat(format);
}

bool isSampled1DColorTextureFormatParam(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    return isColorTextureFormat(format) && isUncompressed(format);
}

bool isComputeStage(const ParamRecord& record) {
    return paramString(record, "stage") == "compute";
}

bool isStorageReadWriteFormatParam(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    return textureFormatInList(format, kReadWriteStorageTextureFormats)
        || textureFormatInList(format, kTextureFormatsTier2EnablesStorageReadWrite);
}

bool isStorageReadWriteAccessOrFormatSupported(const ParamRecord& record) {
    const std::string access = paramString(record, "access");
    const std::string accessMode = paramString(record, "access_mode");
    const bool readWrite = access == "read_write" || accessMode == "read_write";
    return !readWrite || isStorageReadWriteFormatParam(record);
}

bool isNotWritableStorageInVertexStage(const ParamRecord& record) {
    const std::string access = paramString(record, "access");
    const std::string accessMode = paramString(record, "access_mode");
    const std::string value = access.empty() ? accessMode : access;
    return paramString(record, "stage") != "vertex" || value == "read";
}

ParamsBuilder addSampledTextureCommonParams(ParamsBuilder u, bool includeModeU, bool includeModeV) {
    u = u.combine("stage", shortShaderStages())
            .combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .combine("filt", values({"nearest", "linear"}))
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32);
    if (includeModeU) {
        u = u.combine("modeU", shortAddressModes());
    }
    if (includeModeV) {
        u = u.combine("modeV", shortAddressModes());
    }
    return u;
}

ParamsBuilder addDepthTextureCommonParams(ParamsBuilder u) {
    return u.combine("stage", shortShaderStages())
        .combine("format", depthStencilFormats())
        .filter(isDepthTextureFormatParam)
        .combine("mode", shortAddressModes());
}

std::vector<ParamRecord> lodClampParams() {
    return {
        {{"baseMipLevel", 0}, {"lodMinClamp", 0.0}, {"lodMaxClamp", 2.0}},
        {{"baseMipLevel", 0}, {"lodMinClamp", 0.25}, {"lodMaxClamp", 1.75}},
        {{"baseMipLevel", 1}, {"lodMinClamp", 0.0}, {"lodMaxClamp", 1.0}},
        {{"baseMipLevel", 0}, {"lodMinClamp", 0.0}, {"lodMaxClamp", 1.0}},
        {{"baseMipLevel", 0}, {"lodMinClamp", 1.0}, {"lodMaxClamp", 2.0}},
    };
}

std::vector<ParamRecord> depth3DViewDimensionParams() {
    return {
        {{"viewDimension", "cube"}},
        {{"viewDimension", "cube-array"}, {"A", "i32"}},
        {{"viewDimension", "cube-array"}, {"A", "u32"}},
    };
}

void executeTextureSampleLevelStage1(AllFeaturesMaxLimitsGpuTest& t) {
    executeTextureSampleLevelSampled2D(t);
}

void executeTextureSampleLevelSampled1D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseSampledCase(t, SampleKind::Sampled1D);
    c.textureDimension = WGPUTextureDimension_1D;
    c.viewDimension = WGPUTextureViewDimension_1D;
    c.baseSize = baseSize(8, 1, 1);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    executeCase(t, c);
}

void executeTextureSampleLevelSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseSampledCase(t, SampleKind::Sampled2D);
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleLevelSampled2DLodClamp(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseSampledCase(t, SampleKind::Sampled2DLodClamp);
    c.baseSize = baseSize(8, 8, 1);
    c.baseMipLevel = static_cast<uint32_t>(t.param<int>("baseMipLevel"));
    c.lodMinClamp = t.param<double>("lodMinClamp");
    c.lodMaxClamp = t.param<double>("lodMaxClamp");
    executeCase(t, c);
}

void executeTextureSampleLevelSampled2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseSampledCase(t, SampleKind::Sampled2DArray);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleLevelSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string dim = t.param<std::string>("dim");
    TextureCase c = baseSampledCase(t, dim == "cube" ? SampleKind::SampledCube : SampleKind::Sampled3D);
    c.textureDimension = dim == "3d" ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    c.viewDimension = dim == "3d" ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_Cube;
    c.baseSize = dim == "3d" ? baseSize(8, 8, 8) : baseSize(8, 8, 6);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useOffset = dim == "3d" && t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleLevelSampled3DLodClamp(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string dim = t.param<std::string>("dim");
    TextureCase c = baseSampledCase(t, dim == "cube" ? SampleKind::SampledCubeLodClamp : SampleKind::Sampled3DLodClamp);
    c.textureDimension = dim == "3d" ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    c.viewDimension = dim == "3d" ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_Cube;
    c.baseSize = dim == "3d" ? baseSize(8, 8, 8) : baseSize(8, 8, 6);
    c.baseMipLevel = static_cast<uint32_t>(t.param<int>("baseMipLevel"));
    c.lodMinClamp = t.param<double>("lodMinClamp");
    c.lodMaxClamp = t.param<double>("lodMaxClamp");
    executeCase(t, c);
}

void executeTextureSampleLevelSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseSampledCase(t, SampleKind::SampledCubeArray);
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(8, 8, 24);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleLevelDepth2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseDepthCase(t, SampleKind::Depth2D);
    c.baseSize = baseSize(8, 8, 1);
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleLevelDepth2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseDepthCase(t, SampleKind::Depth2DArray);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleLevelDepth3D(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string viewDimension = t.param<std::string>("viewDimension");
    TextureCase c = baseDepthCase(t, viewDimension == "cube" ? SampleKind::DepthCube : SampleKind::DepthCubeArray);
    c.viewDimension = viewDimension == "cube" ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_CubeArray;
    c.baseSize = viewDimension == "cube" ? baseSize(8, 8, 6) : baseSize(8, 8, 24);
    if (viewDimension == "cube-array") {
        c.useArrayIndex = true;
        c.arrayIndexType = t.param<std::string>("A");
    }
    executeCase(t, c);
}

void executeTextureSampleSampled1D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleSampledCase(t, SampleKind::Sampled1D);
    c.textureDimension = WGPUTextureDimension_1D;
    c.viewDimension = WGPUTextureViewDimension_1D;
    c.baseSize = baseSize(8, 1, 1);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    executeCase(t, c);
}

void executeTextureSampleSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleSampledCase(t, SampleKind::Sampled2D);
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleSampled2DLodClamp(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleSampledCase(t, SampleKind::Sampled2DLodClamp);
    c.baseSize = baseSize(8, 8, 1);
    c.baseMipLevel = static_cast<uint32_t>(t.param<int>("baseMipLevel"));
    c.lodMinClamp = t.param<double>("lodMinClamp");
    c.lodMaxClamp = t.param<double>("lodMaxClamp");
    executeCase(t, c);
}

void executeTextureSampleSampled2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleSampledCase(t, SampleKind::Sampled2DArray);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string dim = t.param<std::string>("dim");
    TextureCase c = baseTextureSampleSampledCase(t, dim == "cube" ? SampleKind::SampledCube : SampleKind::Sampled3D);
    c.textureDimension = dim == "3d" ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    c.viewDimension = dim == "3d" ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_Cube;
    c.baseSize = dim == "3d" ? baseSize(8, 8, 8) : baseSize(32, 32, 6);
    c.mipLevelCount = dim == "3d" ? kMipLevelCount : 1;
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.modeW = addressModeFromShort(t.param<std::string>("modeW"));
    c.useOffset = dim == "3d" && t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleSampledCase(t, SampleKind::SampledCubeArray);
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(32, 32, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleDepth2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleDepthCase(t, SampleKind::Depth2D);
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleDepth2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleDepthCase(t, SampleKind::Depth2DArray);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleDepth3D(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string viewDimension = t.param<std::string>("viewDimension");
    TextureCase c = baseTextureSampleDepthCase(t, viewDimension == "cube" ? SampleKind::DepthCube : SampleKind::DepthCubeArray);
    c.viewDimension = viewDimension == "cube" ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_CubeArray;
    c.baseSize = viewDimension == "cube" ? baseSize(32, 32, 6) : baseSize(32, 32, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    if (viewDimension == "cube-array") {
        c.useArrayIndex = true;
        c.arrayIndexType = t.param<std::string>("A");
    }
    executeCase(t, c);
}

void executeTextureSampleDepthCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleDepthCase(t, SampleKind::DepthCubeArray);
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(32, 32, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleGradSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleGradSampledCase(t, SampleKind::Sampled2D);
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleGradSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string dim = t.param<std::string>("dim");
    TextureCase c = baseTextureSampleGradSampledCase(t, dim == "cube" ? SampleKind::SampledCube : SampleKind::Sampled3D);
    c.textureDimension = dim == "3d" ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    c.viewDimension = dim == "3d" ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_Cube;
    c.baseSize = dim == "3d" ? baseSize(8, 8, 8) : baseSize(32, 32, 6);
    c.mipLevelCount = dim == "3d" ? kMipLevelCount : 1;
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.modeW = addressModeFromShort(t.param<std::string>("modeW"));
    c.useOffset = dim == "3d" && t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleGradSampled2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleGradSampledCase(t, SampleKind::Sampled2DArray);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleGradSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleGradSampledCase(t, SampleKind::SampledCubeArray);
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(32, 32, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleBiasSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleBiasSampledCase(t, SampleKind::Sampled2D);
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleBiasSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string dim = t.param<std::string>("dim");
    TextureCase c = baseTextureSampleBiasSampledCase(t, dim == "cube" ? SampleKind::SampledCube : SampleKind::Sampled3D);
    c.textureDimension = dim == "3d" ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    c.viewDimension = dim == "3d" ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_Cube;
    c.baseSize = dim == "3d" ? baseSize(8, 8, 8) : baseSize(32, 32, 6);
    c.mipLevelCount = dim == "3d" ? kMipLevelCount : 1;
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.modeW = addressModeFromShort(t.param<std::string>("modeW"));
    c.useOffset = dim == "3d" && t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleBiasSampled2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleBiasSampledCase(t, SampleKind::Sampled2DArray);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, 4);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleBiasSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleBiasSampledCase(t, SampleKind::SampledCubeArray);
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(32, 32, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleCompare2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::Depth2D, "fragment", "textureSampleCompare");
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleCompareCube(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::DepthCube, "fragment", "textureSampleCompare");
    c.viewDimension = WGPUTextureViewDimension_Cube;
    c.baseSize = baseSize(32, 32, 6);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    executeCase(t, c);
}

void executeTextureSampleCompare2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::Depth2DArray, "fragment", "textureSampleCompare");
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleCompareCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::DepthCubeArray, "fragment", "textureSampleCompare");
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(32, 32, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleCompareLevel2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::Depth2D, t.param<std::string>("stage"), "textureSampleCompareLevel");
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeCase(t, c);
}

void executeTextureSampleCompareLevelCube(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::DepthCube, t.param<std::string>("stage"), "textureSampleCompareLevel");
    c.viewDimension = WGPUTextureViewDimension_Cube;
    c.baseSize = baseSize(32, 32, 6);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    executeCase(t, c);
}

void executeTextureSampleCompareLevel2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::Depth2DArray, t.param<std::string>("stage"), "textureSampleCompareLevel");
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleCompareLevelCubeArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleCompareCase(t, SampleKind::DepthCubeArray, t.param<std::string>("stage"), "textureSampleCompareLevel");
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(8, 8, 24);
    c.mipLevelCount = 1;
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeCase(t, c);
}

void executeTextureSampleBaseClampToEdge2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseTextureSampleBaseClampToEdgeCase(t);
    executeCase(t, c);
}

std::vector<Value> textureMetadataAspectsForFormat(const ParamRecord& record) {
    return formatAspects(paramFormat(record));
}

std::vector<Value> textureMetadataSamplesForFormat(const ParamRecord& record) {
    return formatSamples(paramFormat(record));
}

std::vector<Value> textureMetadataViewDimensions(const ParamRecord& record) {
    const uint32_t samples = findParam(record, "samples") == nullptr ? 1u : paramU32(record, "samples");
    return viewDimensionsFor(paramFormat(record), samples);
}

std::vector<Value> textureMetadataStorageViewDimensions(const ParamRecord& record) {
    std::vector<Value> out;
    for (const Value& value : textureMetadataViewDimensions(record)) {
        if (dimensionsValidForStorage(parseViewDimension(valueAs<std::string>(value)))) {
            out.push_back(value);
        }
    }
    return out;
}

std::vector<Value> textureMetadataMipCounts(const ParamRecord& record) {
    const uint32_t samples = findParam(record, "samples") == nullptr ? 1u : paramU32(record, "samples");
    return metadataMipCounts(paramFormat(record), parseViewDimension(viewDimensionParam(record)), samples);
}

std::vector<Value> textureMetadataBaseMipLevels(const ParamRecord& record) {
    return metadataBaseMipLevels(paramU32(record, "textureMipCount"));
}

std::vector<Value> textureMetadataDimensionsLevels(const ParamRecord& record) {
    const uint32_t samples = findParam(record, "samples") == nullptr ? 1u : paramU32(record, "samples");
    return metadataDimensionsLevels(samples, paramU32(record, "textureMipCount"), paramU32(record, "baseMipLevel"));
}

void executeTextureDimensionsSampledAndMultisampled(AllFeaturesMaxLimitsGpuTest& t) {
    executeMetadataQuery(t, dimensionsCase(t, false, false));
}

void executeTextureDimensionsDepth(AllFeaturesMaxLimitsGpuTest& t) {
    MetadataQueryCase c = dimensionsCase(t, true, false);
    if (c.aspect == WGPUTextureAspect_StencilOnly) {
        t.skip("texture_depth_* cannot bind stencil-only texture views");
    }
    executeMetadataQuery(t, c);
}

void executeTextureDimensionsStorage(AllFeaturesMaxLimitsGpuTest& t) {
    executeMetadataQuery(t, dimensionsCase(t, false, true));
}

void executeTextureDimensionsExternal(AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("texture_external is not exposed through the native C WebGPU API path used by this port");
}

void executeTextureNumLevelsSampled(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string textureType = t.param<std::string>("texture_type");
    const std::string sampledType = t.param<std::string>("sampled_type");
    const std::string viewType = t.param<std::string>("view_type");
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = parseViewDimension(textureType == "texture_1d" ? "1d"
        : textureType == "texture_2d" ? "2d"
        : textureType == "texture_2d_array" ? "2d-array"
        : textureType == "texture_3d" ? "3d"
        : textureType == "texture_cube" ? "cube"
        : "cube-array");
    c.textureDimension = textureDimensionForView(c.viewDimension);
    c.format = sampledType == "i32" ? WGPUTextureFormat_RGBA8Sint
        : sampledType == "u32" ? WGPUTextureFormat_RGBA8Uint
        : WGPUTextureFormat_RGBA8Unorm;
    c.sampleType = sampledType == "i32" ? WGPUTextureSampleType_Sint
        : sampledType == "u32" ? WGPUTextureSampleType_Uint
        : WGPUTextureSampleType_Float;
    c.size = WGPUExtent3D{64, c.textureDimension == WGPUTextureDimension_1D ? 1u : 64u,
        textureType.find("cube") != std::string::npos ? 6u : 1u};
    c.textureMipCount = c.textureDimension == WGPUTextureDimension_1D ? 1u : 4u;
    c.baseMipLevel = viewType == "partial" ? 1u : 0u;
    c.viewMipCount = viewType == "partial" ? 2u : c.textureMipCount;
    c.expected = {c.viewMipCount, 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = textureType + "<" + sampledType + ">";
    c.valueExpr = "textureNumLevels(texture)";
    executeMetadataQuery(t, c);
}

void executeTextureNumLevelsDepth(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string textureType = t.param<std::string>("texture_type");
    const std::string viewType = t.param<std::string>("view_type");
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = parseViewDimension(textureType == "texture_depth_2d" ? "2d"
        : textureType == "texture_depth_2d_array" ? "2d-array"
        : textureType == "texture_depth_cube" ? "cube"
        : "cube-array");
    c.textureDimension = textureDimensionForView(c.viewDimension);
    c.format = WGPUTextureFormat_Depth32Float;
    c.sampleType = WGPUTextureSampleType_Depth;
    c.size = WGPUExtent3D{64, 64, textureType.find("cube") != std::string::npos ? 6u : 1u};
    c.textureMipCount = 4;
    c.baseMipLevel = viewType == "partial" ? 1u : 0u;
    c.viewMipCount = viewType == "partial" ? 2u : c.textureMipCount;
    c.expected = {c.viewMipCount, 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = textureType;
    c.valueExpr = "textureNumLevels(texture)";
    executeMetadataQuery(t, c);
}

void executeTextureNumLayersSampled(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string textureType = t.param<std::string>("texture_type");
    const std::string sampledType = t.param<std::string>("sampled_type");
    const std::string viewType = t.param<std::string>("view_type");
    const bool cubeArray = textureType == "texture_cube_array";
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = cubeArray ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_2DArray;
    c.textureDimension = WGPUTextureDimension_2D;
    c.format = sampledType == "i32" ? WGPUTextureFormat_RGBA8Sint
        : sampledType == "u32" ? WGPUTextureFormat_RGBA8Uint
        : WGPUTextureFormat_RGBA8Unorm;
    c.sampleType = sampledType == "i32" ? WGPUTextureSampleType_Sint
        : sampledType == "u32" ? WGPUTextureSampleType_Uint
        : WGPUTextureSampleType_Float;
    c.size = WGPUExtent3D{1, 1, kMetadataNumLayers};
    c.baseArrayLayer = viewType == "partial" ? 11u : 0u;
    c.arrayLayerCount = viewType == "partial" ? 6u : kMetadataNumLayers;
    c.expected = {c.arrayLayerCount / (cubeArray ? 6u : 1u), 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = textureType + "<" + sampledType + ">";
    c.valueExpr = "textureNumLayers(texture)";
    executeMetadataQuery(t, c);
}

void executeTextureNumLayersArrayed(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string textureType = t.param<std::string>("texture_type");
    const std::string viewType = t.param<std::string>("view_type");
    const bool cubeArray = textureType == "texture_depth_cube_array";
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = cubeArray ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_2DArray;
    c.textureDimension = WGPUTextureDimension_2D;
    c.format = WGPUTextureFormat_Depth32Float;
    c.sampleType = WGPUTextureSampleType_Depth;
    c.size = WGPUExtent3D{1, 1, kMetadataNumLayers};
    c.baseArrayLayer = viewType == "partial" ? 11u : 0u;
    c.arrayLayerCount = viewType == "partial" ? 6u : kMetadataNumLayers;
    c.expected = {c.arrayLayerCount / (cubeArray ? 6u : 1u), 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = textureType;
    c.valueExpr = "textureNumLayers(texture)";
    executeMetadataQuery(t, c);
}

void executeTextureNumLayersStorage(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string viewType = t.param<std::string>("view_type");
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.textureDimension = WGPUTextureDimension_2D;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.storageTexture = true;
    c.storageAccess = parseStorageAccessParam(t.param<std::string>("access_mode"));
    c.usage = WGPUTextureUsage_StorageBinding;
    c.size = WGPUExtent3D{1, 1, kMetadataNumLayers};
    c.baseArrayLayer = viewType == "partial" ? 11u : 0u;
    c.arrayLayerCount = viewType == "partial" ? 6u : kMetadataNumLayers;
    c.expected = {c.arrayLayerCount, 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = "texture_storage_2d_array<" + t.param<std::string>("format") + ", "
        + storageAccessWGSL(c.storageAccess) + ">";
    c.valueExpr = "textureNumLayers(texture)";
    executeMetadataQuery(t, c);
}

void executeTextureNumSamplesSampled(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string sampledType = t.param<std::string>("sampled_type");
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = WGPUTextureViewDimension_2D;
    c.textureDimension = WGPUTextureDimension_2D;
    c.format = sampledType == "i32" ? WGPUTextureFormat_RGBA8Sint
        : sampledType == "u32" ? WGPUTextureFormat_RGBA8Uint
        : WGPUTextureFormat_RGBA8Unorm;
    c.sampleType = sampledType == "i32" ? WGPUTextureSampleType_Sint
        : sampledType == "u32" ? WGPUTextureSampleType_Uint
        : WGPUTextureSampleType_UnfilterableFloat;
    c.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    c.sampleCount = 4;
    c.size = WGPUExtent3D{1, 1, 1};
    c.expected = {c.sampleCount, 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = "texture_multisampled_2d<" + sampledType + ">";
    c.valueExpr = "textureNumSamples(texture)";
    executeMetadataQuery(t, c);
}

void executeTextureNumSamplesDepth(AllFeaturesMaxLimitsGpuTest& t) {
    MetadataQueryCase c;
    c.stage = t.param<std::string>("stage");
    c.viewDimension = WGPUTextureViewDimension_2D;
    c.textureDimension = WGPUTextureDimension_2D;
    c.format = WGPUTextureFormat_Depth32Float;
    c.sampleType = WGPUTextureSampleType_Depth;
    c.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    c.sampleCount = 4;
    c.size = WGPUExtent3D{1, 1, 1};
    c.expected = {c.sampleCount, 0, 0, 0};
    c.expectedCount = 1;
    c.textureType = "texture_depth_multisampled_2d";
    c.valueExpr = "textureNumSamples(texture)";
    executeMetadataQuery(t, c);
}

} // namespace cts::texture_utils
