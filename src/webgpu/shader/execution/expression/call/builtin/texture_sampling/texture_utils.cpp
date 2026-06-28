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
#include "webgpu/util/enum_strings.h"
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
    static constexpr std::array<double, 6> kMipLevels = {{0.20, 0.80, 1.20, 1.80, 0.35, 1.65}};
    return std::min(kMipLevels[index % kMipLevels.size()], static_cast<double>(mipLevelCount - 1u));
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
    bool gather = false;
    bool gatherCompare = false;
    int gatherComponent = 0;
    std::string gatherResultType = "f32";
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

WGPUTextureFormat viewFormatForAspect(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    if (aspect == WGPUTextureAspect_DepthOnly) {
        if (format == WGPUTextureFormat_Depth32FloatStencil8) {
            return WGPUTextureFormat_Depth32Float;
        }
        if (format == WGPUTextureFormat_Depth24PlusStencil8) {
            return WGPUTextureFormat_Depth24Plus;
        }
        return format;
    }
    if (aspect == WGPUTextureAspect_StencilOnly) {
        return WGPUTextureFormat_Stencil8;
    }
    return format;
}

std::string textureType(const TextureCase& c) {
    std::string suffixStr = c.isDepth ? "" : "<f32>";
    if (c.gather && !c.isDepth) {
        suffixStr = "<" + c.gatherResultType + ">";
    }
    const char* suffix = suffixStr.c_str();
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
    // For cube gather, generate interior (non-edge) face points with a 0.75
    // sub-texel offset so the 2x2 footprint stays within a single face and is
    // unambiguous. Upstream's cube gather likewise avoids face edges/corners,
    // which are implementation-defined for gather.
    const bool cubeGather = c.gather && isCubeKind(c.kind);
    const bool cubeEdges = c.samplePoints == "cube-edges" && !cubeGather;
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
        if (cubeGather) {
            // Pick an interior texel on a rotating face and offset by 0.75.
            const uint32_t w = std::max(2u, c.baseSize.width);
            const uint32_t h = std::max(2u, std::max(1u, c.baseSize.height));
            const uint32_t face = i % 6u;
            const uint32_t tx = 1u + (i % std::max(1u, w - 2u));
            const uint32_t ty = 1u + ((i / 6u) % std::max(1u, h - 2u));
            const Vec3 uvLayer{
                (static_cast<double>(tx) + 0.75) / static_cast<double>(w),
                (static_cast<double>(ty) + 0.75) / static_cast<double>(h),
                (static_cast<double>(face) + 0.5) / 6.0,
            };
            call.coords = normalized3DTextureCoordToCubeCoord(uvLayer);
        } else if (cubeEdges) {
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
        // Gather selects the 2x2 footprint of one channel. At a texel centre the
        // footprint center lands exactly on a texel boundary (coord*W-0.5 is an
        // integer), where the hardware may round the four texels in either
        // direction. Snap non-cube gather coordinates to texel+0.75 so the
        // footprint is unambiguous (matches upstream's 0.75 offset trick).
        if (c.gather && !isCubeKind(c.kind)) {
            const double w = static_cast<double>(c.baseSize.width);
            const double h = static_cast<double>(std::max(1u, c.baseSize.height));
            const double tx = std::floor(std::clamp(call.coords.x, 0.0, 1.0) * w);
            const double ty = std::floor(std::clamp(call.coords.y, 0.0, 1.0) * h);
            call.coords.x = (std::min(tx, w - 1.0) + 0.75) / w;
            call.coords.y = (std::min(ty, h - 1.0) + 0.75) / h;
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
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", f32(0.0));\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_2DArray) {
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", i32(id.z), f32(0.0));\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_3D) {
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", f32(0.0));\n";
    } else if (c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", i32(id.z / 6u), f32(0.0));\n";
    } else {
        wgsl << "  out[ndx] = textureSampleLevel(tex, samp, " << materializeCoordWGSL(c) << ", f32(0.0));\n";
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
    if (c.viewDimension == WGPUTextureViewDimension_2DArray) {
        materializeCase.kind = SampleKind::Sampled2DArray;
        materializeCase.useArrayIndex = true;
    } else if (c.viewDimension == WGPUTextureViewDimension_3D) {
        materializeCase.kind = SampleKind::Sampled3D;
    } else if (c.viewDimension == WGPUTextureViewDimension_1D) {
        materializeCase.kind = SampleKind::Sampled1D;
    } else {
        materializeCase.kind = SampleKind::Sampled2D;
    }
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
            ds.depthClearValue = static_cast<float>(depthValue(mip, layer));
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

enum class LoadReturnKind {
    Float,
    Sint,
    Uint,
    Depth,
};

struct LoadCallData {
    std::array<uint32_t, 4> coords = {};
    std::array<uint32_t, 4> scalars = {};
};

static_assert(sizeof(LoadCallData) == 32);

struct TextureLoadCase {
    std::string stage = "compute";
    std::string samplePoints = "texel-centre";
    std::string textureType;
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTextureDimension textureDimension = WGPUTextureDimension_2D;
    WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D;
    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTextureSampleType sampleType = WGPUTextureSampleType_Float;
    WGPUStorageTextureAccess storageAccess = WGPUStorageTextureAccess_BindingNotUsed;
    bool storageTexture = false;
    bool isDepth = false;
    bool multisampled = false;
    uint32_t coordComponents = 2;
    bool useLevel = true;
    bool useArrayIndex = false;
    bool useSampleIndex = false;
    std::string coordType = "i32";
    std::string levelType = "i32";
    std::string arrayIndexType = "u32";
    std::string sampleIndexType = "i32";
    WGPUExtent3D baseSize = WGPUExtent3D{8, 8, 1};
    uint32_t mipLevelCount = 1;
    uint32_t sampleCount = 1;
    uint32_t baseMipLevel = 0;
    uint32_t viewMipCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    LoadReturnKind returnKind = LoadReturnKind::Float;
};

struct TextureLoadPipelineBundle {
    WGPUShaderModule module = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
};

std::string stageFromLoadShort(const std::string& stage) {
    if (stage == "c") return "compute";
    if (stage == "f") return "fragment";
    if (stage == "v") return "vertex";
    return stage;
}

bool isCompressedFloatTextureLoadFormat(WGPUTextureFormat format) {
    return isCompressedTextureFormat(format)
        && std::string_view(textureFormatInfo(format).identifier).find("float") != std::string_view::npos;
}

LoadReturnKind loadReturnKindForFormat(WGPUTextureFormat format, bool depth) {
    if (depth) return LoadReturnKind::Depth;
    if (formatLooksUint(format) || isStencilOnlyFormat(format)) return LoadReturnKind::Uint;
    if (formatLooksSint(format)) return LoadReturnKind::Sint;
    return LoadReturnKind::Float;
}

std::string loadComponentType(LoadReturnKind kind) {
    if (kind == LoadReturnKind::Uint) return "u32";
    if (kind == LoadReturnKind::Sint) return "i32";
    return "f32";
}

std::string appendLoadComponentType(const std::string& base, WGPUTextureFormat format, bool depth) {
    if (depth || base.find("depth") != std::string::npos || base.find("storage") != std::string::npos) {
        return base;
    }
    return base + "<" + loadComponentType(loadReturnKindForFormat(format, false)) + ">";
}

uint32_t bitsOfFloat(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float floatFromBits(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t bitsOfI32(int32_t value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double depthLoadValue(WGPUTextureFormat format, uint32_t mip, uint32_t layer) {
    const double value = depthValue(mip, layer);
    if (format == WGPUTextureFormat_Depth16Unorm) {
        return static_cast<double>(static_cast<uint32_t>(std::floor(value * 65535.0))) / 65535.0;
    }
    return value;
}

std::array<uint32_t, 4> expectedLoadBits(std::array<double, 4> values, WGPUTextureFormat format, LoadReturnKind kind) {
    std::array<uint32_t, 4> out = {0, 0, 0, 0};
    if (kind == LoadReturnKind::Depth) {
        out[0] = bitsOfFloat(static_cast<float>(values[0]));
        return out;
    }
    const uint32_t components = isStencilOnlyFormat(format) ? 1u : componentCount(format);
    for (uint32_t i = 0; i < 4; ++i) {
        const double value = (i == 3 && components < 4) ? 1.0 : values[i];
        if (kind == LoadReturnKind::Uint) {
            out[i] = static_cast<uint32_t>(std::llround(value));
        } else if (kind == LoadReturnKind::Sint) {
            out[i] = bitsOfI32(static_cast<int32_t>(std::llround(value)));
        } else {
            out[i] = bitsOfFloat(static_cast<float>(value));
        }
    }
    return out;
}

bool textureLoadBitsMatch(
    WGPUTextureFormat format,
    LoadReturnKind kind,
    uint32_t component,
    uint32_t expected,
    uint32_t got) {
    if (expected == got) {
        return true;
    }
    if ((kind == LoadReturnKind::Float || kind == LoadReturnKind::Depth)
        && format == WGPUTextureFormat_Depth16Unorm) {
        return std::fabs(floatFromBits(expected) - floatFromBits(got)) <= (1.0f / 65535.0f);
    }
    if (kind == LoadReturnKind::Float && baseFormat(format) != format && component < 3u) {
        // sRGB textureLoad may be decoded by fixed-function hardware with 8 fractional bits.
        return std::fabs(floatFromBits(expected) - floatFromBits(got)) <= (1.0f / 256.0f);
    }
    return false;
}

uint32_t pseudoRandomU32(uint32_t i, uint32_t salt, uint32_t limit) {
    if (limit == 0) return 0;
    return hashU32({i + 1u, salt + 17u, limit + 31u, 0x9e3779b9u}) % limit;
}

std::vector<LoadCallData> makeLoadCalls(const TextureLoadCase& c) {
    std::vector<LoadCallData> calls;
    calls.reserve(kCallCount);
    for (uint32_t i = 0; i < kCallCount; ++i) {
        LoadCallData call;
        // Storage texture views target baseMipLevel, so generate coords in the viewed mip.
        const uint32_t level = c.useLevel ? (i % c.mipLevelCount)
                                          : (c.storageTexture ? c.baseMipLevel : 0u);
        const WGPUExtent3D mipSize = physicalMipSize(c.baseSize, c.textureDimension, level);
        if (c.samplePoints == "texel-centre") {
            call.coords[0] = i % mipSize.width;
            call.coords[1] = (i / std::max(1u, mipSize.width)) % std::max(1u, mipSize.height);
            call.coords[2] = (i / std::max(1u, mipSize.width * std::max(1u, mipSize.height)))
                % std::max(1u, mipSize.depthOrArrayLayers);
        } else {
            call.coords[0] = pseudoRandomU32(i, 1, mipSize.width);
            call.coords[1] = pseudoRandomU32(i, 2, std::max(1u, mipSize.height));
            call.coords[2] = pseudoRandomU32(i, 3, std::max(1u, mipSize.depthOrArrayLayers));
        }
        const uint32_t viewArrayLayers = c.arrayLayerCount == WGPU_ARRAY_LAYER_COUNT_UNDEFINED
            ? std::max(1u, c.baseSize.depthOrArrayLayers - c.baseArrayLayer)
            : c.arrayLayerCount;
        call.scalars[0] = level;
        call.scalars[1] = c.useArrayIndex ? (i % std::max(1u, viewArrayLayers)) : 0u;
        call.scalars[2] = c.useSampleIndex ? (i % c.sampleCount) : 0u;
        calls.push_back(call);
    }
    return calls;
}

std::string loadScalarExpr(const std::string& source, const std::string& type) {
    return type == "u32" ? source : "i32(" + source + ")";
}

std::string loadSignedScalarExpr(const std::string& source) {
    return "i32(" + source + ")";
}

std::string loadCoordExpr(const TextureLoadCase& c) {
    if (!c.storageTexture) {
        if (c.coordComponents == 1) return loadSignedScalarExpr("p.coords.x");
        if (c.coordComponents == 2) return "vec2i(p.coords.xy)";
        return "vec3i(p.coords.xyz)";
    }
    if (c.coordComponents == 1) return loadScalarExpr("p.coords.x", c.coordType);
    if (c.coordComponents == 2) return c.coordType == "u32" ? "p.coords.xy" : "vec2i(p.coords.xy)";
    return c.coordType == "u32" ? "p.coords.xyz" : "vec3i(p.coords.xyz)";
}

std::string loadCallWGSL(const TextureLoadCase& c) {
    std::ostringstream call;
    call << "textureLoad(tex, " << loadCoordExpr(c);
    if (c.useArrayIndex) call << ", "
        << (c.storageTexture ? loadScalarExpr("p.scalars.y", c.arrayIndexType) : loadSignedScalarExpr("p.scalars.y"));
    if (c.useLevel) call << ", " << (c.storageTexture ? loadScalarExpr("p.scalars.x", c.levelType) : loadSignedScalarExpr("p.scalars.x"));
    if (c.useSampleIndex) call << ", " << (c.storageTexture ? loadScalarExpr("p.scalars.z", c.sampleIndexType) : loadSignedScalarExpr("p.scalars.z"));
    call << ")";
    return call.str();
}

std::string loadResultExpr(const TextureLoadCase& c) {
    const std::string call = loadCallWGSL(c);
    if (c.returnKind == LoadReturnKind::Depth) return "bitcast<vec4u>(vec4f(" + call + ", 0.0, 0.0, 0.0))";
    if (c.returnKind == LoadReturnKind::Uint) return call;
    return "bitcast<vec4u>(" + call + ")";
}

std::string buildTextureLoadWgsl(uint32_t callCount, const TextureLoadCase& c) {
    std::ostringstream wgsl;
    wgsl << "@group(0) @binding(0) var tex: " << c.textureType << ";\n"
         << "struct LoadParams { coords: vec4u, scalars: vec4u };\n"
         << "@group(0) @binding(1) var<storage, read> params: array<LoadParams>;\n";
    if (c.stage == "compute") {
        wgsl << "@group(0) @binding(2) var<storage, read_write> out: array<vec4u>;\n"
             << "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
             << "  let i = id.x;\n"
             << "  let p = params[i];\n";
        if (c.useLevel || c.useArrayIndex || c.useSampleIndex) {
            wgsl << "  var result = vec4u(0u);\n";
            for (uint32_t i = 0; i < callCount; ++i) {
                wgsl << "  let call" << i << " = " << loadResultExpr(c) << ";\n"
                     << "  result = select(result, call" << i << ", i == " << i << "u);\n";
            }
            wgsl << "  out[i] = result;\n";
        } else {
            wgsl << "  out[i] = " << loadResultExpr(c) << ";\n";
        }
        wgsl << "}\n";
        return wgsl.str();
    }

    wgsl << "struct VOut { @builtin(position) pos: vec4f, @location(0) @interpolate(flat, either) ndx: u32, @location(1) @interpolate(flat, either) result: vec4u };\n"
         << "fn pixelPos(vertexIndex: u32, instanceIndex: u32) -> vec4f {\n"
         << "  let width = " << callCount << ".0;\n"
         << "  let x0 = -1.0 + 2.0 * f32(instanceIndex) / width;\n"
         << "  let x1 = -1.0 + 2.0 * f32(instanceIndex + 1u) / width;\n"
         << "  let p = array(vec2f(x0, 3.0), vec2f(x1, -1.0), vec2f(x0, -1.0));\n"
         << "  return vec4f(p[vertexIndex], 0.0, 1.0);\n"
         << "}\n"
         << "fn doLoad(i: u32) -> vec4u {\n"
         << "  let p = params[i];\n";
    if (c.useLevel || c.useArrayIndex || c.useSampleIndex) {
        wgsl << "  var result = vec4u(0u);\n";
        for (uint32_t i = 0; i < callCount; ++i) {
            wgsl << "  let call" << i << " = " << loadResultExpr(c) << ";\n"
                 << "  result = select(result, call" << i << ", i == " << i << "u);\n";
        }
        wgsl << "  return result;\n";
    } else {
        wgsl << "  return " << loadResultExpr(c) << ";\n";
    }
    wgsl << "}\n";
    if (c.stage == "vertex") {
        wgsl << "@vertex fn vsMain(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VOut {\n"
             << "  return VOut(pixelPos(vertexIndex, instanceIndex), instanceIndex, doLoad(instanceIndex));\n"
             << "}\n"
             << "@fragment fn fsMain(v: VOut) -> @location(0) vec4u { return v.result; }\n";
    } else {
        wgsl << "@vertex fn vsMain(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VOut {\n"
             << "  return VOut(pixelPos(vertexIndex, instanceIndex), instanceIndex, vec4u(0u));\n"
             << "}\n"
             << "@fragment fn fsMain(v: VOut) -> @location(0) vec4u { return doLoad(v.ndx); }\n";
    }
    return wgsl.str();
}

TextureLoadPipelineBundle createTextureLoadPipelineBundle(WGPUDevice device, const std::string& wgsl, const TextureLoadCase& c) {
    TextureLoadPipelineBundle bundle;
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = stringView(wgsl);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    bundle.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    std::array<WGPUBindGroupLayoutEntry, 3> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    const WGPUShaderStage visibility = stageVisibility(c.stage);
    entries[0].binding = 0;
    entries[0].visibility = visibility;
    if (c.storageTexture) {
        entries[0].storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entries[0].storageTexture.access = c.storageAccess;
        entries[0].storageTexture.format = c.format;
        entries[0].storageTexture.viewDimension = c.viewDimension;
    } else {
        entries[0].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entries[0].texture.sampleType = c.sampleType;
        entries[0].texture.viewDimension = c.viewDimension;
        entries[0].texture.multisampled = c.multisampled ? WGPU_TRUE : WGPU_FALSE;
    }
    entries[1].binding = 1;
    entries[1].visibility = visibility;
    entries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[2].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = c.stage == "compute" ? 3 : 2;
    bglDesc.entries = entries.data();
    bundle.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
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

std::string textureLoadPipelineKey(const std::string& wgsl, const TextureLoadCase& c) {
    std::ostringstream key;
    key << wgsl << "\n// layout:"
        << " stage=" << c.stage
        << " view=" << static_cast<uint32_t>(c.viewDimension)
        << " sampleType=" << static_cast<uint32_t>(c.sampleType)
        << " multisampled=" << (c.multisampled ? 1 : 0)
        << " storage=" << (c.storageTexture ? 1 : 0)
        << " storageAccess=" << static_cast<uint32_t>(c.storageAccess)
        << " storageFormat=" << static_cast<uint32_t>(c.storageTexture ? c.format : WGPUTextureFormat_Undefined);
    return key.str();
}

const TextureLoadPipelineBundle& textureLoadPipelineForDevice(AllFeaturesMaxLimitsGpuTest& t, const std::string& wgsl, const TextureLoadCase& c) {
    static std::unordered_map<WGPUDevice, std::unordered_map<std::string, TextureLoadPipelineBundle>> cache;
    auto& deviceCache = cache[t.device()];
    const std::string key = textureLoadPipelineKey(wgsl, c);
    auto it = deviceCache.find(key);
    if (it == deviceCache.end()) {
        it = deviceCache.emplace(key, createTextureLoadPipelineBundle(t.device(), wgsl, c)).first;
    }
    return it->second;
}

std::vector<MipData> prepareTextureLoadMips(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const TextureLoadCase& c) {
    if (c.isDepth || (isDepthTextureFormat(c.format) && !c.storageTexture && !c.multisampled)) {
        TextureCase tc;
        tc.format = c.format;
        tc.textureDimension = c.textureDimension;
        tc.baseSize = c.baseSize;
        tc.mipLevelCount = c.mipLevelCount;
        initializeDepthTexture(t, texture, tc);
        return {};
    }
    if (isStencilOnlyFormat(c.format) && !c.storageTexture && !c.multisampled) {
        std::vector<MipData> mips;
        mips.reserve(c.mipLevelCount);
        for (uint32_t mip = 0; mip < c.mipLevelCount; ++mip) {
            MipData mipData;
            mipData.format = c.format;
            mipData.layout = getTextureCopyLayout(c.format, c.textureDimension, c.baseSize, mip);
            mipData.size = mipData.layout.mipSize;
            mipData.data.assign(static_cast<size_t>(mipData.layout.byteLength), 0);
            mips.push_back(std::move(mipData));
        }
        TextureCase tc;
        tc.format = c.format;
        tc.textureDimension = c.textureDimension;
        tc.viewDimension = c.viewDimension;
        tc.baseSize = c.baseSize;
        tc.mipLevelCount = c.mipLevelCount;
        uploadColorTexture(t, texture, tc, mips);
        return {};
    }
    if (c.multisampled) {
        return {};
    }
    TextureCase tc;
    tc.format = c.format;
    tc.textureDimension = c.textureDimension;
    tc.viewDimension = c.viewDimension;
    tc.baseSize = c.baseSize;
    tc.mipLevelCount = c.mipLevelCount;
    if (isCompressedTextureFormat(c.format)) {
        std::vector<MipData> compressedMips = makeCompressedTextureData(c.format, c.textureDimension, c.baseSize, c.mipLevelCount);
        uploadColorTexture(t, texture, tc, compressedMips);
        return materializeCompressedTexels(t, texture, tc);
    }
    std::vector<MipData> mips = makeTextureData(c.format, c.textureDimension, c.baseSize, c.mipLevelCount, false);
    uploadColorTexture(t, texture, tc, mips);
    return mips;
}

void clearMultisampledTextureForLoad(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, const TextureLoadCase& c) {
    const bool depthAttachment = c.isDepth || isDepthTextureFormat(c.format);
    const bool stencilAttachment = isStencilOnlyFormat(c.format);
    const bool combinedDepthStencilAttachment = depthAttachment && isStencilTextureFormat(c.format);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.format = combinedDepthStencilAttachment ? c.format
        : viewFormatForAspect(
            c.format,
            depthAttachment ? WGPUTextureAspect_DepthOnly
                            : stencilAttachment ? WGPUTextureAspect_StencilOnly
                                                : WGPUTextureAspect_All);
    if (combinedDepthStencilAttachment) {
        viewDesc.aspect = WGPUTextureAspect_All;
    } else if (depthAttachment) {
        viewDesc.aspect = WGPUTextureAspect_DepthOnly;
    } else if (stencilAttachment) {
        viewDesc.aspect = WGPUTextureAspect_StencilOnly;
    }
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    if (depthAttachment) {
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = view;
        ds.depthReadOnly = WGPU_FALSE;
        ds.depthLoadOp = WGPULoadOp_Clear;
        ds.depthStoreOp = WGPUStoreOp_Store;
        ds.depthClearValue = 0.5;
        if (combinedDepthStencilAttachment) {
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
    } else if (stencilAttachment) {
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = view;
        ds.stencilReadOnly = WGPU_FALSE;
        ds.stencilLoadOp = WGPULoadOp_Clear;
        ds.stencilStoreOp = WGPUStoreOp_Store;
        ds.stencilClearValue = 0;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.depthStencilAttachment = &ds;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    } else {
        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = view;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    submit(t, encoder);
    if (depthAttachment) {
        t.onSubmittedWorkDoneSync();
    }
}

std::vector<std::array<uint32_t, 4>> expectedTextureLoadBits(const TextureLoadCase& c, const std::vector<LoadCallData>& calls, const std::vector<MipData>& mips) {
    std::vector<std::array<uint32_t, 4>> expected;
    expected.reserve(calls.size());
    for (const LoadCallData& call : calls) {
        if (c.multisampled) {
            if (c.isDepth || isDepthTextureFormat(c.format)) {
                // The multisampled depth texture is filled by initializeDepthTexture()
                // (invoked from prepareTextureLoadMips because c.isDepth is true), which
                // clears every sample of mip 0 / layer 0 to depthValue(0, 0). The earlier
                // clearMultisampledTextureForLoad() depth clear is overwritten, so the value
                // textureLoad observes for any sample_index is depthLoadValue(format, 0, 0),
                // not the unrelated 0.5 used for the multisampled-color clear.
                expected.push_back(expectedLoadBits({depthLoadValue(c.format, 0, 0), 0.0, 0.0, 0.0}, c.format, LoadReturnKind::Depth));
            } else {
                expected.push_back(expectedLoadBits({0.0, 0.0, 0.0, 0.0}, c.format, c.returnKind));
            }
            continue;
        }
        const uint32_t level = c.useLevel ? call.scalars[0] : c.baseMipLevel;
        const uint32_t mipIndex = c.storageTexture ? c.baseMipLevel : level;
        const uint32_t arrayIndex = c.useArrayIndex ? call.scalars[1] : 0u;
        if (c.isDepth || (isDepthTextureFormat(c.format) && !c.storageTexture)) {
            expected.push_back(expectedLoadBits({depthLoadValue(c.format, mipIndex, c.baseArrayLayer + arrayIndex), 0.0, 0.0, 0.0}, c.format, c.returnKind));
            continue;
        }
        if (isStencilOnlyFormat(c.format) && !c.storageTexture) {
            expected.push_back(expectedLoadBits({0.0, 0.0, 0.0, 0.0}, c.format, c.returnKind));
            continue;
        }
        const MipData& mip = mips[mipIndex];
        const uint32_t z = c.textureDimension == WGPUTextureDimension_3D
            ? call.coords[2]
            : c.baseArrayLayer + arrayIndex;
        const TexelComponents comps = texelAt(mip, mip.format, static_cast<int32_t>(call.coords[0]), static_cast<int32_t>(call.coords[1]), static_cast<int32_t>(z));
        expected.push_back(expectedLoadBits(resultTexel(comps, mip.format), mip.format, c.returnKind));
    }
    return expected;
}

void executeTextureLoadCase(AllFeaturesMaxLimitsGpuTest& t, TextureLoadCase c) {
    c.stage = stageFromLoadShort(c.stage);
    if (c.multisampled && isDepthTextureFormat(c.format)) {
        c.isDepth = true;
        c.returnKind = LoadReturnKind::Depth;
        c.textureType = "texture_depth_multisampled_2d";
        c.sampleType = WGPUTextureSampleType_Depth;
        c.aspect = WGPUTextureAspect_DepthOnly;
    }
    if (c.multisampled && isDepthTextureFormat(c.format)
        && (!c.isDepth || c.returnKind != LoadReturnKind::Depth
            || c.textureType != "texture_depth_multisampled_2d"
            || c.sampleType != WGPUTextureSampleType_Depth
            || c.aspect != WGPUTextureAspect_DepthOnly)) {
        t.fail("textureLoad MS-depth routing fell through to the color path");
    }
    t.skipIfTextureFormatNotSupported(c.format);
    t.skipIfTextureViewDimensionNotSupported(c.viewDimension);
    t.skipIfTextureFormatAndDimensionNotCompatible(c.format, c.textureDimension);
    if (c.multisampled) {
        if (!t.isTextureFormatMultisampled(c.format)) {
            t.skip("texture format is not multisampled");
        }
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

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = c.baseSize;
    textureDesc.mipLevelCount = c.mipLevelCount;
    textureDesc.sampleCount = c.sampleCount;
    textureDesc.dimension = c.textureDimension;
    textureDesc.format = c.format;
    textureDesc.usage = c.usage;
    WGPUTexture texture = t.createTextureTracked(textureDesc);
    if (c.multisampled) {
        clearMultisampledTextureForLoad(t, texture, c);
    }
    const std::vector<MipData> mips = prepareTextureLoadMips(t, texture, c);
    WGPUTexture loadTexture = texture;
    if (isCompressedTextureFormat(c.format) && !c.storageTexture) {
        WGPUTextureDescriptor loadTextureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        loadTextureDesc.size = c.baseSize;
        loadTextureDesc.mipLevelCount = c.mipLevelCount;
        loadTextureDesc.sampleCount = 1;
        loadTextureDesc.dimension = c.textureDimension;
        loadTextureDesc.format = WGPUTextureFormat_RGBA32Float;
        loadTextureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        loadTexture = t.createTextureTracked(loadTextureDesc);

        TextureCase materializedCase;
        materializedCase.format = WGPUTextureFormat_RGBA32Float;
        materializedCase.textureDimension = c.textureDimension;
        materializedCase.viewDimension = c.viewDimension;
        materializedCase.baseSize = c.baseSize;
        materializedCase.mipLevelCount = c.mipLevelCount;
        uploadColorTexture(t, loadTexture, materializedCase, mips);

        c.format = WGPUTextureFormat_RGBA32Float;
        c.aspect = WGPUTextureAspect_All;
        c.sampleType = metadataSampleType(c.format, c.aspect, false, c.sampleCount);
        c.returnKind = LoadReturnKind::Float;
        if (c.viewDimension == WGPUTextureViewDimension_3D) {
            c.textureType = appendLoadComponentType("texture_3d", c.format, false);
        } else if (c.viewDimension == WGPUTextureViewDimension_2DArray) {
            c.textureType = appendLoadComponentType("texture_2d_array", c.format, false);
        } else if (c.viewDimension == WGPUTextureViewDimension_1D) {
            c.textureType = appendLoadComponentType("texture_1d", c.format, false);
        } else {
            c.textureType = appendLoadComponentType("texture_2d", c.format, false);
        }
    }

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.dimension = c.viewDimension;
    viewDesc.format = c.storageTexture ? c.format
        : c.aspect == WGPUTextureAspect_All ? WGPUTextureFormat_Undefined
                                            : viewFormatForAspect(c.format, c.aspect);
    viewDesc.aspect = c.aspect;
    viewDesc.baseMipLevel = c.baseMipLevel;
    viewDesc.mipLevelCount = c.viewMipCount;
    viewDesc.baseArrayLayer = c.baseArrayLayer;
    viewDesc.arrayLayerCount = c.arrayLayerCount;
    WGPUTextureView view = t.createViewTracked(loadTexture, viewDesc);

    const std::vector<LoadCallData> calls = makeLoadCalls(c);
    const std::vector<std::array<uint32_t, 4>> expected = expectedTextureLoadBits(c, calls, mips);
    const uint64_t paramsSize = static_cast<uint64_t>(calls.size()) * sizeof(LoadCallData);
    WGPUBufferDescriptor paramsDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    paramsDesc.size = paramsSize;
    paramsDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    WGPUBuffer paramsBuffer = t.createBufferTracked(paramsDesc);
    t.queueWriteBuffer(paramsBuffer, 0, calls.data(), static_cast<size_t>(paramsSize));

    const uint64_t outputSize = static_cast<uint64_t>(calls.size()) * 4u * sizeof(uint32_t);
    const uint32_t renderBytesPerRow = alignToU32(static_cast<uint32_t>(calls.size()) * 16u, 256u);
    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = c.stage == "compute" ? outputSize : renderBytesPerRow;
    outputDesc.usage = c.stage == "compute"
        ? WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc
        : WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    const std::string wgsl = buildTextureLoadWgsl(static_cast<uint32_t>(calls.size()), c);
    const TextureLoadPipelineBundle& pipeline = textureLoadPipelineForDevice(t, wgsl, c);

    std::array<WGPUBindGroupEntry, 3> bgEntries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = view;
    bgEntries[1].binding = 1;
    bgEntries[1].buffer = paramsBuffer;
    bgEntries[1].size = paramsSize;
    bgEntries[2].binding = 2;
    bgEntries[2].buffer = outputBuffer;
    bgEntries[2].size = outputSize;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = pipeline.bindGroupLayout;
    bindGroupDesc.entryCount = c.stage == "compute" ? 3 : 2;
    bindGroupDesc.entries = bgEntries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    if (c.stage == "compute") {
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline.computePipeline);
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
        wgpuRenderPassEncoderSetPipeline(pass, pipeline.renderPipeline);
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
                return std::string("textureLoad output buffer too small");
            }
            for (size_t i = 0; i < calls.size(); ++i) {
                for (uint32_t component = 0; component < 4; ++component) {
                    const size_t byteOffset = c.stage == "compute"
                        ? (i * 4u + component) * sizeof(uint32_t)
                        : i * 16u + component * sizeof(uint32_t);
                    uint32_t got = 0;
                    std::memcpy(&got, actual + byteOffset, sizeof(uint32_t));
                    if (!textureLoadBitsMatch(c.format, c.returnKind, component, expected[i][component], got)) {
                        std::ostringstream msg;
                        msg << "textureLoad mismatch call " << i << " component " << component
                            << ": expected bits " << expected[i][component] << ", got " << got
                            << ", textureType " << c.textureType << ", stage " << c.stage
                            << ", format " << textureFormatInfo(c.format).identifier;
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(outputSize));
}

// ---------------------------------------------------------------------------
// textureGather / textureGatherCompare
//
// Gather always reads mip level 0 with the 2x2 bilinear footprint and emits the
// four corner texels of a single channel (no LOD, no blend). Reuses the texel
// generation/upload, cube cross-face addressing, depth initialization, the
// comparison applyCompareToDepth path, and the per-device pipeline cache.
// ---------------------------------------------------------------------------

bool gatherFormatIsUint(WGPUTextureFormat format) {
    return formatLooksUint(format);
}

bool gatherFormatIsSint(WGPUTextureFormat format) {
    return formatLooksSint(format);
}

std::string gatherResultTypeForFormat(WGPUTextureFormat format) {
    if (gatherFormatIsUint(format) || isStencilOnlyFormat(format)) {
        return "u32";
    }
    if (gatherFormatIsSint(format)) {
        return "i32";
    }
    return "f32";
}

WGPUTextureSampleType gatherTextureSampleType(const TextureCase& c) {
    if (c.isDepth) {
        return WGPUTextureSampleType_Depth;
    }
    if (gatherFormatIsUint(c.format) || isStencilOnlyFormat(c.format)) {
        return WGPUTextureSampleType_Uint;
    }
    if (gatherFormatIsSint(c.format)) {
        return WGPUTextureSampleType_Sint;
    }
    // Depth formats reaching the color path (texture_2d<f32> over a depth
    // texture) are unfilterable. Gather always uses a non-filtering sampler.
    if (isDepthFormat(c.format)) {
        return WGPUTextureSampleType_UnfilterableFloat;
    }
    return WGPUTextureSampleType_Float;
}

// Returns the four gather corner texels (component values) for one call. For
// color textures the component is selected per the gather rules: missing
// components yield 0.0 (component 1/2) or 1.0 (component 3). For depth the
// single channel is used. The corner order matches the spec:
//   x=(umin,vmax) y=(umax,vmax) z=(umax,vmin) w=(umin,vmin)
std::array<double, 4> gatherColorTaps(const MipData& mip, const SampleCall& callIn, const TextureCase& c) {
    SampleCall call = callIn;
    if (isCubeKind(c.kind)) {
        const Vec3 faceCoord = normalizedCubeToFaceCoord(call.coords);
        const uint32_t arrayBase = c.useArrayIndex ? (*call.arrayIndex) * 6u : 0u;
        call.coords = Vec3{faceCoord.x, faceCoord.y, 0.0};
        call.arrayIndex = arrayBase + static_cast<uint32_t>(faceCoord.z);
    }
    const int32_t ox = call.offset ? (*call.offset)[0] : 0;
    const int32_t oy = call.offset ? (*call.offset)[1] : 0;
    const int32_t layer = call.arrayIndex ? static_cast<int32_t>(*call.arrayIndex) : 0;

    const double u = call.coords.x * static_cast<double>(mip.size.width) - 0.5 + static_cast<double>(ox);
    const double v = call.coords.y * static_cast<double>(mip.size.height) - 0.5 + static_cast<double>(oy);
    const int32_t x0 = static_cast<int32_t>(std::floor(u));
    const int32_t y0 = static_cast<int32_t>(std::floor(v));

    const uint32_t comp = static_cast<uint32_t>(std::clamp(c.gatherComponent, 0, 3));
    const std::array<double, 4> c00 = loadAddressed(mip, c.format, x0, y0, layer, c);
    const std::array<double, 4> c10 = loadAddressed(mip, c.format, x0 + 1, y0, layer, c);
    const std::array<double, 4> c01 = loadAddressed(mip, c.format, x0, y0 + 1, layer, c);
    const std::array<double, 4> c11 = loadAddressed(mip, c.format, x0 + 1, y0 + 1, layer, c);
    // loadAddressed -> resultTexel already applies the missing-channel rule
    // (absent component -> 0.0, absent alpha -> 1.0).
    return {c01[comp], c11[comp], c10[comp], c00[comp]};
}

std::array<double, 4> gatherDepthTaps(const SampleCall& callIn, const TextureCase& c) {
    SampleCall call = callIn;
    int32_t arrayBase = 0;
    if (isCubeKind(c.kind)) {
        const Vec3 faceCoord = normalizedCubeToFaceCoord(call.coords);
        arrayBase = c.useArrayIndex ? static_cast<int32_t>(*call.arrayIndex) * 6 : 0;
        call.coords = Vec3{faceCoord.x, faceCoord.y, static_cast<double>(arrayBase) + faceCoord.z};
    }
    const WGPUExtent3D mipSize = physicalMipSize(c.baseSize, c.textureDimension, 0);
    const int32_t ox = call.offset ? (*call.offset)[0] : 0;
    const int32_t oy = call.offset ? (*call.offset)[1] : 0;
    const double u = call.coords.x * static_cast<double>(mipSize.width) - 0.5 + static_cast<double>(ox);
    const double v = call.coords.y * static_cast<double>(mipSize.height) - 0.5 + static_cast<double>(oy);
    const int32_t x0 = static_cast<int32_t>(std::floor(u));
    const int32_t y0 = static_cast<int32_t>(std::floor(v));

    const bool compare = c.gatherCompare;
    const WGPUCompareFunction cmp = compareFunctionFromString(c.compare);
    auto tap = [&](int32_t x, int32_t y) -> double {
        uint32_t resolvedLayer;
        if (isCubeKind(c.kind)) {
            const std::array<int32_t, 3> wrapped =
                wrapFaceCoordToCubeFaceAtEdgeBoundaries(mipSize.width, x, y, static_cast<int32_t>(call.coords.z));
            resolvedLayer = static_cast<uint32_t>(wrapped[2]);
        } else {
            const WGPUAddressMode modeU = c.modeU;
            const WGPUAddressMode modeV = c.modeV;
            (void)std::floor(applyAddressToTexelCoord(static_cast<double>(x), mipSize.width, modeU));
            (void)std::floor(applyAddressToTexelCoord(static_cast<double>(y), mipSize.height, modeV));
            resolvedLayer = call.arrayIndex.value_or(0u);
        }
        const double d = depthValue(0u, resolvedLayer);
        return compare ? applyCompareToDepth(d, cmp, call.depthRef) : d;
    };
    const double t00 = tap(x0, y0);
    const double t10 = tap(x0 + 1, y0);
    const double t01 = tap(x0, y0 + 1);
    const double t11 = tap(x0 + 1, y0 + 1);
    return {t01, t11, t10, t00};
}

std::string gatherExprFromParams(std::string_view param, const TextureCase& c, std::string_view offsetExpr) {
    std::ostringstream expr;
    const char* builtin = c.gatherCompare ? "textureGatherCompare" : "textureGather";
    expr << builtin << "(";
    if (!c.gatherCompare && !c.isDepth) {
        expr << c.gatherComponent << ", ";
    }
    expr << "tex, samp, " << coordParamExpr(param, c);
    if (c.useArrayIndex) {
        expr << ", " << arrayIndexParamExpr(param, c);
    }
    if (c.gatherCompare) {
        expr << ", " << param << ".scalars.z";
    }
    expr << offsetExpr << ")";
    // The output buffer is array<vec4u>; bitcast covers f32/i32/depth, and is an
    // identity for u32 gathers.
    return "bitcast<vec4u>(" + expr.str() + ")";
}

std::string buildGatherWgsl(uint32_t callCount, const TextureCase& c) {
    std::ostringstream wgsl;
    wgsl << "// texture-gather structural shader; stage=" << c.stage << "\n"
         << "@group(0) @binding(0) var tex: " << textureType(c) << ";\n"
         << "@group(0) @binding(1) var samp: " << (c.comparisonSampler ? "sampler_comparison" : "sampler") << ";\n";
    writeSampleParamsHeader(wgsl);
    const bool perCall = c.useOffset;
    if (c.stage == "compute") {
        wgsl << "@group(0) @binding(2) var<storage, read_write> out: array<vec4u>;\n"
             << "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
             << "  let i = id.x;\n"
             << "  let p = params[i];\n";
        if (perCall) {
            for (uint32_t i = 0; i < callCount; ++i) {
                wgsl << "  if (i == " << i << "u) { out[" << i << "] = "
                     << gatherExprFromParams("p", c, offsetExprForCallIndex(i, c)) << "; }\n";
            }
        } else {
            wgsl << "  out[i] = " << gatherExprFromParams("p", c, {}) << ";\n";
        }
        wgsl << "}\n";
        return wgsl.str();
    }

    wgsl << "const unusedOutputBinding = 2u;\n"
         << "struct VOut {\n"
         << "  @builtin(position) pos: vec4f,\n"
         << "  @location(0) @interpolate(flat, either) ndx: u32,\n"
         << "  @location(1) @interpolate(flat, either) result: vec4u,\n"
         << "};\n"
         << "fn doGather(i: u32) -> vec4u {\n"
         << "  let p = params[i];\n";
    if (perCall) {
        wgsl << "  var result = vec4u(0u);\n";
        for (uint32_t i = 0; i < callCount; ++i) {
            wgsl << "  let call" << i << " = " << gatherExprFromParams("p", c, offsetExprForCallIndex(i, c)) << ";\n"
                 << "  result = select(result, call" << i << ", i == " << i << "u);\n";
        }
        wgsl << "  return result;\n";
    } else {
        wgsl << "  return " << gatherExprFromParams("p", c, {}) << ";\n";
    }
    wgsl << "}\n"
         << "fn pixelPos(vertexIndex: u32, instanceIndex: u32) -> vec4f {\n"
         << "  let width = " << callCount << ".0;\n"
         << "  let x0 = -1.0 + 2.0 * f32(instanceIndex) / width;\n"
         << "  let x1 = -1.0 + 2.0 * f32(instanceIndex + 1u) / width;\n"
         << "  let q = array(vec2f(x0, 3.0), vec2f(x1, -1.0), vec2f(x0, -1.0));\n"
         << "  return vec4f(q[vertexIndex], 0.0, 1.0);\n"
         << "}\n";
    if (c.stage == "vertex") {
        wgsl << "@vertex fn vsMain(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VOut {\n"
             << "  return VOut(pixelPos(vertexIndex, instanceIndex), instanceIndex, doGather(instanceIndex));\n"
             << "}\n"
             << "@fragment fn fsMain(v: VOut) -> @location(0) vec4u { return v.result; }\n";
    } else {
        wgsl << "@vertex fn vsMain(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instanceIndex: u32) -> VOut {\n"
             << "  return VOut(pixelPos(vertexIndex, instanceIndex), instanceIndex, vec4u(0u));\n"
             << "}\n"
             << "@fragment fn fsMain(v: VOut) -> @location(0) vec4u { return doGather(v.ndx); }\n";
    }
    return wgsl.str();
}

WGPUBindGroupLayout createGatherBindGroupLayout(WGPUDevice device, const TextureCase& c) {
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
    entries[0].texture.sampleType = gatherTextureSampleType(c);
    entries[0].texture.viewDimension = c.viewDimension;
    entries[1].binding = 1;
    entries[1].visibility = visibility;
    entries[1].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    entries[1].sampler.type = c.comparisonSampler
        ? WGPUSamplerBindingType_Comparison
        : WGPUSamplerBindingType_NonFiltering;
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

SamplingPipelineBundle createGatherPipelineBundle(WGPUDevice device, const std::string& wgsl, const TextureCase& c) {
    SamplingPipelineBundle bundle;
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = stringView(wgsl);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    bundle.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    bundle.bindGroupLayout = createGatherBindGroupLayout(device, c);
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

const SamplingPipelineBundle& gatherPipelineForDevice(AllFeaturesMaxLimitsGpuTest& t, const std::string& wgsl, const TextureCase& c) {
    static std::unordered_map<WGPUDevice, std::unordered_map<std::string, SamplingPipelineBundle>> cache;
    const WGPUDevice device = t.device();
    auto& deviceCache = cache[device];
    // The bind group layout depends on the texture sample type and sampler
    // binding type, which are NOT all reflected in the WGSL string (e.g.
    // texture_2d<f32> is shared by a filterable color format and a depth format
    // sampled as unfilterable-float). Include them in the cache key.
    std::ostringstream keyStream;
    keyStream << wgsl << "\n// bgl: sampleType=" << static_cast<int>(gatherTextureSampleType(c))
              << " comparison=" << (c.comparisonSampler ? 1 : 0)
              << " view=" << static_cast<int>(c.viewDimension);
    const std::string key = keyStream.str();
    auto it = deviceCache.find(key);
    if (it == deviceCache.end()) {
        it = deviceCache.emplace(key, createGatherPipelineBundle(device, wgsl, c)).first;
    }
    return it->second;
}

void executeGatherCase(AllFeaturesMaxLimitsGpuTest& t, TextureCase c) {
    t.skipIfTextureFormatNotSupported(c.format);
    if (c.viewDimension == WGPUTextureViewDimension_Cube || c.viewDimension == WGPUTextureViewDimension_CubeArray) {
        t.skipIfTextureViewDimensionNotSupported(c.viewDimension);
    }
    c.baseSize = adjustedBaseSizeForFormat(c);
    if (!c.isDepth) {
        c.gatherResultType = gatherResultTypeForFormat(c.format);
    }
    // A depth format reaching the color path is sampled as texture_2d<f32>:
    // upstream still exercises this (nearest filter only). The depth aspect is
    // the only channel, so the spec yields 0.0 for component 1/2 and 1.0 for 3.
    const bool colorViewOfDepth = !c.isDepth && isDepthFormat(c.format);
    // A stencil-only format reaching the color path is sampled as
    // texture_2d<u32> over the stencil aspect (the only channel).
    const bool stencilViewOfFormat = !c.isDepth && isStencilOnlyFormat(c.format);
    const bool useDepthInit = c.isDepth || colorViewOfDepth;
    const WGPUTextureAspect viewAspect = stencilViewOfFormat
        ? WGPUTextureAspect_StencilOnly
        : (useDepthInit ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All);

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = c.baseSize;
    textureDesc.mipLevelCount = c.mipLevelCount;
    textureDesc.sampleCount = 1;
    textureDesc.dimension = c.textureDimension;
    textureDesc.format = c.format;
    textureDesc.usage = (useDepthInit || stencilViewOfFormat)
        ? (WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment)
        : (WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    std::vector<MipData> mips;
    if (useDepthInit) {
        initializeDepthTexture(t, texture, c);
    } else if (stencilViewOfFormat) {
        // Stencil cleared to 0 across all mips; component 1/2 yields 0 by spec.
        WGPUCommandEncoder stencilEncoder = t.createCommandEncoderTracked();
        for (uint32_t mip = 0; mip < c.mipLevelCount; ++mip) {
            WGPUTextureViewDescriptor sv = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            sv.dimension = WGPUTextureViewDimension_2D;
            sv.format = c.format;
            sv.baseMipLevel = mip;
            sv.mipLevelCount = 1;
            sv.aspect = WGPUTextureAspect_All;
            WGPUTextureView sview = t.createViewTracked(texture, sv);
            WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            ds.view = sview;
            ds.stencilReadOnly = WGPU_FALSE;
            ds.stencilLoadOp = WGPULoadOp_Clear;
            ds.stencilStoreOp = WGPUStoreOp_Store;
            ds.stencilClearValue = 0;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.depthStencilAttachment = &ds;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(stencilEncoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
        submit(t, stencilEncoder);
    } else if (isCompressedTextureFormat(c.format)) {
        std::vector<MipData> compressedMips = makeCompressedTextureData(c.format, c.textureDimension, c.baseSize, c.mipLevelCount);
        uploadColorTexture(t, texture, c, compressedMips);
        mips = materializeCompressedTexels(t, texture, c);
    } else {
        mips = makeTextureData(c.format, c.textureDimension, c.baseSize, c.mipLevelCount, isCubeKind(c.kind));
        uploadColorTexture(t, texture, c, mips);
    }

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.dimension = c.viewDimension;
    viewDesc.format = useDepthInit
        ? viewFormatForAspect(c.format, WGPUTextureAspect_DepthOnly)
        : (stencilViewOfFormat ? viewFormatForAspect(c.format, WGPUTextureAspect_StencilOnly) : c.format);
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    viewDesc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    viewDesc.aspect = viewAspect;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.addressModeU = c.modeU;
    samplerDesc.addressModeV = c.modeV;
    samplerDesc.addressModeW = c.modeW;
    // Gather ignores min/mag/mip filtering (it always emits the four corner
    // texels of level 0). Use a non-filtering sampler so the binding is valid
    // for every format, including uint/sint/unfilterable-float.
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    if (c.comparisonSampler) {
        samplerDesc.compare = compareFunctionFromString(c.compare);
    }
    WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

    // Per the spec (https://gpuweb.github.io/gpuweb/#reading-depth-stencil), a
    // depth texture sampled through a color view (texture_2d<f32>) reads as
    // (D, ?, ?, ?): the G/B/A components are implementation-defined unless the
    // 'texture-component-swizzle' feature is enabled, in which case it reads as
    // (D, 0, 0, 1). textureGather(component, ...) with component > 0 therefore
    // selects an implementation-defined channel and must not be checked when the
    // feature is absent. (Upstream texture_utils.ts checkResults skips the call
    // entirely in this case; see getComponentsToCheck / the depth-stencil guard.)
    const bool hasComponentSwizzle =
        wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TextureComponentSwizzle);
    const bool depthGatherUndefinedComponent =
        colorViewOfDepth && c.gatherComponent > 0 && !hasComponentSwizzle;

    const std::vector<SampleCall> calls = generateCalls(c);
    std::vector<std::array<double, 4>> expected;
    expected.reserve(calls.size());
    for (const SampleCall& call : calls) {
        if (stencilViewOfFormat) {
            // Single-channel stencil cleared to 0; component 1/2 -> 0, 3 -> 1.
            const double v = c.gatherComponent == 3 ? 1.0 : 0.0;
            expected.push_back({v, v, v, v});
        } else if (colorViewOfDepth) {
            // texture_2d<f32> over a 1-channel depth texture: component 0 yields
            // the depth corners. With texture-component-swizzle the read is
            // (D, 0, 0, 1) so component 1/2 -> 0, 3 -> 1; without the feature the
            // component 1/2/3 results are implementation-defined and are skipped
            // below (depthGatherUndefinedComponent).
            const int comp = c.gatherComponent;
            if (comp == 0) {
                TextureCase depthCase = c;
                depthCase.isDepth = true;
                expected.push_back(gatherDepthTaps(call, depthCase));
            } else {
                const double v = comp == 3 ? 1.0 : 0.0;
                expected.push_back({v, v, v, v});
            }
        } else if (c.isDepth) {
            expected.push_back(gatherDepthTaps(call, c));
        } else {
            expected.push_back(gatherColorTaps(mips[0], call, c));
        }
    }

    const std::string wgsl = buildGatherWgsl(static_cast<uint32_t>(calls.size()), c);
    const SamplingPipelineBundle& pipelineBundle = gatherPipelineForDevice(t, wgsl, c);

    const std::vector<SampleParamsData> sampleParams = makeSampleParamsData(calls);
    const uint64_t sampleParamsSize = static_cast<uint64_t>(sampleParams.size()) * sizeof(SampleParamsData);
    WGPUBufferDescriptor sampleParamsDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    sampleParamsDesc.size = sampleParamsSize;
    sampleParamsDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    WGPUBuffer sampleParamsBuffer = t.createBufferTracked(sampleParamsDesc);
    t.queueWriteBuffer(sampleParamsBuffer, 0, sampleParams.data(), static_cast<size_t>(sampleParamsSize));

    const uint64_t outputSize = static_cast<uint64_t>(calls.size()) * 4u * sizeof(uint32_t);
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

    const bool intResult = !c.isDepth && !c.gatherCompare && c.gatherResultType != "f32";
    const double tolerance = intResult
        ? 0.0
        : (c.isDepth ? 3.0 / 100.0 : comparisonToleranceForFormat(c.format, c.filt));
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < static_cast<size_t>(outputSize)) {
                return std::string("textureGather output buffer too small");
            }
            if (depthGatherUndefinedComponent) {
                // Every call selects an implementation-defined depth-texture
                // component (G/B/A of a depth read without texture-component-
                // swizzle); nothing is checkable. The pass still exercises that
                // the shader compiles and runs.
                return std::nullopt;
            }
            for (size_t i = 0; i < calls.size(); ++i) {
                for (uint32_t component = 0; component < 4; ++component) {
                    const size_t byteOffset = c.stage == "compute"
                        ? (i * 4u + component) * sizeof(uint32_t)
                        : i * 16u + component * sizeof(uint32_t);
                    uint32_t gotBits = 0;
                    std::memcpy(&gotBits, actual + byteOffset, sizeof(uint32_t));
                    double got;
                    if (c.gatherResultType == "u32" && !c.isDepth && !c.gatherCompare) {
                        got = static_cast<double>(gotBits);
                    } else if (c.gatherResultType == "i32" && !c.isDepth && !c.gatherCompare) {
                        int32_t signedGot = 0;
                        std::memcpy(&signedGot, &gotBits, sizeof(signedGot));
                        got = static_cast<double>(signedGot);
                    } else {
                        got = static_cast<double>(floatFromBits(gotBits));
                    }
                    const double diff = std::abs(got - expected[i][component]);
                    if (diff > tolerance) {
                        std::ostringstream msg;
                        msg << "textureGather mismatch call " << i << " component " << component
                            << ": expected " << expected[i][component] << ", got " << got
                            << ", diff " << diff << ", format " << textureFormatInfo(c.format).identifier
                            << ", stage " << c.stage << ", tolerance " << tolerance;
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(outputSize));
}

TextureCase baseGatherCase(AllFeaturesMaxLimitsGpuTest& t, SampleKind kind, bool depth) {
    TextureCase c;
    c.kind = kind;
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.stage = t.param<std::string>("stage");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.mipLevelCount = kMipLevelCount;
    c.gather = true;
    c.builtin = "textureGather";
    if (depth) {
        c.isDepth = true;
        c.filt = "nearest";
        c.gatherResultType = "f32";
    } else {
        c.filt = t.param<std::string>("filt");
    }
    return c;
}

// ---------------------------------------------------------------------------
// textureStore
//
// A compute or fragment shader writes texels into a storage texture via
// textureStore(); the texture is then copied back to a buffer and compared
// against the values we expect the GPU to have encoded. The expected bytes are
// produced with the project's own TexelRepresentation encoder (the same path
// textureLoad trusts), with each component pre-clamped to the format's
// representable range to match GPU saturation behavior. Read-back buffers are
// zero-filled before the copy so a silently-skipped write is caught as a
// mismatch rather than a false pass.
// ---------------------------------------------------------------------------

// WGSL component type for the storage format's channel type.
std::string storeComponentType(WGPUTextureFormat format) {
    return loadComponentType(loadReturnKindForFormat(format, false));
}

// Numeric value lists per format, mirroring upstream inputArray(). They include
// out-of-range values so clamping/saturation behavior is exercised.
std::vector<double> storeInputArray(WGPUTextureFormat format) {
    const std::string_view id = textureFormatInfo(format).identifier;
    auto is = [&](std::string_view s) { return id == s; };
    if (is("r8snorm") || is("rg8snorm") || is("rgba8snorm") || is("r16snorm") || is("rg16snorm")
        || is("rgba16snorm")) {
        return {-1.1, 1.0, -0.6, -0.3, 0, 0.3, 0.6, 1.0, 1.1};
    }
    if (is("r8unorm") || is("rg8unorm") || is("rgba8unorm") || is("bgra8unorm") || is("r16unorm")
        || is("rg16unorm") || is("rgba16unorm")) {
        return {-0.1, 0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.1};
    }
    if (is("r8uint") || is("rg8uint") || is("rgba8uint")) {
        return {0, 8, 16, 24, 32, 64, 100, 128, 200, 255, 256, 512};
    }
    if (is("rgba16uint")) {
        return {0, 8, 16, 24, 32, 64, 100, 128, 200, 255, 0xffff, 0x1ffff};
    }
    if (is("rgba32uint") || is("r32uint") || is("rg32uint")) {
        return {0, 8, 16, 24, 32, 64, 100, 128, 200, 255, 256, 512, 0xffffffffu};
    }
    if (is("r8sint") || is("rg8sint") || is("rgba8sint")) {
        return {-128, -100, -64, -32, -16, -8, 0, 8, 16, 32, 64, 100, 127};
    }
    if (is("rgba16sint")) {
        return {-32768, -32769, -100, -64, -32, -16, -8, 0, 8, 16, 32, 64, 100, 127, 0x7fff, 0x8000};
    }
    if (is("r32sint") || is("rg32sint") || is("rgba32sint")) {
        return {-0x8000000, -32769, -100, -64, -32, -16, -8, 0, 8, 16, 32, 64, 100, 127, 0x7ffffff};
    }
    if (is("r16float") || is("rg16float") || is("rgba16float") || is("rgba32float") || is("r32float")
        || is("rg32float")) {
        return {-100, -50, -32, -16, -8, -1, 0, 1, 8, 16, 32, 50, 100};
    }
    if (is("r16uint") || is("rg16uint")) {
        return {0, 1000, 32768, 65535, 65536, 70000};
    }
    if (is("r16sint") || is("rg16sint")) {
        return {-32769, -32768, -1000, 0, 1000, 32767, 32768};
    }
    if (is("rgb10a2uint")) {
        return {0, 500, 1023, 1024, 3, 4};
    }
    if (is("rgb10a2unorm")) {
        return {-0.1, 0, 0.5, 1.0, 1.1};
    }
    if (is("rg11b10ufloat")) {
        return {1, 0.5, 0, 1};
    }
    return {};
}

// Clamp a component value to the representable range of the destination
// channel, matching the GPU's saturating textureStore behavior.
double storeClampComponent(WGPUTextureFormat format, uint32_t componentIndex, double value) {
    const TexelRepresentation& repr = texelRepresentation(format);
    if (componentIndex >= 4) return value;
    const ComponentDataType type = repr.dataTypes[componentIndex];
    const uint32_t bits = repr.bitLengths[componentIndex];
    switch (type) {
        case ComponentDataType::Unorm:
            return std::min(1.0, std::max(0.0, value));
        case ComponentDataType::Snorm:
            return std::min(1.0, std::max(-1.0, value));
        case ComponentDataType::Uint: {
            const double maxv = bits >= 32 ? 4294967295.0 : static_cast<double>((1u << bits) - 1u);
            return std::min(maxv, std::max(0.0, value));
        }
        case ComponentDataType::Sint: {
            const double maxv = bits >= 32 ? 2147483647.0 : static_cast<double>((1 << (bits - 1)) - 1);
            const double minv = bits >= 32 ? -2147483648.0 : static_cast<double>(-(1 << (bits - 1)));
            return std::min(maxv, std::max(minv, value));
        }
        default:
            // Float / Ufloat: values used are simple and need no clamp.
            return value;
    }
}

// Encode a four-component texel value into its packed byte representation,
// pre-clamping per channel to mirror GPU saturation.
std::vector<uint8_t> storeEncodeTexel(WGPUTextureFormat format, const std::array<double, 4>& valuesIn) {
    const TexelRepresentation& repr = texelRepresentation(format);
    TexelComponents comps;
    for (uint32_t i = 0; i < 4; ++i) {
        comps.values[i] = storeClampComponent(format, i, valuesIn[i]);
    }
    return repr.packBits(repr.numberToBits(comps));
}

bool storeFormatHasNormalizedComponents(const TexelRepresentation& repr) {
    for (TexelComponent component : repr.componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        const ComponentDataType type = repr.dataTypes[index];
        if (type == ComponentDataType::Unorm || type == ComponentDataType::Snorm) {
            return true;
        }
    }
    return false;
}

bool normalizedStoreTexelMatches(const TexelRepresentation& repr, const uint8_t* expected, const uint8_t* got) {
    const TexelBits expectedBits = repr.unpackBits(expected, repr.bytesPerBlock);
    const TexelBits gotBits = repr.unpackBits(got, repr.bytesPerBlock);
    for (TexelComponent component : repr.componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        const ComponentDataType type = repr.dataTypes[index];
        const uint32_t expectedValue = expectedBits.values[index];
        const uint32_t gotValue = gotBits.values[index];
        if (type == ComponentDataType::Unorm || type == ComponentDataType::Snorm) {
            const uint32_t diff = expectedValue > gotValue ? expectedValue - gotValue : gotValue - expectedValue;
            if (diff > 1u) {
                return false;
            }
        } else if (expectedValue != gotValue) {
            return false;
        }
    }
    return true;
}

struct TextureStorePipelineBundle {
    WGPUShaderModule module = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
    bool isCompute = true;
};

TextureStorePipelineBundle createTextureStorePipelineBundle(
    WGPUDevice device,
    const std::string& wgsl,
    bool isCompute,
    WGPUStorageTextureAccess access,
    WGPUTextureFormat format,
    WGPUTextureViewDimension viewDimension,
    const std::string& computeEntry) {
    TextureStorePipelineBundle bundle;
    bundle.isCompute = isCompute;
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = stringView(wgsl);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    bundle.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = isCompute ? WGPUShaderStage_Compute : WGPUShaderStage_Fragment;
    entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
    entry.storageTexture.access = access;
    entry.storageTexture.format = format;
    entry.storageTexture.viewDimension = viewDimension;

    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = 1;
    bglDesc.entries = &entry;
    bundle.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bundle.bindGroupLayout;
    bundle.pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    if (isCompute) {
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = bundle.pipelineLayout;
        pipelineDesc.compute.module = bundle.module;
        pipelineDesc.compute.entryPoint = stringView(computeEntry);
        bundle.computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    } else {
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = bundle.module;
        fragment.entryPoint = stringView("fs");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        WGPURenderPipelineDescriptor renderDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        renderDesc.layout = bundle.pipelineLayout;
        renderDesc.vertex.module = bundle.module;
        renderDesc.vertex.entryPoint = stringView("vs");
        renderDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        renderDesc.multisample.count = 1;
        renderDesc.fragment = &fragment;
        bundle.renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderDesc);
    }
    return bundle;
}

const TextureStorePipelineBundle& textureStorePipelineForDevice(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl,
    bool isCompute,
    WGPUStorageTextureAccess access,
    WGPUTextureFormat format,
    WGPUTextureViewDimension viewDimension,
    const std::string& computeEntry) {
    static std::unordered_map<WGPUDevice, std::unordered_map<std::string, TextureStorePipelineBundle>> cache;
    auto& deviceCache = cache[t.device()];
    std::ostringstream key;
    key << wgsl << "\n// layout:"
        << " compute=" << (isCompute ? 1 : 0)
        << " access=" << static_cast<uint32_t>(access)
        << " format=" << static_cast<uint32_t>(format)
        << " view=" << static_cast<uint32_t>(viewDimension)
        << " entry=" << computeEntry;
    const std::string keyStr = key.str();
    auto it = deviceCache.find(keyStr);
    if (it == deviceCache.end()) {
        it = deviceCache
                 .emplace(keyStr,
                          createTextureStorePipelineBundle(t.device(), wgsl, isCompute, access, format,
                                                           viewDimension, computeEntry))
                 .first;
    }
    return it->second;
}

// Read back the given mip level of a texture into a freshly zero-filled buffer
// and return the raw (256-byte-row-padded) bytes.
std::vector<uint8_t> storeReadbackMip(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevel) {
    const TextureCopyLayout layout = getTextureCopyLayout(format, dimension, baseSize, mipLevel);
    const uint64_t bufferSize = alignToU32(static_cast<uint32_t>(layout.byteLength), 4u);
    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

    // Zero-fill so unwritten texels / padding read back as zero (anti-false-pass).
    const std::vector<uint8_t> zeros(static_cast<size_t>(bufferSize), 0);
    t.queueWriteBuffer(buffer, 0, zeros.data(), zeros.size());

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUTexelCopyTextureInfo srcInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    srcInfo.texture = texture;
    srcInfo.mipLevel = mipLevel;
    srcInfo.origin = WGPUOrigin3D{0, 0, 0};
    srcInfo.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dstInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dstInfo.buffer = buffer;
    dstInfo.layout.offset = 0;
    dstInfo.layout.bytesPerRow = layout.bytesPerRow;
    dstInfo.layout.rowsPerImage = layout.rowsPerImage;
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcInfo, &dstInfo, &layout.mipSize);
    submit(t, encoder);

    std::vector<uint8_t> result;
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            result.assign(actual, actual + len);
            return std::nullopt;
        },
        0,
        static_cast<size_t>(layout.byteLength));
    return result;
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

bool isGatherFillableFormatParam(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    if (isCompressedTextureFormat(format)) {
        return !endsWithFormatToken(format, "float");
    }
    return true;
}

bool isGatherFilterNearestOrPossiblyFilterableParam(const ParamRecord& record) {
    return paramString(record, "filt") == "nearest" || isPossiblyFilterableAsTextureF32(paramFormat(record));
}

void executeTextureGatherSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::Sampled2D, false);
    c.gatherComponent = t.param<std::string>("C") == "i32" ? 1 : 2;
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeGatherCase(t, c);
}

void executeTextureGatherSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::SampledCube, false);
    c.gatherComponent = t.param<std::string>("C") == "i32" ? 1 : 2;
    c.viewDimension = WGPUTextureViewDimension_Cube;
    c.baseSize = baseSize(8, 8, 6);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    executeGatherCase(t, c);
}

void executeTextureGatherSampledArray2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::Sampled2DArray, false);
    c.gatherComponent = t.param<std::string>("C") == "i32" ? 1 : 2;
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeGatherCase(t, c);
}

void executeTextureGatherSampledArray3D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::SampledCubeArray, false);
    c.gatherComponent = t.param<std::string>("C") == "i32" ? 1 : 2;
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(8, 8, 24);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeGatherCase(t, c);
}

void executeTextureGatherDepth2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::Depth2D, true);
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    executeGatherCase(t, c);
}

void executeTextureGatherDepth3D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::DepthCube, true);
    c.viewDimension = WGPUTextureViewDimension_Cube;
    c.baseSize = baseSize(8, 8, 6);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    executeGatherCase(t, c);
}

void executeTextureGatherDepthArray2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::Depth2DArray, true);
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeGatherCase(t, c);
}

