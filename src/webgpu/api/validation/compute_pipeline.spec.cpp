// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/compute_pipeline.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,compute_pipeline",
    R"(
createComputePipeline and createComputePipelineAsync validation tests.

Note: entry point matching tests are in shader_module/entry_point.spec.ts
)");

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

WGPUConstantEntry makeConstantEntry(std::string_view key, double value) {
    WGPUConstantEntry entry = WGPU_CONSTANT_ENTRY_INIT;
    entry.key = sv(key);
    entry.value = value;
    return entry;
}

WGPUShaderModule createShaderModuleOnDevice(WGPUDevice device, std::string_view wgsl) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = sv(wgsl);
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

WGPUShaderModule createInvalidShaderModule(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule module = nullptr;
    t.expectValidationError([&] {
        module = t.createShaderModuleTracked("deadbeaf");
    }, true);
    return module;
}

std::string shaderWithEntryPoint(std::string_view stage, std::string_view entryPoint) {
    if (stage == "compute") {
        return "@" + std::string(stage) + " @workgroup_size(1) fn " + std::string(entryPoint) + "() {}\n";
    }
    if (stage == "vertex") {
        return "@vertex fn " + std::string(entryPoint)
            + "() -> @builtin(position) vec4<f32> { return vec4<f32>(); }\n";
    }
    return "@fragment fn " + std::string(entryPoint)
        + "() -> @location(0) vec4<f32> { return vec4<f32>(); }\n";
}

WGPUShaderModule getShaderModule(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view stage = "compute",
    std::string_view entryPoint = "main") {
    const std::string code = shaderWithEntryPoint(stage, entryPoint);
    return t.createShaderModuleTracked(code);
}

void doCreateComputePipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    WGPUComputePipelineDescriptor& desc) {
    // isAsync=true uses this same synchronous create path; native validation is eager.
    t.expectValidationError([&] {
        t.createComputePipelineTracked(desc);
    }, !success);
}

void createPipelineWithCode(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    std::string_view code,
    std::string_view entryPoint = "main",
    const std::vector<WGPUConstantEntry>& constants = {}) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.compute.module = module;
    desc.compute.entryPoint = sv(entryPoint);
    desc.compute.constantCount = constants.size();
    desc.compute.constants = constants.empty() ? nullptr : constants.data();
    doCreateComputePipelineTest(t, success, desc);
}

std::vector<WGPUTextureFormat> possibleStorageTextureFormats() {
    std::vector<WGPUTextureFormat> result;
    for (WGPUTextureFormat format : kStorageTextureFormats) {
        result.push_back(format);
    }
    result.push_back(WGPUTextureFormat_BGRA8Unorm);
    for (WGPUTextureFormat format : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        result.push_back(format);
    }
    return result;
}

