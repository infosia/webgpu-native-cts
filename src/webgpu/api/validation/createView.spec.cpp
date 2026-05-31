// Ported from gpuweb/cts src/webgpu/api/validation/createView.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,createView",
    "createView validation tests.");

template <std::size_t N>
std::vector<Value> textureFormatValues(const std::array<WGPUTextureFormat, N>& formats) {
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(static_cast<int64_t>(format));
    }
    return values;
}

std::vector<Value> allTextureFormatValues() {
    return textureFormatValues(kAllTextureFormats);
}

std::vector<Value> textureAspectValues() {
    std::vector<Value> values;
    values.reserve(kTextureAspects.size());
    for (WGPUTextureAspect aspect : kTextureAspects) {
        values.emplace_back(static_cast<int64_t>(aspect));
    }
    return values;
}

std::vector<Value> textureFormatFeatureValues() {
    std::vector<Value> values;
    values.reserve(kFeaturesForFormats.size());
    for (WGPUFeatureName feature : kFeaturesForFormats) {
        if (feature == WGPUFeatureName_Force32) {
            values.push_back(Value::undef());
        } else {
            values.emplace_back(static_cast<int64_t>(feature));
        }
    }
    return values;
}

WGPUFeatureName textureFormatFeatureFromValue(const Value& value) {
    if (std::holds_alternative<Value::Undefined>(value.data())) {
        return WGPUFeatureName_Force32;
    }
    return static_cast<WGPUFeatureName>(valueAs<int64_t>(value));
}

std::vector<Value> textureFormatsForFeatureParam(const ParamRecord& params, std::string_view key) {
    const WGPUFeatureName feature = textureFormatFeatureFromValue(*findParam(params, key));
    const std::vector<WGPUTextureFormat> formats = filterFormatsByFeature(feature);
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(static_cast<int64_t>(format));
    }
    return values;
}

std::vector<Value> viewFormatsForFeatureParam(const ParamRecord& params) {
    std::vector<Value> values;
    values.push_back(Value::undef());
    std::vector<Value> formats = textureFormatsForFeatureParam(params, "viewFormatFeature");
    values.insert(values.end(), formats.begin(), formats.end());
    return values;
}

CTS_TEST(g, "aspect")
    .desc("Test texture view aspect validation.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormatValues())
            .combine("aspect", textureAspectValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const WGPUTextureAspect aspect = static_cast<WGPUTextureAspect>(t.param<int64_t>("aspect"));

        t.skipIfTextureFormatNotSupported(format);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size.width = info.blockWidth;
        textureDesc.size.height = info.blockHeight;
        textureDesc.size.depthOrArrayLayers = 1;
        textureDesc.format = format;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        const bool success = aspect == WGPUTextureAspect_All
            || (aspect == WGPUTextureAspect_DepthOnly && isDepthTextureFormat(format))
            || (aspect == WGPUTextureAspect_StencilOnly && isStencilTextureFormat(format));

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.aspect = aspect;

        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, !success);
    });

CTS_TEST(g, "format")
    .desc("Test texture view format compatibility validation.")
    .params([](ParamsBuilder u) {
        const std::vector<Value> features = textureFormatFeatureValues();
        return u.combine("textureFormatFeature", features)
            .combine("viewFormatFeature", features)
            .beginSubcases()
            .expand("textureFormat", [](const ParamRecord& params) {
                return textureFormatsForFeatureParam(params, "textureFormatFeature");
            })
            .expand("viewFormat", [](const ParamRecord& params) {
                return viewFormatsForFeatureParam(params);
            })
            .combine("useViewFormatList", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat textureFormat = static_cast<WGPUTextureFormat>(t.param<int64_t>("textureFormat"));
        const bool viewFormatIsUndefined = t.paramIsUndefined("viewFormat");
        const WGPUTextureFormat viewFormat = viewFormatIsUndefined
            ? WGPUTextureFormat_Undefined
            : static_cast<WGPUTextureFormat>(t.param<int64_t>("viewFormat"));
        const bool useViewFormatList = t.param<bool>("useViewFormatList");

        t.skipIfTextureFormatNotSupported(textureFormat);
        if (!viewFormatIsUndefined) {
            t.skipIfTextureFormatNotSupported(viewFormat);
        }

        const TextureBlockInfo info = getBlockInfoForTextureFormat(textureFormat);
        const bool compatible = viewFormatIsUndefined
            || textureFormatsAreViewCompatible(textureFormat, viewFormat);

        const WGPUTextureFormat listedViewFormats[] = {viewFormat};

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size.width = info.blockWidth;
        textureDesc.size.height = info.blockHeight;
        textureDesc.size.depthOrArrayLayers = 1;
        textureDesc.format = textureFormat;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        if (useViewFormatList && compatible && !viewFormatIsUndefined) {
            textureDesc.viewFormatCount = 1;
            textureDesc.viewFormats = listedViewFormats;
        }
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        const bool success = viewFormatIsUndefined
            || viewFormat == textureFormat
            || (compatible && useViewFormatList);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.format = viewFormat;

        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, !success);
    });

} // namespace