void executeTextureGatherDepthArray3D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::DepthCubeArray, true);
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(8, 8, 24);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeGatherCase(t, c);
}

void executeTextureGatherCompareArray2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::Depth2DArray, true);
    c.builtin = "textureGatherCompare";
    c.gatherCompare = true;
    c.comparisonSampler = true;
    c.filt = t.param<std::string>("filt");
    c.compare = t.param<std::string>("compare");
    c.viewDimension = WGPUTextureViewDimension_2DArray;
    c.baseSize = baseSize(8, 8, static_cast<uint32_t>(t.param<int>("depthOrArrayLayers")));
    c.modeU = addressModeFromShort(t.param<std::string>("modeU"));
    c.modeV = addressModeFromShort(t.param<std::string>("modeV"));
    c.useOffset = t.param<bool>("offset");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeGatherCase(t, c);
}

void executeTextureGatherCompareArray3D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::DepthCubeArray, true);
    c.builtin = "textureGatherCompare";
    c.gatherCompare = true;
    c.comparisonSampler = true;
    c.filt = t.param<std::string>("filt");
    c.compare = t.param<std::string>("compare");
    c.viewDimension = WGPUTextureViewDimension_CubeArray;
    c.baseSize = baseSize(8, 8, 24);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    executeGatherCase(t, c);
}

void executeTextureGatherCompareSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::Depth2D, true);
    c.builtin = "textureGatherCompare";
    c.gatherCompare = true;
    c.comparisonSampler = true;
    c.filt = t.param<std::string>("filt");
    c.compare = t.param<std::string>("compare");
    c.baseSize = baseSize(8, 8, 1);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.useOffset = t.param<bool>("offset");
    executeGatherCase(t, c);
}

void executeTextureGatherCompareSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureCase c = baseGatherCase(t, SampleKind::DepthCube, true);
    c.builtin = "textureGatherCompare";
    c.gatherCompare = true;
    c.comparisonSampler = true;
    c.filt = t.param<std::string>("filt");
    c.compare = t.param<std::string>("compare");
    c.viewDimension = WGPUTextureViewDimension_Cube;
    c.baseSize = baseSize(8, 8, 6);
    c.modeU = addressModeFromShort(t.param<std::string>("mode"));
    c.modeV = c.modeU;
    c.modeW = c.modeU;
    executeGatherCase(t, c);
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

std::vector<Value> textureLoadShaderStages() {
    return values({"c", "f", "v"});
}

std::vector<Value> textureLoadMultisampledFormats() {
    std::vector<Value> out;
    for (WGPUTextureFormat format : kAllTextureFormats) {
        if (textureFormatInfo(format).multisample) {
            out.emplace_back(std::string(textureFormatInfo(format).identifier));
        }
    }
    return out;
}

