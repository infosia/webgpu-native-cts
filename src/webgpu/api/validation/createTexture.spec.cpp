// Ported from gpuweb/cts src/webgpu/api/validation/createTexture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,createTexture",
    "createTexture validation tests.");

std::vector<Value> textureDimensionValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureDimensions.size() + 1);
    values.push_back(Value::undef());
    for (WGPUTextureDimension dimension : kTextureDimensions) {
        values.emplace_back(static_cast<int64_t>(dimension));
    }
    return values;
}

std::vector<Value> allTextureFormatValues() {
    std::vector<Value> values;
    values.reserve(kAllTextureFormats.size());
    for (WGPUTextureFormat format : kAllTextureFormats) {
        values.emplace_back(static_cast<int64_t>(format));
    }
    return values;
}

std::vector<Value> sampleCountValues() {
    return {
        Value(0),
        Value(1),
        Value(2),
        Value(4),
        Value(8),
        Value(16),
        Value(32),
        Value(256),
    };
}

bool valueIsUndefined(const Value& value) {
    return std::holds_alternative<Value::Undefined>(value.data());
}

WGPUTextureDimension dimensionFromValue(const Value& value) {
    if (valueIsUndefined(value)) {
        return WGPUTextureDimension_Undefined;
    }
    return static_cast<WGPUTextureDimension>(valueAs<int64_t>(value));
}

WGPUTextureDimension dimensionFromParams(const ParamRecord& params) {
    return dimensionFromValue(*findParam(params, "dimension"));
}

WGPUTextureDimension dimensionParam(AllFeaturesMaxLimitsGpuTest& t) {
    return t.paramIsUndefined("dimension") ? WGPUTextureDimension_Undefined
                                           : static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
}

std::vector<Value> mipLevelCountValues() {
    return {
        Value(1),
        Value(2),
        Value(3),
        Value(6),
        Value(7),
    };
}

CTS_TEST(g, "sample_count,1d_2d_array_3d")
    .desc("Test that 1d, 2d array, and 3d multisampled textures are invalid.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"dimension", static_cast<int64_t>(WGPUTextureDimension_2D)}, {"w", 4}, {"h", 4}, {"d", 1}, {"shouldError", false}},
            ParamRecord{{"dimension", static_cast<int64_t>(WGPUTextureDimension_1D)}, {"w", 4}, {"h", 1}, {"d", 1}, {"shouldError", true}},
            ParamRecord{{"dimension", static_cast<int64_t>(WGPUTextureDimension_2D)}, {"w", 4}, {"h", 4}, {"d", 4}, {"shouldError", true}},
            ParamRecord{{"dimension", static_cast<int64_t>(WGPUTextureDimension_2D)}, {"w", 4}, {"h", 4}, {"d", 6}, {"shouldError", true}},
            ParamRecord{{"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}, {"w", 4}, {"h", 4}, {"d", 4}, {"shouldError", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = static_cast<uint32_t>(t.param<int>("w"));
        desc.size.height = static_cast<uint32_t>(t.param<int>("h"));
        desc.size.depthOrArrayLayers = static_cast<uint32_t>(t.param<int>("d"));
        desc.dimension = static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
        desc.sampleCount = 4;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;

        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, t.param<bool>("shouldError"));
    });

CTS_TEST(g, "dimension_type_and_format_compatibility")
    .desc("Test every dimension type on every texture format.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", textureDimensionValuesWithUndefined())
            .combine("format", allTextureFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = t.paramIsUndefined("dimension")
            ? WGPUTextureDimension_Undefined
            : static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));

        t.skipIfTextureFormatNotSupported(format);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = info.blockWidth;
        desc.size.height = info.blockHeight;
        desc.size.depthOrArrayLayers = 1;
        desc.dimension = dimension;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !t.textureDimensionAndFormatCompatibleForDevice(dimension, format));
    });

