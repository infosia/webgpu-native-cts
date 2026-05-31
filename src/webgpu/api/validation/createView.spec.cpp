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

std::vector<Value> textureDimensionValues() {
    std::vector<Value> values;
    values.reserve(kTextureDimensions.size());
    for (WGPUTextureDimension dimension : kTextureDimensions) {
        values.emplace_back(static_cast<int64_t>(dimension));
    }
    return values;
}

std::vector<Value> textureViewDimensionValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureViewDimensions.size() + 1);
    for (WGPUTextureViewDimension dimension : kTextureViewDimensions) {
        values.emplace_back(static_cast<int64_t>(dimension));
    }
    values.push_back(Value::undef());
    return values;
}

std::vector<Value> textureViewCubeDimensionValues() {
    return {
        Value(static_cast<int64_t>(WGPUTextureViewDimension_2D)),
        Value(static_cast<int64_t>(WGPUTextureViewDimension_Cube)),
        Value(static_cast<int64_t>(WGPUTextureViewDimension_CubeArray)),
    };
}

std::vector<Value> resourceStateValues() {
    std::vector<Value> values;
    values.reserve(kResourceStates.size());
    for (ResourceState state : kResourceStates) {
        values.emplace_back(static_cast<int64_t>(state));
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

CTS_TEST(g, "dimension")
    .desc("Test texture view dimension compatibility validation.")
    .params([](ParamsBuilder u) {
        return u.combine("textureDimension", textureDimensionValues())
            .combine("viewDimension", textureViewDimensionValuesWithUndefined());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension textureDimension =
            static_cast<WGPUTextureDimension>(t.param<int64_t>("textureDimension"));
        const bool viewDimensionIsUndefined = t.paramIsUndefined("viewDimension");
        const WGPUTextureViewDimension viewDimension = viewDimensionIsUndefined
            ? WGPUTextureViewDimension_Undefined
            : static_cast<WGPUTextureViewDimension>(t.param<int64_t>("viewDimension"));

        if (!viewDimensionIsUndefined) {
            t.skipIfTextureViewDimensionNotSupported(viewDimension);
        }

        const WGPUExtent3D size = textureDimension == WGPUTextureDimension_1D
            ? WGPUExtent3D{4, 1, 1}
            : WGPUExtent3D{4, 4, 6};

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size = size;
        textureDesc.dimension = textureDimension;
        textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        const bool success = viewDimensionIsUndefined
            || getTextureDimensionFromView(viewDimension) == textureDimension;

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = viewDimension;

        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, !success);
    });

CTS_TEST(g, "cube_faces_square")
    .desc("Test cube texture views require square faces.")
    .params([](ParamsBuilder u) {
        return u.combine("viewDimension", textureViewCubeDimensionValues())
            .combineWithParams({
                ParamRecord{{"w", 4}, {"h", 4}, {"d", 6}},
                ParamRecord{{"w", 5}, {"h", 5}, {"d", 6}},
                ParamRecord{{"w", 4}, {"h", 5}, {"d", 6}},
                ParamRecord{{"w", 4}, {"h", 8}, {"d", 6}},
                ParamRecord{{"w", 8}, {"h", 4}, {"d", 6}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureViewDimension viewDimension =
            static_cast<WGPUTextureViewDimension>(t.param<int64_t>("viewDimension"));
        const uint32_t width = static_cast<uint32_t>(t.param<int>("w"));
        const uint32_t height = static_cast<uint32_t>(t.param<int>("h"));
        const uint32_t depthOrArrayLayers = static_cast<uint32_t>(t.param<int>("d"));

        t.skipIfTextureViewDimensionNotSupported(viewDimension);

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size.width = width;
        textureDesc.size.height = height;
        textureDesc.size.depthOrArrayLayers = depthOrArrayLayers;
        textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        const bool success = viewDimension == WGPUTextureViewDimension_2D || width == height;

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = viewDimension;

        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, !success);
    });

CTS_TEST(g, "texture_state")
    .desc("Test createView validation for texture state.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("state", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state = static_cast<ResourceState>(t.param<int64_t>("state"));

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size.width = 1;
        textureDesc.size.height = 1;
        textureDesc.size.depthOrArrayLayers = 1;
        textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureWithState(state, textureDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, state == ResourceState::Invalid);
    });

} // namespace