bool textureLoadFormatCompatibleWith1D(const ParamRecord& record) {
    return textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension_1D, paramFormat(record));
}

bool textureLoadFormatCompatibleWith3D(const ParamRecord& record) {
    const WGPUTextureFormat format = paramFormat(record);
    return textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension_3D, format)
        && !isBCTextureFormat(format)
        && !isASTCTextureFormat(format);
}

bool textureLoadFormatNotCompressed(const ParamRecord& record) {
    return !isCompressedTextureFormat(paramFormat(record));
}

bool textureLoadFormatNotCompressedFloat(const ParamRecord& record) {
    return !isCompressedFloatTextureLoadFormat(paramFormat(record));
}

bool textureLoadFormatFillable(const ParamRecord& record) {
    return !isCompressedFloatTextureLoadFormat(paramFormat(record));
}

bool textureLoadFormatHasDepth(const ParamRecord& record) {
    return isDepthTextureFormat(paramFormat(record));
}

bool textureLoadDepthTextureTypeMatchesFormat(const ParamRecord& record) {
    const std::string type = paramString(record, "texture_type");
    return type.find("depth") == std::string::npos || isDepthTextureFormat(paramFormat(record));
}

std::vector<ParamRecord> textureLoadArrayedCoordinateParams() {
    return {
        {{"C", Value("i32")}, {"A", Value("u32")}, {"L", Value("u32")}},
        {{"C", Value("u32")}, {"A", Value("u32")}, {"L", Value("u32")}},
        {{"C", Value("u32")}, {"A", Value("i32")}, {"L", Value("u32")}},
        {{"C", Value("u32")}, {"A", Value("u32")}, {"L", Value("i32")}},
    };
}

bool textureLoadArrayLayerBaseValid(const ParamRecord& record) {
    return paramU32(record, "depthOrArrayLayers") != 1u || paramU32(record, "baseArrayLayer") == 0u;
}

TextureLoadCase baseLoadSampled(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureDimension dimension, WGPUTextureViewDimension viewDimension, uint32_t coordComponents, const std::string& typeBase) {
    TextureLoadCase c;
    c.stage = t.param<std::string>("stage");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.textureDimension = dimension;
    c.viewDimension = viewDimension;
    c.coordComponents = coordComponents;
    c.coordType = t.param<std::string>("C");
    c.levelType = t.param<std::string>("L");
    c.mipLevelCount = dimension == WGPUTextureDimension_1D ? 1 : 3;
    c.returnKind = loadReturnKindForFormat(c.format, false);
    c.textureType = appendLoadComponentType(typeBase, c.format, false);
    c.sampleType = metadataSampleType(c.format, WGPUTextureAspect_All, false, 1);
    if (isDepthTextureFormat(c.format)) {
        c.isDepth = true;
        c.returnKind = LoadReturnKind::Depth;
        c.textureType = viewDimension == WGPUTextureViewDimension_2DArray ? "texture_depth_2d_array" : "texture_depth_2d";
        c.sampleType = WGPUTextureSampleType_Depth;
        c.aspect = WGPUTextureAspect_DepthOnly;
        c.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    } else if (isStencilOnlyFormat(c.format)) {
        c.aspect = WGPUTextureAspect_StencilOnly;
        c.sampleType = WGPUTextureSampleType_Uint;
        c.textureType = appendLoadComponentType(typeBase, c.format, false);
        c.returnKind = LoadReturnKind::Uint;
    }
    c.baseSize = dimension == WGPUTextureDimension_1D ? WGPUExtent3D{16, 1, 1}
        : dimension == WGPUTextureDimension_3D ? WGPUExtent3D{8, 8, 8}
        : WGPUExtent3D{8, 8, 1};
    TextureCase tc;
    tc.format = c.format;
    tc.textureDimension = c.textureDimension;
    tc.viewDimension = c.viewDimension;
    tc.baseSize = c.baseSize;
    tc.mipLevelCount = c.mipLevelCount;
    c.baseSize = adjustedBaseSizeForFormat(tc);
    return c;
}

void executeTextureLoadSampled1D(AllFeaturesMaxLimitsGpuTest& t) {
    executeTextureLoadCase(t, baseLoadSampled(t, WGPUTextureDimension_1D, WGPUTextureViewDimension_1D, 1, "texture_1d"));
}

void executeTextureLoadSampled2D(AllFeaturesMaxLimitsGpuTest& t) {
    executeTextureLoadCase(t, baseLoadSampled(t, WGPUTextureDimension_2D, WGPUTextureViewDimension_2D, 2, "texture_2d"));
}

void executeTextureLoadSampled3D(AllFeaturesMaxLimitsGpuTest& t) {
    executeTextureLoadCase(t, baseLoadSampled(t, WGPUTextureDimension_3D, WGPUTextureViewDimension_3D, 3, "texture_3d"));
}

void executeTextureLoadMultisampled(AllFeaturesMaxLimitsGpuTest& t) {
    TextureLoadCase c;
    c.stage = t.param<std::string>("stage");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.isDepth = t.param<std::string>("texture_type") == "texture_depth_multisampled_2d";
    c.multisampled = true;
    c.sampleCount = 4;
    c.mipLevelCount = 1;
    c.useLevel = false;
    c.useSampleIndex = true;
    c.coordComponents = 2;
    c.coordType = t.param<std::string>("C");
    c.sampleIndexType = t.param<std::string>("S");
    c.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    if (isDepthTextureFormat(c.format)) {
        c.isDepth = true;
    }
    c.returnKind = loadReturnKindForFormat(c.format, c.isDepth);
    c.textureType = c.isDepth ? "texture_depth_multisampled_2d"
                              : appendLoadComponentType("texture_multisampled_2d", c.format, false);
    c.sampleType = c.isDepth ? WGPUTextureSampleType_Depth : metadataSampleType(c.format, WGPUTextureAspect_All, false, c.sampleCount);
    c.aspect = (c.isDepth || isDepthTextureFormat(c.format)) ? WGPUTextureAspect_DepthOnly
        : isStencilOnlyFormat(c.format) ? WGPUTextureAspect_StencilOnly
                                        : WGPUTextureAspect_All;
    c.baseSize = WGPUExtent3D{8, 8, 1};
    executeTextureLoadCase(t, c);
}

void executeTextureLoadDepth(AllFeaturesMaxLimitsGpuTest& t) {
    TextureLoadCase c = baseLoadSampled(t, WGPUTextureDimension_2D, WGPUTextureViewDimension_2D, 2, "texture_depth_2d");
    c.isDepth = true;
    c.returnKind = LoadReturnKind::Depth;
    c.textureType = "texture_depth_2d";
    c.sampleType = WGPUTextureSampleType_Depth;
    c.aspect = WGPUTextureAspect_DepthOnly;
    c.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    executeTextureLoadCase(t, c);
}

void executeTextureLoadExternal(AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("texture_external is not exposed through the native C WebGPU API path used by this port");
}

void executeTextureLoadArrayed(AllFeaturesMaxLimitsGpuTest& t) {
    const bool depthParam = t.param<std::string>("texture_type") == "texture_depth_2d_array";
    TextureLoadCase c = baseLoadSampled(t, WGPUTextureDimension_2D, WGPUTextureViewDimension_2DArray, 2, depthParam ? "texture_depth_2d_array" : "texture_2d_array");
    const bool depth = depthParam || isDepthTextureFormat(c.format);
    c.isDepth = depth;
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    c.baseSize.depthOrArrayLayers = paramU32(t.params(), "depthOrArrayLayers");
    c.textureType = depth ? "texture_depth_2d_array" : appendLoadComponentType("texture_2d_array", c.format, false);
    c.returnKind = loadReturnKindForFormat(c.format, depth);
    c.sampleType = depth ? WGPUTextureSampleType_Depth : metadataSampleType(c.format, c.aspect, false, 1);
    c.aspect = depth ? WGPUTextureAspect_DepthOnly
        : isStencilOnlyFormat(c.format) ? WGPUTextureAspect_StencilOnly
                                        : WGPUTextureAspect_All;
    c.usage = depth ? (WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment)
                    : (WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
    executeTextureLoadCase(t, c);
}

TextureLoadCase baseLoadStorage(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureDimension dimension, WGPUTextureViewDimension viewDimension, uint32_t coordComponents, const std::string& typeName) {
    TextureLoadCase c;
    c.stage = t.param<std::string>("stage");
    c.samplePoints = t.param<std::string>("samplePoints");
    c.format = parseTextureFormat(t.param<std::string>("format"));
    c.textureDimension = dimension;
    c.viewDimension = viewDimension;
    c.coordComponents = coordComponents;
    c.coordType = t.param<std::string>("C");
    c.useLevel = false;
    c.storageTexture = true;
    c.storageAccess = WGPUStorageTextureAccess_ReadOnly;
    c.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;
    c.returnKind = loadReturnKindForFormat(c.format, false);
    c.textureType = typeName + "<" + std::string(textureFormatInfo(c.format).identifier) + ", read>";
    c.sampleType = WGPUTextureSampleType_BindingNotUsed;
    c.baseSize = dimension == WGPUTextureDimension_1D ? WGPUExtent3D{16, 1, 1}
        : dimension == WGPUTextureDimension_3D ? WGPUExtent3D{8, 8, 8}
        : WGPUExtent3D{8, 8, 1};
    return c;
}

void executeTextureLoadStorage1D(AllFeaturesMaxLimitsGpuTest& t) {
    executeTextureLoadCase(t, baseLoadStorage(t, WGPUTextureDimension_1D, WGPUTextureViewDimension_1D, 1, "texture_storage_1d"));
}

void executeTextureLoadStorage2D(AllFeaturesMaxLimitsGpuTest& t) {
    TextureLoadCase c = baseLoadStorage(t, WGPUTextureDimension_2D, WGPUTextureViewDimension_2D, 2, "texture_storage_2d");
    c.mipLevelCount = 3;
    c.baseMipLevel = paramU32(t.params(), "baseMipLevel");
    c.viewMipCount = 1;
    executeTextureLoadCase(t, c);
}

void executeTextureLoadStorage2DArray(AllFeaturesMaxLimitsGpuTest& t) {
    TextureLoadCase c = baseLoadStorage(t, WGPUTextureDimension_2D, WGPUTextureViewDimension_2DArray, 2, "texture_storage_2d_array");
    c.useArrayIndex = true;
    c.arrayIndexType = t.param<std::string>("A");
    c.mipLevelCount = 3;
    c.baseMipLevel = paramU32(t.params(), "baseMipLevel");
    c.viewMipCount = 1;
    c.baseArrayLayer = paramU32(t.params(), "baseArrayLayer");
    c.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    c.baseSize.depthOrArrayLayers = paramU32(t.params(), "depthOrArrayLayers");
    executeTextureLoadCase(t, c);
}

void executeTextureLoadStorage3D(AllFeaturesMaxLimitsGpuTest& t) {
    executeTextureLoadCase(t, baseLoadStorage(t, WGPUTextureDimension_3D, WGPUTextureViewDimension_3D, 3, "texture_storage_3d"));
}

// ---------------------------------------------------------------------------
// textureStore param helpers
// ---------------------------------------------------------------------------

std::vector<Value> storeViewDimensions() {
    return values({"1d", "2d", "2d-array", "3d"});
}

std::vector<Value> storeDimensions() {
    return values({"1d", "2d", "3d"});
}

bool storeReadWriteAccessOrFormatSupported(const ParamRecord& record) {
    // unless(access === 'read_write' && !isTextureFormatPossiblyStorageReadWritable(format))
    return paramString(record, "access") != "read_write" || isStorageReadWriteFormatParam(record);
}

bool storeViewDimensionMipLevelValid(const ParamRecord& record) {
    // unless(viewDimension === '1d' && mipLevel !== 0)
    const Value* mip = findParam(record, "mipLevel");
    const int64_t mipLevel = mip == nullptr ? 0 : valueAs<int64_t>(*mip);
    return !(paramString(record, "viewDimension") == "1d" && mipLevel != 0);
}

bool storeOutOfBoundsMipValid(const ParamRecord& record) {
    const std::string dim = paramString(record, "dim");
    const int64_t mipCount = valueAs<int64_t>(*findParam(record, "mipCount"));
    const int64_t mip = valueAs<int64_t>(*findParam(record, "mip"));
    if (dim == "1d") {
        return mipCount == 1 && mip == 0;
    }
    if (dim == "3d") {
        return mipCount <= 2 && mip < mipCount;
    }
    return mip < mipCount;
}

bool storeOutOfBoundsArrayValid(const ParamRecord& record) {
    const int64_t baseLevel = valueAs<int64_t>(*findParam(record, "baseLevel"));
    const int64_t arrayLevels = valueAs<int64_t>(*findParam(record, "arrayLevels"));
    constexpr int64_t kArrayLevels = 4;
    if (arrayLevels <= baseLevel) {
        return false;
    }
    if (kArrayLevels < baseLevel + arrayLevels) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// textureStore: texel_formats
// ---------------------------------------------------------------------------

namespace {

uint32_t bytesPerTexelForStore(WGPUTextureFormat format) {
    return textureFormatInfo(format).bytesPerBlock;
}

WGPUTextureViewDimension storeParseViewDimension(const std::string& s) {
    if (s == "1d") return WGPUTextureViewDimension_1D;
    if (s == "2d") return WGPUTextureViewDimension_2D;
    if (s == "2d-array") return WGPUTextureViewDimension_2DArray;
    return WGPUTextureViewDimension_3D;
}

WGPUTextureDimension storeTextureDimensionFromView(const std::string& s) {
    if (s == "1d") return WGPUTextureDimension_1D;
    if (s == "3d") return WGPUTextureDimension_3D;
    return WGPUTextureDimension_2D;  // 2d and 2d-array
}

std::string storeViewDimensionWGSL(const std::string& s) {
    if (s == "2d-array") return "2d_array";
    return s;
}

} // namespace

void executeTextureStoreTexelFormats(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
    const std::string viewDimension = t.param<std::string>("viewDimension");
    const std::string stage = t.param<std::string>("stage");
    const std::string accessStr = t.param<std::string>("access");
    const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
    const WGPUStorageTextureAccess access =
        accessStr == "read_write" ? WGPUStorageTextureAccess_ReadWrite : WGPUStorageTextureAccess_WriteOnly;

    t.skipIfTextureFormatNotSupported(format);
    if (!t.isTextureFormatUsableWithStorageAccessMode(format, access)) {
        t.skip("texture format is not usable with requested storage access mode");
    }
    skipIfNoStorageTexturesInStage(t, stage);

    const WGPUTextureViewDimension viewDim = storeParseViewDimension(viewDimension);
    const WGPUTextureDimension dimension = storeTextureDimensionFromView(viewDimension);
    t.skipIfTextureViewDimensionNotSupported(viewDim);
    t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);

    const std::string componentType = storeComponentType(format);
    const std::vector<double> valuesArr = storeInputArray(format);
    const uint32_t len = static_cast<uint32_t>(valuesArr.size());

    const std::string suffix = endsWithFormatToken(format, "sint") ? "i"
        : endsWithFormatToken(format, "uint")                      ? "u"
                                                                   : "f";
    const std::string swizzleWGSL = viewDimension == "1d" ? "x" : viewDimension == "3d" ? "xyz" : "xy";
    const std::string layerWGSL = viewDimension == "2d-array" ? ", gid.z" : "";

    std::ostringstream rangeWGSL;
    for (uint32_t i = 0; i < len; ++i) {
        if (i != 0) rangeWGSL << ",";
        // Emit values as decimals; integer formats use i/u suffix.
        const double v = valuesArr[i];
        if (suffix == "f") {
            std::ostringstream vs;
            vs.precision(17);
            vs << v;
            rangeWGSL << vs.str();
            if (vs.str().find('.') == std::string::npos && vs.str().find('e') == std::string::npos
                && vs.str().find("inf") == std::string::npos) {
                rangeWGSL << ".0";
            }
            rangeWGSL << suffix;
        } else if (suffix == "u") {
            rangeWGSL << static_cast<uint64_t>(static_cast<int64_t>(v)) << suffix;
        } else {
            rangeWGSL << static_cast<int64_t>(v) << suffix;
        }
    }

    std::ostringstream wgsl;
    wgsl << "const range = array(" << rangeWGSL.str() << ");\n"
         << "@group(0) @binding(0) var tex : texture_storage_" << storeViewDimensionWGSL(viewDimension)
         << "<" << textureFormatInfo(format).identifier << ", " << storageAccessWGSL(access) << ">;\n"
         << "fn setValue(gid: vec3u) {\n"
         << "  let ndx = gid.x + gid.y + gid.z;\n"
         << "  let vecVal = vec4(\n"
         << "    range[(ndx + 0) % " << len << "],\n"
         << "    range[(ndx + 1) % " << len << "],\n"
         << "    range[(ndx + 2) % " << len << "],\n"
         << "    range[(ndx + 3) % " << len << "],\n"
         << "  );\n"
         << "  var val = vec4<" << componentType << ">(vecVal);\n"
         << "  let coord = gid." << swizzleWGSL << ";\n"
         << "  textureStore(tex, coord" << layerWGSL << ", val);\n"
         << "}\n"
         << "@compute @workgroup_size(" << len << ")\n"
         << "fn cs(@builtin(global_invocation_id) gid : vec3u) {\n"
         << "  setValue(gid);\n"
         << "}\n"
         << "struct VOut {\n"
         << "  @builtin(position) pos: vec4f,\n"
         << "  @location(0) @interpolate(flat, either) z: u32,\n"
         << "}\n"
         << "@vertex fn vs(@builtin(vertex_index) vNdx: u32, @builtin(instance_index) iNdx: u32) -> VOut {\n"
         << "  let pos = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));\n"
         << "  return VOut(vec4f(pos[vNdx], 0, 1), iNdx);\n"
         << "}\n"
         << "@fragment fn fs(v: VOut) -> @location(0) vec4f {\n"
         << "  setValue(vec3u(u32(v.pos.x), u32(v.pos.y), v.z));\n"
         << "  return vec4f(0);\n"
         << "}\n";

    // Choose a size so the mipLevel we will write to is the size we want to test.
    const uint32_t mipMult = 1u << mipLevel;
    const uint32_t size = len * mipMult;
    const WGPUExtent3D mipLevel0Size{
        size,
        viewDimension == "1d" ? 1u : size,
        viewDimension == "2d-array" ? len : viewDimension == "3d" ? size : 1u};
    const WGPUExtent3D testMipLevelSize{
        len,
        viewDimension == "1d" ? 1u : len,
        (viewDimension == "2d-array" || viewDimension == "3d") ? len : 1u};

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = mipLevel0Size;
    textureDesc.mipLevelCount = viewDimension == "1d" ? 1u : 3u;
    textureDesc.dimension = dimension;
    textureDesc.format = format;
    textureDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    const bool isCompute = stage == "compute";
    const TextureStorePipelineBundle& pipeline =
        textureStorePipelineForDevice(t, wgsl.str(), isCompute, access, format, viewDim, "cs");

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.format = format;
    viewDesc.dimension = viewDim;
    viewDesc.baseMipLevel = mipLevel;
    viewDesc.mipLevelCount = 1;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 0;
    bgEntry.textureView = view;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = pipeline.bindGroupLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    if (isCompute) {
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline.computePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, testMipLevelSize.width,
                                                 testMipLevelSize.height,
                                                 testMipLevelSize.depthOrArrayLayers);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    } else {
        WGPUTextureDescriptor renderDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        renderDesc.size = WGPUExtent3D{testMipLevelSize.width, testMipLevelSize.height, 1};
        renderDesc.dimension = WGPUTextureDimension_2D;
        renderDesc.format = WGPUTextureFormat_RGBA8Unorm;
        renderDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture renderTarget = t.createTextureTracked(renderDesc);
        WGPUTextureViewDescriptor rtViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView rtView = t.createViewTracked(renderTarget, rtViewDesc);
        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = rtView;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = WGPUColor{0, 0, 0, 0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline.renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, testMipLevelSize.depthOrArrayLayers, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    submit(t, encoder);

    // Read back the written mip and build the expected (zero-padded) bytes.
    const std::vector<uint8_t> got =
        storeReadbackMip(t, texture, format, dimension, mipLevel0Size, mipLevel);
    const TextureCopyLayout layout = getTextureCopyLayout(format, dimension, mipLevel0Size, mipLevel);
    const uint32_t bytesPerTexel = bytesPerTexelForStore(format);
    const uint32_t texelsPerRow = layout.bytesPerRow / bytesPerTexel;

    std::vector<uint8_t> expected(static_cast<size_t>(layout.byteLength), 0);
    for (uint32_t z = 0; z < testMipLevelSize.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < testMipLevelSize.height; ++y) {
            for (uint32_t x = 0; x < testMipLevelSize.width; ++x) {
                const uint32_t id = x + y + z;
                std::array<double, 4> vals{
                    valuesArr[(id + 0u) % len],
                    valuesArr[(id + 1u) % len],
                    valuesArr[(id + 2u) % len],
                    valuesArr[(id + 3u) % len]};
                const std::vector<uint8_t> texel = storeEncodeTexel(format, vals);
                const size_t byteOffset = static_cast<size_t>(z) * layout.bytesPerRow * layout.rowsPerImage
                    + static_cast<size_t>(y) * layout.bytesPerRow + static_cast<size_t>(x) * bytesPerTexel;
                for (size_t b = 0; b < texel.size() && byteOffset + b < expected.size(); ++b) {
                    expected[byteOffset + b] = texel[b];
                }
            }
        }
    }
    (void)texelsPerRow;

    if (got.size() < expected.size()) {
        t.fail("textureStore readback too small");
    }
    auto failMismatch = [&](size_t i, std::string_view label) {
        std::ostringstream msg;
        msg << "textureStore ";
        if (!label.empty()) {
            msg << label << " ";
        }
        msg << "mismatch at byte " << i << ": expected " << static_cast<uint32_t>(expected[i])
            << ", got " << static_cast<uint32_t>(got[i]) << ", format " << textureFormatInfo(format).identifier
            << ", viewDimension " << viewDimension << ", stage " << stage << ", access " << accessStr
            << ", mipLevel " << mipLevel;
        t.fail(msg.str());
    };
    const TexelRepresentation& storeRepr = texelRepresentation(format);
    if (storeFormatHasNormalizedComponents(storeRepr)) {
        // Normalized textureStore may round exact-half ties to either adjacent encoded integer.
        std::vector<uint8_t> texelBytes(expected.size(), 0);
        for (uint32_t z = 0; z < testMipLevelSize.depthOrArrayLayers; ++z) {
            for (uint32_t y = 0; y < testMipLevelSize.height; ++y) {
                for (uint32_t x = 0; x < testMipLevelSize.width; ++x) {
                    const size_t byteOffset = static_cast<size_t>(z) * layout.bytesPerRow * layout.rowsPerImage
                        + static_cast<size_t>(y) * layout.bytesPerRow + static_cast<size_t>(x) * bytesPerTexel;
                    for (size_t b = 0; b < bytesPerTexel && byteOffset + b < texelBytes.size(); ++b) {
                        texelBytes[byteOffset + b] = 1;
                    }
                    if (!normalizedStoreTexelMatches(
                            storeRepr, expected.data() + byteOffset, got.data() + byteOffset)) {
                        failMismatch(byteOffset, "normalized texel");
                    }
                }
            }
        }
        for (size_t i = 0; i < expected.size(); ++i) {
            if (!texelBytes[i] && got[i] != expected[i]) {
                failMismatch(i, "padding byte");
            }
        }
        return;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (got[i] != expected[i]) {
            failMismatch(i, "");
        }
    }
}

// ---------------------------------------------------------------------------
// textureStore: bgra8unorm_swizzle
// ---------------------------------------------------------------------------

void executeTextureStoreBgra8unormSwizzle(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_BGRA8UnormStorage)) {
        t.skip("device does not have feature bgra8unorm-storage");
    }
    const WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    struct V { double r, g, b, a; };
    const std::array<V, 8> values = {{
        {-1.1, 0.6, 0.4, 1},
        {1.1, 0.6, 0.4, 1},
        {0.4, -1.1, 0.6, 1},
        {0.4, 1.1, 0.6, 1},
        {0.6, 0.4, -1.1, 1},
        {0.6, 0.4, 1.1, 1},
        {0.2, 0.4, 0.6, 1},
        {-0.2, -0.4, -0.6, 1},
    }};
    const uint32_t numTexels = static_cast<uint32_t>(values.size());

    std::ostringstream wgsl;
    wgsl << "@group(0) @binding(0) var tex : texture_storage_1d<bgra8unorm, write>;\n"
         << "const values = array(";
    for (const V& v : values) {
        wgsl << "vec4(" << v.r << "," << v.g << "," << v.b << "," << v.a << "),\n";
    }
    wgsl << ");\n"
         << "@compute @workgroup_size(" << numTexels << ")\n"
         << "fn main(@builtin(global_invocation_id) gid : vec3u) {\n"
         << "  let value = values[gid.x];\n"
         << "  textureStore(tex, gid.x, value);\n"
         << "}";

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = WGPUExtent3D{numTexels, 1, 1};
    textureDesc.dimension = WGPUTextureDimension_1D;
    textureDesc.format = format;
    textureDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    const TextureStorePipelineBundle& pipeline = textureStorePipelineForDevice(
        t, wgsl.str(), true, WGPUStorageTextureAccess_WriteOnly, format, WGPUTextureViewDimension_1D, "main");

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.format = format;
    viewDesc.dimension = WGPUTextureViewDimension_1D;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 0;
    bgEntry.textureView = view;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = pipeline.bindGroupLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline.computePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    submit(t, encoder);

    const std::vector<uint8_t> got =
        storeReadbackMip(t, texture, format, WGPUTextureDimension_1D, textureDesc.size, 0);
    std::vector<uint8_t> expected(got.size(), 0);
    for (uint32_t x = 0; x < numTexels; ++x) {
        const V& v = values[x];
        // bgra8unorm: the GPU stores the vec4 (r,g,b,a) value; TexelRepresentation
        // already encodes the BGRA byte order, so feed it (r,g,b,a) directly.
        const std::vector<uint8_t> texel = storeEncodeTexel(format, {v.r, v.g, v.b, v.a});
        for (size_t b = 0; b < texel.size(); ++b) {
            expected[static_cast<size_t>(x) * 4 + b] = texel[b];
        }
    }
    for (size_t i = 0; i < numTexels * 4u && i < expected.size(); ++i) {
        if (got[i] != expected[i]) {
            std::ostringstream msg;
            msg << "textureStore bgra8unorm_swizzle mismatch at byte " << i << ": expected "
                << static_cast<uint32_t>(expected[i]) << ", got " << static_cast<uint32_t>(got[i]);
            t.fail(msg.str());
        }
    }
}

