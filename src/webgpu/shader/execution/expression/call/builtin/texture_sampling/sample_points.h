// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/texture_utils.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// The texture builtin sample-point generators: upstream generateTextureBuiltinInputs{1D,2D,3D}
// (generateTextureBuiltinInputsImpl) and generateSamplePointsCube, with their hash helpers
// (hashU32 from webgpu/util/math.ts, makeRandValue, makeRangeValue, makeIntHashValue*).
//
// Every value is computed with the same double-precision operations, in the same order, as the
// JavaScript source, and JavaScript's integer conversions (ToInt32/ToUint32 for the bitwise
// operators, `>>` on a fractional shift count, Math.round) are reproduced exactly, so the
// generated call arguments equal upstream's for the same inputs. The only expected differences
// are the last-bit results of the transcendental functions (Math.cos/sin/pow) where the C library
// rounds differently from V8; coordinates are quantized after those, so in practice only
// derivativeMult (a power of two of a fractional level) can differ, by at most an ulp.
//
// Header-only so the unit tests can call it without a separate translation unit.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// JavaScript rounds every operation; the compiler must not fuse a multiply and an add into one
// FMA (clang and GCC do on arm64 by default), or quantized coordinates near a texel boundary can
// land on the other side of it.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace cts::texture_utils::sample_points {

// ---------------------------------------------------------------------------
// JavaScript number semantics
// ---------------------------------------------------------------------------

// ECMAScript ToInt32: NaN and infinities become 0; otherwise truncate and wrap modulo 2^32.
inline int32_t jsToInt32(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    double m = std::fmod(std::trunc(value), 4294967296.0);
    if (m < 0.0) {
        m += 4294967296.0;
    }
    const uint32_t bits = static_cast<uint32_t>(m);
    return static_cast<int32_t>(bits);
}

// ECMAScript ToUint32.
inline uint32_t jsToUint32(double value) {
    return static_cast<uint32_t>(jsToInt32(value));
}

// `a << b` for an int32 `a` (the shift count is already masked by the caller).
inline int32_t jsShl(int32_t a, uint32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) << (b & 31u));
}

// Math.round: rounds half up (towards +Infinity).
inline double jsRound(double value) {
    const double f = std::floor(value);
    return (value - f >= 0.5) ? f + 1.0 : f;
}

// `%` on doubles (C fmod has the same sign-of-dividend semantics).
inline double jsMod(double a, double b) {
    return std::fmod(a, b);
}

// upstream sumOfCharCodesOfString(): a string hash input.
inline double hashInputString(std::string_view s) {
    double sum = 0.0;
    for (char ch : s) {
        sum += static_cast<double>(static_cast<unsigned char>(ch));
    }
    return sum;
}

inline double hashInputBool(bool b) {
    return b ? 1.0 : 0.0;
}

// An `undefined` hash input: `undefined + n` is NaN in JavaScript.
inline double hashInputUndefined() {
    return std::numeric_limits<double>::quiet_NaN();
}

// upstream hashU32 (webgpu/util/math.ts) over JavaScript numbers.
//   n = v + (n << 7) + (n >>> 1);  n = (n * 0x29493) & 0xffff_ffff;
//   n ^= n >>> 8;  n += n << 15;  n = n & 0xffff_ffff;  if (n < 0) n = ~n * 2 + 1;
inline double jsHashU32(const double* values, size_t count) {
    double n = static_cast<double>(0x3504f333);
    for (size_t i = 0; i < count; ++i) {
        const double shl = static_cast<double>(jsShl(jsToInt32(n), 7u));
        const double ushr = static_cast<double>(jsToUint32(n) >> 1u);
        n = values[i] + shl + ushr;
        n = static_cast<double>(jsToInt32(n * static_cast<double>(0x29493)));
    }
    {
        const int32_t a = jsToInt32(n);
        const int32_t b = jsToInt32(static_cast<double>(jsToUint32(n) >> 8u));
        n = static_cast<double>(a ^ b);
    }
    n += static_cast<double>(jsShl(jsToInt32(n), 15u));
    n = static_cast<double>(jsToInt32(n));
    if (n < 0.0) {
        n = static_cast<double>(~jsToInt32(n)) * 2.0 + 1.0;
    }
    return n;
}