CTS_TEST(g, "sampleCount,various_sampleCount_with_all_formats")
    .desc("Test texture creation with various sample counts on all texture formats.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", {Value::undef(), Value(static_cast<int64_t>(WGPUTextureDimension_2D))})
            .combine("format", allTextureFormatValues())
            .beginSubcases()
            .combine("sampleCount", sampleCountValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = t.paramIsUndefined("dimension")
            ? WGPUTextureDimension_Undefined
            : static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));

        t.skipIfTextureFormatNotSupported(format);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = 32 * info.blockWidth;
        desc.size.height = 32 * info.blockHeight;
        desc.size.depthOrArrayLayers = 1;
        desc.dimension = dimension;
        desc.sampleCount = sampleCount;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;
        if (sampleCount > 1) {
            desc.usage |= WGPUTextureUsage_RenderAttachment;
        }

        const bool success = sampleCount == 1 || (sampleCount == 4 && t.isTextureFormatMultisampled(format));
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

CTS_TEST(g, "zero_size_and_usage")
    .desc("Test that zero-sized texture descriptors and zero usage are invalid.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", textureDimensionValuesWithUndefined())
            .combine("format",
                     {Value(static_cast<int64_t>(WGPUTextureFormat_RGBA8Unorm)),
                      Value(static_cast<int64_t>(WGPUTextureFormat_RGB10A2Unorm)),
                      Value(static_cast<int64_t>(WGPUTextureFormat_BC1RGBAUnorm)),
                      Value(static_cast<int64_t>(WGPUTextureFormat_Depth24PlusStencil8))})
            .filter([](const ParamRecord& params) {
                const WGPUTextureDimension dimension = dimensionFromParams(params);
                const auto format = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "format")));
                return textureFormatAndDimensionPossiblyCompatible(dimension, format);
            })
            .beginSubcases()
            .combine("zeroArgument",
                     {Value("none"),
                      Value("width"),
                      Value("height"),
                      Value("depthOrArrayLayers"),
                      Value("mipLevelCount"),
                      Value("usage")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = dimensionParam(t);
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const std::string zeroArgument = t.param<std::string>("zeroArgument");

        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = info.blockWidth;
        desc.size.height = info.blockHeight;
        desc.size.depthOrArrayLayers = 1;
        desc.dimension = dimension;
        desc.mipLevelCount = 1;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        if (zeroArgument == "width") {
            desc.size.width = 0;
        } else if (zeroArgument == "height") {
            desc.size.height = 0;
        } else if (zeroArgument == "depthOrArrayLayers") {
            desc.size.depthOrArrayLayers = 0;
        } else if (zeroArgument == "mipLevelCount") {
            desc.mipLevelCount = 0;
        } else if (zeroArgument == "usage") {
            desc.usage = WGPUTextureUsage_None;
        }

        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, zeroArgument != "none");
    });