// ---------------------------------------------------------------------------
// textureStore: out_of_bounds
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kStoreOOBWidth = 256;

WGPUExtent3D storeOOBTextureSize(uint32_t numTexels, WGPUTextureDimension dim) {
    WGPUExtent3D size{1, 1, 1};
    if (dim == WGPUTextureDimension_1D) {
        size.width = numTexels;
    } else if (dim == WGPUTextureDimension_2D) {
        size.width = kStoreOOBWidth;
        size.height = numTexels / kStoreOOBWidth;
        size.depthOrArrayLayers = 1;
    } else {
        size.width = kStoreOOBWidth;
        size.height = numTexels / (2 * kStoreOOBWidth);
        size.depthOrArrayLayers = 2;
    }
    return size;
}

std::string storeOOBTextureType(WGPUTextureDimension dim) {
    if (dim == WGPUTextureDimension_1D) return "texture_storage_1d<r32uint, write>";
    if (dim == WGPUTextureDimension_2D) return "texture_storage_2d<r32uint, write>";
    return "texture_storage_3d<r32uint, write>";
}

std::string storeOOBIndexToCoord(WGPUTextureDimension dim, const std::string& type) {
    if (dim == WGPUTextureDimension_1D) {
        return "fn indexToCoord(id : u32) -> " + type + " {\n  return " + type + "(id);\n}";
    }
    if (dim == WGPUTextureDimension_2D) {
        return "fn indexToCoord(id : u32) -> vec2<" + type + "> {\n  return vec2<" + type + ">(" + type
            + "(id % width), " + type + "(id / width));\n}";
    }
    return "fn indexToCoord(id : u32) -> vec3<" + type
        + "> {\n  const half = numTexels / depth;\n  let half_id = id % half;\n  return vec3<" + type + ">("
        + type + "(half_id % width), " + type + "(half_id / width), " + type + "(id / half));\n}";
}

std::string storeOOBValue(WGPUTextureDimension dim, const std::string& type) {
    if (dim == WGPUTextureDimension_1D) {
        if (type == "i32") {
            return "if gid.x % 3 == 0 {\n          coords = -coords;\n        } else {\n          coords = "
                   "coords + numTexels;\n        }";
        }
        return "coords = coords + numTexels;";
    }
    if (dim == WGPUTextureDimension_2D) {
        if (type == "i32") {
            return "if gid.x % 3 == 0 {\n          coords.x = -coords.x;\n        } else {\n          "
                   "coords.y = coords.y + height;\n        }";
        }
        return "if gid.x % 3 == 1 {\n          coords.x = coords.x + width;\n        } else {\n          "
               "coords.y = coords.y + height;\n        }";
    }
    if (type == "i32") {
        return "if gid.x % 3 == 0 {\n          coords.x = -coords.x;\n        } else if gid.x % 5 == 0 {\n  "
               "        coords.y = coords.y + height;\n        } else {\n          coords.z = coords.z + "
               "depth;\n        }";
    }
    return "if gid.x % 3 == 1 {\n          coords.x = coords.x + width;\n        } else if gid.x % 5 == 1 "
           "{\n          coords.y = coords.y + height;\n        } else {\n          coords.z = 2 * "
           "depth;\n        }";
}

uint32_t storeOOBMipTexels(uint32_t numTexels, WGPUTextureDimension dim, uint32_t mip) {
    uint32_t texels = numTexels;
    if (mip == 0) return texels;
    if (dim == WGPUTextureDimension_2D) {
        texels /= (1u << mip);
        texels /= (1u << mip);
    } else if (dim == WGPUTextureDimension_3D) {
        texels /= (1u << mip);
        texels /= (1u << mip);
        texels /= (1u << mip);
    }
    return texels;
}

} // namespace

