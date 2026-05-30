// Ported from gpuweb/cts src/webgpu/api/validation/createTexture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,createTexture",
    "createTexture validation tests.");

// Size variant params keep the upstream key names but encode unsupported array/object values as integer indexes:
// widthVariant 0..2 maps to offsets -1..+1; sizeVariant 0..8 maps to dimension=index/3 and offset=index%3-1.
// Compressed texture sizeVariant 0..27 indexes kCompressedTextureSizeVariants.
// The C enum exposes every texture usage member, so new_usages is faithful but success is always true.

std::vector<Value> textureDimensionValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureDimensions.size() + 1);
    values.push_back(Value::undef());
    for (WGPUTextureDimension dimension : kTextureDimensions) {
        values.emplace_back(static_cast<int64_t>(dimension));
    }
    return values;
}

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

std::vector<Value> uncompressedTextureFormatValues() {
    return textureFormatValues(kUncompressedTextureFormats);
}

std::vector<Value> regularTextureFormatValues() {
    return textureFormatValues(kRegularTextureFormats);
}

std::vector<Value> compressedTextureFormatValues() {
    return textureFormatValues(kCompressedTextureFormats);
}

std::vector<Value> textureUsageValuesWithZeroAndBogus() {
    std::vector<Value> values;
    values.reserve(kTextureUsages.size() + 2);
    values.emplace_back(0);
    for (WGPUTextureUsage usage : kTextureUsages) {
        values.emplace_back(static_cast<int64_t>(usage));
    }
    values.emplace_back(static_cast<int64_t>(kSomeBogusTextureUsage));
    return values;
}

