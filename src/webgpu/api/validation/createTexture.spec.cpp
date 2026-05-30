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

} // namespace