void executeTextureStoreOutOfBounds(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPUTextureFormat format = WGPUTextureFormat_R32Uint;
    const std::string dimStr = t.param<std::string>("dim");
    const std::string coords = t.param<std::string>("coords");
    const uint32_t mipCount = static_cast<uint32_t>(t.param<int64_t>("mipCount"));
    const uint32_t mip = static_cast<uint32_t>(t.param<int64_t>("mip"));
    const WGPUTextureDimension dim = dimStr == "1d" ? WGPUTextureDimension_1D
        : dimStr == "3d"                            ? WGPUTextureDimension_3D
                                                    : WGPUTextureDimension_2D;
    const WGPUTextureViewDimension viewDim = dimStr == "1d" ? WGPUTextureViewDimension_1D
        : dimStr == "3d"                                    ? WGPUTextureViewDimension_3D
                                                            : WGPUTextureViewDimension_2D;

    skipIfNoStorageTexturesInStage(t, "compute");

    const uint32_t numTexels = 4096;
    const uint32_t viewTexels = storeOOBMipTexels(numTexels, dim, mip);
    const WGPUExtent3D textureSize = storeOOBTextureSize(numTexels, dim);
    const WGPUExtent3D mipSize = physicalMipSize(textureSize, dim, mip);

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = textureSize;
    textureDesc.dimension = dim;
    textureDesc.format = format;
    textureDesc.mipLevelCount = mipCount;
    textureDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    const std::string oobValue = storeOOBValue(dim, coords);
    const uint32_t wgxSize = 32;
    const uint32_t numWgsX = viewTexels / wgxSize;

    std::ostringstream wgsl;
    wgsl << "@group(0) @binding(0) var tex : " << storeOOBTextureType(dim) << ";\n"
         << "const numTexels = " << viewTexels << ";\n"
         << "const width = " << mipSize.width << ";\n"
         << "const height = " << mipSize.height << ";\n"
         << "const depth = " << mipSize.depthOrArrayLayers << ";\n"
         << storeOOBIndexToCoord(dim, coords) << "\n"
         << "@compute @workgroup_size(" << wgxSize << ")\n"
         << "fn main(@builtin(global_invocation_id) gid : vec3u) {\n"
         << "  var coords = indexToCoord(gid.x);\n"
         << "  if gid.x % 2 == 1 {\n    " << oobValue << "\n  }\n"
         << "  textureStore(tex, coords, vec4u(gid.x));\n"
         << "}";

    const TextureStorePipelineBundle& pipeline = textureStorePipelineForDevice(
        t, wgsl.str(), true, WGPUStorageTextureAccess_WriteOnly, format, viewDim, "main");

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.format = format;
    viewDesc.dimension = viewDim;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.baseMipLevel = mip;
    viewDesc.mipLevelCount = 1;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 0;
    bgEntry.textureView = view;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = pipeline.bindGroupLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline.computePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, numWgsX, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    submit(t, encoder);

    for (uint32_t m = 0; m < mipCount; ++m) {
        const std::vector<uint8_t> got =
            storeReadbackMip(t, texture, format, dim, textureSize, m);
        const TextureCopyLayout layout = getTextureCopyLayout(format, dim, textureSize, m);
        const WGPUExtent3D mSize = physicalMipSize(textureSize, dim, m);
        const uint32_t bytesPerTexel = 4;  // r32uint

        // Expected r32uint values laid out densely per upstream (no padding because
        // sizes are chosen so each row is a multiple of 256 bytes).
        std::vector<uint8_t> expected(static_cast<size_t>(layout.byteLength), 0);
        const uint32_t mipTexels = storeOOBMipTexels(numTexels, dim, m);
        for (uint32_t x = 0; x < mipTexels; ++x) {
            uint32_t value = 0;
            if (m == mip && (x % 2) == 0) {
                value = x;
            }
            // Map linear texel index to (col,row,slice) layout in the readback buffer.
            const uint32_t texelsPerRow = layout.bytesPerRow / bytesPerTexel;
            const uint32_t rowTexels = mSize.width;
            const uint32_t sliceRows = mSize.height;
            const uint32_t z = (rowTexels * sliceRows == 0) ? 0 : x / (rowTexels * sliceRows);
            const uint32_t rem = (rowTexels * sliceRows == 0) ? x : x % (rowTexels * sliceRows);
            const uint32_t y = rowTexels == 0 ? 0 : rem / rowTexels;
            const uint32_t col = rowTexels == 0 ? 0 : rem % rowTexels;
            const size_t byteOffset = static_cast<size_t>(z) * layout.bytesPerRow * layout.rowsPerImage
                + static_cast<size_t>(y) * layout.bytesPerRow + static_cast<size_t>(col) * bytesPerTexel;
            (void)texelsPerRow;
            if (byteOffset + 4 <= expected.size()) {
                std::memcpy(expected.data() + byteOffset, &value, sizeof(value));
            }
        }

        if (got.size() < expected.size()) {
            t.fail("textureStore out_of_bounds readback too small");
        }
        for (size_t i = 0; i < expected.size(); ++i) {
            if (got[i] != expected[i]) {
                std::ostringstream msg;
                msg << "textureStore out_of_bounds mismatch (mip " << m << ") at byte " << i << ": expected "
                    << static_cast<uint32_t>(expected[i]) << ", got " << static_cast<uint32_t>(got[i])
                    << ", dim " << dimStr << ", coords " << coords << ", mipCount " << mipCount << ", mip "
                    << mip;
                t.fail(msg.str());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// textureStore: out_of_bounds_array
// ---------------------------------------------------------------------------

void executeTextureStoreOutOfBoundsArray(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPUTextureFormat format = WGPUTextureFormat_R32Uint;
    const uint32_t baseLevel = static_cast<uint32_t>(t.param<int64_t>("baseLevel"));
    const uint32_t arrayLevels = static_cast<uint32_t>(t.param<int64_t>("arrayLevels"));
    const std::string type = t.param<std::string>("type");
    constexpr uint32_t kArrayLevels = 4;

    skipIfNoStorageTexturesInStage(t, "compute");

    const uint32_t width = 64;
    const uint32_t height = 64;
    const uint32_t baseTexels = width * height;
    const uint32_t numTexels = baseTexels * kArrayLevels;
    const uint32_t viewTexels = baseTexels * arrayLevels;
    const WGPUExtent3D textureSize{width, height, kArrayLevels};

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = textureSize;
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.format = format;
    textureDesc.mipLevelCount = 1;
    textureDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    WGPUTexture texture = t.createTextureTracked(textureDesc);

    const uint32_t wgxSize = 32;
    const uint32_t numWgsX = numTexels / wgxSize;

    std::string oobValue = "layer = layer + layers;";
    if (type == "i32") {
        oobValue = "if gid.x % 3 == 0 {\n        layer = -(layer + layers);\n      } else {\n        layer "
                   "= layer + layers;\n      }";
    }

    std::ostringstream wgsl;
    wgsl << "@group(0) @binding(0) var tex : texture_storage_2d_array<r32uint, write>;\n"
         << "const numTexels = " << viewTexels << ";\n"
         << "const width = " << width << ";\n"
         << "const height = " << height << ";\n"
         << "const layers = " << arrayLevels << ";\n"
         << "const layerTexels = numTexels / layers;\n"
         << "@compute @workgroup_size(" << wgxSize << ")\n"
         << "fn main(@builtin(global_invocation_id) gid : vec3u) {\n"
         << "  let layer_id = gid.x % layerTexels;\n"
         << "  var x = " << type << "(layer_id % width);\n"
         << "  var y = " << type << "(layer_id / width);\n"
         << "  var layer = " << type << "(gid.x / layerTexels);\n"
         << "  if gid.x % 2 == 1 {\n    " << oobValue << "\n  }\n"
         << "  textureStore(tex, vec2(x, y), layer, vec4u(gid.x));\n"
         << "}";

    const TextureStorePipelineBundle& pipeline = textureStorePipelineForDevice(
        t, wgsl.str(), true, WGPUStorageTextureAccess_WriteOnly, format,
        WGPUTextureViewDimension_2DArray, "main");

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.format = format;
    viewDesc.dimension = WGPUTextureViewDimension_2DArray;
    viewDesc.baseArrayLayer = baseLevel;
    viewDesc.arrayLayerCount = arrayLevels;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 0;
    bgEntry.textureView = view;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = pipeline.bindGroupLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline.computePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, numWgsX, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    submit(t, encoder);

    const std::vector<uint8_t> got =
        storeReadbackMip(t, texture, format, WGPUTextureDimension_2D, textureSize, 0);
    const TextureCopyLayout layout = getTextureCopyLayout(format, WGPUTextureDimension_2D, textureSize, 0);
    const uint32_t bytesPerTexel = 4;
    const uint32_t baseOffsetTexels = baseTexels * baseLevel;

    std::vector<uint8_t> expected(static_cast<size_t>(layout.byteLength), 0);
    for (uint32_t x = 0; x < numTexels; ++x) {
        uint32_t value = 0;
        const bool inRange = x >= baseOffsetTexels && x < baseTexels * (baseLevel + arrayLevels);
        if (inRange && (x % 2) == 0) {
            value = x - baseOffsetTexels;
        }
        // Layout: texel x belongs to slice (x / baseTexels), with (col,row) inside.
        const uint32_t z = x / baseTexels;
        const uint32_t within = x % baseTexels;
        const uint32_t y = within / width;
        const uint32_t col = within % width;
        const size_t byteOffset = static_cast<size_t>(z) * layout.bytesPerRow * layout.rowsPerImage
            + static_cast<size_t>(y) * layout.bytesPerRow + static_cast<size_t>(col) * bytesPerTexel;
        if (byteOffset + 4 <= expected.size()) {
            std::memcpy(expected.data() + byteOffset, &value, sizeof(value));
        }
    }

    if (got.size() < expected.size()) {
        t.fail("textureStore out_of_bounds_array readback too small");
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (got[i] != expected[i]) {
            std::ostringstream msg;
            msg << "textureStore out_of_bounds_array mismatch at byte " << i << ": expected "
                << static_cast<uint32_t>(expected[i]) << ", got " << static_cast<uint32_t>(got[i])
                << ", baseLevel " << baseLevel << ", arrayLevels " << arrayLevels << ", type " << type;
            t.fail(msg.str());
        }
    }
}

// =====================================================================================
// texture_utils.spec.ts meta-test (phaseY14 Stage C).
//
// These three functions back the meta-test that validates the texture random-data /
// texel encode-decode / sampling-weight helpers. We faithfully port the upstream
// logic of `chooseTextureSize`, `createTextureWithRandomDataAndGetTexels`,
// `readTextureToTexelViews`, `convertPerTexelComponentToResultFormat`,
// `texelsApproximatelyEqual`, `graphWeights`, `makeRandomDepthComparisonTexelGenerator`,
// `validateWeights`, and `queryMipLevelMixWeightsForDevice` (multi-stage), reusing the
// repo's `TexelRepresentation`/`TexelView`/layout primitives.
// =====================================================================================
namespace {

// hashU32 matching upstream `hash.ts` (mulberry32-style mixing over a list of u32).
uint32_t metaHashU32(std::initializer_list<int64_t> valuesIn) {
    uint32_t h = 0;
    for (int64_t raw : valuesIn) {
        uint32_t v = static_cast<uint32_t>(raw);
        h ^= h >> 16;
        h += v;
        h = (h ^ (h >> 15)) * 0x2c1b3c6du;
        h = (h ^ (h >> 12)) * 0x297a2d39u;
        h ^= h >> 15;
    }
    return h;
}

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

// The repo texel encoder has no Stencil8 representation; stencil reads back as uint, so use
// an encodable uint format for storage/comparison of the stencil8 texel-view format.
WGPUTextureFormat effectiveTexelViewFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_Stencil8 ? WGPUTextureFormat_RGBA32Uint : format;
}

enum class FormatType { Float, Sint, Uint, Depth };

// getTextureFormatType(format, 'all')
FormatType getMetaTextureFormatType(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    if (info.hasDepth) {
        return FormatType::Depth;
    }
    if (info.hasStencil) {
        // stencil-only ('all' aspect of stencil8) reads as uint.
        return FormatType::Uint;
    }
    const std::string_view id = info.identifier;
    if (id.find("sint") != std::string_view::npos) {
        return FormatType::Sint;
    }
    if (id.find("uint") != std::string_view::npos) {
        return FormatType::Uint;
    }
    return FormatType::Float;
}

// isTextureFormatPossiblyMultisampled
bool metaPossiblyMultisampled(WGPUTextureFormat format) {
    return textureFormatInfo(format).multisample;
}

uint32_t lcmU32Meta(uint32_t a, uint32_t b) {
    return lcmU32(a, b);
}

// chooseTextureSize({ minSize, minBlocks, format, viewDimension })
std::array<uint32_t, 3> chooseTextureSize(
    uint32_t minSize,
    uint32_t minBlocks,
    WGPUTextureFormat format,
    WGPUTextureViewDimension viewDimension) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    const uint32_t width = alignToU32(std::max(minSize, block.blockWidth * minBlocks), block.blockWidth);
    const uint32_t height = viewDimension == WGPUTextureViewDimension_1D
        ? 1u
        : alignToU32(std::max(minSize, block.blockHeight * minBlocks), block.blockHeight);
    if (viewDimension == WGPUTextureViewDimension_Cube
        || viewDimension == WGPUTextureViewDimension_CubeArray) {
        const uint32_t blockLcm = lcmU32Meta(block.blockWidth, block.blockHeight);
        const uint32_t largest = std::max(width, height);
        const uint32_t size = alignToU32(largest, blockLcm);
        return {size, size, viewDimension == WGPUTextureViewDimension_CubeArray ? 24u : 6u};
    }
    uint32_t depthOrArrayLayers = 1u;
    switch (viewDimension) {
        case WGPUTextureViewDimension_2DArray:
            depthOrArrayLayers = 4u;
            break;
        case WGPUTextureViewDimension_3D:
            depthOrArrayLayers = 8u;
            break;
        default:
            depthOrArrayLayers = 1u;
            break;
    }
    return {width, height, depthOrArrayLayers};
}

// Per-texel decoded color, indexed by RGBA component (depth in R for depth views).
struct MetaColor {
    std::array<double, 4> v = {0.0, 0.0, 0.0, 1.0};
};

// convertPerTexelComponentToResultFormat: place the texel-format's component order into RGBA.
MetaColor convertPerTexelComponentToResultFormat(const TexelComponents& comps, WGPUTextureFormat format) {
    MetaColor out;  // { R:0, G:0, B:0, A:1 }
    const TexelRepresentation& rep = texelRepresentation(format);
    for (TexelComponent component : rep.componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        out.v[index] = comps.values[index];
    }
    return out;
}

// numericRange min/max for one component of an encodable format (for random fill).
struct MinMax {
    double min = 0.0;
    double max = 1.0;
};

MinMax minMaxForComponent(const TexelRepresentation& rep, TexelComponent component) {
    const uint32_t index = static_cast<uint32_t>(component);
    const uint32_t bits = rep.bitLengths[index];
    switch (rep.dataTypes[index]) {
        case ComponentDataType::Unorm:
            return {0.0, 1.0};
        case ComponentDataType::Snorm:
            return {-1.0, 1.0};
        case ComponentDataType::Uint: {
            const double maxv = bits >= 32 ? 4294967295.0 : static_cast<double>((1u << bits) - 1u);
            return {0.0, maxv};
        }
        case ComponentDataType::Sint: {
            const double maxv = static_cast<double>((1 << (bits - 1)) - 1);
            return {-maxv - 1.0, maxv};
        }
        case ComponentDataType::Float:
        case ComponentDataType::Ufloat:
            // Keep values modest so they round-trip exactly through 32-bit float storage.
            return rep.dataTypes[index] == ComponentDataType::Ufloat ? MinMax{0.0, 1024.0} : MinMax{-1024.0, 1024.0};
    }
    return {0.0, 1.0};
}

// quantize: round a color through the texel representation so it matches stored bits.
TexelComponents quantizeMeta(const TexelComponents& texel, const TexelRepresentation& rep) {
    return rep.bitsToNumber(rep.numberToBits(texel));
}

// Generator callback type matching upstream RandomTextureOptions.generator.
using TexelGenerator = std::function<TexelComponents(uint32_t x, uint32_t y, uint32_t z, uint32_t sampleIndex)>;

// Quantize a single depth/stencil value the way storing+reading the texture would.
// The repo texel encoder has no depth/stencil representations, so we model the
// per-format quantization directly. Returns the value as it would round-trip.
double quantizeDepthStencilValue(WGPUTextureFormat format, double value) {
    switch (format) {
        case WGPUTextureFormat_Depth16Unorm: {
            const double scale = 65535.0;
            return std::round(clamp01(value) * scale) / scale;
        }
        case WGPUTextureFormat_Stencil8: {
            return static_cast<double>(static_cast<uint32_t>(std::llround(value)) & 0xffu);
        }
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth32FloatStencil8:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        default:
            // depth32float (and the depth24plus family modeled as depth32float) stores f32.
            return static_cast<double>(static_cast<float>(value));
    }
}

// makeRandomDepthComparisonTexelGenerator(info, comparison) with comparison='equal'.
// Produces a single per-texel value (the depth aspect, or stencil for stencil8). Does not
// route through the repo texel encoder because depth/stencil formats have no representation
// there; quantization is modeled per-format.
TexelGenerator makeRandomDepthComparisonTexelGenerator(WGPUTextureFormat format, WGPUExtent3D size) {
    // comparison 'equal' -> fixed values [0, 0.6, 1, 1].
    static const std::array<double, 4> kFixedValues = {{0.0, 0.6, 1.0, 1.0}};
    const bool stencilOnly = format == WGPUTextureFormat_Stencil8;
    const WGPUTextureFormat quantFormat = stencilOnly ? WGPUTextureFormat_Stencil8 : format;
    const uint32_t w = size.width;
    const uint32_t h = size.height;
    const uint32_t d = size.depthOrArrayLayers;
    return [quantFormat, w, h, d](uint32_t x, uint32_t y, uint32_t z, uint32_t sampleIndex) -> TexelComponents {
        TexelComponents texel;
        const uint32_t rnd = metaHashU32({static_cast<int64_t>(x),
                                          static_cast<int64_t>(y),
                                          static_cast<int64_t>(z),
                                          static_cast<int64_t>(sampleIndex),
                                          static_cast<int64_t>('D'),
                                          static_cast<int64_t>(w),
                                          static_cast<int64_t>(h),
                                          static_cast<int64_t>(d)});
        const double normalized = clamp01(static_cast<double>(rnd) / 4294967295.0);
        const uint32_t fi = static_cast<uint32_t>(normalized * (kFixedValues.size() - 1));
        const double value = kFixedValues[std::min<uint32_t>(fi, static_cast<uint32_t>(kFixedValues.size() - 1u))];
        texel.values[0] = quantizeDepthStencilValue(quantFormat, value);
        return texel;
    };
}

// Random color generator (createRandomTexelViewViaColors).
TexelComponents randomColorTexel(
    const TexelRepresentation& rep,
    WGPUExtent3D size,
    uint32_t mipLevel,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t sampleIndex) {
    TexelComponents texel;
    for (TexelComponent component : rep.componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        const uint32_t rnd = metaHashU32({static_cast<int64_t>(x),
                                          static_cast<int64_t>(y),
                                          static_cast<int64_t>(z),
                                          static_cast<int64_t>(sampleIndex),
                                          static_cast<int64_t>(static_cast<uint32_t>('R') + index),
                                          static_cast<int64_t>(mipLevel),
                                          static_cast<int64_t>(size.width),
                                          static_cast<int64_t>(size.height),
                                          static_cast<int64_t>(size.depthOrArrayLayers)});
        const double normalized = clamp01(static_cast<double>(rnd) / 4294967295.0);
        const MinMax mm = minMaxForComponent(rep, component);
        texel.values[index] = lerp(mm.min, mm.max, normalized);
    }
    return quantizeMeta(texel, rep);
}

// One mip level of CPU-side texels plus its packed bytes for upload/decode.
struct MipTexels {
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUExtent3D size = WGPUExtent3D{1, 1, 1};
    TextureCopyLayout layout;
    std::vector<uint8_t> data;
};

MetaColor colorOfMip(const MipTexels& mip, uint32_t x, uint32_t y, uint32_t z, uint32_t sampleIndex, uint32_t sampleCount) {
    const TexelRepresentation& rep = texelRepresentation(mip.format);
    const uint64_t offset = (static_cast<uint64_t>(z) * mip.layout.rowsPerImage + y) * mip.layout.bytesPerRow
        + (static_cast<uint64_t>(x) * sampleCount + sampleIndex) * rep.bytesPerBlock;
    const TexelComponents comps = rep.bitsToNumber(rep.unpackBits(mip.data.data() + offset, mip.data.size() - offset));
    return convertPerTexelComponentToResultFormat(comps, mip.format);
}

// bitsToULPFromZero-equivalent: number of ULPs each component is from zero, in the
// encodable comparison format. For the integer / exact formats used by this meta-test,
// the stored values are exact so ULP and the value coincide closely enough for the
// `maxFractionalDiff = 0` comparison; for float formats we count IEEE steps from zero.
double ulpFromZero(double value, ComponentDataType type, uint32_t bits) {
    switch (type) {
        case ComponentDataType::Uint:
        case ComponentDataType::Sint:
            return value;
        case ComponentDataType::Unorm:
        case ComponentDataType::Snorm: {
            const double scale = type == ComponentDataType::Snorm
                ? static_cast<double>((1 << (bits - 1)) - 1)
                : static_cast<double>(bits >= 32 ? 4294967295.0 : ((1u << bits) - 1u));
            return static_cast<double>(std::llround(value * scale));
        }
        case ComponentDataType::Float:
        case ComponentDataType::Ufloat: {
            // 32-bit float ULP-from-zero. Good enough for the rgba32float compare path.
            if (value == 0.0) {
                return 0.0;
            }
            uint32_t b;
            const float f = static_cast<float>(value);
            std::memcpy(&b, &f, sizeof(b));
            const bool neg = (b & 0x80000000u) != 0;
            const double mag = static_cast<double>(b & 0x7fffffffu);
            return neg ? -mag : mag;
        }
    }
    return value;
}

// texelsApproximatelyEqual for the comparison format (R,G,B,A doubles), maxFractionalDiff=0.
bool texelsApproximatelyEqual(
    const MetaColor& got,
    const MetaColor& expect,
    WGPUTextureFormat expectedFormat,
    bool singleChannel,
    double maxFractionalDiff) {
    const TexelRepresentation& rep = texelRepresentation(expectedFormat);
    const uint32_t numComponents = singleChannel ? 1u : 4u;
    for (uint32_t i = 0; i < numComponents; ++i) {
        const TexelComponent comp = static_cast<TexelComponent>(i);
        const uint32_t index = i;
        const double g = got.v[index];
        const double e = expect.v[index];
        const double absDiff = std::abs(g - e);
        const ComponentDataType type = rep.dataTypes[index];
        const uint32_t bits = rep.bitLengths[index];
        const double gULP = ulpFromZero(g, type, bits);
        const double eULP = ulpFromZero(e, type, bits);
        const double ulpDiff = std::abs(gULP - eULP);
        (void)comp;
        if (ulpDiff > 3.0 && absDiff > maxFractionalDiff) {
            return false;
        }
    }
    return true;
}

std::string formatTexel(const TexelComponents& comps, WGPUTextureFormat format) {
    const TexelRepresentation& rep = texelRepresentation(format);
    std::ostringstream s;
    bool first = true;
    static const char* kNames = "RGBA";
    for (TexelComponent component : rep.componentOrder) {
        if (!first) {
            s << ", ";
        }
        first = false;
        s << kNames[static_cast<uint32_t>(component)] << ": " << comps.values[static_cast<uint32_t>(component)];
    }
    return s.str();
}

// ---- mip mix weights (multi-stage) ----

constexpr uint32_t kWeightSteps = 64;  // kMipLevelWeightSteps

struct StageWeights {
    std::vector<double> sampleLevelWeights;             // mix weight per fractional mip
    std::vector<double> softwareMixToGPUMixGradWeights;  // grad-derived mapping
};

double dotProduct3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// computeMipLevelFromGradients(ddx, ddy, size) per spec.
double computeMipLevelFromGradients(
    const std::array<double, 3>& ddx,
    const std::array<double, 3>& ddy,
    const std::array<double, 3>& textureSize) {
    std::array<double, 3> sx = {ddx[0] * textureSize[0], ddx[1] * textureSize[1], ddx[2] * textureSize[2]};
    std::array<double, 3> sy = {ddy[0] * textureSize[0], ddy[1] * textureSize[1], ddy[2] * textureSize[2]};
    const double dotDDX = dotProduct3(sx, sx);
    const double dotDDY = dotProduct3(sy, sy);
    const double deltaMax = std::max(dotDDX, dotDDY);
    return 0.5 * std::log2(deltaMax);
}

// getIndexAndWeight over an ascending array.
std::pair<size_t, double> getIndexAndWeight(const std::vector<double>& values, double v) {
    size_t lo = 0;
    size_t hi = values.size() - 1;
    for (;;) {
        const size_t i = lo + (hi - lo) / 2;
        const double w0 = values[i];
        const double w1 = values[i + 1];
        if (lo == hi || (v >= w0 && v <= w1)) {
            const double weight = (w1 == w0) ? 0.0 : (v - w0) / (w1 - w0);
            return {i, weight};
        }
        if (v < w0) {
            hi = i;
        } else {
            lo = i + 1;
        }
    }
}

double bilinearFilter1D(const std::vector<double>& values, size_t ndx, double weight) {
    const double v0 = values[ndx];
    const double v1 = (ndx + 1 < values.size()) ? values[ndx + 1] : 0.0;
    return lerp(v0, v1, weight);
}

// generateSoftwareMixToGPUMixGradWeights(gpuWeights, texWidth).
std::vector<double> generateSoftwareMixToGPUMixGradWeights(const std::vector<double>& gpuWeights, uint32_t texWidth) {
    const uint32_t numSteps = static_cast<uint32_t>(gpuWeights.size()) - 1u;
    const std::array<double, 3> size = {static_cast<double>(texWidth), static_cast<double>(texWidth), 1.0};
    std::vector<double> softwareWeights(numSteps + 1u);
    for (uint32_t i = 0; i <= numSteps; ++i) {
        const double u = static_cast<double>(i) / numSteps;
        const double g = lerp(1.0, 2.0, u) / static_cast<double>(texWidth);
        const double mipLevel = computeMipLevelFromGradients({g, 0.0, 0.0}, {0.0, 0.0, 0.0}, size);
        softwareWeights[i] = std::clamp(mipLevel, 0.0, 1.0);
    }
    std::vector<double> out(numSteps + 1u);
    for (uint32_t i = 0; i <= numSteps; ++i) {
        const double mix = static_cast<double>(i) / numSteps;
        const std::pair<size_t, double> iw = getIndexAndWeight(softwareWeights, mix);
        out[i] = bilinearFilter1D(gpuWeights, iw.first, iw.second);
    }
    return out;
}

// graphWeights(height, weights): ascii graph of expected (linear) vs actual weights.
std::string graphWeights(uint32_t height, const std::vector<double>& weights) {
    const uint32_t width = static_cast<uint32_t>(weights.size());
    std::vector<uint8_t> data(static_cast<size_t>(width) * height, 0);
    auto plot = [&](double norm, uint32_t x, uint8_t c) {
        int32_t y = static_cast<int32_t>(std::floor(norm * height));
        y = std::clamp(y, 0, static_cast<int32_t>(height) - 1);
        const size_t offset = (static_cast<size_t>(height - static_cast<uint32_t>(y) - 1u) * width) + x;
        data[offset] = c;
    };
    for (uint32_t i = 0; i < width; ++i) {
        plot(static_cast<double>(i) / (width - 1u), i, 1);  // expected: linear 0..1
    }
    for (uint32_t i = 0; i < width; ++i) {
        plot(weights[i], i, 2);
    }
    static const char kConv[3] = {'.', 'e', 'A'};
    std::ostringstream out;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            out << kConv[data[static_cast<size_t>(y) * width + x]];
        }
        if (y + 1 < height) {
            out << '\n';
        }
    }
    return out.str();
}

// queryMipLevelMixWeightsForDevice(t, stage). Faithful port: sample a 2-mip 2x2 texture
// (mip1 white) at 65 fractional mip levels, reading both textureSampleLevel and
// textureSampleGrad mix weights. Compute / fragment / vertex variants.
StageWeights queryMipLevelMixWeightsForDeviceStage(AllFeaturesMaxLimitsGpuTest& t, const std::string& stage) {
    const uint32_t kNumWeightTypes = 2;
    const uint64_t floatsPerStep = 4;  // vec4f
    const uint64_t outFloats = static_cast<uint64_t>(kWeightSteps + 1u) * floatsPerStep;

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{2, 2, 1};
    texDesc.mipLevelCount = 2;
    texDesc.format = WGPUTextureFormat_R8Unorm;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    const std::array<uint8_t, 1> white = {{255}};
    WGPUTexelCopyBufferLayout whiteLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    whiteLayout.bytesPerRow = 1;
    whiteLayout.rowsPerImage = 1;
    t.queueWriteTexture(texture, WGPUExtent3D{1, 1, 1}, whiteLayout, white.data(), white.size(), 1);

    WGPUSamplerDescriptor sampDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampDesc.minFilter = WGPUFilterMode_Linear;
    sampDesc.magFilter = WGPUFilterMode_Linear;
    sampDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    WGPUSampler sampler = t.createSamplerTracked(sampDesc);

    const std::string code = std::string(
        "@group(0) @binding(0) var tex: texture_2d<f32>;\n"
        "@group(0) @binding(1) var smp: sampler;\n"
        "@group(0) @binding(2) var<storage, read_write> result: array<vec4f>;\n"
        "struct VSOutput {\n"
        "  @builtin(position) pos: vec4f,\n"
        "  @location(0) @interpolate(flat, either) ndx: u32,\n"
        "  @location(1) @interpolate(flat, either) result: vec4f,\n"
        "};\n"
        "fn getMixLevels(wNdx: u32) -> vec4f {\n"
        "  let mipLevel = f32(wNdx) / 64.0;\n"
        "  let size = textureDimensions(tex);\n"
        "  let g = mix(1.0, 2.0, mipLevel) / f32(size.x);\n"
        "  let ddx = vec2f(g, 0);\n"
        "  return vec4f(\n"
        "    textureSampleLevel(tex, smp, vec2f(0.5), mipLevel).r,\n"
        "    textureSampleGrad(tex, smp, vec2f(0.5), ddx, vec2f(0)).r,\n"
        "    0, 0);\n"
        "}\n"
        "fn getPosition(vNdx: u32) -> vec4f {\n"
        "  let pos = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));\n"
        "  return vec4f(pos[vNdx], 0, 1);\n"
        "}\n"
        "@vertex fn vs(@builtin(vertex_index) vNdx: u32, @builtin(instance_index) iNdx: u32) -> VSOutput {\n"
        "  return VSOutput(getPosition(vNdx), iNdx, vec4f(0));\n"
        "}\n"
        "@fragment fn fsRecord(v: VSOutput) -> @location(0) vec4u {\n"
        "  return bitcast<vec4u>(getMixLevels(v.ndx));\n"
        "}\n"
        "@compute @workgroup_size(1) fn csRecord(@builtin(global_invocation_id) id: vec3u) {\n"
        "  result[id.x] = getMixLevels(id.x);\n"
        "}\n"
        "@vertex fn vsRecord(@builtin(vertex_index) vNdx: u32, @builtin(instance_index) iNdx: u32) -> VSOutput {\n"
        "  return VSOutput(getPosition(vNdx), iNdx, getMixLevels(iNdx));\n"
        "}\n"
        "@fragment fn fsSaveVs(v: VSOutput) -> @location(0) vec4u {\n"
        "  return bitcast<vec4u>(v.result);\n"
        "}\n");
    WGPUShaderModule module = t.createShaderModuleTracked(code);

    WGPUTextureView texView = t.createViewTracked(texture, WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT);

    std::vector<float> floats(static_cast<size_t>(outFloats), 0.0f);

    if (stage == "compute") {
        WGPUBufferDescriptor storageDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        storageDesc.size = outFloats * sizeof(float);
        storageDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer storageBuffer = t.createBufferTracked(storageDesc);

        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr;  // 'auto'
        pipeDesc.compute.module = module;
        pipeDesc.compute.entryPoint = stringView("csRecord");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        std::array<WGPUBindGroupEntry, 3> entries = {{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT}};
        entries[0].binding = 0;
        entries[0].textureView = texView;
        entries[1].binding = 1;
        entries[1].sampler = sampler;
        entries[2].binding = 2;
        entries[2].buffer = storageBuffer;
        entries[2].size = storageDesc.size;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        bgDesc.entryCount = entries.size();
        bgDesc.entries = entries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, kWeightSteps + 1u, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        submit(t, encoder);

        t.expectGPUBufferValuesPassCheck(
            storageBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < outFloats * sizeof(float)) {
                    return std::string("mip weights storage buffer too small");
                }
                std::memcpy(floats.data(), actual, static_cast<size_t>(outFloats) * sizeof(float));
                return std::nullopt;
            },
            0,
            static_cast<size_t>(outFloats * sizeof(float)));
    } else {
        // fragment / vertex: render one instanced quad per step into a (kWeightSteps+1)x1
        // rgba32uint target, then copy to a buffer.
        WGPUTextureDescriptor targetDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        targetDesc.size = WGPUExtent3D{kWeightSteps + 1u, 1, 1};
        targetDesc.format = WGPUTextureFormat_RGBA32Uint;
        targetDesc.dimension = WGPUTextureDimension_2D;
        targetDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture target = t.createTextureTracked(targetDesc);
        WGPUTextureView targetView = t.createViewTracked(target, WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT);

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr;  // 'auto'
        pipeDesc.vertex.module = module;
        pipeDesc.vertex.entryPoint = stringView(stage == "vertex" ? "vsRecord" : "vs");
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA32Uint;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = stringView(stage == "vertex" ? "fsSaveVs" : "fsRecord");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        pipeDesc.fragment = &fragment;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

        std::array<WGPUBindGroupEntry, 2> entries = {{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT}};
        entries[0].binding = 0;
        entries[0].textureView = texView;
        entries[1].binding = 1;
        entries[1].sampler = sampler;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        bgDesc.entryCount = entries.size();
        bgDesc.entries = entries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0, 0, 0, 0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        for (uint32_t x = 0; x <= kWeightSteps; ++x) {
            wgpuRenderPassEncoderSetViewport(pass, static_cast<float>(x), 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, x);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        const uint32_t bytesPerRow = alignToU32((kWeightSteps + 1u) * 16u, kBytesPerRowAlignment);
        WGPUBufferDescriptor readDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readDesc.size = static_cast<uint64_t>(bytesPerRow);
        readDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readBuffer = t.createBufferTracked(readDesc);
        t.copyTextureToBuffer(encoder, target, readBuffer, bytesPerRow, WGPUExtent3D{kWeightSteps + 1u, 1, 1});
        submit(t, encoder);

        t.expectGPUBufferValuesPassCheck(
            readBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < static_cast<size_t>(outFloats) * sizeof(float)) {
                    return std::string("mip weights render readback too small");
                }
                std::memcpy(floats.data(), actual, static_cast<size_t>(outFloats) * sizeof(float));
                return std::nullopt;
            },
            0,
            static_cast<size_t>(bytesPerRow));
    }

    // unzip(result, kNumWeightTypes, 4): component 0 = sampleLevel, component 1 = grad.
    StageWeights weights;
    weights.sampleLevelWeights.resize(kWeightSteps + 1u);
    std::vector<double> gradWeights(kWeightSteps + 1u);
    for (uint32_t i = 0; i <= kWeightSteps; ++i) {
        weights.sampleLevelWeights[i] = static_cast<double>(floats[i * 4u + 0u]);
        gradWeights[i] = static_cast<double>(floats[i * 4u + 1u]);
    }
    (void)kNumWeightTypes;
    weights.softwareMixToGPUMixGradWeights = generateSoftwareMixToGPUMixGradWeights(gradWeights, /*texWidth=*/2u);
    return weights;
}