std::vector<Value> possibleStorageTextureFormatValues() {
    const std::vector<WGPUTextureFormat> formats = possibleStorageTextureFormats();
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

struct BufferResource {
    WGPUBufferBindingType type;
};

struct SamplerResource {
    WGPUSamplerBindingType type;
};

struct TextureResource {
    WGPUTextureSampleType sampleType;
    WGPUTextureViewDimension viewDimension;
    bool multisampled;
};

struct StorageTextureResource {
    WGPUStorageTextureAccess access;
    WGPUTextureFormat format;
    WGPUTextureViewDimension viewDimension;
};

struct Resource {
    std::string key;
    std::string wgslCode;
    std::string staticUse;
    std::optional<BufferResource> buffer;
    std::optional<SamplerResource> sampler;
    std::optional<TextureResource> texture;
    std::optional<StorageTextureResource> storageTexture;
};

std::vector<Resource> buildResources() {
    std::vector<Resource> resources;
    resources.push_back(Resource{"uniform_buffer", "var<uniform> res : array<vec4u, 16>", "res[0]", BufferResource{WGPUBufferBindingType_Uniform}, std::nullopt, std::nullopt, std::nullopt});
    resources.push_back(Resource{"storage_buffer", "var<storage, read_write> res : array<vec4u>", "res[0]", BufferResource{WGPUBufferBindingType_Storage}, std::nullopt, std::nullopt, std::nullopt});
    resources.push_back(Resource{"read-only-storage_buffer", "var<storage> res : array<vec4u>", "res[0]", BufferResource{WGPUBufferBindingType_ReadOnlyStorage}, std::nullopt, std::nullopt, std::nullopt});
    resources.push_back(Resource{"filtering_sampler", "var res : sampler", "", std::nullopt, SamplerResource{WGPUSamplerBindingType_Filtering}, std::nullopt, std::nullopt});
    resources.push_back(Resource{"non-filtering_sampler", "var res : sampler", "", std::nullopt, SamplerResource{WGPUSamplerBindingType_NonFiltering}, std::nullopt, std::nullopt});
    resources.push_back(Resource{"comparison_sampler", "var res : sampler_comparison", "", std::nullopt, SamplerResource{WGPUSamplerBindingType_Comparison}, std::nullopt, std::nullopt});

    struct DimInfo {
        WGPUTextureViewDimension dim;
        const char* key;
        const char* wgsl;
    };
    const std::vector<DimInfo> sampleDims = {
        {WGPUTextureViewDimension_1D, "1d", "1d"},
        {WGPUTextureViewDimension_2D, "2d", "2d"},
        {WGPUTextureViewDimension_2DArray, "2d-array", "2d_array"},
        {WGPUTextureViewDimension_3D, "3d", "3d"},
        {WGPUTextureViewDimension_Cube, "cube", "cube"},
        {WGPUTextureViewDimension_CubeArray, "cube-array", "cube_array"},
    };
    struct SampleTypeInfo {
        WGPUTextureSampleType type;
        const char* key;
        const char* elem;
    };
    const std::vector<SampleTypeInfo> sampleTypes = {
        {WGPUTextureSampleType_Float, "float", "f32"},
        {WGPUTextureSampleType_UnfilterableFloat, "unfilterable-float", "f32"},
        {WGPUTextureSampleType_Sint, "sint", "i32"},
        {WGPUTextureSampleType_Uint, "uint", "u32"},
    };
    resources.push_back(Resource{"texture_depth_2d_true", "var res : texture_depth_multisampled_2d", "", std::nullopt, std::nullopt, TextureResource{WGPUTextureSampleType_Depth, WGPUTextureViewDimension_2D, true}, std::nullopt});
    resources.push_back(Resource{"texture_unfilterable-float_2d_true", "var res : texture_multisampled_2d<f32>", "", std::nullopt, std::nullopt, TextureResource{WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, true}, std::nullopt});
    resources.push_back(Resource{"texture_sint_2d_true", "var res : texture_multisampled_2d<i32>", "", std::nullopt, std::nullopt, TextureResource{WGPUTextureSampleType_Sint, WGPUTextureViewDimension_2D, true}, std::nullopt});
    resources.push_back(Resource{"texture_uint_2d_true", "var res : texture_multisampled_2d<u32>", "", std::nullopt, std::nullopt, TextureResource{WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, true}, std::nullopt});
    for (const DimInfo& di : sampleDims) {
        for (const SampleTypeInfo& ti : sampleTypes) {
            resources.push_back(Resource{
                std::string("texture_") + ti.key + "_" + di.key + "_false",
                std::string("var res : texture_") + di.wgsl + "<" + ti.elem + ">",
                "",
                std::nullopt,
                std::nullopt,
                TextureResource{ti.type, di.dim, false},
                std::nullopt});
        }
    }
    const std::vector<DimInfo> depthDims = {
        {WGPUTextureViewDimension_2D, "2d", "2d"},
        {WGPUTextureViewDimension_2DArray, "2d-array", "2d_array"},
        {WGPUTextureViewDimension_Cube, "cube", "cube"},
        {WGPUTextureViewDimension_CubeArray, "cube-array", "cube_array"},
    };
    for (const DimInfo& di : depthDims) {
        resources.push_back(Resource{
            std::string("texture_depth_") + di.key + "_false",
            std::string("var res : texture_depth_") + di.wgsl,
            "",
            std::nullopt,
            std::nullopt,
            TextureResource{WGPUTextureSampleType_Depth, di.dim, false},
            std::nullopt});
    }
    const std::vector<DimInfo> storageDims = {
        {WGPUTextureViewDimension_1D, "1d", "1d"},
        {WGPUTextureViewDimension_2D, "2d", "2d"},
        {WGPUTextureViewDimension_2DArray, "2d-array", "2d_array"},
        {WGPUTextureViewDimension_3D, "3d", "3d"},
    };
    struct StorageFormatInfo {
        WGPUTextureFormat format;
        const char* key;
    };
    const std::vector<StorageFormatInfo> storageFormats = {
        {WGPUTextureFormat_R32Float, "r32float"},
        {WGPUTextureFormat_R32Sint, "r32sint"},
        {WGPUTextureFormat_R32Uint, "r32uint"},
    };
    struct AccessInfo {
        WGPUStorageTextureAccess access;
        const char* key;
        const char* wgsl;
    };
    const std::vector<AccessInfo> accesses = {
        {WGPUStorageTextureAccess_WriteOnly, "write-only", "write"},
        {WGPUStorageTextureAccess_ReadOnly, "read-only", "read"},
        {WGPUStorageTextureAccess_ReadWrite, "read-write", "read_write"},
    };
    for (const DimInfo& di : storageDims) {
        for (const StorageFormatInfo& fi : storageFormats) {
            for (const AccessInfo& ai : accesses) {
                resources.push_back(Resource{
                    std::string("storage_texture_") + di.key + "_" + fi.key + "_" + ai.key,
                    std::string("var res : texture_storage_") + di.wgsl + "<" + fi.key + "," + ai.wgsl + ">",
                    "",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    StorageTextureResource{ai.access, fi.format, di.dim}});
            }
        }
    }
    return resources;
}

const std::vector<Resource>& allResources() {
    static const std::vector<Resource> resources = buildResources();
    return resources;
}

const Resource& findResource(const std::string& key) {
    for (const Resource& resource : allResources()) {
        if (resource.key == key) return resource;
    }
    std::abort();
}

std::vector<Value> resourceKeyValues() {
    std::vector<Value> values;
    for (const Resource& resource : allResources()) {
        values.emplace_back(resource.key);
    }
    return values;
}

std::string getWGSLShaderForResource(const Resource& resource) {
    std::string code = "@group(0) @binding(0) " + resource.wgslCode + ";\n";
    const std::string use = resource.staticUse.empty() ? "res" : resource.staticUse;
    code += "@compute @workgroup_size(1) fn main() {\n  _ = " + use + ";\n}\n";
    return code;
}

WGPUBindGroupLayout getAPIBindGroupLayoutForResource(AllFeaturesMaxLimitsGpuTest& t, const Resource& resource) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Compute;
    if (resource.buffer) {
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = resource.buffer->type;
    } else if (resource.sampler) {
        entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry.sampler.type = resource.sampler->type;
    } else if (resource.texture) {
        entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry.texture.sampleType = resource.texture->sampleType;
        entry.texture.viewDimension = resource.texture->viewDimension;
        entry.texture.multisampled = resource.texture->multisampled ? WGPU_TRUE : WGPU_FALSE;
    } else if (resource.storageTexture) {
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry.storageTexture.access = resource.storageTexture->access;
        entry.storageTexture.format = resource.storageTexture->format;
        entry.storageTexture.viewDimension = resource.storageTexture->viewDimension;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

bool sampleTypesMatch(WGPUTextureSampleType api, WGPUTextureSampleType wgsl) {
    if (api == WGPUTextureSampleType_Float || api == WGPUTextureSampleType_UnfilterableFloat) {
        return wgsl == WGPUTextureSampleType_Float || wgsl == WGPUTextureSampleType_UnfilterableFloat;
    }
    return api == wgsl;
}

bool accessesMatch(WGPUStorageTextureAccess api, WGPUStorageTextureAccess wgsl) {
    if (api == WGPUStorageTextureAccess_ReadWrite) {
        return wgsl == WGPUStorageTextureAccess_ReadWrite || wgsl == WGPUStorageTextureAccess_WriteOnly;
    }
    return api == wgsl;
}

bool doResourcesMatch(const Resource& api, const Resource& wgsl) {
    if (api.buffer) return wgsl.buffer && api.buffer->type == wgsl.buffer->type;
    if (api.sampler) {
        return wgsl.sampler
            && (api.sampler->type == wgsl.sampler->type
                || (api.sampler->type != WGPUSamplerBindingType_Comparison
                    && wgsl.sampler->type != WGPUSamplerBindingType_Comparison));
    }
    if (api.texture) {
        return wgsl.texture
            && sampleTypesMatch(api.texture->sampleType, wgsl.texture->sampleType)
            && api.texture->viewDimension == wgsl.texture->viewDimension
            && api.texture->multisampled == wgsl.texture->multisampled;
    }
    if (api.storageTexture) {
        return wgsl.storageTexture
            && accessesMatch(api.storageTexture->access, wgsl.storageTexture->access)
            && api.storageTexture->format == wgsl.storageTexture->format
            && api.storageTexture->viewDimension == wgsl.storageTexture->viewDimension;
    }
    return false;
}

static const char* kWorkgroupSizeShaderU32 =
    "override x: u32 = 1u;\n"
    "override y: u32 = 1u;\n"
    "override z: u32 = 1u;\n"
    "@compute @workgroup_size(x, y, z) fn main () {\n"
    "  _ = 0u;\n"
    "}\n";

static const char* kWorkgroupSizeShaderI32 =
    "override x: i32 = 1;\n"
    "override y: i32 = 1;\n"
    "override z: i32 = 1;\n"
    "@compute @workgroup_size(x, y, z) fn main () {\n"
    "  _ = 0u;\n"
    "}\n";

const char* workgroupSizeShader(const std::string& type) {
    return type == "u32" ? kWorkgroupSizeShaderU32 : kWorkgroupSizeShaderI32;
}

CTS_TEST(g, "basic")
    .desc("Control case for createComputePipeline and createComputePipelineAsync.")
    .params([](ParamsBuilder u) { return u.combine("isAsync", {Value(true), Value(false)}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        WGPUShaderModule module = getShaderModule(t);
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.compute.module = module;
        desc.compute.entryPoint = sv("main");
        doCreateComputePipelineTest(t, true, desc);
    });

CTS_TEST(g, "shader_module,invalid")
    .desc("Tests calling createComputePipeline(Async) with a invalid compute shader.")
    .params([](ParamsBuilder u) { return u.combine("isAsync", {Value(true), Value(false)}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.compute.module = createInvalidShaderModule(t);
        desc.compute.entryPoint = sv("main");
        doCreateComputePipelineTest(t, false, desc);
    });

CTS_TEST(g, "shader_module,compute")
    .desc("Tests calling createComputePipeline(Async) with valid but different stage shader.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .combine("shaderModuleStage", {Value("compute"), Value("vertex"), Value("fragment")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string stage = t.param<std::string>("shaderModuleStage");
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.compute.module = getShaderModule(t, stage);
        desc.compute.entryPoint = sv("main");
        doCreateComputePipelineTest(t, stage == "compute", desc);
    });

CTS_TEST(g, "shader_module,device_mismatch")
    .desc("Tests createComputePipeline(Async) cannot be called with a shader module created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("isAsync", {Value(true), Value(false)})
            .combine("mismatched", {Value(true), Value(false)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const bool mismatched = t.param<bool>("mismatched");
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();
        WGPUShaderModule module = createShaderModuleOnDevice(sourceDevice, "@compute @workgroup_size(1) fn main() {}\n");
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.compute.module = module;
        desc.compute.entryPoint = sv("main");
        doCreateComputePipelineTest(t, !mismatched, desc);
        wgpuShaderModuleRelease(module);
    });

CTS_TEST(g, "pipeline_layout,device_mismatch")
    .desc("Tests createComputePipeline(Async) cannot be called with a pipeline layout created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("isAsync", {Value(true), Value(false)})
            .combine("mismatched", {Value(true), Value(false)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const bool mismatched = t.param<bool>("mismatched");
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();
        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(sourceDevice, &layoutDesc);
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = layout;
        desc.compute.module = getShaderModule(t);
        desc.compute.entryPoint = sv("main");
        doCreateComputePipelineTest(t, !mismatched, desc);
        wgpuPipelineLayoutRelease(layout);
    });

CTS_TEST(g, "limits,workgroup_storage_size")
    .desc("Tests compute workgroup storage size limit validation.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .combineWithParams({
                ParamRecord{{"type", Value("vec4<f32>")}, {"_typeSize", Value(16)}},
                ParamRecord{{"type", Value("mat4x4<f32>")}, {"_typeSize", Value(64)}},
            })
            .beginSubcases()
            .combine("countDeltaFromLimit", {Value(0), Value(1)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string type = t.param<std::string>("type");
        const uint32_t typeSize = static_cast<uint32_t>(t.param<int>("_typeSize"));
        const uint32_t countAtLimit = t.getLimits().maxComputeWorkgroupStorageSize / typeSize;
        const uint32_t count = countAtLimit + static_cast<uint32_t>(t.param<int>("countDeltaFromLimit"));
        const std::string code = "var<workgroup> data: array<" + type + ", " + std::to_string(count) + ">;\n"
            "@compute @workgroup_size(64) fn main () {\n  _ = data;\n}\n";
        createPipelineWithCode(t, count <= countAtLimit, code);
    });

CTS_TEST(g, "limits,invocations_per_workgroup")
    .desc("Tests maxComputeInvocationsPerWorkgroup validation.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .combineWithParams({
                ParamRecord{{"size", Value("128,1,2")}},
                ParamRecord{{"size", Value("129,1,2")}},
                ParamRecord{{"size", Value("2,128,1")}},
                ParamRecord{{"size", Value("2,129,1")}},
                ParamRecord{{"size", Value("1,8,32")}},
                ParamRecord{{"size", Value("1,8,33")}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string size = t.param<std::string>("size");
        uint32_t x = 1;
        uint32_t y = 1;
        uint32_t z = 1;
        (void)std::sscanf(size.c_str(), "%u,%u,%u", &x, &y, &z);
        const std::string code = "@compute @workgroup_size(" + size + ") fn main () {}\n";
        createPipelineWithCode(t, x * y * z <= t.getLimits().maxComputeInvocationsPerWorkgroup, code);
    });

CTS_TEST(g, "limits,invocations_per_workgroup,each_component")
    .desc("Tests workgroup_size attribute component limits.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .combineWithParams({
                ParamRecord{{"size", Value("64")}},
                ParamRecord{{"size", Value("256,1,1")}},
                ParamRecord{{"size", Value("257,1,1")}},
                ParamRecord{{"size", Value("1,256,1")}},
                ParamRecord{{"size", Value("1,257,1")}},
                ParamRecord{{"size", Value("1,1,63")}},
                ParamRecord{{"size", Value("1,1,64")}},
                ParamRecord{{"size", Value("1,1,65")}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string size = t.param<std::string>("size");
        uint32_t x = 1;
        uint32_t y = 1;
        uint32_t z = 1;
        (void)std::sscanf(size.c_str(), "%u,%u,%u", &x, &y, &z);
        const WGPULimits limits = t.getLimits();
        const std::string code = "@compute @workgroup_size(" + size + ") fn main () {}\n";
        createPipelineWithCode(t, x <= limits.maxComputeWorkgroupSizeX
                && y <= limits.maxComputeWorkgroupSizeY
                && z <= limits.maxComputeWorkgroupSizeZ,
            code);
    });

CTS_TEST(g, "overrides,identifier")
    .desc("Tests validation for overridable constants identifiers.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .beginSubcases()
            .combine("subcaseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(5), Value(6), Value(7), Value(8), Value(9), Value(10), Value(11), Value(12)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const int idx = t.param<int>("subcaseIndex");
        std::vector<WGPUConstantEntry> constants;
        bool success = true;
        switch (idx) {
            case 1: constants.push_back(makeConstantEntry("c0", 0.0)); break;
            case 2: constants.push_back(makeConstantEntry("c0", 0.0)); constants.push_back(makeConstantEntry("c1", 1.0)); break;
            case 3: constants.push_back(makeConstantEntry(std::string_view("c0\0", 3), 0.0)); success = false; break;
            case 4: constants.push_back(makeConstantEntry("c9", 0.0)); success = false; break;
            case 5: constants.push_back(makeConstantEntry("1", 0.0)); break;
            case 6: constants.push_back(makeConstantEntry("c3", 0.0)); success = false; break;
            case 7: constants.push_back(makeConstantEntry("2", 0.0)); success = false; break;
            case 8: constants.push_back(makeConstantEntry("1000", 0.0)); break;
            case 9: constants.push_back(makeConstantEntry("9999", 0.0)); success = false; break;
            case 10: constants.push_back(makeConstantEntry("1000", 0.0)); constants.push_back(makeConstantEntry("c2", 0.0)); success = false; break;
            case 11: constants.push_back(makeConstantEntry("\xe6\x95\xb0", 0.0)); break;
            case 12: constants.push_back(makeConstantEntry("se" "\xcc" "\x81" "quen" "\xc3" "\xa7" "age", 0.0)); success = false; break;
            default: break;
        }
        static const char* code =
            "override c0: bool = true;\n"
            "override c1: u32 = 0u;\n"
            "override \xe6\x95\xb0: u32 = 0u;\n"
            "override s" "\xc3" "\xa9" "quen" "\xc3" "\xa7" "age: u32 = 0u;\n"
            "@id(1000) override c2: u32 = 10u;\n"
            "@id(1) override c3: u32 = 11u;\n"
            "@compute @workgroup_size(1) fn main () {\n"
            "  _ = u32(c0);\n  _ = u32(c1);\n  _ = u32(c2 + s" "\xc3" "\xa9" "quen" "\xc3" "\xa7" "age);\n  _ = u32(c3 + \xe6\x95\xb0);\n"
            "}\n";
        createPipelineWithCode(t, success, code, "main", constants);
    });

CTS_TEST(g, "overrides,uninitialized")
    .desc("Tests validation for uninitialized overridable constants.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .beginSubcases()
            .combine("subcaseIndex", {Value(0), Value(1), Value(2), Value(3)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const int idx = t.param<int>("subcaseIndex");
        std::vector<WGPUConstantEntry> constants;
        bool success = false;
        if (idx >= 1) {
            constants.push_back(makeConstantEntry("c0", 0.0));
            constants.push_back(makeConstantEntry("c2", 0.0));
            constants.push_back(makeConstantEntry("c8", 0.0));
        }
        if (idx >= 2) {
            constants.push_back(makeConstantEntry("c5", 0.0));
            success = true;
        }
        if (idx == 3) constants.push_back(makeConstantEntry("c1", 0.0));
        static const char* code =
            "override c0: bool;\n override c1: bool = false;\n override c2: f32;\n"
            "override c3: f32 = 0.0;\n override c4: f32 = 4.0;\n override c5: i32;\n"
            "override c6: i32 = 0;\n override c7: i32 = 7;\n override c8: u32;\n"
            "override c9: u32 = 0u;\n @id(1000) override c10: u32 = 10u;\n"
            "@compute @workgroup_size(1) fn main () {\n"
            " _ = u32(c0); _ = u32(c1); _ = u32(c2); _ = u32(c3); _ = u32(c4);\n"
            " _ = u32(c5); _ = u32(c6); _ = u32(c7); _ = u32(c8); _ = u32(c9); _ = u32(c10);\n}\n";
        createPipelineWithCode(t, success, code, "main", constants);
    });

CTS_TEST(g, "overrides,value,type_error")
    .desc("Tests constant values like inf and NaN.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .beginSubcases()
            .combine("subcaseIndex", {Value(0), Value(1), Value(2), Value(3)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const int idx = t.param<int>("subcaseIndex");
        const double values[] = {1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
        std::vector<WGPUConstantEntry> constants = {makeConstantEntry("cf", values[idx])};
        createPipelineWithCode(t, idx == 0, "override cf: f32 = 0.0;\n@compute @workgroup_size(1) fn main () { _ = cf; }\n", "main", constants);
    });

CTS_TEST(g, "overrides,value,validation_error")
    .desc("Tests validation for unrepresentable constant values in compute stage.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .beginSubcases()
            .combine("subcaseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(5), Value(6), Value(7), Value(8), Value(9), Value(10), Value(11), Value(12), Value(13)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const int idx = t.param<int>("subcaseIndex");
        constexpr double kU32Min = 0.0;
        constexpr double kU32Max = 4294967295.0;
        constexpr double kI32Min = -2147483648.0;
        constexpr double kI32Max = 2147483647.0;
        constexpr double kF32Max = 3.4028234663852886e+38;
        constexpr double kF32FirstNonCastable = 3.4028235677973366e+38;
        struct Case {
            const char* key;
            double value;
            bool success;
        };
        const Case cases[] = {
            {"cu", kU32Min, true}, {"cu", -1.0, false}, {"cu", kU32Max, true}, {"cu", kU32Max + 1.0, false},
            {"ci", kI32Min, true}, {"ci", kI32Min - 1.0, false}, {"ci", kI32Max, true}, {"ci", kI32Max + 1.0, false},
            {"cf", -kF32Max, true}, {"cf", -kF32FirstNonCastable, false}, {"cf", kF32Max, true}, {"cf", kF32FirstNonCastable, false},
            {"cb", std::numeric_limits<double>::max(), true}, {"cb", kI32Min - 1.0, true},
        };
        const Case& testCase = cases[idx];
        std::vector<WGPUConstantEntry> constants = {makeConstantEntry(testCase.key, testCase.value)};
        static const char* code =
            "override cb: bool = false;\n override cu: u32 = 0u;\n override ci: i32 = 0;\n override cf: f32 = 0.0;\n"
            "@compute @workgroup_size(1) fn main () { _ = cb; _ = cu; _ = ci; _ = cf; }\n";
        createPipelineWithCode(t, testCase.success, code, "main", constants);
    });

CTS_TEST(g, "overrides,entry_point,validation_error")
    .desc("Tests that pipeline constant errors only trigger on entry point usage.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .combine("pipeEntryPoint", {Value("main_success"), Value("main_pipe_error")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string entryPoint = t.param<std::string>("pipeEntryPoint");
        static const char* code =
            "override cu: u32 = 0u;\n"
            "override cx: u32 = 1u/cu;\n"
            "@compute @workgroup_size(1) fn main_success () { _ = cu; }\n"
            "@compute @workgroup_size(1) fn main_pipe_error () { _ = cx; }\n";
        createPipelineWithCode(t, entryPoint == "main_success", code, entryPoint);
    });

CTS_TEST(g, "overrides,value,validation_error,f16")
    .desc("Tests validation for unrepresentable f16 constant values in compute stage.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .beginSubcases()
            .combine("subcaseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(5), Value(6), Value(7)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        (void)t.param<bool>("isAsync");
        const int idx = t.param<int>("subcaseIndex");
        constexpr double kF16Max = 65504.0;
        constexpr double kF16FirstNonCastable = 65520.0;
        constexpr double kF32Max = 3.4028234663852886e+38;
        constexpr double kF32FirstNonCastable = 3.4028235677973366e+38;
        struct Case {
            double value;
            bool success;
        };
        const Case cases[] = {
            {-kF16Max, true}, {-kF16FirstNonCastable, false}, {kF16Max, true}, {kF16FirstNonCastable, false},
            {-kF32Max, false}, {kF32Max, false}, {-kF32FirstNonCastable, false}, {kF32FirstNonCastable, false},
        };
        std::vector<WGPUConstantEntry> constants = {makeConstantEntry("cf16", cases[idx].value)};
        createPipelineWithCode(t, cases[idx].success,
            "enable f16;\n override cf16: f16 = 0.0h;\n @compute @workgroup_size(1) fn main () { _ = cf16; }\n",
            "main", constants);
    });

CTS_TEST(g, "overrides,workgroup_size")
    .desc("Tests overridable constants used for workgroup size.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)})
            .combine("type", {Value("u32"), Value("i32")})
            .beginSubcases()
            .combine("subcaseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string type = t.param<std::string>("type");
        const int idx = t.param<int>("subcaseIndex");
        std::vector<WGPUConstantEntry> constants;
        bool success = true;
        if (idx == 1) {
            constants = {makeConstantEntry("x", 0.0), makeConstantEntry("y", 0.0), makeConstantEntry("z", 0.0)};
            success = false;
        } else if (idx == 2) {
            constants = {makeConstantEntry("x", 1.0), makeConstantEntry("y", -1.0), makeConstantEntry("z", 1.0)};
            success = false;
        } else if (idx == 3) {
            constants = {makeConstantEntry("x", 1.0), makeConstantEntry("y", 0.0), makeConstantEntry("z", 0.0)};
            success = false;
        } else if (idx == 4) {
            constants = {makeConstantEntry("x", 16.0), makeConstantEntry("y", 1.0), makeConstantEntry("z", 1.0)};
        }
        createPipelineWithCode(t, success, workgroupSizeShader(type), "main", constants);
    });

CTS_TEST(g, "overrides,workgroup_size,limits")
    .desc("Tests overridable constants for workgroupSize exceeds device limits.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(true), Value(false)}).combine("type", {Value("u32"), Value("i32")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string type = t.param<std::string>("type");
        const WGPULimits limits = t.getLimits();
        auto testFn = [&](uint32_t x, uint32_t y, uint32_t z, bool success) {
            std::vector<WGPUConstantEntry> constants = {
                makeConstantEntry("x", static_cast<double>(x)),
                makeConstantEntry("y", static_cast<double>(y)),
                makeConstantEntry("z", static_cast<double>(z)),
            };
            createPipelineWithCode(t, success, workgroupSizeShader(type), "main", constants);
        };
        testFn(limits.maxComputeWorkgroupSizeX, 1, 1, true);
        testFn(limits.maxComputeWorkgroupSizeX + 1, 1, 1, false);
        testFn(1, limits.maxComputeWorkgroupSizeY, 1, true);
        testFn(1, limits.maxComputeWorkgroupSizeY + 1, 1, false);
        testFn(1, 1, limits.maxComputeWorkgroupSizeZ, true);
        testFn(1, 1, limits.maxComputeWorkgroupSizeZ + 1, false);
        const uint64_t invocations = static_cast<uint64_t>(limits.maxComputeWorkgroupSizeX)
            * limits.maxComputeWorkgroupSizeY * limits.maxComputeWorkgroupSizeZ;
        testFn(limits.maxComputeWorkgroupSizeX, limits.maxComputeWorkgroupSizeY, limits.maxComputeWorkgroupSizeZ,
            invocations <= limits.maxComputeInvocationsPerWorkgroup);
    });

CTS_TEST(g, "overrides,workgroup_size,limits,workgroup_storage_size")
    .desc("Tests overridable constants for workgroupStorageSize exceeds device limits.")
    .params([](ParamsBuilder u) { return u.combine("isAsync", {Value(true), Value(false)}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const WGPULimits limits = t.getLimits();
        const uint32_t maxVec4Count = limits.maxComputeWorkgroupStorageSize / 16;
        const uint32_t maxMat4Count = limits.maxComputeWorkgroupStorageSize / 64;
        auto testFn = [&](uint32_t vec4Count, uint32_t mat4Count, bool success) {
            std::string code = "override a: u32;\noverride b: u32;\n";
            if (vec4Count > 0) code += "var<workgroup> vec4_data: array<vec4<f32>, a>;\n";
            if (mat4Count > 0) code += "var<workgroup> mat4_data: array<mat4x4<f32>, b>;\n";
            code += "@compute @workgroup_size(1) fn main() {\n";
            if (vec4Count > 0) code += "  _ = vec4_data[0];\n";
            if (mat4Count > 0) code += "  _ = mat4_data[0];\n";
            code += "}\n";
            std::vector<WGPUConstantEntry> constants = {
                makeConstantEntry("a", static_cast<double>(vec4Count)),
                makeConstantEntry("b", static_cast<double>(mat4Count)),
            };
            createPipelineWithCode(t, success, code, "main", constants);
        };
        testFn(1, 1, true);
        testFn(maxVec4Count + 1, 0, false);
        testFn(0, maxMat4Count + 1, false);
    });

CTS_TEST(g, "resource_compatibility")
    .desc("Tests validation of resource (bind group) compatibility between pipeline layout and WGSL shader")
    .params([](ParamsBuilder u) {
        return u.combine("apiResource", resourceKeyValues())
            .beginSubcases()
            .combine("isAsync", {Value(true), Value(false)})
            .combine("wgslResource", resourceKeyValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const Resource& apiResource = findResource(t.param<std::string>("apiResource"));
        const Resource& wgslResource = findResource(t.param<std::string>("wgslResource"));
        if (wgslResource.texture) {
            t.skipIfTextureViewDimensionNotSupported(wgslResource.texture->viewDimension);
        }
        if (wgslResource.storageTexture) {
            t.skipIfTextureViewDimensionNotSupported(wgslResource.storageTexture->viewDimension);
        }
        WGPUBindGroupLayout bgl = getAPIBindGroupLayoutForResource(t, apiResource);
        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &bgl;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);
        const std::string code = getWGSLShaderForResource(wgslResource);
        WGPUShaderModule module = t.createShaderModuleTracked(code);
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = layout;
        desc.compute.module = module;
        desc.compute.entryPoint = sv("main");
        doCreateComputePipelineTest(t, doResourcesMatch(apiResource, wgslResource), desc);
    });

CTS_TEST(g, "storage_texture,format")
    .desc("Test storage texture access/format support with auto layout.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormatValues())
            .beginSubcases()
            .combine("isAsync", {Value(true), Value(false)})
            .combine("access", {Value("read"), Value("write"), Value("read_write")})
            .combine("dimension", {Value("1d"), Value("2d"), Value("3d")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        (void)t.param<bool>("isAsync");
        const std::string formatStr = t.param<std::string>("format");
        const std::string access = t.param<std::string>("access");
        const std::string dimension = t.param<std::string>("dimension");
        const WGPUTextureFormat format = parseTextureFormat(formatStr);
        t.skipIfTextureFormatNotSupported(format);
        const std::string code = std::string("@group(0) @binding(0) var tex: texture_storage_") + dimension
            + "<" + formatStr + ", " + access + ">;\n"
            "@compute @workgroup_size(1) fn main() {\n  _ = tex;\n}\n";
        WGPUStorageTextureAccess wgpuAccess = WGPUStorageTextureAccess_WriteOnly;
        if (access == "read") {
            wgpuAccess = WGPUStorageTextureAccess_ReadOnly;
        } else if (access == "read_write") {
            wgpuAccess = WGPUStorageTextureAccess_ReadWrite;
        }
        createPipelineWithCode(t, t.isTextureFormatUsableWithStorageAccessMode(format, wgpuAccess), code);
    });

} // namespace
