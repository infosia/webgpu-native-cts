#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "common/query.h"
#include "cts/format_sample.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_layout.h"
#include "webgpu/util/texture_ok.h"
#include "webgpu/util/write_buffer.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool containsRegularTextureFormat(WGPUTextureFormat format) {
    for (WGPUTextureFormat regularFormat : cts::kRegularTextureFormats) {
        if (regularFormat == format) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
bool containsTextureFormat(const std::array<WGPUTextureFormat, N>& formats, WGPUTextureFormat format) {
    for (WGPUTextureFormat listedFormat : formats) {
        if (listedFormat == format) {
            return true;
        }
    }
    return false;
}

void requireBytes(
    const std::vector<uint8_t>& actual,
    const std::vector<uint8_t>& expected,
    const std::string& message) {
    require(actual == expected, message);
}

std::vector<uint8_t> sampleBytesForFormat(WGPUTextureFormat format) {
    const cts::TextureBlockInfo block = cts::getBlockInfoForTextureFormat(format);
    std::vector<uint8_t> bytes(block.bytesPerBlock);
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(0x21 + i * 17);
    }
    return bytes;
}

double srgbDecode(double value) {
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return std::pow((value + 0.055) / 1.055, 2.4);
}

cts::FormatSampleHook representativeFormatHook() {
    return [](std::string_view key, int64_t value) -> std::optional<bool> {
        if (key == "format" || key == "textureFormat" || key == "viewFormat") {
            return cts::isRepresentativeTextureFormat(static_cast<WGPUTextureFormat>(value));
        }
        return std::nullopt;
    };
}

std::vector<cts::Value> colorFormatValues() {
    std::vector<cts::Value> values;
    values.reserve(cts::kColorTextureFormats.size());
    for (WGPUTextureFormat format : cts::kColorTextureFormats) {
        values.push_back(static_cast<int64_t>(format));
    }
    return values;
}

std::size_t totalRuns(const std::vector<cts::ParamsBuilder::ExpandedCase>& cases) {
    std::size_t count = 0;
    for (const auto& c : cases) {
        count += c.subcases.empty() ? 1 : c.subcases.size();
    }
    return count;
}

std::size_t representativeColorFormatCount() {
    std::size_t count = 0;
    for (WGPUTextureFormat format : cts::kColorTextureFormats) {
        count += cts::isRepresentativeTextureFormat(format) ? 1 : 0;
    }
    return count;
}

} // namespace