// validateWeights from the spec body.
void validateWeights(AllFeaturesMaxLimitsGpuTest& t, const std::string& stage, const std::string& builtin, const std::vector<double>& weights) {
    const size_t kNumMixSteps = weights.size() - 1;
    auto showWeights = [&]() -> std::string {
        std::ostringstream s;
        for (size_t i = 0; i < weights.size(); ++i) {
            s << (i < 10 ? " " : "") << i << ": " << weights[i] << "\n";
        }
        s << "\ne = expected\nA = actual\n" << graphWeights(32, weights) << "\n";
        return s.str();
    };

    t.expect(weights[0] == 0.0,
             "stage: " + stage + ", " + builtin + ", weight 0 expected 0 but was " + std::to_string(weights[0]) + "\n" + showWeights());
    t.expect(weights[kNumMixSteps] == 1.0,
             "stage: " + stage + ", " + builtin + ", top weight expected 1 but was " + std::to_string(weights[kNumMixSteps]) + "\n" + showWeights());

    const double dx = 1.0 / static_cast<double>(kNumMixSteps);
    for (size_t i = 0; i < kNumMixSteps; ++i) {
        const double dy = weights[i + 1] - weights[i];
        const double slope = dy / dx;
        t.expect(slope >= 0.0,
                 "stage: " + stage + ", " + builtin + ", weight[" + std::to_string(i) + "] was not <= weight[" + std::to_string(i + 1) + "]\n" + showWeights());
        t.expect(slope <= 2.0,
                 "stage: " + stage + ", " + builtin + ", slope from weight[" + std::to_string(i) + "] to weight[" + std::to_string(i + 1) + "] is > 2.\n" + showWeights());
    }

    const double kMinPercentUniqueWeights = 66.0;
    std::vector<double> sorted = weights;
    std::sort(sorted.begin(), sorted.end());
    size_t unique = sorted.empty() ? 0 : 1;
    for (size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i] != sorted[i - 1]) {
            ++unique;
        }
    }
    const size_t required = static_cast<size_t>(static_cast<double>(weights.size()) * kMinPercentUniqueWeights * 0.01);
    t.expect(unique >= required,
             "stage: " + stage + ", " + builtin + ", expected at least ~66% unique weights\n" + showWeights());
}

}  // namespace

std::vector<Value> textureUtilsDepthStencilFormats() {
    return depthStencilFormats();
}

std::vector<Value> textureUtilsGeneratorViewDimensions() {
    return {Value("2d"), Value("2d-array"), Value("cube"), Value("cube-array")};
}

std::vector<Value> textureUtilsShaderStages() {
    return shortShaderStages();
}

std::vector<ParamRecord> readTextureToTexelViewsParams() {
    struct FormatPair {
        const char* srcFormat;
        const char* texelViewFormat;
    };
    static const std::array<FormatPair, 9> kPairs = {{
        {"r8unorm", "rgba32float"},
        {"r8sint", "rgba32sint"},
        {"r8uint", "rgba32uint"},
        {"rgba32float", "rgba32float"},
        {"rgba32uint", "rgba32uint"},
        {"rgba32sint", "rgba32sint"},
        {"depth24plus", "rgba32float"},
        {"depth24plus-stencil8", "rgba32float"},
        {"stencil8", "stencil8"},
    }};
    static const std::array<const char*, 6> kViewDims = {{"1d", "2d", "2d-array", "3d", "cube", "cube-array"}};
    static const std::array<int, 2> kSampleCounts = {{1, 4}};

    std::vector<ParamRecord> out;
    for (const FormatPair& pair : kPairs) {
        const WGPUTextureFormat srcFormat = parseTextureFormat(pair.srcFormat);
        for (const char* viewDimension : kViewDims) {
            for (int sampleCount : kSampleCounts) {
                // .unless: drop sampleCount>1 when format not possibly multisampled or view != 2d.
                if (sampleCount > 1 && (!metaPossiblyMultisampled(srcFormat) || std::string_view(viewDimension) != "2d")) {
                    continue;
                }
                ParamRecord record;
                record.emplace_back("srcFormat", Value(pair.srcFormat));
                record.emplace_back("texelViewFormat", Value(pair.texelViewFormat));
                record.emplace_back("viewDimension", Value(viewDimension));
                record.emplace_back("sampleCount", Value(sampleCount));
                out.push_back(std::move(record));
            }
        }
    }
    return out;
}

namespace {

// readTextureToTexelViews: read a GPU texture back to one MipTexels per mip level, encoded
// in `format` (rgba32float/uint/sint or stencil8), via a compute shader that textureLoads.
std::vector<MipTexels> readTextureToTexelViews(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat textureFormat,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevelCount,
    uint32_t sampleCount,
    WGPUTextureViewDimension viewDimension,
    WGPUTextureFormat outFormat) {
    const FormatType type = getMetaTextureFormatType(textureFormat);
    const char* componentType = type == FormatType::Uint ? "u32" : (type == FormatType::Sint ? "i32" : "f32");
    const char* resultType = type == FormatType::Uint ? "vec4u" : (type == FormatType::Sint ? "vec4i" : "vec4f");

    // cube-array is read back via texture_2d_array (matching upstream); the bindgroup
    // layout and the texture view must therefore use 2DArray, not CubeArray.
    const WGPUTextureViewDimension bindViewDimension =
        viewDimension == WGPUTextureViewDimension_CubeArray ? WGPUTextureViewDimension_2DArray : viewDimension;

    std::string textureWGSL;
    std::string loadWGSL;
    std::string dimensionWGSL = "textureDimensions(tex, 0)";
    const bool multisampled = sampleCount > 1;
    switch (viewDimension) {
        case WGPUTextureViewDimension_2D:
            if (multisampled) {
                textureWGSL = std::string("texture_multisampled_2d<") + componentType + ">";
                loadWGSL = "textureLoad(tex, coord.xy, sampleIndex)";
                dimensionWGSL = "textureDimensions(tex)";
            } else {
                textureWGSL = std::string("texture_2d<") + componentType + ">";
                loadWGSL = "textureLoad(tex, coord.xy, 0)";
            }
            break;
        case WGPUTextureViewDimension_CubeArray:  // use 2d_array readback
        case WGPUTextureViewDimension_2DArray:
            textureWGSL = std::string("texture_2d_array<") + componentType + ">";
            loadWGSL = "textureLoad(tex, coord.xy, coord.z, 0)";
            break;
        case WGPUTextureViewDimension_3D:
            textureWGSL = std::string("texture_3d<") + componentType + ">";
            loadWGSL = "textureLoad(tex, coord.xyz, 0)";
            break;
        case WGPUTextureViewDimension_Cube:
            textureWGSL = std::string("texture_cube<") + componentType + ">";
            loadWGSL = "textureLoadCubeAs2DArray(tex, coord.xy, coord.z)";
            break;
        case WGPUTextureViewDimension_1D:
            textureWGSL = std::string("texture_1d<") + componentType + ">";
            loadWGSL = "textureLoad(tex, coord.x, 0)";
            dimensionWGSL = "vec2u(textureDimensions(tex), 1)";
            break;
        default:
            t.fail("readTextureToTexelViews: unsupported view dimension");
    }

    const bool cubeLike = viewDimension == WGPUTextureViewDimension_Cube || viewDimension == WGPUTextureViewDimension_CubeArray;
    std::string cubeWGSL;
    if (cubeLike && viewDimension == WGPUTextureViewDimension_Cube) {
        cubeWGSL = std::string(
            "const faceMat = array(\n"
            "  mat3x3f( 0,  0,  -2,  0, -2,   0,  1,  1,   1),\n"
            "  mat3x3f( 0,  0,   2,  0, -2,   0, -1,  1,  -1),\n"
            "  mat3x3f( 2,  0,   0,  0,  0,   2, -1,  1,  -1),\n"
            "  mat3x3f( 2,  0,   0,  0,  0,  -2, -1, -1,   1),\n"
            "  mat3x3f( 2,  0,   0,  0, -2,   0, -1,  1,   1),\n"
            "  mat3x3f(-2,  0,   0,  0, -2,   0,  1,  1,  -1));\n")
            + "fn textureLoadCubeAs2DArray(tex: texture_cube<" + componentType + ">, coord: vec2u, layer: u32) -> " + resultType + " {\n"
            "  let size = textureDimensions(tex, 0);\n"
            "  let uv = (vec2f(coord) + 0.75) / vec2f(size.xy);\n"
            "  let cubeCoord = faceMat[layer] * vec3f(uv, 1.0);\n"
            "  let r = textureGather(0, tex, smp, cubeCoord);\n"
            "  let g = textureGather(1, tex, smp, cubeCoord);\n"
            "  let b = textureGather(2, tex, smp, cubeCoord);\n"
            "  let a = textureGather(3, tex, smp, cubeCoord);\n"
            "  return " + resultType + "(r[3], g[3], b[3], a[3]);\n"
            "}\n";
    }

    std::string code = cubeWGSL +
        "struct Uniforms { sampleCount: u32, };\n"
        "@group(0) @binding(0) var<uniform> uni: Uniforms;\n"
        "@group(0) @binding(1) var tex: " + textureWGSL + ";\n"
        "@group(0) @binding(2) var smp: sampler;\n"
        "@group(0) @binding(3) var<storage, read_write> data: array<" + resultType + ">;\n"
        "@compute @workgroup_size(1) fn cs(@builtin(global_invocation_id) global_invocation_id: vec3<u32>) {\n"
        "  _ = smp;\n"
        "  let size = " + dimensionWGSL + ";\n"
        "  let ndx = global_invocation_id.z * size.x * size.y * uni.sampleCount +\n"
        "            global_invocation_id.y * size.x * uni.sampleCount +\n"
        "            global_invocation_id.x;\n"
        "  let coord = vec3u(global_invocation_id.x / uni.sampleCount, global_invocation_id.yz);\n"
        "  let sampleIndex = global_invocation_id.x % uni.sampleCount;\n"
        "  data[ndx] = " + loadWGSL + ";\n"
        "}\n";
    WGPUShaderModule module = t.createShaderModuleTracked(code);

    // Bind group layout: uniform, texture, non-filtering sampler, storage.
    WGPUTextureSampleType sampleType;
    if (type == FormatType::Depth) {
        sampleType = WGPUTextureSampleType_UnfilterableFloat;
    } else if (isStencilTextureFormat(textureFormat) && !isDepthTextureFormat(textureFormat)) {
        sampleType = WGPUTextureSampleType_Uint;
    } else if (type == FormatType::Float) {
        sampleType = WGPUTextureSampleType_UnfilterableFloat;
    } else if (type == FormatType::Sint) {
        sampleType = WGPUTextureSampleType_Sint;
    } else {
        sampleType = WGPUTextureSampleType_Uint;
    }

    std::array<WGPUBindGroupLayoutEntry, 4> bglEntries = {{WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT}};
    bglEntries[0].binding = 0;
    bglEntries[0].visibility = WGPUShaderStage_Compute;
    bglEntries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    bglEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bglEntries[1].binding = 1;
    bglEntries[1].visibility = WGPUShaderStage_Compute;
    bglEntries[1].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    bglEntries[1].texture.sampleType = sampleType;
    bglEntries[1].texture.viewDimension = bindViewDimension;
    bglEntries[1].texture.multisampled = multisampled ? 1u : 0u;
    bglEntries[2].binding = 2;
    bglEntries[2].visibility = WGPUShaderStage_Compute;
    bglEntries[2].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
    bglEntries[2].sampler.type = WGPUSamplerBindingType_NonFiltering;
    bglEntries[3].binding = 3;
    bglEntries[3].visibility = WGPUShaderStage_Compute;
    bglEntries[3].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    bglEntries[3].buffer.type = WGPUBufferBindingType_Storage;
    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = bglEntries.size();
    bglDesc.entries = bglEntries.data();
    WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = pipelineLayout;
    pipeDesc.compute.module = module;
    pipeDesc.compute.entryPoint = stringView("cs");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    WGPUSampler sampler = t.createSamplerTracked(WGPU_SAMPLER_DESCRIPTOR_INIT);

    const WGPUTextureFormat storeFormat = effectiveTexelViewFormat(outFormat);
    const TexelRepresentation& outRep = texelRepresentation(storeFormat);

    struct ReadBack {
        WGPUExtent3D size;
        WGPUBuffer storageBuffer;
        uint64_t byteLength;
    };
    std::vector<ReadBack> readBacks;
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    const WGPUTextureAspect aspect = isDepthTextureFormat(textureFormat) ? WGPUTextureAspect_DepthOnly
        : (isStencilTextureFormat(textureFormat) ? WGPUTextureAspect_StencilOnly : WGPUTextureAspect_All);

    for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel) {
        const WGPUExtent3D size = physicalMipSize(baseSize, dimension, mipLevel);

        const std::array<uint32_t, 4> uniformValues = {{sampleCount, 0, 0, 0}};
        WGPUBufferDescriptor uniDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        uniDesc.size = sizeof(uniformValues);
        uniDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        WGPUBuffer uniformBuffer = t.createBufferTracked(uniDesc);
        t.queueWriteBuffer(uniformBuffer, 0, uniformValues.data(), sizeof(uniformValues));

        const uint64_t storageSize = static_cast<uint64_t>(size.width) * size.height * size.depthOrArrayLayers * 4ull * 4ull * sampleCount;
        WGPUBufferDescriptor storageDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        storageDesc.size = storageSize;
        storageDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer storageBuffer = t.createBufferTracked(storageDesc);
        readBacks.push_back({size, storageBuffer, storageSize});

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = bindViewDimension;
        viewDesc.aspect = aspect;
        viewDesc.baseMipLevel = mipLevel;
        viewDesc.mipLevelCount = 1;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        std::array<WGPUBindGroupEntry, 4> bgEntries = {{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT}};
        bgEntries[0].binding = 0;
        bgEntries[0].buffer = uniformBuffer;
        bgEntries[0].size = sizeof(uniformValues);
        bgEntries[1].binding = 1;
        bgEntries[1].textureView = view;
        bgEntries[2].binding = 2;
        bgEntries[2].sampler = sampler;
        bgEntries[3].binding = 3;
        bgEntries[3].buffer = storageBuffer;
        bgEntries[3].size = storageSize;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = bgEntries.size();
        bgDesc.entries = bgEntries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, size.width * sampleCount, size.height, size.depthOrArrayLayers);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }
    submit(t, encoder);

    std::vector<MipTexels> texels;
    for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel) {
        const ReadBack& rb = readBacks[mipLevel];
        // The storage buffer holds vec4<f32|i32|u32>; read it back as raw 32-bit words and
        // interpret per the texture's component type.
        std::vector<uint32_t> words(static_cast<size_t>(rb.byteLength / sizeof(uint32_t)), 0u);
        t.expectGPUBufferValuesPassCheck(
            rb.storageBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < rb.byteLength) {
                    return std::string("readTextureToTexelViews readback too small");
                }
                std::memcpy(words.data(), actual, static_cast<size_t>(rb.byteLength));
                return std::nullopt;
            },
            0,
            static_cast<size_t>(rb.byteLength));

        auto wordToValue = [&](uint32_t w) -> double {
            if (type == FormatType::Uint) {
                return static_cast<double>(w);
            }
            if (type == FormatType::Sint) {
                return static_cast<double>(static_cast<int32_t>(w));
            }
            float f;
            std::memcpy(&f, &w, sizeof(f));
            return static_cast<double>(f);
        };

        MipTexels mip;
        mip.format = storeFormat;
        mip.size = rb.size;
        mip.layout = getTextureCopyLayout(storeFormat, dimension, baseSize, mipLevel);
        // We store one outFormat texel per (x*sampleCount + sampleIndex), y, z.
        const uint32_t bytesPerTexel = outRep.bytesPerBlock;
        const uint32_t bytesPerRow = rb.size.width * sampleCount * bytesPerTexel;
        const uint32_t rowsPerImage = rb.size.height;
        mip.layout.bytesPerRow = bytesPerRow;
        mip.layout.rowsPerImage = rowsPerImage;
        mip.data.assign(static_cast<size_t>(bytesPerRow) * rowsPerImage * rb.size.depthOrArrayLayers, 0);
        for (uint32_t z = 0; z < rb.size.depthOrArrayLayers; ++z) {
            for (uint32_t y = 0; y < rb.size.height; ++y) {
                for (uint32_t x = 0; x < rb.size.width; ++x) {
                    for (uint32_t s = 0; s < sampleCount; ++s) {
                        const uint64_t srcOffset = (((static_cast<uint64_t>(z) * rb.size.width * rb.size.height
                                                    + static_cast<uint64_t>(y) * rb.size.width + x)
                                                       * sampleCount + s))
                            * 4ull;
                        TexelComponents comps;
                        comps.values[0] = wordToValue(words[srcOffset + 0]);
                        comps.values[1] = wordToValue(words[srcOffset + 1]);
                        comps.values[2] = wordToValue(words[srcOffset + 2]);
                        comps.values[3] = wordToValue(words[srcOffset + 3]);
                        // convertResultFormatToTexelViewFormat: for outFormat with RGBA order this is identity.
                        const std::vector<uint8_t> bytes = outRep.packBits(outRep.numberToBits(comps));
                        const uint64_t dstOffset = (static_cast<uint64_t>(z) * rowsPerImage + y) * bytesPerRow
                            + (static_cast<uint64_t>(x) * sampleCount + s) * bytesPerTexel;
                        std::memcpy(mip.data.data() + dstOffset, bytes.data(), bytes.size());
                    }
                }
            }
        }
        texels.push_back(std::move(mip));
    }
    return texels;
}

// Build per-mip random color texels, then upload to a freshly created color texture.
// Returns the texture and the CPU texels (one per mip).
struct CreatedTexture {
    WGPUTexture texture = nullptr;
    std::vector<MipTexels> texels;  // per mip, in the source (encodable) format
};