std::vector<Value> newTextureUsageValues() {
    std::vector<Value> values;
    values.reserve(kTextureUsages.size() + 1);
    for (WGPUTextureUsage usage : kTextureUsages) {
        values.emplace_back(static_cast<int64_t>(usage));
    }
    values.emplace_back(static_cast<int64_t>(WGPUTextureUsage_RenderAttachment |
                                             WGPUTextureUsage_TransientAttachment));
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

std::vector<Value> sizeVariantValues() {
    return {
        Value(0),
        Value(1),
        Value(2),
        Value(3),
        Value(4),
        Value(5),
        Value(6),
        Value(7),
        Value(8),
    };
}

std::vector<Value> compressedSizeVariantValues() {
    std::vector<Value> values;
    values.reserve(kCompressedTextureSizeVariants.size());
    for (size_t i = 0; i < kCompressedTextureSizeVariants.size(); ++i) {
        values.emplace_back(static_cast<int64_t>(i));
    }
    return values;
}

uint32_t limitWithOffset(uint32_t limit, int offset) {
    return static_cast<uint32_t>(static_cast<int64_t>(limit) + offset);
}

uint32_t applySizeVariantComponent(uint32_t base,
                                   const SizeVariantComponent& component,
                                   const TextureBlockInfo& blockInfo) {
    return static_cast<uint32_t>(static_cast<int64_t>(base) * component.mult
                                 + component.addLiteral
                                 + component.addBlockW * static_cast<int64_t>(blockInfo.blockWidth)
                                 + component.addBlockH * static_cast<int64_t>(blockInfo.blockHeight));
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

CTS_TEST(g, "usage")
    .desc("Test combinations of zero to two usage flags are validated to be valid.")
    .params([](ParamsBuilder u) {
        const std::vector<Value> usages = textureUsageValuesWithZeroAndBogus();
        return u.combine("usage1", usages)
            .combine("usage2", usages)
            .filter([](const ParamRecord& params) {
                const auto usage1 = valueAs<uint64_t>(*findParam(params, "usage1"));
                const auto usage2 = valueAs<uint64_t>(*findParam(params, "usage2"));
                return usage1 <= usage2;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureUsage usage1 = t.param<WGPUTextureUsage>("usage1");
        const WGPUTextureUsage usage2 = t.param<WGPUTextureUsage>("usage2");
        const WGPUTextureUsage usage = usage1 | usage2;

        if (usage & WGPUTextureUsage_TransientAttachment) {
            t.skipIfTransientAttachmentNotSupported();
        }

        const bool isValid =
            usage != 0 &&
            (usage & ~kAllTextureUsages) == 0 &&
            ((usage & WGPUTextureUsage_TransientAttachment) == 0 ||
             usage == (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment));

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = 1;
        desc.size.height = 1;
        desc.size.depthOrArrayLayers = 1;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = usage;

        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !isValid);
    });

CTS_TEST(g, "new_usages")
    .desc("Valid usages exposed by GPUTextureUsage should be accepted by createTexture().")
    .params([](ParamsBuilder u) {
        return u.combine("usage", newTextureUsageValues())
            .filter([](const ParamRecord& params) {
                const auto usage = static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage")));
                return isValidTextureUsageCombination(usage);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureUsage usage = t.param<WGPUTextureUsage>("usage");
        constexpr WGPUTextureUsage exposedUsages = kAllTextureUsages;
        const bool success = (usage & exposedUsages) == usage;

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = 1;
        desc.size.height = 1;
        desc.size.depthOrArrayLayers = 1;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = usage;

        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

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

CTS_TEST(g, "texture_size,1d_texture")
    .desc("Test upper-bound texture size validation for 1D regular textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", regularTextureFormatValues())
            .beginSubcases()
            .combine("widthVariant", {Value(0), Value(1), Value(2)})
            .combine("height", {Value(1), Value(2)})
            .combine("depthOrArrayLayers", {Value(1), Value(2)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const int widthVariant = t.param<int>("widthVariant");
        const uint32_t height = static_cast<uint32_t>(t.param<int>("height"));
        const uint32_t depthOrArrayLayers = static_cast<uint32_t>(t.param<int>("depthOrArrayLayers"));
        const WGPULimits limits = t.getLimits();

        t.skipIfTextureFormatNotSupported(format);

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width = limitWithOffset(limits.maxTextureDimension1D, widthVariant - 1);
        desc.size.height = height;
        desc.size.depthOrArrayLayers = depthOrArrayLayers;
        desc.dimension = WGPUTextureDimension_1D;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        const bool success = desc.size.width <= limits.maxTextureDimension1D && height == 1 && depthOrArrayLayers == 1;
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

CTS_TEST(g, "texture_size,2d_texture,uncompressed_format")
    .desc("Test upper-bound texture size validation for 2D uncompressed textures.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", {Value::undef(), Value(static_cast<int64_t>(WGPUTextureDimension_2D))})
            .combine("format", uncompressedTextureFormatValues())
            .combine("sizeVariant", sizeVariantValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = dimensionParam(t);
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const int sizeVariant = t.param<int>("sizeVariant");
        const int testedDim = sizeVariant / 3;
        const int offset = sizeVariant % 3 - 1;
        const WGPULimits limits = t.getLimits();
        const std::array<uint32_t, 3> dimensionLimits = {
            limits.maxTextureDimension2D,
            limits.maxTextureDimension2D,
            limits.maxTextureArrayLayers,
        };

        t.skipIfTextureFormatNotSupported(format);

        WGPUExtent3D size = {1, 1, 1};
        if (testedDim == 0) {
            size.width = limitWithOffset(dimensionLimits[0], offset);
        } else if (testedDim == 1) {
            size.height = limitWithOffset(dimensionLimits[1], offset);
        } else {
            size.depthOrArrayLayers = limitWithOffset(dimensionLimits[2], offset);
        }

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        const bool success = size.width <= limits.maxTextureDimension2D
            && size.height <= limits.maxTextureDimension2D
            && size.depthOrArrayLayers <= limits.maxTextureArrayLayers;
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

CTS_TEST(g, "texture_size,3d_texture,uncompressed_format")
    .desc("Test upper-bound texture size validation for 3D regular textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", regularTextureFormatValues())
            .beginSubcases()
            .combine("sizeVariant", sizeVariantValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const int sizeVariant = t.param<int>("sizeVariant");
        const int testedDim = sizeVariant / 3;
        const int offset = sizeVariant % 3 - 1;
        const WGPULimits limits = t.getLimits();

        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, WGPUTextureDimension_3D);

        WGPUExtent3D size = {1, 1, 1};
        if (testedDim == 0) {
            size.width = limitWithOffset(limits.maxTextureDimension3D, offset);
        } else if (testedDim == 1) {
            size.height = limitWithOffset(limits.maxTextureDimension3D, offset);
        } else {
            size.depthOrArrayLayers = limitWithOffset(limits.maxTextureDimension3D, offset);
        }

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = WGPUTextureDimension_3D;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        const bool success = size.width <= limits.maxTextureDimension3D
            && size.height <= limits.maxTextureDimension3D
            && size.depthOrArrayLayers <= limits.maxTextureDimension3D;
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

CTS_TEST(g, "texture_size,2d_texture,compressed_format")
    .desc("Test upper-bound texture size validation for 2D compressed textures.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", {Value::undef(), Value(static_cast<int64_t>(WGPUTextureDimension_2D))})
            .combine("format", compressedTextureFormatValues())
            .beginSubcases()
            .combine("sizeVariant", compressedSizeVariantValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = dimensionParam(t);
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const size_t sizeVariant = static_cast<size_t>(t.param<int>("sizeVariant"));
        const WGPULimits limits = t.getLimits();

        t.skipIfTextureFormatNotSupported(format);
        const TextureBlockInfo blockInfo = getBlockInfoForTextureFormat(format);
        const std::array<uint32_t, 3> base =
            getMaxValidTextureSizeForFormatAndDimension(limits, format, WGPUTextureDimension_2D);
        const SizeVariant& variant = kCompressedTextureSizeVariants[sizeVariant];

        const WGPUExtent3D size = {
            applySizeVariantComponent(base[0], variant[0], blockInfo),
            applySizeVariantComponent(base[1], variant[1], blockInfo),
            applySizeVariantComponent(base[2], variant[2], blockInfo),
        };

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        const bool success = size.width % blockInfo.blockWidth == 0
            && size.height % blockInfo.blockHeight == 0
            && size.width <= limits.maxTextureDimension2D
            && size.height <= limits.maxTextureDimension2D
            && size.depthOrArrayLayers <= limits.maxTextureArrayLayers;
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

CTS_TEST(g, "texture_size,3d_texture,compressed_format")
    .desc("Test upper-bound texture size validation for 3D compressed textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", compressedTextureFormatValues())
            .beginSubcases()
            .combine("sizeVariant", compressedSizeVariantValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const size_t sizeVariant = static_cast<size_t>(t.param<int>("sizeVariant"));
        const WGPULimits limits = t.getLimits();

        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, WGPUTextureDimension_3D);
        const TextureBlockInfo blockInfo = getBlockInfoForTextureFormat(format);
        const std::array<uint32_t, 3> base =
            getMaxValidTextureSizeForFormatAndDimension(limits, format, WGPUTextureDimension_3D);
        const SizeVariant& variant = kCompressedTextureSizeVariants[sizeVariant];

        const WGPUExtent3D size = {
            applySizeVariantComponent(base[0], variant[0], blockInfo),
            applySizeVariantComponent(base[1], variant[1], blockInfo),
            applySizeVariantComponent(base[2], variant[2], blockInfo),
        };

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = WGPUTextureDimension_3D;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;

        const bool success = size.width % blockInfo.blockWidth == 0
            && size.height % blockInfo.blockHeight == 0
            && size.width <= limits.maxTextureDimension3D
            && size.height <= limits.maxTextureDimension3D
            && size.depthOrArrayLayers <= limits.maxTextureDimension3D
            && t.textureDimensionAndFormatCompatibleForDevice(WGPUTextureDimension_3D, format);
        t.expectValidationError([&] {
            t.createTextureTracked(desc);
        }, !success);
    });

} // namespace
