// Ported from gpuweb/cts src/webgpu/api/validation/texture/float32_filterable.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,texture,float32_filterable",
    "Tests for capabilities added by float32-filterable flag.");

// Mirrors kFloat32Formats from the upstream spec.
constexpr std::array<WGPUTextureFormat, 3> kFloat32Formats = {
    WGPUTextureFormat_R32Float,
    WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Float,
};

std::vector<Value> float32FormatValues() {
    std::vector<Value> values;
    values.reserve(kFloat32Formats.size());
    for (WGPUTextureFormat format : kFloat32Formats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> textureSampleTypeValues() {
    std::vector<Value> values;
    values.reserve(kTextureSampleTypes.size());
    for (WGPUTextureSampleType st : kTextureSampleTypes) {
        values.emplace_back(std::string(textureSampleTypeIdentifier(st)));
    }
    return values;
}

CTS_TEST(g, "create_bind_group")
    .desc(R"(
Test that it is valid to bind a float32 texture format to a 'float' sampled texture iff
float32-filterable is enabled.
)")
    .params([](ParamsBuilder u) {
        return u
            .combine("enabled", {Value(true), Value(false)})
            .beginSubcases()
            .combine("format", float32FormatValues())
            .combine("sampleType", textureSampleTypeValues());
    })
    .fn([](GpuTest& t) {
        const bool enabled    = t.param<bool>("enabled");
        const std::string formatStr     = t.param<std::string>("format");
        const std::string sampleTypeStr = t.param<std::string>("sampleType");

        const WGPUTextureFormat      format     = parseTextureFormat(formatStr);
        const WGPUTextureSampleType  sampleType = parseTextureSampleType(sampleTypeStr);

        // Gate on device feature availability, mirroring beforeAllSubcases /
        // selectDeviceOrSkipTestCase in the upstream.
        const bool deviceHasFeature =
            wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Float32Filterable) != 0;

        if (enabled && !deviceHasFeature) {
            t.skip("float32-filterable feature is not available on this device");
        }
        if (!enabled && deviceHasFeature) {
            t.skip("float32-filterable is always enabled on this device; cannot test without-feature path");
        }

        // Build a BGL with the requested sampleType.
        WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        bglEntry.binding    = 0;
        bglEntry.visibility = WGPUShaderStage_Fragment;
        bglEntry.texture.sampleType = sampleType;

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries    = &bglEntry;
        WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(bglDesc);

        // Create the float32 texture.
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size        = WGPUExtent3D{4, 4, 1};
        texDesc.format      = format;
        texDesc.usage       = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        // shouldError is true unless the combination is explicitly allowed.
        //   Allowed:  (enabled && sampleType == 'float')  OR  sampleType == 'unfilterable-float'
        const bool shouldError = !(
            (enabled && sampleType == WGPUTextureSampleType_Float) ||
            sampleType == WGPUTextureSampleType_UnfilterableFloat
        );

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        t.expectValidationError([&] {
            WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bgEntry.binding     = 0;
            bgEntry.textureView = view;

            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout     = layout;
            bgDesc.entryCount = 1;
            bgDesc.entries    = &bgEntry;
            t.createBindGroupTracked(bgDesc);
        }, shouldError);
    });

} // namespace
