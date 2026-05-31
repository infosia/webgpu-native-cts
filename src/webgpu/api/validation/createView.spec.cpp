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

ParamsBuilder combineTextureAndViewDimensions(ParamsBuilder u) {
    return u.combine("textureDimension", textureDimensionValues())
        .expand("viewDimension", [](const ParamRecord& params) {
            const WGPUTextureDimension textureDimension =
                static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "textureDimension")));
            const std::vector<WGPUTextureViewDimension> dimensions =
                viewDimensionsForTextureDimension(textureDimension);

            std::vector<Value> values;
            values.reserve(dimensions.size() + 1);
            values.push_back(Value::undef());
            for (WGPUTextureViewDimension dimension : dimensions) {
                values.emplace_back(static_cast<int64_t>(dimension));
            }
            return values;
        });
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

bool isUndefinedValue(const Value& value) {
    return std::holds_alternative<Value::Undefined>(value.data());
}

uint32_t uint32Param(const ParamRecord& params, std::string_view key) {
    return static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, key)));
}

bool paramRecordValueIsUndefined(const ParamRecord& params, std::string_view key) {
    return isUndefinedValue(*findParam(params, key));
}

std::vector<uint32_t> dedupedBoundaryValues(uint32_t limit) {
    const std::array<uint32_t, 8> candidates = {0, 1, 5, 6, 7, limit - 1, limit, limit + 1};
    std::vector<uint32_t> values;
    for (uint32_t candidate : candidates) {
        if (std::find(values.begin(), values.end(), candidate) == values.end()) {
            values.push_back(candidate);
        }
    }
    return values;
}

std::vector<Value> optionalBoundaryValues(uint32_t limit) {
    std::vector<Value> values;
    values.push_back(Value::undef());
    for (uint32_t value : dedupedBoundaryValues(limit)) {
        values.emplace_back(static_cast<uint64_t>(value));
    }
    return values;
}

std::vector<Value> layerCountValues(const ParamRecord& params) {
    const uint32_t textureLayers = uint32Param(params, "textureLayers");
    const uint32_t baseArrayLayer = paramRecordValueIsUndefined(params, "baseArrayLayer")
        ? 0
        : uint32Param(params, "baseArrayLayer");

    std::vector<Value> values;
    values.push_back(Value::undef());
    for (uint32_t last : dedupedBoundaryValues(textureLayers)) {
        if (baseArrayLayer <= last) {
            values.emplace_back(static_cast<uint64_t>(last - baseArrayLayer));
        }
    }
    return values;
}

std::vector<Value> mipLevelCountValues(const ParamRecord& params) {
    const uint32_t textureLevels = uint32Param(params, "textureLevels");
    const uint32_t baseMipLevel = paramRecordValueIsUndefined(params, "baseMipLevel")
        ? 0
        : uint32Param(params, "baseMipLevel");

    std::vector<Value> values;
    values.push_back(Value::undef());
    for (uint32_t last : dedupedBoundaryValues(textureLevels)) {
        if (baseMipLevel <= last) {
            values.emplace_back(static_cast<uint64_t>(last - baseMipLevel));
        }
    }
    return values;
}

WGPUTextureViewDimension viewDimensionParam(AllFeaturesMaxLimitsGpuTest& t) {
    return t.paramIsUndefined("viewDimension")
        ? WGPUTextureViewDimension_Undefined
        : static_cast<WGPUTextureViewDimension>(t.param<int64_t>("viewDimension"));
}

void setViewDimensionIfGiven(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureViewDescriptor& viewDesc) {
    if (!t.paramIsUndefined("viewDimension")) {
        viewDesc.dimension = static_cast<WGPUTextureViewDimension>(t.param<int64_t>("viewDimension"));
    }
}