inline double jsHashU32(const std::vector<double>& values) {
    return jsHashU32(values.data(), values.size());
}

// upstream lerp (webgpu/util/math.ts) for finite inputs.
inline double jsLerp(double a, double b, double t) {
    if ((a <= 0.0 && b >= 0.0) || (a >= 0.0 && b <= 0.0)) {
        return t * b + (1.0 - t) * a;
    }
    if (t == 1.0) {
        return b;
    }
    const double x = a + t * (b - a);
    return (t > 1.0) == (b > a) ? std::max(b, x) : std::min(b, x);
}

inline double jsClamp(double n, double lo, double hi) {
    return std::min(std::max(n, lo), hi);
}

// ---------------------------------------------------------------------------
// Generator inputs and outputs
// ---------------------------------------------------------------------------

// upstream RangeDef: `type` is "f32", "i32" or "u32".
struct RangeDef {
    double num = 0.0;
    std::string type = "f32";
};

// The fields of upstream TextureBuiltinInputArgs (plus the parts of the descriptor, view
// descriptor and sampler the generators read). Empty strings stand for `undefined`.
struct BuiltinInputArgs {
    std::string method = "texel-centre";  // 'texel-centre', 'spiral' or 'cube-edges'
    std::string textureBuiltin;           // only gather builtins change the generated values
    // GPUTextureDescriptor
    std::string dimension = "2d";  // '1d', '2d' or '3d'
    std::array<double, 3> size = {{1.0, 1.0, 1.0}};
    double mipLevelCount = 1.0;
    bool depthOrStencilFormat = false;
    // GPUTextureViewDescriptor
    std::optional<double> viewBaseMipLevel;
    std::optional<double> viewMipLevelCount;
    std::optional<double> viewBaseArrayLayer;
    std::optional<double> viewArrayLayerCount;
    // GPUSamplerDescriptor (`hasSampler` false: no sampler was passed)
    bool hasSampler = false;
    std::string minFilter;
    std::string mipmapFilter;
    std::string addressModeU;
    std::string addressModeV;
    std::string addressModeW;
    // flags
    bool derivatives = false;
    bool grad = false;
    bool bias = false;
    bool component = false;
    bool depthRef = false;
    bool offset = false;
    std::optional<RangeDef> mipLevel;
    std::optional<RangeDef> sampleIndex;
    std::optional<RangeDef> arrayIndex;
    // already converted: strings by hashInputString, booleans by hashInputBool
    std::vector<double> hashInputs;
    // 'spiral' options
    double radius = 1.5;
    double loops = 2.0;
};

// One generated call. Optional fields are `undefined` upstream when unset.
struct BuiltinInput {
    std::vector<double> coords;
    std::optional<std::vector<double>> derivativeMult;
    std::optional<std::vector<double>> ddx;
    std::optional<std::vector<double>> ddy;
    double mipLevel = 0.0;
    std::optional<double> sampleIndex;
    std::optional<double> arrayIndex;
    std::optional<double> bias;
    std::optional<std::vector<double>> offset;
    std::optional<double> component;
    std::optional<double> depthRef;
};

