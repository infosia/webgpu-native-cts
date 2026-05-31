#include <algorithm>
#include <iostream>
#include <cstddef>
#include <string>
#include <vector>

#include "common/query.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"

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