void setUint32FieldIfGiven(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureViewDescriptor& viewDesc,
    std::string_view key) {
    if (t.paramIsUndefined(key)) {
        return;
    }

    const uint32_t value = static_cast<uint32_t>(t.param<int64_t>(key));
    if (key == "baseArrayLayer") {
        viewDesc.baseArrayLayer = value;
    } else if (key == "arrayLayerCount") {
        viewDesc.arrayLayerCount = value;
    } else if (key == "baseMipLevel") {
        viewDesc.baseMipLevel = value;
    } else if (key == "mipLevelCount") {
        viewDesc.mipLevelCount = value;
    } else {
        std::abort();
    }
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

CTS_TEST(g, "array_layers")
    .desc("Test texture view array layer range validation.")
    .params([](ParamsBuilder u) {
        return combineTextureAndViewDimensions(u)
            .beginSubcases()
            .expand("textureLayers", [](const ParamRecord& params) {
                const WGPUTextureDimension textureDimension =
                    static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "textureDimension")));
                if (textureDimension == WGPUTextureDimension_2D) {
                    return std::vector<Value>{1, 6, 18};
                }
                return std::vector<Value>{1};
            })
            .combine("textureLevels", {1, static_cast<int64_t>(kLevels)})
            .filter([](const ParamRecord& params) {
                const WGPUTextureDimension textureDimension =
                    static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "textureDimension")));
                const uint32_t textureLevels = uint32Param(params, "textureLevels");
                return !(textureDimension == WGPUTextureDimension_1D && textureLevels != 1);
            })
            .expand("baseArrayLayer", [](const ParamRecord& params) {
                return optionalBoundaryValues(uint32Param(params, "textureLayers"));
            })
            .expand("arrayLayerCount", [](const ParamRecord& params) {
                return layerCountValues(params);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension textureDimension =
            static_cast<WGPUTextureDimension>(t.param<int64_t>("textureDimension"));
        const WGPUTextureViewDimension viewDimension = viewDimensionParam(t);
        const uint32_t textureLayers = static_cast<uint32_t>(t.param<int64_t>("textureLayers"));
        const uint32_t textureLevels = static_cast<uint32_t>(t.param<int64_t>("textureLevels"));

        if (viewDimension != WGPUTextureViewDimension_Undefined) {
            t.skipIfTextureViewDimensionNotSupported(viewDimension);
        }

        const uint32_t kWidth = 1u << (kLevels - 1);
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        if (textureDimension == WGPUTextureDimension_1D) {
            textureDesc.size = WGPUExtent3D{kWidth, 1, 1};
        } else if (textureDimension == WGPUTextureDimension_2D) {
            textureDesc.size = WGPUExtent3D{kWidth, kWidth, textureLayers};
        } else {
            textureDesc.size = WGPUExtent3D{kWidth, kWidth, kWidth};
        }
        textureDesc.dimension = textureDimension;
        textureDesc.mipLevelCount = textureLevels;
        textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        setViewDimensionIfGiven(t, viewDesc);
        setUint32FieldIfGiven(t, viewDesc, "baseArrayLayer");
        setUint32FieldIfGiven(t, viewDesc, "arrayLayerCount");

        const bool success = validateCreateViewLayersLevels(
            textureDesc,
            viewDesc,
            !t.paramIsUndefined("viewDimension"),
            false,
            false,
            !t.paramIsUndefined("baseArrayLayer"),
            !t.paramIsUndefined("arrayLayerCount"));

        WGPUTexture texture = t.createTextureTracked(textureDesc);
        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, !success);
    });

CTS_TEST(g, "mip_levels")
    .desc("Test texture view mip level range validation.")
    .params([](ParamsBuilder u) {
        return combineTextureAndViewDimensions(u)
            .beginSubcases()
            .combine("textureLevels", {1, static_cast<int64_t>(kLevels - 2), static_cast<int64_t>(kLevels)})
            .filter([](const ParamRecord& params) {
                const WGPUTextureDimension textureDimension =
                    static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "textureDimension")));
                const uint32_t textureLevels = uint32Param(params, "textureLevels");
                return !(textureDimension == WGPUTextureDimension_1D && textureLevels != 1);
            })
            .expand("baseMipLevel", [](const ParamRecord& params) {
                return optionalBoundaryValues(uint32Param(params, "textureLevels"));
            })
            .expand("mipLevelCount", [](const ParamRecord& params) {
                return mipLevelCountValues(params);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension textureDimension =
            static_cast<WGPUTextureDimension>(t.param<int64_t>("textureDimension"));
        const WGPUTextureViewDimension viewDimension = viewDimensionParam(t);
        const uint32_t textureLevels = static_cast<uint32_t>(t.param<int64_t>("textureLevels"));

        if (viewDimension != WGPUTextureViewDimension_Undefined) {
            t.skipIfTextureViewDimensionNotSupported(viewDimension);
        }

        const uint32_t kWidth = 1u << (kLevels - 1);
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        if (textureDimension == WGPUTextureDimension_1D) {
            textureDesc.size = WGPUExtent3D{kWidth, 1, 1};
        } else if (textureDimension == WGPUTextureDimension_3D) {
            textureDesc.size = WGPUExtent3D{kWidth, kWidth, kWidth};
        } else {
            textureDesc.size = WGPUExtent3D{kWidth, kWidth, 18};
        }
        textureDesc.dimension = textureDimension;
        textureDesc.mipLevelCount = textureLevels;
        textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        setViewDimensionIfGiven(t, viewDesc);
        setUint32FieldIfGiven(t, viewDesc, "baseMipLevel");
        setUint32FieldIfGiven(t, viewDesc, "mipLevelCount");

        const bool success = validateCreateViewLayersLevels(
            textureDesc,
            viewDesc,
            !t.paramIsUndefined("viewDimension"),
            !t.paramIsUndefined("baseMipLevel"),
            !t.paramIsUndefined("mipLevelCount"),
            false,
            false);

        WGPUTexture texture = t.createTextureTracked(textureDesc);
        t.expectValidationError([&] {
            t.createViewTracked(texture, viewDesc);
        }, !success);
    });

} // namespace