namespace detail {

constexpr double kPI = 3.141592653589793;  // Math.PI
constexpr double kTwoTo32 = 4294967296.0;  // 0x1_0000_0000
constexpr double kSubdivisionsPerTexel = 4.0;
constexpr double kMipEpsilon = 0.02;

inline bool isBuiltinGather(const std::string& builtin) {
    return builtin == "textureGather" || builtin == "textureGatherCompare";
}

// upstream virtualMipSize(): `n >> mipLevel` with JavaScript shift-count semantics.
inline std::array<double, 3> virtualMipSize(const std::string& dimension, const std::array<double, 3>& size, double mipLevel) {
    const uint32_t shift = jsToUint32(mipLevel) & 31u;
    const auto shiftMinOne = [shift](double n) {
        return std::max(1.0, static_cast<double>(jsToInt32(n) >> shift));
    };
    if (dimension == "1d") {
        return {{shiftMinOne(size[0]), size[1], size[2]}};
    }
    if (dimension == "3d") {
        return {{shiftMinOne(size[0]), shiftMinOne(size[1]), shiftMinOne(size[2])}};
    }
    return {{shiftMinOne(size[0]), shiftMinOne(size[1]), size[2]}};
}

struct BaseMipLevelInfo {
    double mipLevelCount = 1.0;
    std::array<double, 3> baseMipLevelSize = {{1.0, 1.0, 1.0}};
};

// upstream getBaseMipLevelInfo().
inline BaseMipLevelInfo getBaseMipLevelInfo(const BuiltinInputArgs& args) {
    BaseMipLevelInfo info;
    const double baseMipLevel = args.viewBaseMipLevel.value_or(0.0);
    info.mipLevelCount = args.viewMipLevelCount.has_value() ? *args.viewMipLevelCount : args.mipLevelCount - baseMipLevel;
    info.baseMipLevelSize = virtualMipSize(args.dimension, args.size, baseMipLevel);
    const double baseArrayLayer = args.viewBaseArrayLayer.value_or(0.0);
    const double arrayLayerCount = args.viewArrayLayerCount.has_value() ? *args.viewArrayLayerCount : info.baseMipLevelSize[2] - baseArrayLayer;
    info.baseMipLevelSize[2] = arrayLayerCount;
    return info;
}

// upstream quantizeMipLevel().
inline double quantizeMipLevel(double mipLevel, const std::string& mipmapFilter) {
    if (mipmapFilter == "linear") {
        return mipLevel;
    }
    const double intMip = std::floor(mipLevel);
    const double fractionalMip = mipLevel - intMip;
    if (fractionalMip < 0.5 - kMipEpsilon || fractionalMip > 0.5 + kMipEpsilon) {
        return mipLevel;
    }
    return intMip + 0.5 + (fractionalMip < 0.5 ? -kMipEpsilon : +kMipEpsilon);
}

// The per-case random helpers, closed over `_hashInputs`.
class Hasher {
  public:
    explicit Hasher(const std::vector<double>& hashInputs) : hashInputs_(hashInputs) {}

    // hashU32(..._hashInputs, ...extra) / 0x1_0000_0000
    double unit(std::initializer_list<double> extra) const {
        std::vector<double> values = hashInputs_;
        values.insert(values.end(), extra.begin(), extra.end());
        return jsHashU32(values) / kTwoTo32;
    }

    // makeRandValue: a number in [0, num)
    double randValue(const RangeDef& def, std::initializer_list<double> extra) const {
        const double range = def.num;
        const double number = unit(extra) * range;
        return def.type == "f32" ? number : std::floor(number);
    }

    // makeRangeValue: [-1, num] for signed and float types, [0, num] for unsigned
    double rangeValue(const RangeDef& def, std::initializer_list<double> extra) const {
        const bool isU32 = def.type == "u32";
        const double range = def.num + (isU32 ? 1.0 : 2.0);
        const double number = unit(extra) * range - (isU32 ? 0.0 : 1.0);
        return def.type == "f32" ? number : std::floor(number);
    }

    // makeIntHashValue (cube): uses the case hash inputs
    double intHashValue(double min, double max, std::initializer_list<double> extra) const {
        const double range = max - min;
        return min + std::floor(unit(extra) * range);
    }