CTS_TEST(g, "mipLevelCount,format")
    .desc("Test mipLevelCount validation over all texture formats.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", textureDimensionValuesWithUndefined())
            .combine("format", allTextureFormatValues())
            .beginSubcases()
            .combine("mipLevelCount", mipLevelCountValues())
            .filter([](const ParamRecord& params) {
                const WGPUTextureDimension dimension = dimensionFromParams(params);
                const auto format = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "format")));
                return textureFormatAndDimensionPossiblyCompatible(dimension, format);
            })
            .combine("largestDimension", {Value(0), Value(1), Value(2)})
            .filter([](const ParamRecord& params) {
                const WGPUTextureDimension dimension = dimensionFromParams(params);
                const int largestDimension = valueAs<int>(*findParam(params, "largestDimension"));
                return !(dimension == WGPUTextureDimension_1D && largestDimension > 0);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = dimensionParam(t);
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const uint32_t mipLevelCount = static_cast<uint32_t>(t.param<int>("mipLevelCount"));
        const int largestDimension = t.param<int>("largestDimension");

        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);

        constexpr uint32_t kTargetLargeSize = 31;
        const WGPUExtent3D largeSize = {
            (kTargetLargeSize / info.blockWidth) * info.blockWidth,
            (kTargetLargeSize / info.blockHeight) * info.blockHeight,
            kTargetLargeSize,
        };

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = info.blockWidth;
        desc.size.height = info.blockHeight;
        desc.size.depthOrArrayLayers = 1;
        if (largestDimension == 0) {
            desc.size.width = largeSize.width;
        } else if (largestDimension == 1) {
            desc.size.height = largeSize.height;
        } else {
            desc.size.depthOrArrayLayers = largeSize.depthOrArrayLayers;
        }
        desc.dimension = dimension;
        desc.mipLevelCount = mipLevelCount;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        const bool success = mipLevelCount <= maxMipLevelCount(desc.size, dimension);
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

CTS_TEST(g, "mipLevelCount,bound_check")
    .desc("Test mipLevelCount boundary validation.")
    .params([](ParamsBuilder u) {
        return u.combine("format",
                         {Value(static_cast<int64_t>(WGPUTextureFormat_RGBA8Unorm)),
                          Value(static_cast<int64_t>(WGPUTextureFormat_BC1RGBAUnorm))})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"w", 32}, {"h", 32}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 31}, {"h", 32}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 28}, {"h", 32}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 32}, {"h", 31}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 32}, {"h", 28}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 31}, {"h", 31}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 32}, {"h", 1}, {"d", 1}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_1D)}},
                ParamRecord{{"w", 31}, {"h", 1}, {"d", 1}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_1D)}},
                ParamRecord{{"w", 32}, {"h", 32}, {"d", 32}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}},
                ParamRecord{{"w", 32}, {"h", 31}, {"d", 31}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}},
                ParamRecord{{"w", 31}, {"h", 32}, {"d", 31}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}},
                ParamRecord{{"w", 31}, {"h", 31}, {"d", 32}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}},
                ParamRecord{{"w", 31}, {"h", 31}, {"d", 31}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}},
                ParamRecord{{"w", 32}, {"h", 8}, {"d", 1}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 32}, {"h", 32}, {"d", 64}, {"dimension", Value::undef()}},
                ParamRecord{{"w", 32}, {"h", 32}, {"d", 64}, {"dimension", static_cast<int64_t>(WGPUTextureDimension_3D)}},
            })
            .filter([](const ParamRecord& params) {
                const auto format = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "format")));
                if (format != WGPUTextureFormat_BC1RGBAUnorm) {
                    return true;
                }
                const WGPUTextureDimension dimension = dimensionFromParams(params);
                const int width = valueAs<int>(*findParam(params, "w"));
                const int height = valueAs<int>(*findParam(params, "h"));
                const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
                return !(dimension == WGPUTextureDimension_1D || dimension == WGPUTextureDimension_3D
                         || width % static_cast<int>(info.blockWidth) != 0
                         || height % static_cast<int>(info.blockHeight) != 0);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const WGPUTextureDimension dimension = dimensionParam(t);

        t.skipIfTextureFormatNotSupported(format);

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = static_cast<uint32_t>(t.param<int>("w"));
        desc.size.height = static_cast<uint32_t>(t.param<int>("h"));
        desc.size.depthOrArrayLayers = static_cast<uint32_t>(t.param<int>("d"));
        desc.dimension = dimension;
        desc.mipLevelCount = maxMipLevelCount(desc.size, dimension);
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        t.createTextureTracked(desc);

        desc.mipLevelCount += 1;
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, true);
    });

CTS_TEST(g, "mipLevelCount,bound_check,bigger_than_integer_bit_width")
    .desc("Test that very large mipLevelCount values are invalid.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = 32;
        desc.size.height = 32;
        desc.size.depthOrArrayLayers = 1;
        desc.mipLevelCount = 100;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_TextureBinding;

        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, true);
    });

} // namespace