int main() {
    try {
        cts::ParamsBuilder builder;
        auto cases = builder.combine("case", {1, 2}).beginSubcases().combine("subcase", {true, false}).expand();
        require(cases.size() == 2, "case expansion count");
        require(cases[0].subcases.size() == 2, "subcase expansion count");

        auto filtered = cts::ParamsBuilder()
            .combine("x", {1, 2, 3})
            .filter([](const cts::ParamRecord& params) {
                return cts::valueAs<int>(*cts::findParam(params, "x")) >= 2;
            })
            .expand();
        require(filtered.size() == 2, "case filter count");

        auto combined = cts::ParamsBuilder()
            .combine("outer", {7, 8})
            .combineWithParams({
                cts::ParamRecord{{"a", 1}, {"b", true}},
                cts::ParamRecord{{"a", 2}, {"b", false}},
            })
            .beginSubcases()
            .combine("sub", {1, 2, 3})
            .filter([](const cts::ParamRecord& params) {
                return cts::valueAs<int>(*cts::findParam(params, "sub")) != 2;
            })
            .expand();
        require(combined.size() == 4, "combineWithParams case count");
        require(combined[0].subcases.size() == 2, "subcase filter count");
        require(cts::findParam(combined[0].params, "a") != nullptr, "combineWithParams record key");

        auto caseAwareSubcases = cts::ParamsBuilder()
            .combine("x", {1, 2})
            .beginSubcases()
            .combine("y", {1, 2, 3})
            .filter([](const cts::ParamRecord& params) {
                return cts::valueAs<int>(*cts::findParam(params, "y"))
                    > cts::valueAs<int>(*cts::findParam(params, "x"));
            })
            .expand();
        require(caseAwareSubcases.size() == 2, "case-aware subcase case count");
        require(caseAwareSubcases[0].subcases.size() == 2, "case-aware subcase filter x=1");
        require(caseAwareSubcases[1].subcases.size() == 1, "case-aware subcase filter x=2");
        require(cts::findParam(caseAwareSubcases[0].subcases[0], "x") == nullptr, "subcase omits case key");
        require(cts::findParam(caseAwareSubcases[0].subcases[0], "y") != nullptr, "subcase keeps subcase key");

        auto expandedSubcases = cts::ParamsBuilder()
            .combine("x", {1, 2})
            .beginSubcases()
            .expand("y", [](const cts::ParamRecord& params) {
                const int x = cts::valueAs<int>(*cts::findParam(params, "x"));
                return std::vector<cts::Value>{cts::Value(x), cts::Value(x * 10)};
            })
            .expand();
        require(expandedSubcases.size() == 2, "subcase expand case count");
        require(expandedSubcases[0].subcases.size() == 2, "subcase expand x=1 count");
        require(expandedSubcases[1].subcases.size() == 2, "subcase expand x=2 count");
        require(cts::findParam(expandedSubcases[0].subcases[0], "x") == nullptr,
                "subcase expand omits case key");
        require(cts::valueAs<int>(*cts::findParam(expandedSubcases[0].subcases[0], "y")) == 1,
                "subcase expand x=1 first");
        require(cts::valueAs<int>(*cts::findParam(expandedSubcases[0].subcases[1], "y")) == 10,
                "subcase expand x=1 second");
        require(cts::valueAs<int>(*cts::findParam(expandedSubcases[1].subcases[0], "y")) == 2,
                "subcase expand x=2 first");
        require(cts::valueAs<int>(*cts::findParam(expandedSubcases[1].subcases[1], "y")) == 20,
                "subcase expand x=2 second");

        auto omittedCase = cts::ParamsBuilder()
            .combine("x", {1, 2})
            .beginSubcases()
            .combine("y", {1})
            .filter([](const cts::ParamRecord& params) {
                return cts::valueAs<int>(*cts::findParam(params, "y"))
                    >= cts::valueAs<int>(*cts::findParam(params, "x"));
            })
            .expand();
        require(omittedCase.size() == 1, "empty-subcase case omission count");
        require(cts::valueAs<int>(*cts::findParam(omittedCase[0].params, "x")) == 1,
                "empty-subcase case omission survivor");
        require(omittedCase[0].subcases.size() == 1, "empty-subcase survivor subcase count");

        require(cts::stringifyValue(cts::Value(1)) == "1", "int stringify");
        require(cts::stringifyValue(cts::Value(true)) == "true", "bool stringify");
        require(cts::stringifyValue(cts::Value(0.5)) == "0.5", "double stringify");
        require(cts::stringifyValue(cts::Value::undef()) == "_undef_", "undefined stringify");
        require(cts::stringifyValue(cts::Value("abc")) == "\"abc\"", "string stringify");
        require(cts::valueAs<double>(cts::Value(0.5)) == 0.5, "double valueAs double");
        require(cts::valueAs<double>(cts::Value(1)) == 1.0, "double valueAs int");
        require(cts::kColorTextureFormats.size() == 43, "color texture format count");
        require(cts::isRepresentativeTextureFormat(WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm is representative");
        require(!cts::isRepresentativeTextureFormat(WGPUTextureFormat_RG16Snorm),
                "rg16snorm is not representative");
        require(representativeColorFormatCount() == 8, "representative color format count");

        {
            cts::FormatSampleStats stats;
            const auto sampled = cts::sampleFormatsInCases(
                cts::ParamsBuilder()
                    .combine("format", colorFormatValues())
                    .combine("mode", {1, 2, 3})
                    .expand(),
                representativeFormatHook(),
                cts::kFormatSampleThreshold,
                &stats);
            require(sampled.size() == 8 * 3, "case-level format sampling survivor count");
            require(totalRuns(sampled) == 8 * 3, "case-level format sampling run count");
            require(stats.testsSampled == 1, "case-level format sampling stats tests");
            require(stats.runsKept == 8 * 3, "case-level format sampling stats kept");
            require(stats.runsDropped == (43 - 8) * 3, "case-level format sampling stats dropped");
            for (const auto& c : sampled) {
                const auto format = static_cast<WGPUTextureFormat>(
                    cts::valueAs<int64_t>(*cts::findParam(c.params, "format")));
                require(cts::isRepresentativeTextureFormat(format), "case-level survivor is representative");
            }
        }

        {
            const auto cases = cts::ParamsBuilder()
                .combine("format", {
                    static_cast<int64_t>(WGPUTextureFormat_R8Unorm),
                    static_cast<int64_t>(WGPUTextureFormat_RGBA8Unorm),
                    static_cast<int64_t>(WGPUTextureFormat_RGBA8Snorm),
                    static_cast<int64_t>(WGPUTextureFormat_RGBA8Uint),
                })
                .expand();
            const auto sampled = cts::sampleFormatsInCases(cases, representativeFormatHook());
            require(sampled.size() == cases.size(), "small format sweep unchanged");
        }

        {
            const auto cases = cts::ParamsBuilder()
                .combine("format", {
                    static_cast<int64_t>(WGPUTextureFormat_R8Unorm),
                    static_cast<int64_t>(WGPUTextureFormat_R8Snorm),
                    static_cast<int64_t>(WGPUTextureFormat_R8Uint),
                    static_cast<int64_t>(WGPUTextureFormat_R8Sint),
                    static_cast<int64_t>(WGPUTextureFormat_RG8Unorm),
                    static_cast<int64_t>(WGPUTextureFormat_RG8Snorm),
                    static_cast<int64_t>(WGPUTextureFormat_RG8Uint),
                })
                .expand();
            const auto sampled = cts::sampleFormatsInCases(cases, representativeFormatHook());
            require(sampled.size() == cases.size(), "zero-survivor format sweep unchanged");
        }

        {
            const auto cases = cts::ParamsBuilder().combine("dimension", {1, 2, 3, 4, 5, 6, 7}).expand();
            const auto sampled = cts::sampleFormatsInCases(cases, representativeFormatHook());
            require(sampled.size() == cases.size(), "no format-like param unchanged");
        }

        {
            const auto sampled = cts::sampleFormatsInCases(
                cts::ParamsBuilder()
                    .combine("case", {1})
                    .beginSubcases()
                    .combine("format", colorFormatValues())
                    .expand(),
                representativeFormatHook());
            require(sampled.size() == 1, "subcase-level format sampling keeps case");
            require(sampled[0].subcases.size() == 8, "subcase-level format sampling survivor count");
            for (const cts::ParamRecord& subcase : sampled[0].subcases) {
                const auto format = static_cast<WGPUTextureFormat>(
                    cts::valueAs<int64_t>(*cts::findParam(subcase, "format")));
                require(cts::isRepresentativeTextureFormat(format), "subcase-level survivor is representative");
            }
        }

        for (WGPUTextureFormat format : cts::kColorTextureFormats) {
            const cts::TexelRepresentation& repr = cts::texelRepresentation(format);
            const std::vector<uint8_t> bytes = sampleBytesForFormat(format);
            const std::vector<uint8_t> roundTrip = repr.packBits(repr.unpackBits(bytes.data(), bytes.size()));
            requireBytes(roundTrip, bytes, "texel bits pack/unpack roundtrip format=" + std::to_string(format));

            cts::TexelBits maxBits;
            for (size_t i = 0; i < repr.bitLengths.size(); ++i) {
                if (repr.bitLengths[i] > 0) {
                    maxBits.values[i] = repr.bitLengths[i] == 32 ? 0xffffffffu : ((1u << repr.bitLengths[i]) - 1u);
                }
            }
            const cts::TexelComponents maxNumbers = repr.bitsToNumber(maxBits);
            if (format == WGPUTextureFormat_RGBA8Unorm) {
                require(maxNumbers.values[0] == 1.0, "rgba8unorm max decodes to 1.0");
            }
            if (format == WGPUTextureFormat_R8Snorm) {
                cts::TexelBits minSnorm;
                minSnorm.values[0] = 0x80;
                require(cts::texelRepresentation(format).bitsToNumber(minSnorm).values[0] == -1.0,
                        "r8snorm min decodes to -1.0");
            }
            if (format == WGPUTextureFormat_R32Float) {
                cts::TexelBits one;
                one.values[0] = 0x3f800000;
                require(cts::texelRepresentation(format).bitsToNumber(one).values[0] == 1.0,
                        "r32float 1.0 bit pattern");
            }
        }

        {
            const cts::TexelRepresentation& r8snorm = cts::texelRepresentation(WGPUTextureFormat_R8Snorm);
            cts::TexelBits snormBits;
            snormBits.values[0] = 0x80;
            require(r8snorm.bitsToNumber(snormBits).values[0] == -1.0, "r8snorm -128 clamps to -1.0");
            snormBits.values[0] = 0x81;
            require(r8snorm.bitsToNumber(snormBits).values[0] == -1.0, "r8snorm -127 decodes to -1.0");
            snormBits.values[0] = 0x7f;
            require(r8snorm.bitsToNumber(snormBits).values[0] == 1.0, "r8snorm 127 decodes to 1.0");

            const cts::TexelRepresentation& r8unorm = cts::texelRepresentation(WGPUTextureFormat_R8Unorm);
            cts::TexelBits unormBits;
            unormBits.values[0] = 0x80;
            require(r8unorm.bitsToNumber(unormBits).values[0] == 128.0 / 255.0,
                    "r8unorm 128 decodes to 128/255");
            unormBits.values[0] = 0xff;
            require(r8unorm.bitsToNumber(unormBits).values[0] == 1.0, "r8unorm max decodes to 1.0");

            cts::TexelBits integerBits;
            integerBits.values[0] = 200;
            require(cts::texelRepresentation(WGPUTextureFormat_R8Uint).bitsToNumber(integerBits).values[0] == 200.0,
                    "r8uint 200 decodes to 200");
            integerBits.values[0] = 0x80;
            require(cts::texelRepresentation(WGPUTextureFormat_R8Sint).bitsToNumber(integerBits).values[0] == -128.0,
                    "r8sint 0x80 decodes to -128");

            cts::TexelBits srgbBits;
            srgbBits.values[0] = 0xbc;
            srgbBits.values[3] = 0xbc;
            const cts::TexelComponents srgb = cts::texelRepresentation(WGPUTextureFormat_RGBA8UnormSrgb)
                                                  .bitsToNumber(srgbBits);
            require(srgb.values[0] == srgbDecode(188.0 / 255.0), "rgba8unorm-srgb non-alpha gamma decodes");
            require(srgb.values[3] == 188.0 / 255.0, "rgba8unorm-srgb alpha stays linear");

            cts::TexelBits f16Bits;
            f16Bits.values[0] = 0x3c00;
            require(cts::texelRepresentation(WGPUTextureFormat_R16Float).bitsToNumber(f16Bits).values[0] == 1.0,
                    "r16float 0x3c00 decodes to 1.0");
            f16Bits.values[0] = 0xc000;
            require(cts::texelRepresentation(WGPUTextureFormat_R16Float).bitsToNumber(f16Bits).values[0] == -2.0,
                    "r16float 0xc000 decodes to -2.0");

            cts::TexelBits rg11b10Bits;
            rg11b10Bits.values[0] = 15u << 6;
            rg11b10Bits.values[2] = (15u << 5) | 16u;
            const cts::TexelComponents rg11b10 = cts::texelRepresentation(WGPUTextureFormat_RG11B10Ufloat)
                                                     .bitsToNumber(rg11b10Bits);
            require(rg11b10.values[0] == 1.0, "rg11b10ufloat R 11-bit 1.0 pattern");
            require(rg11b10.values[2] == 1.5, "rg11b10ufloat B 10-bit 5-mantissa pattern");

            const uint32_t rgb9e5Word = 256u | (128u << 9) | (64u << 18) | (15u << 27);
            const std::vector<uint8_t> rgb9e5Bytes = {
                static_cast<uint8_t>(rgb9e5Word & 0xffu),
                static_cast<uint8_t>((rgb9e5Word >> 8) & 0xffu),
                static_cast<uint8_t>((rgb9e5Word >> 16) & 0xffu),
                static_cast<uint8_t>((rgb9e5Word >> 24) & 0xffu),
            };
            const cts::TexelComponents rgb9e5 = cts::texelRepresentation(WGPUTextureFormat_RGB9E5Ufloat)
                                                    .bitsToNumber(cts::texelRepresentation(WGPUTextureFormat_RGB9E5Ufloat)
                                                                      .unpackBits(rgb9e5Bytes.data(), rgb9e5Bytes.size()));
            require(rgb9e5.values[0] == 0.5, "rgb9e5 R shared-exponent decode");
            require(rgb9e5.values[1] == 0.25, "rgb9e5 G shared-exponent decode");
            require(rgb9e5.values[2] == 0.125, "rgb9e5 B shared-exponent decode");

            bool rgb9e5EncodeThrew = false;
            try {
                cts::TexelComponents color;
                color.values[0] = 0.5;
                color.values[1] = 0.25;
                color.values[2] = 0.125;
                (void)cts::texelRepresentation(WGPUTextureFormat_RGB9E5Ufloat).numberToBits(color);
            } catch (const std::runtime_error&) {
                rgb9e5EncodeThrew = true;
            }
            require(rgb9e5EncodeThrew, "rgb9e5 numberToBits guard");
        }

        {
            const std::vector<uint8_t> identical = {
                1, 2, 3, 4, 0xee, 0xee, 0xee, 0xee,
                5, 6, 7, 8, 0xee, 0xee, 0xee, 0xee,
            };
            cts::TexelViewConfig config;
            config.bytesPerRow = 8;
            config.rowsPerImage = 2;
            config.subrectSize = WGPUExtent3D{1, 2, 1};
            const cts::TexelView actual = cts::TexelView::fromTextureDataByReference(
                WGPUTextureFormat_RGBA8Uint, identical.data(), identical.size(), config);
            const cts::TexelView expected = cts::TexelView::fromTextureDataByReference(
                WGPUTextureFormat_RGBA8Uint, identical.data(), identical.size(), config);
            require(!cts::findFailedPixels(
                        WGPUTextureFormat_RGBA8Uint, WGPUOrigin3D{0, 0, 0}, WGPUExtent3D{1, 2, 1},
                        actual, expected, 0.0),
                    "findFailedPixels identical padded views");

            std::vector<uint8_t> changed = identical;
            changed[8] = 9;
            const cts::TexelView changedView = cts::TexelView::fromTextureDataByReference(
                WGPUTextureFormat_RGBA8Uint, changed.data(), changed.size(), config);
            require(cts::findFailedPixels(
                        WGPUTextureFormat_RGBA8Uint, WGPUOrigin3D{0, 0, 0}, WGPUExtent3D{1, 2, 1},
                        changedView, expected, 0.0).has_value(),
                    "findFailedPixels detects planted diff");
        }

        {
            require(cts::bytesInACompleteRow(3, WGPUTextureFormat_RGBA8Unorm) == 12,
                    "rgba8unorm complete row bytes");
            const cts::TextureCopyLayout rgbaLayout = cts::getTextureCopyLayout(
                WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, WGPUExtent3D{3, 4, 5});
            require(rgbaLayout.bytesPerRow == 256, "rgba8unorm layout bytesPerRow");
            require(rgbaLayout.rowsPerImage == 4, "rgba8unorm layout rowsPerImage");
            require(rgbaLayout.byteLength == 4876, "rgba8unorm layout byteLength");
            const cts::TextureCopyLayout r8Layout = cts::getTextureCopyLayout(
                WGPUTextureFormat_R8Unorm, WGPUTextureDimension_2D, WGPUExtent3D{7, 1, 1});
            require(r8Layout.byteLength == 8, "r8unorm single row aligned byteLength");
            require(cts::bytesInACompleteRow(1, WGPUTextureFormat_RGB10A2Unorm) == 4,
                    "rgb10a2unorm complete row bytes");
            require(cts::dataBytesForCopyOrFail(
                        cts::TexelCopyBufferLayout{0, 256, 4},
                        WGPUTextureFormat_RGBA8Unorm,
                        WGPUExtent3D{3, 4, 5},
                        true)
                        == 4876,
                    "rgba8unorm dataBytesForCopyOrFail");
        }

        {
            std::vector<uint8_t> src = {
                1, 2, 3, 4, 0, 0, 0, 0,
                5, 6, 7, 8, 0, 0, 0, 0,
            };
            std::vector<uint8_t> dst(24, 0xff);
            cts::LinearTextureSubBox copy;
            copy.src = src.data();
            copy.srcLen = src.size();
            copy.srcLayout = cts::TexelCopyBufferLayout{0, 8, 2};
            copy.dst = dst.data();
            copy.dstLen = dst.size();
            copy.dstLayout = cts::TexelCopyBufferLayout{0, 12, 2};
            copy.dstOrigin = WGPUOrigin3D{1, 0, 0};
            cts::updateLinearTextureDataSubBox(WGPUTextureFormat_RGBA8Uint, WGPUExtent3D{1, 2, 1}, copy);
            require(dst[4] == 1 && dst[5] == 2 && dst[6] == 3 && dst[7] == 4,
                    "sub-box first row update");
            require(dst[16] == 5 && dst[17] == 6 && dst[18] == 7 && dst[19] == 8,
                    "sub-box second row update");
        }

        requireBytes(cts::encodeWriteBufferData({0, 1, 2, 3}, cts::WriteBufferArrayType::U8),
                     {0x00, 0x01, 0x02, 0x03},
                     "writeBuffer U8 encoding");
        requireBytes(cts::encodeWriteBufferData({0, 1, 2, 3}, cts::WriteBufferArrayType::U16),
                     {0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00},
                     "writeBuffer U16 encoding");
        requireBytes(cts::encodeWriteBufferData({0, 1}, cts::WriteBufferArrayType::U32),
                     {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00},
                     "writeBuffer U32 encoding");
        requireBytes(cts::encodeWriteBufferData({-1}, cts::WriteBufferArrayType::I8),
                     {0xff},
                     "writeBuffer I8 encoding");
        requireBytes(cts::encodeWriteBufferData({-1}, cts::WriteBufferArrayType::I16),
                     {0xff, 0xff},
                     "writeBuffer I16 encoding");
        requireBytes(cts::encodeWriteBufferData({-1}, cts::WriteBufferArrayType::I32),
                     {0xff, 0xff, 0xff, 0xff},
                     "writeBuffer I32 encoding");
        requireBytes(cts::encodeWriteBufferData({1}, cts::WriteBufferArrayType::F32),
                     {0x00, 0x00, 0x80, 0x3f},
                     "writeBuffer F32 encoding");
        requireBytes(cts::encodeWriteBufferData({1}, cts::WriteBufferArrayType::F64),
                     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f},
                     "writeBuffer F64 encoding");

        cts::ExpectationSet expectations;
        expectations.exact.insert("a:b:exact:foo=1");
        expectations.exact.insert("a:b:test:*");
        expectations.prefixes.push_back("a:b:test:");
        require(cts::expectationMatches(expectations, "a:b:exact:foo=1"), "expectation exact match");
        require(!cts::expectationMatches(expectations, "a:b:exact:foo=2"), "expectation exact mismatch");
        require(cts::expectationMatches(expectations, "a:b:test:foo=1"), "expectation prefix match params");
        require(cts::expectationMatches(expectations, "a:b:test:"), "expectation prefix match empty params");
        require(!cts::expectationMatches(expectations, "a:b:test2:foo"), "expectation prefix avoids test2");
        require(!cts::expectationMatches(expectations, "a:b:test_more:foo"), "expectation prefix avoids test_more");
        require(!cts::expectationMatches(expectations, "a:b:tes:foo"), "expectation prefix avoids shorter test");
        require(cts::expectationMatches(expectations, "a:b:test:*"), "expectation non-wildcard exact match");
        cts::ExpectationSet crashList;
        crashList.exact.insert("webgpu:a:b:c:x=1");
        crashList.prefixes.push_back("webgpu:a:b:d:");
        require(cts::expectationMatches(crashList, "webgpu:a:b:c:x=1"), "crash-list exact selects isolate");
        require(cts::expectationMatches(crashList, "webgpu:a:b:d:x=2"), "crash-list prefix selects isolate");
        require(!cts::expectationMatches(crashList, "webgpu:a:b:e:x=3"), "crash-list non-match stays in-process");
        const std::vector<std::string> crashLines = cts::crashListLines({
            cts::SubcaseResult{"webgpu:z:file:test:*", cts::TestStatus::Crash, "signal"},
            cts::SubcaseResult{"webgpu:a:file:test:*", cts::TestStatus::Pass, ""},
            cts::SubcaseResult{"webgpu:m:file:test:*", cts::TestStatus::Fail, "failure"},
            cts::SubcaseResult{"webgpu:z:file:test:*", cts::TestStatus::Crash, "signal"},
            cts::SubcaseResult{"webgpu:b:file:test:*", cts::TestStatus::Crash, "signal"},
        });
        require(crashLines == std::vector<std::string>({
                    "webgpu:b:file:test:*",
                    "webgpu:z:file:test:*",
                }),
                "crash-list lines sorted unique crashes");

        {
            const std::vector<std::string> cases = {
                "case0", "case1", "case2", "case3", "case4", "case5", "case6", "case7", "case8", "case9",
                "case10", "case11", "case12", "case13", "case14", "case15", "case16",
            };
            for (const int shardCount : {1, 3, 8}) {
                std::vector<std::string> unioned;
                std::vector<bool> seen(cases.size(), false);
                for (int shardIndex = 0; shardIndex < shardCount; ++shardIndex) {
                    for (size_t i = 0; i < cases.size(); ++i) {
                        const bool selected = cts::caseBelongsToShard(i, shardIndex, shardCount);
                        require(selected == (i % static_cast<size_t>(shardCount) == static_cast<size_t>(shardIndex)),
                                "shard assignment is idx modulo shard count");
                        if (selected) {
                            require(!seen[i], "shard partition is disjoint");
                            seen[i] = true;
                            unioned.push_back(cases[i]);
                        }
                    }
                }
                for (bool selected : seen) {
                    require(selected, "shard partition covers all cases");
                }
                std::vector<std::string> sortedUnion = unioned;
                std::sort(sortedUnion.begin(), sortedUnion.end());
                std::vector<std::string> sortedCases = cases;
                std::sort(sortedCases.begin(), sortedCases.end());
                require(sortedUnion == sortedCases, "shard partition union equals full set");
            }
            require(cts::caseBelongsToShard(5, -1, 0), "disabled sharding selects all cases");
        }

        {
            const std::optional<cts::SubcaseResult> parsed = cts::parseResultLine(
                "RESULT\tfail\twebgpu:a,b,c:test:x=1\tmessage with spaces");
            require(parsed.has_value(), "RESULT line parses");
            require(parsed->status == cts::TestStatus::Fail, "RESULT line status parses");
            require(parsed->query == "webgpu:a,b,c:test:x=1", "RESULT line query parses");
            require(parsed->message == "message with spaces", "RESULT line message parses");

            const std::optional<cts::SubcaseResult> noMessage = cts::parseResultLine(
                "RESULT\tpass\twebgpu:a,b,c:test:x=2");
            require(noMessage.has_value(), "RESULT line without message parses");
            require(noMessage->status == cts::TestStatus::Pass, "RESULT line pass status parses");
            require(noMessage->message.empty(), "RESULT line empty message");

            const std::optional<cts::SubcaseResult> sanitized = cts::parseResultLine(
                "RESULT\twarn\twebgpu:a,b,c:test:x=3\tmessage\twith\ncontrols");
            require(sanitized.has_value(), "RESULT line with controls parses");
            require(sanitized->message == "message with controls", "RESULT line controls are sanitized");

            const std::optional<cts::SubcaseResult> ignored = cts::parseResultLine("noise");
            require(!ignored.has_value(), "non-RESULT line ignored");
        }

        require(cts::kTextureUsages.size() == 6, "texture usages count");
        require(cts::kAllTextureUsages == 0x3F, "all texture usages bits");
        require((cts::kSomeBogusTextureUsage & cts::kAllTextureUsages) == 0, "bogus texture usage disjoint");
        require(!cts::isValidTextureUsageCombination(0), "zero texture usage invalid");
        require(cts::isValidTextureUsageCombination(WGPUTextureUsage_CopySrc), "copy-src texture usage valid");
        require(!cts::isValidTextureUsageCombination(WGPUTextureUsage_TransientAttachment),
                "transient texture usage alone invalid");
        require(cts::isValidTextureUsageCombination(WGPUTextureUsage_RenderAttachment |
                                                    WGPUTextureUsage_TransientAttachment),
                "transient render attachment texture usage valid");
        require(cts::isValidTextureUsageCombination(WGPUTextureUsage_CopySrc |
                                                    WGPUTextureUsage_RenderAttachment),
                "copy-src render attachment texture usage valid");
        require(!cts::isValidTextureUsageCombination(cts::kSomeBogusTextureUsage), "bogus texture usage invalid");
        for (WGPUTextureUsage usage : cts::kValidCombinationsOfOneOrTwoTextureUsages) {
            require(cts::isValidTextureUsageCombination(usage), "one-or-two texture usage combination valid");
        }
        require(std::find(cts::kValidCombinationsOfOneOrTwoTextureUsages.begin(),
                          cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                          WGPUTextureUsage_TextureBinding)
                    != cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                "one-or-two texture usage combinations include texture binding");
        require(std::find(cts::kValidCombinationsOfOneOrTwoTextureUsages.begin(),
                          cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                          WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst)
                    != cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                "one-or-two texture usage combinations include copy src dst");
        require(std::find(cts::kValidCombinationsOfOneOrTwoTextureUsages.begin(),
                          cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                          WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment)
                    != cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                "one-or-two texture usage combinations include transient render attachment");
        require(std::find(cts::kValidCombinationsOfOneOrTwoTextureUsages.begin(),
                          cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                          WGPUTextureUsage_TransientAttachment)
                    == cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                "one-or-two texture usage combinations exclude transient alone");
        require(std::find(cts::kValidCombinationsOfOneOrTwoTextureUsages.begin(),
                          cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                          WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_CopySrc)
                    == cts::kValidCombinationsOfOneOrTwoTextureUsages.end(),
                "one-or-two texture usage combinations exclude transient copy src");
        require(cts::kShaderStageCombinations.size() == 8, "shader stage combination count");
        for (size_t i = 0; i < cts::kShaderStageCombinations.size(); ++i) {
            require(cts::kShaderStageCombinations[i] == static_cast<WGPUShaderStage>(i),
                    "shader stage combination value");
        }
        require(cts::validStagesForEntryKey("buffer_storage") == cts::kValidStagesStorageWrite,
                "buffer storage valid stages");
        require(cts::validStagesForEntryKey("buffer_read-only-storage") == cts::kValidStagesAll,
                "buffer read-only-storage valid stages");
        require(cts::validStagesForEntryKey("buffer_uniform") == cts::kValidStagesAll,
                "buffer uniform valid stages");
        require(cts::validStagesForEntryKey("sampler_filtering") == cts::kValidStagesAll,
                "sampler filtering valid stages");
        require(cts::validStagesForEntryKey("texture_ms-false") == cts::kValidStagesAll,
                "sampled texture valid stages");
        require(cts::validStagesForEntryKey("storageTexture_write-only") == cts::kValidStagesStorageWrite,
                "storage texture write-only valid stages");
        require(cts::validStagesForEntryKey("storageTexture_read-only") == cts::kValidStagesAll,
                "storage texture read-only valid stages");
        require(cts::validStagesForEntryKey("storageTexture_read-write") == cts::kValidStagesStorageWrite,
                "storage texture read-write valid stages");
        const std::vector<std::string_view> bglEntries = cts::allBindingEntries(false);
        require(bglEntries.size() == 11, "all binding entries count");
        require(bglEntries[0] == "buffer_uniform", "all binding entries first");
        require(bglEntries[1] == "buffer_storage", "all binding entries second");
        require(bglEntries[2] == "buffer_read-only-storage", "all binding entries third");
        require(bglEntries[3] == "sampler_comparison", "all binding entries fourth");
        require(bglEntries[4] == "sampler_filtering", "all binding entries fifth");
        require(bglEntries[5] == "sampler_non-filtering", "all binding entries sixth");
        require(bglEntries[6] == "texture_ms-false", "all binding entries seventh");
        require(bglEntries[7] == "texture_ms-true", "all binding entries eighth");
        require(bglEntries[8] == "storageTexture_write-only", "all binding entries ninth");
        require(bglEntries[9] == "storageTexture_read-only", "all binding entries tenth");
        require(bglEntries[10] == "storageTexture_read-write", "all binding entries eleventh");
        WGPUBindGroupLayoutEntry bglBufferStorage = cts::bglEntryFromKey("buffer_storage");
        require(bglBufferStorage.buffer.type == WGPUBufferBindingType_Storage,
                "buffer storage entry buffer type");
        require(bglBufferStorage.sampler.type == WGPUSamplerBindingType_BindingNotUsed,
                "buffer storage entry sampler unused");
        require(bglBufferStorage.texture.sampleType == WGPUTextureSampleType_BindingNotUsed,
                "buffer storage entry texture unused");
        require(bglBufferStorage.storageTexture.access == WGPUStorageTextureAccess_BindingNotUsed,
                "buffer storage entry storage texture unused");
        WGPUBindGroupLayoutEntry bglStorageTexture = cts::bglEntryFromKey("storageTexture_read-only");
        require(bglStorageTexture.storageTexture.access == WGPUStorageTextureAccess_ReadOnly,
                "storage texture read-only entry access");
        require(bglStorageTexture.storageTexture.format == WGPUTextureFormat_R32Float,
                "storage texture read-only entry format");
        WGPULimits bglLimitProbe = WGPU_LIMITS_INIT;
        bglLimitProbe.maxDynamicUniformBuffersPerPipelineLayout = 11;
        bglLimitProbe.maxDynamicStorageBuffersPerPipelineLayout = 12;
        bglLimitProbe.maxUniformBuffersPerShaderStage = 13;
        bglLimitProbe.maxStorageBuffersPerShaderStage = 14;
        require(cts::bufferTypeMaxDynamicBuffersLimit(bglLimitProbe, WGPUBufferBindingType_Uniform) == 11,
                "uniform max dynamic buffer limit");
        require(cts::bufferTypeMaxDynamicBuffersLimit(bglLimitProbe, WGPUBufferBindingType_Storage) == 12,
                "storage max dynamic buffer limit");
        require(cts::bufferTypeMaxDynamicBuffersLimit(bglLimitProbe, WGPUBufferBindingType_ReadOnlyStorage) == 12,
                "read-only-storage max dynamic buffer limit");
        require(cts::bufferTypePerStageComputeLimit(bglLimitProbe, WGPUBufferBindingType_Uniform) == 13,
                "uniform per-stage compute buffer limit");
        require(cts::bufferTypePerStageComputeLimit(bglLimitProbe, WGPUBufferBindingType_Storage) == 14,
                "storage per-stage compute buffer limit");
        require(cts::bufferTypePerStageComputeLimit(bglLimitProbe, WGPUBufferBindingType_ReadOnlyStorage) == 14,
                "read-only-storage per-stage compute buffer limit");
        require(cts::bglEntryPerStageLimitClass("buffer_uniform") == cts::BGLPerStageLimitClass::UniformBuffer,
                "buffer uniform per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("buffer_storage") == cts::BGLPerStageLimitClass::StorageBuffer,
                "buffer storage per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("buffer_read-only-storage")
                    == cts::BGLPerStageLimitClass::StorageBuffer,
                "buffer read-only-storage per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("sampler_filtering") == cts::BGLPerStageLimitClass::Sampler,
                "sampler per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("texture_ms-false")
                    == cts::BGLPerStageLimitClass::SampledTexture,
                "sampled texture per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("storageTexture_read-only")
                    == cts::BGLPerStageLimitClass::ReadOnlyStorageTexture,
                "read-only storage texture per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("storageTexture_write-only")
                    == cts::BGLPerStageLimitClass::WriteOnlyStorageTexture,
                "write-only storage texture per-stage limit class");
        require(cts::bglEntryPerStageLimitClass("storageTexture_read-write")
                    == cts::BGLPerStageLimitClass::ReadWriteStorageTexture,
                "read-write storage texture per-stage limit class");
        const std::vector<std::string_view> sameStorageClass =
            cts::pickExtraBindingTypesForPerStage("buffer_storage", true);
        require(std::find(sameStorageClass.begin(), sameStorageClass.end(), "buffer_storage")
                    != sameStorageClass.end(),
                "same storage class includes storage buffer");
        require(std::find(sameStorageClass.begin(), sameStorageClass.end(), "buffer_read-only-storage")
                    != sameStorageClass.end(),
                "same storage class includes read-only-storage buffer");
        require(std::find(sameStorageClass.begin(), sameStorageClass.end(), "buffer_uniform")
                    == sameStorageClass.end(),
                "same storage class excludes uniform buffer");
        const std::vector<std::string_view> differentStorageClass =
            cts::pickExtraBindingTypesForPerStage("buffer_storage", false);
        require(differentStorageClass.size() == 1, "different storage class count");
        require(differentStorageClass[0].starts_with("sampler_"), "different storage class picks sampler");
        require(cts::kShaderStages.size() == 3, "individual shader stage count");
        require(cts::kShaderStages[0] == WGPUShaderStage_Vertex, "individual shader stage vertex");
        require(cts::kShaderStages[1] == WGPUShaderStage_Fragment, "individual shader stage fragment");
        require(cts::kShaderStages[2] == WGPUShaderStage_Compute, "individual shader stage compute");
        require(!cts::kUncompressedTextureFormats.empty(), "uncompressed texture formats non-empty");
        require(cts::kUncompressedTextureFormats.size() == 49, "uncompressed texture format count");
        require(cts::kRegularTextureFormats.size() == 43, "regular texture format count");
        require(cts::kCompressedTextureFormats.size() == 52, "compressed texture format count");
        require(cts::kAllTextureFormats.size() == 101, "all texture format count");
        require(cts::kColorRenderableTextureFormats.size() == 39, "color-renderable texture format count");
        require(cts::kStorageTextureFormats.size() == 22, "storage texture format count");
        require(cts::kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly.size() == 17,
                "tier1 storage texture format count");
        require(cts::kTextureAspects.size() == 3, "texture aspect count");
        require(cts::kTextureViewDimensions.size() == 6, "texture view dimension count");
        require(cts::getTextureDimensionFromView(WGPUTextureViewDimension_Cube) == WGPUTextureDimension_2D,
                "cube view dimension maps to 2d");
        require(cts::getTextureDimensionFromView(WGPUTextureViewDimension_2DArray) == WGPUTextureDimension_2D,
                "2d-array view dimension maps to 2d");
        require(cts::getTextureDimensionFromView(WGPUTextureViewDimension_1D) == WGPUTextureDimension_1D,
                "1d view dimension maps to 1d");
        require(cts::getTextureDimensionFromView(WGPUTextureViewDimension_3D) == WGPUTextureDimension_3D,
                "3d view dimension maps to 3d");
        require(cts::kLevels == 6, "texture createView range level count");
        const std::vector<WGPUTextureViewDimension> viewDimensions2D =
            cts::viewDimensionsForTextureDimension(WGPUTextureDimension_2D);
        require(viewDimensions2D.size() == 4, "2d texture view dimension count");
        require(viewDimensions2D[0] == WGPUTextureViewDimension_2D, "2d view dimension first");
        require(viewDimensions2D[1] == WGPUTextureViewDimension_2DArray, "2d-array view dimension second");
        require(viewDimensions2D[2] == WGPUTextureViewDimension_Cube, "cube view dimension third");
        require(viewDimensions2D[3] == WGPUTextureViewDimension_CubeArray, "cube-array view dimension fourth");
        require(cts::effectiveViewDimensionForDimension(WGPUTextureViewDimension_Undefined,
                                                        WGPUTextureDimension_2D, 6)
                    == WGPUTextureViewDimension_2DArray,
                "2d multilayer default view dimension");
        require(cts::effectiveViewDimensionForDimension(WGPUTextureViewDimension_Undefined,
                                                        WGPUTextureDimension_2D, 1)
                    == WGPUTextureViewDimension_2D,
                "2d single-layer default view dimension");
        require(cts::effectiveViewDimensionForDimension(WGPUTextureViewDimension_Undefined,
                                                        WGPUTextureDimension_1D, 1)
                    == WGPUTextureViewDimension_1D,
                "1d default view dimension");
        require(cts::effectiveViewDimensionForDimension(WGPUTextureViewDimension_Undefined,
                                                        WGPUTextureDimension_3D, 32)
                    == WGPUTextureViewDimension_3D,
                "3d default view dimension");
        WGPUTextureDescriptor rangeTextureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        rangeTextureDesc.size = WGPUExtent3D{32, 32, 6};
        rangeTextureDesc.dimension = WGPUTextureDimension_2D;
        rangeTextureDesc.mipLevelCount = 6;
        rangeTextureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        rangeTextureDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTextureViewDescriptor rangeViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        rangeViewDesc.dimension = WGPUTextureViewDimension_2D;
        rangeViewDesc.baseArrayLayer = 0;
        rangeViewDesc.arrayLayerCount = 1;
        rangeViewDesc.baseMipLevel = 0;
        rangeViewDesc.mipLevelCount = 6;
        require(cts::validateCreateViewLayersLevels(rangeTextureDesc, rangeViewDesc, true, true, true, true, true),
                "2d view one array layer valid");
        rangeViewDesc.arrayLayerCount = 2;
        require(!cts::validateCreateViewLayersLevels(rangeTextureDesc, rangeViewDesc, true, true, true, true, true),
                "2d view two array layers invalid");
        rangeViewDesc.dimension = WGPUTextureViewDimension_Cube;
        rangeViewDesc.arrayLayerCount = 6;
        require(cts::validateCreateViewLayersLevels(rangeTextureDesc, rangeViewDesc, true, true, true, true, true),
                "cube view six array layers valid");
        rangeViewDesc.arrayLayerCount = 3;
        require(!cts::validateCreateViewLayersLevels(rangeTextureDesc, rangeViewDesc, true, true, true, true, true),
                "cube view three array layers invalid");
        require(cts::isDepthTextureFormat(WGPUTextureFormat_Depth24Plus), "depth24plus depth format");
        require(cts::isStencilTextureFormat(WGPUTextureFormat_Stencil8), "stencil8 stencil format");
        require(cts::isDepthTextureFormat(WGPUTextureFormat_Depth24PlusStencil8),
                "depth24plus-stencil8 depth format");
        require(cts::isStencilTextureFormat(WGPUTextureFormat_Depth24PlusStencil8),
                "depth24plus-stencil8 stencil format");
        require(!cts::isDepthTextureFormat(WGPUTextureFormat_RGBA8Unorm), "rgba8unorm not depth format");
        require(!cts::isStencilTextureFormat(WGPUTextureFormat_RGBA8Unorm), "rgba8unorm not stencil format");
        require(!cts::isDepthTextureFormat(WGPUTextureFormat_BC1RGBAUnorm), "bc1 not depth format");
        require(!cts::isStencilTextureFormat(WGPUTextureFormat_BC1RGBAUnorm), "bc1 not stencil format");
        require(cts::isDepthOrStencilTextureFormat(WGPUTextureFormat_Depth24Plus),
                "depth24plus depth-or-stencil format");
        require(!cts::isDepthOrStencilTextureFormat(WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm not depth-or-stencil format");
        require(cts::isTextureFormatPossiblyUsableAsRenderAttachment(WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm possibly render attachment");
        require(cts::isTextureFormatPossiblyUsableAsRenderAttachment(WGPUTextureFormat_Depth24Plus),
                "depth24plus possibly render attachment");
        require(cts::isTextureFormatPossiblyUsableAsRenderAttachment(WGPUTextureFormat_RG11B10Ufloat),
                "rg11b10ufloat possibly render attachment");
        require(!cts::isTextureFormatPossiblyUsableAsRenderAttachment(WGPUTextureFormat_BC1RGBAUnorm),
                "bc1 not possibly render attachment");
        require(cts::isTextureFormatPossiblyUsableAsColorRenderAttachment(WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm possibly color render attachment");
        require(!cts::isTextureFormatPossiblyUsableAsColorRenderAttachment(WGPUTextureFormat_Depth24Plus),
                "depth24plus not possibly color render attachment");
        require(cts::isTextureFormatPossiblyUsableAsColorRenderAttachment(WGPUTextureFormat_RG11B10Ufloat),
                "rg11b10ufloat possibly color render attachment");
        require(!cts::isTextureFormatPossiblyUsableAsColorRenderAttachment(WGPUTextureFormat_BC1RGBAUnorm),
                "bc1 not possibly color render attachment");
        require(cts::isTextureFormatPossiblyStorageReadable(WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm possibly storage readable");
        require(!cts::isTextureFormatPossiblyStorageReadable(WGPUTextureFormat_Depth24Plus),
                "depth24plus not possibly storage readable");
        require(!cts::isTextureFormatPossiblyStorageReadable(WGPUTextureFormat_BC1RGBAUnorm),
                "bc1 not possibly storage readable");
        require(cts::baseFormat(WGPUTextureFormat_RGBA8UnormSrgb) == WGPUTextureFormat_RGBA8Unorm,
                "rgba8unorm-srgb base format");
        require(cts::baseFormat(WGPUTextureFormat_RGBA8Unorm) == WGPUTextureFormat_RGBA8Unorm,
                "rgba8unorm base format identity");
        require(cts::baseFormat(WGPUTextureFormat_RGBA8Snorm) == WGPUTextureFormat_RGBA8Snorm,
                "rgba8snorm base format identity");
        require(cts::textureFormatsAreViewCompatible(WGPUTextureFormat_RGBA8Unorm,
                                                     WGPUTextureFormat_RGBA8UnormSrgb),
                "rgba8unorm srgb view compatible");
        require(!cts::textureFormatsAreViewCompatible(WGPUTextureFormat_RGBA8Unorm,
                                                      WGPUTextureFormat_RGBA8Snorm),
                "rgba8unorm rgba8snorm not view compatible");
        require(cts::textureFormatsAreViewCompatible(WGPUTextureFormat_BC1RGBAUnorm,
                                                     WGPUTextureFormat_BC1RGBAUnormSrgb),
                "bc1 srgb view compatible");
        require(cts::kFeaturesForFormats.size() == 6, "format feature count");
        require(cts::filterFormatsByFeature(WGPUFeatureName_Force32).size() == 42,
                "no-feature texture format count");
        require(cts::filterFormatsByFeature(WGPUFeatureName_TextureCompressionBC).size() == 14,
                "bc feature texture format count");
        for (WGPUTextureFormat format : cts::kRegularTextureFormats) {
            const cts::TextureFormatInfo& info = cts::textureFormatInfo(format);
            require(info.formatClass == cts::TextureFormatClass::Uncompressed, "regular texture format class");
            require(!info.hasDepth, "regular texture format has no depth");
            require(!info.hasStencil, "regular texture format has no stencil");
        }
        require(cts::isColorTextureFormat(WGPUTextureFormat_RGBA8Unorm), "rgba8unorm color format");
        require(!cts::isColorTextureFormat(WGPUTextureFormat_Depth24Plus), "depth24plus not color format");
        require(cts::isColorTextureFormat(WGPUTextureFormat_BC1RGBAUnorm), "bc1 color format");
        require(containsRegularTextureFormat(WGPUTextureFormat_RGBA8Unorm), "regular contains rgba8unorm");
        require(!containsRegularTextureFormat(WGPUTextureFormat_Depth16Unorm), "regular excludes depth16unorm");
        require(!containsRegularTextureFormat(WGPUTextureFormat_BC1RGBAUnorm), "regular excludes bc1-rgba-unorm");
        require(containsTextureFormat(cts::kColorRenderableTextureFormats, WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm color-renderable");
        require(containsTextureFormat(cts::kStorageTextureFormats, WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm storage");
        require(containsTextureFormat(cts::kColorRenderableTextureFormats, WGPUTextureFormat_RGBA8UnormSrgb),
                "rgba8unorm-srgb color-renderable");
        require(!containsTextureFormat(cts::kStorageTextureFormats, WGPUTextureFormat_RGBA8UnormSrgb),
                "rgba8unorm-srgb not storage");
        require(!containsTextureFormat(cts::kColorRenderableTextureFormats, WGPUTextureFormat_R8Snorm),
                "r8snorm not base color-renderable");
        require(!containsTextureFormat(cts::kStorageTextureFormats, WGPUTextureFormat_R8Unorm),
                "r8unorm not base storage");
        require(containsTextureFormat(cts::kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly,
                                      WGPUTextureFormat_R8Unorm),
                "r8unorm tier1 storage");
        require(cts::kReadWriteStorageTextureFormats.size() == 3, "read-write storage format count");
        require(containsTextureFormat(cts::kReadWriteStorageTextureFormats, WGPUTextureFormat_R32Float),
                "read-write storage contains r32float");
        require(containsTextureFormat(cts::kReadWriteStorageTextureFormats, WGPUTextureFormat_R32Uint),
                "read-write storage contains r32uint");
        require(containsTextureFormat(cts::kReadWriteStorageTextureFormats, WGPUTextureFormat_R32Sint),
                "read-write storage contains r32sint");
        require(!containsTextureFormat(cts::kReadWriteStorageTextureFormats, WGPUTextureFormat_RGBA8Unorm),
                "read-write storage excludes rgba8unorm");
        require(containsTextureFormat(cts::kStorageTextureFormats, WGPUTextureFormat_RGBA8Unorm),
                "rgba8unorm read-only storage base usable");
        require(!containsTextureFormat(cts::kStorageTextureFormats, WGPUTextureFormat_BGRA8Unorm),
                "bgra8unorm read-only storage base unusable");
        require(!containsTextureFormat(cts::kStorageTextureFormats, WGPUTextureFormat_BC1RGBAUnorm),
                "bc1 read-only storage base unusable");
        size_t multisampleCount = 0;
        for (WGPUTextureFormat format : cts::kAllTextureFormats) {
            if (cts::textureFormatInfo(format).multisample) {
                ++multisampleCount;
            }
        }
        require(multisampleCount == 37, "texture multisample true count");
        require(cts::kTextureFormatTier1AllowsRenderAttachmentBlendableMultisample.size() == 10,
                "tier1 blendable multisample count");
        const cts::TextureBlockInfo rgba8 = cts::getBlockInfoForTextureFormat(WGPUTextureFormat_RGBA8Unorm);
        require(rgba8.blockWidth == 1, "rgba8unorm block width");
        require(rgba8.blockHeight == 1, "rgba8unorm block height");
        require(rgba8.bytesPerBlock == 4, "rgba8unorm bytes per block");
        const cts::TextureBlockInfo bc1 = cts::getBlockInfoForTextureFormat(WGPUTextureFormat_BC1RGBAUnorm);
        require(bc1.blockWidth == 4 && bc1.blockHeight == 4 && bc1.bytesPerBlock == 8, "bc1 block info");
        const cts::TextureBlockInfo eac = cts::getBlockInfoForTextureFormat(WGPUTextureFormat_EACRG11Unorm);
        require(eac.blockWidth == 4 && eac.blockHeight == 4 && eac.bytesPerBlock == 16, "eac-rg11 block info");
        const cts::TextureBlockInfo astc = cts::getBlockInfoForTextureFormat(WGPUTextureFormat_ASTC12x12Unorm);
        require(astc.blockWidth == 12 && astc.blockHeight == 12 && astc.bytesPerBlock == 16, "astc-12x12 block info");
        require(cts::kCompressedTextureSizeVariants.size() == 28, "compressed texture size variant count");
        require(cts::roundDown(8192, 4) == 8192, "roundDown exact multiple");
        require(cts::roundDown(8191, 4) == 8188, "roundDown below multiple");
        require(cts::roundDown(100, 12) == 96, "roundDown larger multiple");
        WGPULimits syntheticLimits = WGPU_LIMITS_INIT;
        syntheticLimits.maxTextureDimension1D = 4096;
        syntheticLimits.maxTextureDimension2D = 8192;
        syntheticLimits.maxTextureDimension3D = 2048;
        syntheticLimits.maxTextureArrayLayers = 256;
        const auto astc2dMax = cts::getMaxValidTextureSizeForFormatAndDimension(
            syntheticLimits, WGPUTextureFormat_ASTC12x12Unorm, WGPUTextureDimension_2D);
        require(astc2dMax[0] == 8184 && astc2dMax[1] == 8184 && astc2dMax[2] == 256,
                "astc 2d max valid texture size");
        const auto bc3dMax = cts::getMaxValidTextureSizeForFormatAndDimension(
            syntheticLimits, WGPUTextureFormat_BC1RGBAUnorm, WGPUTextureDimension_3D);
        require(bc3dMax[0] == 2048 && bc3dMax[1] == 2048 && bc3dMax[2] == 2048,
                "bc1 3d max valid texture size");
        require(cts::isBCTextureFormat(WGPUTextureFormat_BC7RGBAUnorm), "bc predicate");
        require(cts::isASTCTextureFormat(WGPUTextureFormat_ASTC4x4Unorm), "astc predicate");
        require(!cts::isCompressedTextureFormat(WGPUTextureFormat_RGBA8Unorm), "compressed predicate false");
        require(cts::maxMipLevelCount(WGPUExtent3D{32, 32, 1}, WGPUTextureDimension_2D) == 6,
                "2d max mip level count");
        require(cts::maxMipLevelCount(WGPUExtent3D{31, 1, 1}, WGPUTextureDimension_1D) == 1,
                "1d max mip level count");
        require(cts::maxMipLevelCount(WGPUExtent3D{32, 32, 64}, WGPUTextureDimension_3D) == 7,
                "3d max mip level count");
        require(cts::textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension_3D,
                                                                 WGPUTextureFormat_BC1RGBAUnorm),
                "3d bc possible compatibility");
        require(!cts::textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension_1D,
                                                                  WGPUTextureFormat_BC1RGBAUnorm),
                "1d bc possible compatibility");
        require(!cts::textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension_1D,
                                                                  WGPUTextureFormat_Depth16Unorm),
                "1d depth possible compatibility");

        cts::Fixture fixture;
        fixture.setParams({{"x", cts::Value::undef()}, {"y", 1}});
        require(fixture.paramIsUndefined("x"), "paramIsUndefined true");
        require(!fixture.paramIsUndefined("y"), "paramIsUndefined false");
        require(!fixture.paramIsUndefined("missing"), "paramIsUndefined missing");

        cts::Query query = cts::parseQuery("webgpu:api,validation,buffer,create:limit:sizeAddition=0");
        cts::ParamRecord params{{"sizeAddition", cts::Value(0)}};
        require(cts::queryMatchesCase(query, "api,validation,buffer,create", "limit", params), "single-case query");
        require(cts::caseQuery("api,validation,buffer,create", "limit", params) ==
                    "webgpu:api,validation,buffer,create:limit:sizeAddition=0",
                "case query stringify");

        auto failures = cts::runSyntheticFailureForSelfTest();
        require(failures.size() == 1, "synthetic failure result count");
        require(failures[0].status == cts::TestStatus::Fail, "synthetic failure status");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