  private:
    std::vector<double> hashInputs_;
};

// makeIntHashValueRepeatable: hashes only the given values.
inline double intHashValueRepeatable(double min, double max, std::initializer_list<double> values) {
    const double range = max - min;
    const std::vector<double> v(values);
    return min + std::floor((jsHashU32(v) / kTwoTo32) * range);
}

inline std::array<double, 3> transformMat3(const std::array<double, 3>& v, const std::array<double, 9>& m) {
    const double x = v[0];
    const double y = v[1];
    const double z = v[2];
    return {{
        x * m[0] + y * m[3] + z * m[6],
        x * m[1] + y * m[4] + z * m[7],
        x * m[2] + y * m[5] + z * m[8],
    }};
}

inline std::array<double, 3> normalize(const std::array<double, 3>& v) {
    const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return {{v[0] / length, v[1] / length, v[2] / length}};
}

// upstream convertCubeCoordToNormalized3DTextureCoord().
inline std::array<double, 3> convertCubeCoordToNormalized3DTextureCoord(const std::array<double, 3>& v) {
    std::array<double, 3> uvw = {};
    double layer = 0.0;
    const std::array<double, 3> r = normalize(v);
    const std::array<double, 3> absR = {{std::abs(r[0]), std::abs(r[1]), std::abs(r[2])}};
    if (absR[0] > absR[1] && absR[0] > absR[2]) {
        const bool negX = r[0] < 0.0;
        uvw = {{negX ? r[2] : -r[2], -r[1], absR[0]}};
        layer = negX ? 1.0 : 0.0;
    } else if (absR[1] > absR[2]) {
        const bool negY = r[1] < 0.0;
        uvw = {{r[0], negY ? -r[2] : r[2], absR[1]}};
        layer = 2.0 + (negY ? 1.0 : 0.0);
    } else {
        const bool negZ = r[2] < 0.0;
        uvw = {{negZ ? -r[0] : r[0], -r[1], absR[2]}};
        layer = 4.0 + (negZ ? 1.0 : 0.0);
    }
    return {{(uvw[0] / uvw[2] + 1.0) * 0.5, (uvw[1] / uvw[2] + 1.0) * 0.5, (layer + 0.5) / 6.0}};
}

// upstream convertNormalized3DTexCoordToCubeCoord(): the face is `Math.min(5, faceLayer * 6) | 0`.
inline std::array<double, 3> convertNormalized3DTexCoordToCubeCoord(const std::array<double, 3>& uvLayer) {
    static constexpr std::array<std::array<double, 9>, 6> kFaceUVMatrices = {{
        {{0, 0, -2, 0, -2, 0, 1, 1, 1}},     // pos-x
        {{0, 0, 2, 0, -2, 0, -1, 1, -1}},    // neg-x
        {{2, 0, 0, 0, 0, 2, -1, 1, -1}},     // pos-y
        {{2, 0, 0, 0, 0, -2, -1, -1, 1}},    // neg-y
        {{2, 0, 0, 0, -2, 0, -1, 1, 1}},     // pos-z
        {{-2, 0, 0, 0, -2, 0, 1, 1, -1}},    // neg-z
    }};
    const int32_t face = jsToInt32(std::min(5.0, uvLayer[2] * 6.0));
    return normalize(transformMat3({{uvLayer[0], uvLayer[1], 1.0}}, kFaceUVMatrices[static_cast<size_t>(face)]));
}

// upstream getUnusedCubeCornerSampleIndex().
inline int getUnusedCubeCornerSampleIndex(double textureSize, const std::array<double, 3>& coords) {
    const double u = coords[0] * textureSize;
    const double v = coords[1] * textureSize;
    if (v < 0.5) {
        if (u < 0.5) {
            return 0;
        } else if (u >= textureSize - 0.5) {
            return 1;
        }
    } else if (v >= textureSize - 0.5) {
        if (u < 0.5) {
            return 2;
        } else if (u >= textureSize - 0.5) {
            return 3;
        }
    }
    return -1;
}

}  // namespace detail