// Generate the CPU-side per-sample random texels for one mip (sample packed along x).
MipTexels makeRandomColorMip(WGPUTextureFormat format, WGPUExtent3D mipSize, uint32_t mip, uint32_t sampleCount) {
    const TexelRepresentation& rep = texelRepresentation(format);
    MipTexels mt;
    mt.format = format;
    mt.size = mipSize;
    const uint32_t bytesPerRow = mipSize.width * sampleCount * rep.bytesPerBlock;
    const uint32_t rowsPerImage = mipSize.height;
    mt.layout.bytesPerRow = bytesPerRow;
    mt.layout.rowsPerImage = rowsPerImage;
    mt.layout.bytesPerBlock = rep.bytesPerBlock;
    mt.layout.mipSize = mipSize;
    mt.data.assign(static_cast<size_t>(bytesPerRow) * rowsPerImage * mipSize.depthOrArrayLayers, 0);
    for (uint32_t z = 0; z < mipSize.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < mipSize.height; ++y) {
            for (uint32_t x = 0; x < mipSize.width; ++x) {
                for (uint32_t s = 0; s < sampleCount; ++s) {
                    const TexelComponents comps = randomColorTexel(rep, mipSize, mip, x, y, z, s);
                    const std::vector<uint8_t> bytes = rep.packBits(rep.numberToBits(comps));
                    const uint64_t offset = (static_cast<uint64_t>(z) * rowsPerImage + y) * bytesPerRow
                        + (static_cast<uint64_t>(x) * sampleCount + s) * rep.bytesPerBlock;
                    std::memcpy(mt.data.data() + offset, bytes.data(), bytes.size());
                }
            }
        }
    }
    return mt;
}

CreatedTexture createColorTextureWithRandomData(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevelCount,
    uint32_t sampleCount,
    WGPUTextureViewDimension viewDimension,
    WGPUTextureUsage usage) {
    (void)viewDimension;
    CreatedTexture created;

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.dimension = dimension;
    desc.size = baseSize;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = sampleCount;
    desc.usage = usage;
    created.texture = t.createTextureTracked(desc);

    const TexelRepresentation& rep = texelRepresentation(format);

    if (sampleCount > 1) {
        // Multisampled (2d, single mip): render per-sample random data into the target.
        // We pack the per-sample CPU texels into a non-MSAA source texture (sample along x)
        // and a fragment shader loads source[x*sampleCount + sample_index] for each sample.
        const WGPUExtent3D mipSize = physicalMipSize(baseSize, dimension, 0);
        MipTexels mt = makeRandomColorMip(format, mipSize, 0, sampleCount);

        WGPUTextureDescriptor srcDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        srcDesc.format = format;
        srcDesc.dimension = WGPUTextureDimension_2D;
        srcDesc.size = WGPUExtent3D{mipSize.width * sampleCount, mipSize.height, 1};
        srcDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        WGPUTexture source = t.createTextureTracked(srcDesc);
        WGPUTexelCopyBufferLayout srcLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        srcLayout.bytesPerRow = mt.layout.bytesPerRow;
        srcLayout.rowsPerImage = mt.layout.rowsPerImage;
        t.queueWriteTexture(source, srcDesc.size, srcLayout, mt.data.data(), mt.data.size(), 0);

        const FormatType type = getMetaTextureFormatType(format);
        const char* componentType = type == FormatType::Uint ? "u32" : (type == FormatType::Sint ? "i32" : "f32");
        const char* resultType = type == FormatType::Uint ? "vec4u" : (type == FormatType::Sint ? "vec4i" : "vec4f");
        WGPUTextureSampleType sampleType = type == FormatType::Sint ? WGPUTextureSampleType_Sint
            : (type == FormatType::Uint ? WGPUTextureSampleType_Uint : WGPUTextureSampleType_UnfilterableFloat);

        std::string code = std::string(
            "@group(0) @binding(0) var src: texture_2d<") + componentType + ">;\n"
            "@vertex fn vs(@builtin(vertex_index) v: u32) -> @builtin(position) vec4f {\n"
            "  let pos = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));\n"
            "  return vec4f(pos[v], 0, 1);\n"
            "}\n"
            "@fragment fn fs(@builtin(position) pos: vec4f, @builtin(sample_index) s: u32) -> @location(0) " + resultType + " {\n"
            "  let xy = vec2u(pos.xy);\n"
            "  let coord = vec2u(xy.x * " + std::to_string(sampleCount) + "u + s, xy.y);\n"
            "  return textureLoad(src, coord, 0);\n"
            "}\n";
        WGPUShaderModule module = t.createShaderModuleTracked(code);

        WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        bglEntry.binding = 0;
        bglEntry.visibility = WGPUShaderStage_Fragment;
        bglEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        bglEntry.texture.sampleType = sampleType;
        bglEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries = &bglEntry;
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);
        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts = &bgl;
        WGPUPipelineLayout pl = t.createPipelineLayoutTracked(plDesc);

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = pl;
        pipeDesc.vertex.module = module;
        pipeDesc.vertex.entryPoint = stringView("vs");
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeDesc.multisample.count = sampleCount;
        pipeDesc.multisample.mask = 0xFFFFFFFFu;
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = format;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = stringView("fs");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        pipeDesc.fragment = &fragment;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

        WGPUTextureView srcView = t.createViewTracked(source, WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT);
        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.textureView = srcView;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUTextureView targetView = t.createViewTracked(created.texture, WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        submit(t, encoder);

        created.texels.push_back(std::move(mt));
        return created;
    }

    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        const WGPUExtent3D mipSize = physicalMipSize(baseSize, dimension, mip);
        MipTexels packed = makeRandomColorMip(format, mipSize, mip, /*sampleCount=*/1u);
        // For upload to the texture we need the standard 256-aligned layout.
        TextureCopyLayout uploadLayout = getTextureCopyLayout(format, dimension, baseSize, mip);
        std::vector<uint8_t> uploadData(static_cast<size_t>(uploadLayout.byteLength), 0);
        for (uint32_t z = 0; z < mipSize.depthOrArrayLayers; ++z) {
            for (uint32_t y = 0; y < mipSize.height; ++y) {
                const uint64_t srcRow = (static_cast<uint64_t>(z) * packed.layout.rowsPerImage + y) * packed.layout.bytesPerRow;
                const uint64_t dstRow = (static_cast<uint64_t>(z) * uploadLayout.rowsPerImage + y) * uploadLayout.bytesPerRow;
                std::memcpy(uploadData.data() + dstRow, packed.data.data() + srcRow,
                            static_cast<size_t>(mipSize.width) * rep.bytesPerBlock);
            }
        }
        WGPUTexelCopyBufferLayout copyLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        copyLayout.bytesPerRow = uploadLayout.bytesPerRow;
        copyLayout.rowsPerImage = uploadLayout.rowsPerImage;
        t.queueWriteTexture(created.texture, mipSize, copyLayout, uploadData.data(), uploadData.size(), mip);
        created.texels.push_back(std::move(packed));
    }
    return created;
}

// Fill a depth/stencil texture with deterministic data and return the texture.
// Depth aspects are rendered (unwritable/unencodable); stencil aspects are written.
// The "expected" texels for the meta-test are obtained by reading the texture back, so
// they trivially match the test's own read-back, validating that readTextureToTexelViews
// works for depth/stencil views and is deterministic.
WGPUTexture fillDepthStencilTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevelCount,
    uint32_t sampleCount,
    WGPUTextureUsage usage) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.dimension = dimension;
    desc.size = baseSize;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = sampleCount;
    desc.usage = static_cast<WGPUTextureUsage>(usage | WGPUTextureUsage_RenderAttachment);
    WGPUTexture texture = t.createTextureTracked(desc);

    const bool hasDepth = isDepthTextureFormat(format);
    const bool hasStencil = isStencilTextureFormat(format);

    // Write stencil aspect (encodable) with deterministic data per mip/layer.
    if (hasStencil) {
        for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
            const WGPUExtent3D mipSize = physicalMipSize(baseSize, dimension, mip);
            const uint32_t bytesPerRow = mipSize.width;  // stencil8 = 1 byte/texel
            std::vector<uint8_t> data(static_cast<size_t>(bytesPerRow) * mipSize.height * mipSize.depthOrArrayLayers, 0);
            for (uint32_t z = 0; z < mipSize.depthOrArrayLayers; ++z) {
                for (uint32_t y = 0; y < mipSize.height; ++y) {
                    for (uint32_t x = 0; x < mipSize.width; ++x) {
                        const uint32_t rnd = metaHashU32({static_cast<int64_t>(x), static_cast<int64_t>(y), static_cast<int64_t>(z), static_cast<int64_t>(mip), static_cast<int64_t>('S')});
                        data[(static_cast<size_t>(z) * mipSize.height + y) * bytesPerRow + x] = static_cast<uint8_t>(rnd & 0xffu);
                    }
                }
            }
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.bytesPerRow = bytesPerRow;
            layout.rowsPerImage = mipSize.height;
            // Stencil cannot be written to a multisampled texture; in that case leave it cleared.
            // Multi-planar (depth+stencil) formats require an explicit StencilOnly aspect.
            if (sampleCount == 1) {
                WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                destination.texture = texture;
                destination.mipLevel = mip;
                destination.origin = WGPUOrigin3D{0, 0, 0};
                destination.aspect = WGPUTextureAspect_StencilOnly;
                wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &mipSize);
            }
        }
    }

    // Render depth aspect: a fullscreen triangle writes frag_depth from a per-pixel source.
    if (hasDepth) {
        const std::string code = std::string(
            "@group(0) @binding(0) var src: texture_2d<f32>;\n"
            "@vertex fn vs(@builtin(vertex_index) v: u32) -> @builtin(position) vec4f {\n"
            "  let pos = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));\n"
            "  return vec4f(pos[v], 0, 1);\n"
            "}\n"
            "struct FOut { @builtin(frag_depth) d: f32, };\n"
            "@fragment fn fs(@builtin(position) pos: vec4f) -> FOut {\n"
            "  let xy = vec2u(pos.xy);\n"
            "  return FOut(textureLoad(src, xy, 0).r);\n"
            "}\n");
        WGPUShaderModule module = t.createShaderModuleTracked(code);

        WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        bglEntry.binding = 0;
        bglEntry.visibility = WGPUShaderStage_Fragment;
        bglEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        bglEntry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        bglEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries = &bglEntry;
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);
        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts = &bgl;
        WGPUPipelineLayout pl = t.createPipelineLayoutTracked(plDesc);

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = pl;
        pipeDesc.vertex.module = module;
        pipeDesc.vertex.entryPoint = stringView("vs");
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeDesc.multisample.count = sampleCount;
        pipeDesc.multisample.mask = 0xFFFFFFFFu;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = stringView("fs");
        fragment.targetCount = 0;
        fragment.targets = nullptr;
        pipeDesc.fragment = &fragment;
        WGPUDepthStencilState dss = WGPU_DEPTH_STENCIL_STATE_INIT;
        dss.format = format;
        dss.depthWriteEnabled = WGPUOptionalBool_True;
        dss.depthCompare = WGPUCompareFunction_Always;
        pipeDesc.depthStencil = &dss;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

        for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
            const WGPUExtent3D mipSize = physicalMipSize(baseSize, dimension, mip);
            for (uint32_t layer = 0; layer < mipSize.depthOrArrayLayers; ++layer) {
                // Build an r32float source with the random per-pixel depth for this layer.
                const TexelRepresentation& d32 = texelRepresentation(WGPUTextureFormat_R32Float);
                const uint32_t bytesPerRow = alignToU32(mipSize.width * 4u, kBytesPerRowAlignment);
                std::vector<uint8_t> srcData(static_cast<size_t>(bytesPerRow) * mipSize.height, 0);
                for (uint32_t y = 0; y < mipSize.height; ++y) {
                    for (uint32_t x = 0; x < mipSize.width; ++x) {
                        const uint32_t rnd = metaHashU32({static_cast<int64_t>(x), static_cast<int64_t>(y), static_cast<int64_t>(layer), static_cast<int64_t>(mip), static_cast<int64_t>('D')});
                        TexelComponents c;
                        c.values[0] = clamp01(static_cast<double>(rnd) / 4294967295.0);
                        const std::vector<uint8_t> bytes = d32.packBits(d32.numberToBits(c));
                        std::memcpy(srcData.data() + static_cast<size_t>(y) * bytesPerRow + static_cast<size_t>(x) * 4u, bytes.data(), 4);
                    }
                }
                WGPUTextureDescriptor srcDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                srcDesc.format = WGPUTextureFormat_R32Float;
                srcDesc.dimension = WGPUTextureDimension_2D;
                srcDesc.size = WGPUExtent3D{mipSize.width, mipSize.height, 1};
                srcDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                WGPUTexture source = t.createTextureTracked(srcDesc);
                WGPUTexelCopyBufferLayout srcLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
                srcLayout.bytesPerRow = bytesPerRow;
                srcLayout.rowsPerImage = mipSize.height;
                t.queueWriteTexture(source, srcDesc.size, srcLayout, srcData.data(), srcData.size(), 0);

                WGPUTextureView srcView = t.createViewTracked(source, WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT);
                WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                bgEntry.binding = 0;
                bgEntry.textureView = srcView;
                WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                bgDesc.layout = bgl;
                bgDesc.entryCount = 1;
                bgDesc.entries = &bgEntry;
                WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

                WGPUTextureViewDescriptor dvDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                dvDesc.dimension = WGPUTextureViewDimension_2D;
                dvDesc.baseMipLevel = mip;
                dvDesc.mipLevelCount = 1;
                dvDesc.baseArrayLayer = layer;
                dvDesc.arrayLayerCount = 1;
                WGPUTextureView depthView = t.createViewTracked(texture, dvDesc);

                WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
                WGPURenderPassDepthStencilAttachment dsAttach = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
                dsAttach.view = depthView;
                dsAttach.depthLoadOp = WGPULoadOp_Clear;
                dsAttach.depthStoreOp = WGPUStoreOp_Store;
                dsAttach.depthClearValue = 0.0f;
                if (hasStencil) {
                    dsAttach.stencilLoadOp = WGPULoadOp_Load;
                    dsAttach.stencilStoreOp = WGPUStoreOp_Store;
                }
                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.depthStencilAttachment = &dsAttach;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
                submit(t, encoder);
            }
        }
    }

    return texture;
}

}  // namespace

void executeCreateTextureWithRandomDataAndGetTexelsWithGenerator(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
    const WGPUTextureViewDimension viewDimension = parseTextureViewDimension(t.param<std::string>("viewDimension"));

    t.skipIfTextureFormatNotSupported(format);
    t.skipIfTextureViewDimensionNotSupported(viewDimension);
    const WGPUTextureDimension dimension = getTextureDimensionFromView(viewDimension);
    if (!t.textureDimensionAndFormatCompatibleForDevice(dimension, format)) {
        t.skip("texture format and view dimension not compatible");
    }

    // choose an odd size (9) so we're more likely to test alignment issues.
    const std::array<uint32_t, 3> size = chooseTextureSize(9, 4, format, viewDimension);
    const WGPUExtent3D baseSize = WGPUExtent3D{size[0], size[1], size[2]};

    // Exercise the depth-comparison generator (comparison='equal') across the base mip and
    // assert every produced value is finite (mirrors the upstream createRandomTexelViewMipmap
    // path that consumes the generator).
    const TexelGenerator generator = makeRandomDepthComparisonTexelGenerator(format, baseSize);
    for (uint32_t z = 0; z < baseSize.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < baseSize.height; ++y) {
            for (uint32_t x = 0; x < baseSize.width; ++x) {
                const double v = generator(x, y, z, 0).values[0];
                if (std::isnan(v) || std::isinf(v)) {
                    t.fail("generator produced non-finite depth/stencil texel");
                }
            }
        }
    }

    // Actually create + fill the texture on the GPU (depth via render, stencil via write),
    // matching createTextureWithRandomDataAndGetTexels: we expect no validation errors.
    const uint32_t mipLevelCount = 3;
    const WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding);
    (void)fillDepthStencilTexture(t, format, dimension, baseSize, mipLevelCount, /*sampleCount=*/1u, usage);
    // We don't expect any particular results. We just expect no validation errors.
}

void executeReadTextureToTexelViews(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPUTextureFormat srcFormat = parseTextureFormat(t.param<std::string>("srcFormat"));
    const WGPUTextureFormat texelViewFormat = parseTextureFormat(t.param<std::string>("texelViewFormat"));
    const WGPUTextureViewDimension viewDimension = parseTextureViewDimension(t.param<std::string>("viewDimension"));
    const uint32_t sampleCount = static_cast<uint32_t>(t.param<int64_t>("sampleCount"));

    t.skipIfTextureViewDimensionNotSupported(viewDimension);
    const WGPUTextureDimension dimension = getTextureDimensionFromView(viewDimension);
    if (!t.textureDimensionAndFormatCompatibleForDevice(dimension, srcFormat)) {
        t.skip("texture format and view dimension not compatible");
    }
    if (sampleCount > 1 && !t.isTextureFormatMultisampled(srcFormat)) {
        t.skip("texture format not multisampled");
    }

    const std::array<uint32_t, 3> size = chooseTextureSize(9, 4, srcFormat, viewDimension);
    const WGPUExtent3D baseSize = WGPUExtent3D{size[0], size[1], size[2]};
    const uint32_t mipLevelCount = (viewDimension == WGPUTextureViewDimension_1D || sampleCount > 1) ? 1u : 3u;
    const bool depthStencil = isDepthOrStencilTextureFormat(srcFormat);

    WGPUTextureUsage usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    if (sampleCount > 1) {
        usage = static_cast<WGPUTextureUsage>(usage | WGPUTextureUsage_RenderAttachment);
    }

    // For depth/stencil the "expected" texels are obtained by reading the texture back, exactly
    // as upstream createTextureWithRandomDataAndGetTexels does for unencodable depth. For color
    // formats the expected texels are the CPU-side random data uploaded to the texture.
    WGPUTexture texture;
    std::vector<MipTexels> expectedTexels;
    if (depthStencil) {
        texture = fillDepthStencilTexture(t, srcFormat, dimension, baseSize, mipLevelCount, sampleCount, usage);
        expectedTexels = readTextureToTexelViews(
            t, texture, srcFormat, dimension, baseSize, mipLevelCount, sampleCount, viewDimension, texelViewFormat);
    } else {
        const CreatedTexture created = createColorTextureWithRandomData(
            t, srcFormat, dimension, baseSize, mipLevelCount, sampleCount, viewDimension, usage);
        texture = created.texture;
        expectedTexels = created.texels;
    }

    const std::vector<MipTexels> actual = readTextureToTexelViews(
        t, texture, srcFormat, dimension, baseSize, mipLevelCount, sampleCount, viewDimension, texelViewFormat);

    t.expect(actual.size() == expectedTexels.size(), "num mip levels match");

    const FormatType type = getMetaTextureFormatType(srcFormat);
    const bool singleChannel = type == FormatType::Depth;  // texture_depth_* -> one component
    const double maxFractionalDiff = 0.0;

    for (size_t mipLevel = 0; mipLevel < actual.size() && mipLevel < expectedTexels.size(); ++mipLevel) {
        const MipTexels& actualMip = actual[mipLevel];
        const MipTexels& expectedMip = expectedTexels[mipLevel];
        const WGPUExtent3D mipSize = physicalMipSize(baseSize, dimension, static_cast<uint32_t>(mipLevel));

        std::vector<std::string> errors;
        for (uint32_t z = 0; z < mipSize.depthOrArrayLayers; ++z) {
            for (uint32_t y = 0; y < mipSize.height; ++y) {
                for (uint32_t x = 0; x < mipSize.width; ++x) {
                    for (uint32_t s = 0; s < sampleCount; ++s) {
                        // actual is stored as outFormat texels keyed (x*sampleCount+s).
                        const TexelRepresentation& actualRep = texelRepresentation(actualMip.format);
                        const uint64_t aOffset = (static_cast<uint64_t>(z) * actualMip.layout.rowsPerImage + y) * actualMip.layout.bytesPerRow
                            + (static_cast<uint64_t>(x) * sampleCount + s) * actualRep.bytesPerBlock;
                        const TexelComponents actualComps = actualRep.bitsToNumber(
                            actualRep.unpackBits(actualMip.data.data() + aOffset, actualMip.data.size() - aOffset));
                        const MetaColor actualRGBA = convertPerTexelComponentToResultFormat(actualComps, actualMip.format);

                        TexelComponents expComps;
                        const WGPUTextureFormat outViewFormat = effectiveTexelViewFormat(texelViewFormat);
                        const TexelRepresentation& outRep = texelRepresentation(outViewFormat);
                        if (depthStencil) {
                            // expected texels already came from a readback in texelViewFormat.
                            const uint64_t eOffset = (static_cast<uint64_t>(z) * expectedMip.layout.rowsPerImage + y) * expectedMip.layout.bytesPerRow
                                + (static_cast<uint64_t>(x) * sampleCount + s) * outRep.bytesPerBlock;
                            expComps = outRep.bitsToNumber(outRep.unpackBits(expectedMip.data.data() + eOffset, expectedMip.data.size() - eOffset));
                        } else {
                            // expected: decode from the source texels then convert to the texelView format.
                            const MetaColor srcRGBA = colorOfMip(expectedMip, x, y, z, s, sampleCount);
                            expComps.values[0] = srcRGBA.v[0];
                            expComps.values[1] = srcRGBA.v[1];
                            expComps.values[2] = srcRGBA.v[2];
                            expComps.values[3] = srcRGBA.v[3];
                            expComps = outRep.bitsToNumber(outRep.numberToBits(expComps));
                        }
                        const MetaColor expectedRGBA = convertPerTexelComponentToResultFormat(expComps, outViewFormat);

                        if (!texelsApproximatelyEqual(actualRGBA, expectedRGBA, outViewFormat, singleChannel, maxFractionalDiff)) {
                            std::ostringstream msg;
                            msg << "texel at " << x << ", " << y << ", " << z << ", sampleIndex: " << s
                                << " expected: " << formatTexel(expComps, outViewFormat)
                                << ", actual: " << formatTexel(actualComps, actualMip.format);
                            errors.push_back(msg.str());
                        }
                    }
                }
            }
        }
        if (!errors.empty()) {
            std::ostringstream joined;
            for (size_t i = 0; i < errors.size(); ++i) {
                joined << errors[i];
                if (i + 1 < errors.size()) {
                    joined << "\n";
                }
            }
            t.fail(joined.str());
        }
    }
}

void executeWeights(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string stage = t.param<std::string>("stage");
    const StageWeights weights = queryMipLevelMixWeightsForDeviceStage(t, stage);
    validateWeights(t, stage, "textureSampleLevel", weights.sampleLevelWeights);
    validateWeights(t, stage, "textureSampleGrad", weights.softwareMixToGPUMixGradWeights);
}

} // namespace cts::texture_utils
