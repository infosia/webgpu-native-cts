#include <iostream>
#include <cstddef>
#include <string>

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
        require(!cts::kUncompressedTextureFormats.empty(), "uncompressed texture formats non-empty");
        require(cts::kUncompressedTextureFormats.size() == 49, "uncompressed texture format count");
        require(cts::kRegularTextureFormats.size() == 43, "regular texture format count");
        require(cts::kCompressedTextureFormats.size() == 52, "compressed texture format count");
        require(cts::kAllTextureFormats.size() == 101, "all texture format count");
        for (WGPUTextureFormat format : cts::kRegularTextureFormats) {
            const cts::TextureFormatInfo& info = cts::textureFormatInfo(format);
            require(info.formatClass == cts::TextureFormatClass::Uncompressed, "regular texture format class");
            require(!info.hasDepth, "regular texture format has no depth");
            require(!info.hasStencil, "regular texture format has no stencil");
        }
        require(containsRegularTextureFormat(WGPUTextureFormat_RGBA8Unorm), "regular contains rgba8unorm");
        require(!containsRegularTextureFormat(WGPUTextureFormat_Depth16Unorm), "regular excludes depth16unorm");
        require(!containsRegularTextureFormat(WGPUTextureFormat_BC1RGBAUnorm), "regular excludes bc1-rgba-unorm");
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