// upstream generateTextureBuiltinInputs1D/2D/3D (generateTextureBuiltinInputsImpl). `dims` is the
// number of coordinate components (1, 2 or 3).
inline std::vector<BuiltinInput> generateTextureBuiltinInputs(uint32_t dims, uint32_t n, const BuiltinInputArgs& args) {
    using namespace detail;
    const BaseMipLevelInfo info = getBaseMipLevelInfo(args);
    const double mipLevelCount = info.mipLevelCount;
    const std::array<double, 3>& baseMipLevelSize = info.baseMipLevelSize;
    const std::string& dimension = args.dimension;

    const auto makeValue = [dims](double x, double y, double z) {
        std::vector<double> v = {x, y, z};
        v.resize(dims);
        return v;
    };

    std::vector<std::vector<double>> coords;
    if (args.method == "texel-centre") {
        for (uint32_t i = 0; i < n; ++i) {
            const double r = jsHashU32(std::vector<double>{static_cast<double>(i)});
            const int32_t ri = jsToInt32(r);
            const double x = std::floor(jsLerp(0.0, baseMipLevelSize[0] - 1.0, static_cast<double>(ri & 0xff) / 255.0)) + 0.5;
            const double y = std::floor(jsLerp(0.0, baseMipLevelSize[1] - 1.0, static_cast<double>((ri >> 8) & 0xff) / 255.0)) + 0.5;
            const double z = std::floor(jsLerp(0.0, baseMipLevelSize[2] - 1.0, static_cast<double>((ri >> 16) & 0xff) / 255.0)) + 0.5;
            coords.push_back(makeValue(x / baseMipLevelSize[0], y / baseMipLevelSize[1], z / baseMipLevelSize[2]));
        }
    } else if (args.method == "spiral") {
        for (uint32_t i = 0; i < n; ++i) {
            const double f = static_cast<double>(i) / (std::max(static_cast<double>(n), 2.0) - 1.0);
            const double r = args.radius * f;
            const double a = args.loops * 2.0 * kPI * f;
            coords.push_back(makeValue(0.5 + r * std::cos(a), 0.5 + r * std::sin(a), 0.0));
        }
    }

    const Hasher hasher(args.hashInputs);
    const bool gather = isBuiltinGather(args.textureBuiltin);
    const bool avoidEdgeCase = !args.hasSampler || args.minFilter == "nearest" || gather;
    const double edgeRemainder = gather ? kSubdivisionsPerTexel / 2.0 : 0.0;
    const std::string mipmapFilter = args.hasSampler && !args.mipmapFilter.empty() ? args.mipmapFilter : "nearest";
    const bool linearMipmap = args.hasSampler && args.mipmapFilter == "linear";
    const auto modeOrClamp = [&](const std::string& mode) {
        return args.hasSampler && !mode.empty() ? mode : std::string("clamp-to-edge");
    };
    const std::array<std::string, 3> addressMode = args.textureBuiltin == "textureSampleBaseClampToEdge"
        ? std::array<std::string, 3>{{"clamp-to-edge", "clamp-to-edge", "clamp-to-edge"}}
        : std::array<std::string, 3>{{modeOrClamp(args.addressModeU), modeOrClamp(args.addressModeV), modeOrClamp(args.addressModeW)}};
    const auto euclideanModulo = [](double a, double m) { return jsMod(jsMod(a, m) + m, m); };
    const auto avoidTextureEdge = [&](size_t axis, double textureDimensionUnits, double v) {
        if (addressMode[axis] == "repeat") {
            return v;
        }
        const double inside = euclideanModulo(v, textureDimensionUnits);
        const double outside = v - inside;
        return outside + jsClamp(inside, 1.0, textureDimensionUnits - 1.0);
    };
    const double numComponents = args.depthOrStencilFormat ? 1.0 : 4.0;

    std::vector<BuiltinInput> out;
    out.reserve(coords.size());
    for (size_t ci = 0; ci < coords.size(); ++ci) {
        const double i = static_cast<double>(ci);
        const std::vector<double>& c = coords[ci];
        BuiltinInput input;
        const double mipLevel = args.mipLevel.has_value()
            ? quantizeMipLevel(hasher.rangeValue(*args.mipLevel, {i}), mipmapFilter)
            : 0.0;
        const double clampedMipLevel = jsClamp(mipLevel, 0.0, mipLevelCount - 1.0);
        const std::array<double, 3> mipSize = virtualMipSize(dimension, baseMipLevelSize, clampedMipLevel);
        const std::array<double, 3> q = {{mipSize[0] * kSubdivisionsPerTexel, mipSize[1] * kSubdivisionsPerTexel, mipSize[2] * kSubdivisionsPerTexel}};

        std::vector<double> qc(c.size());
        for (size_t k = 0; k < c.size(); ++k) {
            const double v1 = std::floor(c[k] * q[k]);
            const bool isTexelEdgeCase = std::abs(jsMod(v1, kSubdivisionsPerTexel)) == edgeRemainder;
            const double v2 = isTexelEdgeCase && avoidEdgeCase ? v1 + 1.0 : v1;
            const double v3 = gather ? avoidTextureEdge(k, q[k], v2) : v2;
            qc[k] = v3 / q[k];
        }

        const auto makeGradient = [&](double hashInput) {
            std::vector<double> g(qc.size());
            for (size_t k = 0; k < qc.size(); ++k) {
                const double intPart = hasher.rangeValue(RangeDef{8.0, "u32"}, {static_cast<double>(k), hashInput}) - 4.0;
                const double fractPart = hasher.rangeValue(RangeDef{0.0, "f32"}, {static_cast<double>(k), hashInput + 1.0}) * 0.25;
                g[k] = intPart + fractPart;
            }
            return g;
        };
        const auto makeDerivativeMult = [&](double level) {
            std::vector<double> mult(qc.size(), 0.0);
            const double ndx = hasher.rangeValue(RangeDef{static_cast<double>(qc.size()) - 1.0, "u32"}, {i, 8.0});
            mult[static_cast<size_t>(ndx)] = std::pow(2.0, level);
            return mult;
        };
        const auto chooseMipLevel = [&]() {
            const double innerLevelR = hasher.randValue(RangeDef{9.0, "u32"}, {i, 11.0});
            const double innerLevel = linearMipmap ? innerLevelR + 1.0 : (innerLevelR < 5.0 ? innerLevelR : innerLevelR + 1.0);
            const double outerLevel = hasher.rangeValue(RangeDef{mipLevelCount - 1.0, "i32"}, {i, 11.0});
            return outerLevel + innerLevel / 10.0;
        };

        if (args.bias) {
            const double testType = hasher.randValue(RangeDef{4.0, "u32"}, {i, 11.0});
            double level = 0.0;
            double bias = 0.0;
            if (testType == 0.0) {
                level = mipLevelCount + 1.0;
                bias = -25.0;
            } else if (testType == 1.0) {
                level = -1.0;
                bias = 25.0;
            } else {
                level = chooseMipLevel();
                bias = hasher.rangeValue(RangeDef{6.0, "f32"}, {i, 9.0}) - 3.0;
            }
            const double clampedBias = jsClamp(bias, -16.0, 15.99);
            const double derivativeBasedMipLevel = level - clampedBias;
            input.bias = bias;
            input.derivativeMult = makeDerivativeMult(derivativeBasedMipLevel);
        } else if (args.derivatives) {
            input.derivativeMult = makeDerivativeMult(chooseMipLevel());
        }

        input.coords = qc;
        input.mipLevel = mipLevel;
        if (args.sampleIndex.has_value()) {
            input.sampleIndex = hasher.rangeValue(*args.sampleIndex, {i, 1.0});
        }
        if (args.arrayIndex.has_value()) {
            input.arrayIndex = hasher.rangeValue(*args.arrayIndex, {i, 2.0});
        }
        if (args.depthRef) {
            input.depthRef = hasher.randValue(RangeDef{3.0, "u32"}, {i, 5.0}) / 2.0;
        }
        if (args.grad) {
            input.ddx = makeGradient(7.0);
            input.ddy = makeGradient(8.0);
        }
        if (args.offset) {
            std::vector<double> offset(qc.size());
            for (size_t j = 0; j < qc.size(); ++j) {
                offset[j] = intHashValueRepeatable(-8.0, 8.0, {i, 3.0 + static_cast<double>(j)});
            }
            input.offset = offset;
        }
        if (args.component) {
            input.component = intHashValueRepeatable(0.0, numComponents, {i, 4.0});
        }
        out.push_back(std::move(input));
    }
    return out;
}

// upstream generateSamplePointsCube().
inline std::vector<BuiltinInput> generateSamplePointsCube(uint32_t n, const BuiltinInputArgs& args) {
    using namespace detail;
    const BaseMipLevelInfo info = getBaseMipLevelInfo(args);
    const double mipLevelCount = info.mipLevelCount;
    const std::array<double, 3>& baseMipLevelSize = info.baseMipLevelSize;
    const double textureWidth = baseMipLevelSize[0];

    std::vector<std::array<double, 3>> coords;
    if (args.method == "texel-centre") {
        for (uint32_t i = 0; i < n; ++i) {
            const double r = jsHashU32(std::vector<double>{static_cast<double>(i)});
            const int32_t ri = jsToInt32(r);
            const double u = (std::floor(jsLerp(0.0, textureWidth - 1.0, static_cast<double>(ri & 0xff) / 255.0)) + 0.5) / textureWidth;
            const double v = (std::floor(jsLerp(0.0, textureWidth - 1.0, static_cast<double>((ri >> 8) & 0xff) / 255.0)) + 0.5) / textureWidth;
            const double face = std::floor(jsLerp(0.0, 6.0, static_cast<double>((ri >> 16) & 0xff) / 256.0));
            coords.push_back(convertNormalized3DTexCoordToCubeCoord({{u, v, face}}));
        }
    } else if (args.method == "spiral") {
        for (uint32_t i = 0; i < n; ++i) {
            const double f = (static_cast<double>(i) + 1.0) / (std::max(static_cast<double>(n), 2.0) - 1.0);
            const double r = args.radius * f;
            const double theta = args.loops * 2.0 * kPI * f;
            const double phi = args.loops * 1.3 * kPI * f;
            const double sinTheta = std::sin(theta);
            const double cosTheta = std::cos(theta);
            const double sinPhi = std::sin(phi);
            const double cosPhi = std::cos(phi);
            const double ux = cosTheta * sinPhi;
            const double uy = cosPhi;
            const double uz = sinTheta * sinPhi;
            coords.push_back({{ux * r, uy * r, uz * r}});
        }
    } else if (args.method == "cube-edges") {
        coords = {
            {{1, -1.01, 0}}, {{1, +1.01, 0}}, {{1, 0, -1.01}}, {{1, 0, +1.01}},
            {{-1, -1.01, 0}}, {{-1, +1.01, 0}}, {{-1, 0, -1.01}}, {{-1, 0, +1.01}},
            {{-1.01, 1, 0}}, {{+1.01, 1, 0}}, {{0, 1, -1.01}}, {{0, 1, +1.01}},
            {{-1.01, -1, 0}}, {{+1.01, -1, 0}}, {{0, -1, -1.01}}, {{0, -1, +1.01}},
            {{-1.01, 0, 1}}, {{+1.01, 0, 1}}, {{0, -1.01, 1}}, {{0, +1.01, 1}},
            {{-1.01, 0, -1}}, {{+1.01, 0, -1}}, {{0, -1.01, -1}}, {{0, +1.01, -1}},
        };
    }

    const Hasher hasher(args.hashInputs);
    const bool gather = isBuiltinGather(args.textureBuiltin);
    const bool avoidEdgeCase = !args.hasSampler || args.minFilter == "nearest" || gather;
    const double edgeRemainder = gather ? kSubdivisionsPerTexel / 2.0 : 0.0;
    const std::string mipmapFilter = args.hasSampler && !args.mipmapFilter.empty() ? args.mipmapFilter : "nearest";
    const bool linearMipmap = args.hasSampler && args.mipmapFilter == "linear";

    std::vector<BuiltinInput> out;
    out.reserve(coords.size());
    for (size_t ci = 0; ci < coords.size(); ++ci) {
        const double i = static_cast<double>(ci);
        BuiltinInput input;
        const double mipLevel = args.mipLevel.has_value()
            ? quantizeMipLevel(hasher.rangeValue(*args.mipLevel, {i}), mipmapFilter)
            : 0.0;
        const double clampedMipLevel = jsClamp(mipLevel, 0.0, mipLevelCount - 1.0);
        const std::array<double, 3> mipSize = virtualMipSize("2d", baseMipLevelSize, std::ceil(clampedMipLevel));
        const std::array<double, 3> q = {{mipSize[0] * kSubdivisionsPerTexel, mipSize[0] * kSubdivisionsPerTexel, 6.0 * kSubdivisionsPerTexel}};

        std::array<double, 3> uvw = convertCubeCoordToNormalized3DTextureCoord(coords[ci]);
        if (getUnusedCubeCornerSampleIndex(mipSize[0], uvw) >= 0) {
            const double halfTexel = 0.5 / mipSize[0];
            uvw[0] = jsClamp(uvw[0], halfTexel, 1.0 - halfTexel);
        }
        std::array<double, 3> quantizedUVW = {};
        for (size_t k = 0; k < 3; ++k) {
            const double v1 = std::floor(uvw[k] * q[k]);
            const bool isEdgeCase = std::abs(jsMod(v1, kSubdivisionsPerTexel)) == edgeRemainder;
            const double v2 = isEdgeCase && avoidEdgeCase ? v1 + 1.0 : v1;
            quantizedUVW[k] = (v2 + 1.0 / 16.0) / q[k];
        }
        const std::array<double, 3> cubeCoords = convertNormalized3DTexCoordToCubeCoord(quantizedUVW);

        const auto quantize = [](double v, double units) { return std::floor(v * units) * units; };
        const auto makeGradient = [&](double hashInput) {
            std::vector<double> g(3);
            for (size_t k = 0; k < 3; ++k) {
                g[k] = quantize(hasher.rangeValue(RangeDef{8.0, "f32"}, {static_cast<double>(k), hashInput}) - 4.0, 1.0 / 3.0);
            }
            return g;
        };
        const auto makeDerivativeMult = [&](double level) {
            std::vector<double> mult(3, 0.0);
            const double ndx = hasher.rangeValue(RangeDef{2.0, "u32"}, {i, 8.0});
            mult[static_cast<size_t>(ndx)] = std::pow(2.0, level);
            return mult;
        };
        const auto chooseMipLevel = [&]() {
            const double innerLevelR = hasher.randValue(RangeDef{9.0, "u32"}, {i, 11.0});
            const double innerLevel = linearMipmap ? innerLevelR + 1.0 : (innerLevelR < 4.0 ? innerLevelR : innerLevelR + 1.0);
            const double outerLevel = hasher.rangeValue(RangeDef{mipLevelCount - 1.0, "i32"}, {i, 11.0});
            return outerLevel + innerLevel / 10.0;
        };

        if (args.bias) {
            const double testType = hasher.randValue(RangeDef{4.0, "u32"}, {i, 11.0});
            double level = 0.0;
            double bias = 0.0;
            if (testType == 0.0) {
                level = mipLevelCount + 1.0;
                bias = -25.0;
            } else if (testType == 1.0) {
                level = -1.0;
                bias = 25.0;
            } else {
                level = chooseMipLevel();
                bias = hasher.rangeValue(RangeDef{6.0, "f32"}, {i, 9.0}) - 3.0;
            }
            const double clampedBias = jsClamp(bias, -16.0, 15.99);
            input.bias = bias;
            input.derivativeMult = makeDerivativeMult(level - clampedBias);
        } else if (args.derivatives) {
            input.derivativeMult = makeDerivativeMult(chooseMipLevel());
        }

        input.coords = {cubeCoords[0], cubeCoords[1], cubeCoords[2]};
        if (args.grad) {
            input.ddx = makeGradient(7.0);
            input.ddy = makeGradient(8.0);
        }
        input.mipLevel = mipLevel;
        if (args.arrayIndex.has_value()) {
            input.arrayIndex = hasher.rangeValue(*args.arrayIndex, {i, 2.0});
        }
        if (args.depthRef) {
            input.depthRef = hasher.randValue(RangeDef{3.0, "u32"}, {i, 5.0}) / 2.0;
        }
        if (args.component) {
            input.component = hasher.intHashValue(0.0, 4.0, {i, 4.0});
        }
        out.push_back(std::move(input));
    }
    return out;
}

}  // namespace cts::texture_utils::sample_points

#if defined(__clang__)
#pragma clang fp contract(on)
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
