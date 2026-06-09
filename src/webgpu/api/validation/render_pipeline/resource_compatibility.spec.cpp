// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/resource_compatibility.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.
// Note: externalTexture resources are omitted (no C WebGPU API equivalent).
// Note: hasLanguageFeature('readonly_and_readwrite_storage_textures') guard is omitted:
//   AllFeaturesMaxLimitsGpuTest requests all adapter features; NAGA/tint always support
//   read/read_write storage textures in the native backends (consistent with read_only.spec.cpp).
//   The WGPUInstance is not exposed through the GpuTest harness API, so
//   wgpuInstanceHasWGSLLanguageFeature cannot be called from test bodies.
// Note: Compatibility-mode limit guards (maxStorageBuffersInVertexStage etc.) are omitted:
//   the harness always runs with AllFeaturesMaxLimitsGpuTest which enables max limits.

#include <optional>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,resource_compatibility",
    "Tests for resource compatibility between pipeline layout and shader modules");

// Returns a WGPUStringView from a null-terminated C string.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// ---------------------------------------------------------------------------
// Resource description (mirrors the JS Resource interface in utils.ts)
// ---------------------------------------------------------------------------

struct BufferResource {
    WGPUBufferBindingType type; // Uniform | Storage | ReadOnlyStorage
};

struct SamplerResource {
    WGPUSamplerBindingType type; // Filtering | NonFiltering | Comparison
};

struct TextureResource {
    WGPUTextureSampleType  sampleType;
    WGPUTextureViewDimension viewDimension;
    bool                   multisampled;
};

struct StorageTextureResource {
    WGPUStorageTextureAccess  access;
    WGPUTextureFormat         format;
    WGPUTextureViewDimension  viewDimension;
};

struct Resource {
    std::string  key;   // the resourceKey() string used as param value
    std::string  wgslCode; // the `code` field — declaration snippet (no @group/@binding prefix)
    std::string  staticUse; // optional static use expression; empty means "res"

    // At most one of these is set:
    std::optional<BufferResource>        buffer;
    std::optional<SamplerResource>       sampler;
    std::optional<TextureResource>       texture;
    std::optional<StorageTextureResource> storageTexture;
};

// ---------------------------------------------------------------------------
// Build the flat list of resources (matches generateResources() in utils.ts)
// ---------------------------------------------------------------------------
static std::vector<Resource> buildResources() {
    std::vector<Resource> resources;

    // --- Buffers ---
    {
        Resource r;
        r.buffer = BufferResource{WGPUBufferBindingType_Uniform};
        r.key    = "uniform_buffer";
        r.wgslCode   = "var<uniform> res : array<vec4u, 16>";
        r.staticUse  = "res[0]";
        resources.push_back(r);
    }
    {
        Resource r;
        r.buffer = BufferResource{WGPUBufferBindingType_Storage};
        r.key    = "storage_buffer";
        r.wgslCode   = "var<storage, read_write> res : array<vec4u>";
        r.staticUse  = "res[0]";
        resources.push_back(r);
    }
    {
        Resource r;
        r.buffer = BufferResource{WGPUBufferBindingType_ReadOnlyStorage};
        r.key    = "read-only-storage_buffer";
        r.wgslCode   = "var<storage> res : array<vec4u>";
        r.staticUse  = "res[0]";
        resources.push_back(r);
    }

    // --- Samplers ---
    {
        Resource r;
        r.sampler = SamplerResource{WGPUSamplerBindingType_Filtering};
        r.key     = "filtering_sampler";
        r.wgslCode    = "var res : sampler";
        resources.push_back(r);
    }
    {
        Resource r;
        r.sampler = SamplerResource{WGPUSamplerBindingType_NonFiltering};
        r.key     = "non-filtering_sampler";
        r.wgslCode    = "var res : sampler";
        resources.push_back(r);
    }
    {
        Resource r;
        r.sampler = SamplerResource{WGPUSamplerBindingType_Comparison};
        r.key     = "comparison_sampler";
        r.wgslCode    = "var res : sampler_comparison";
        resources.push_back(r);
    }

    // --- Multisampled textures ---
    {
        // depth, 2d, multisampled
        Resource r;
        r.texture = TextureResource{WGPUTextureSampleType_Depth, WGPUTextureViewDimension_2D, true};
        r.key     = "texture_depth_2d_true";
        r.wgslCode    = "var res : texture_depth_multisampled_2d";
        resources.push_back(r);
    }
    {
        // unfilterable-float, 2d, multisampled
        Resource r;
        r.texture = TextureResource{WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, true};
        r.key     = "texture_unfilterable-float_2d_true";
        r.wgslCode    = "var res : texture_multisampled_2d<f32>";
        resources.push_back(r);
    }
    {
        // sint, 2d, multisampled
        Resource r;
        r.texture = TextureResource{WGPUTextureSampleType_Sint, WGPUTextureViewDimension_2D, true};
        r.key     = "texture_sint_2d_true";
        r.wgslCode    = "var res : texture_multisampled_2d<i32>";
        resources.push_back(r);
    }
    {
        // uint, 2d, multisampled
        Resource r;
        r.texture = TextureResource{WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, true};
        r.key     = "texture_uint_2d_true";
        r.wgslCode    = "var res : texture_multisampled_2d<u32>";
        resources.push_back(r);
    }

    // --- Sampled textures (non-multisampled) ---
    // dims: 1d, 2d, 2d-array, 3d, cube, cube-array
    // types: float, unfilterable-float, sint, uint
    // WGSL types: f32, f32, i32, u32
    struct DimInfo {
        WGPUTextureViewDimension dim;
        std::string dimStr;    // key string segment (e.g. "2d-array")
        std::string wgslDim;   // WGSL name segment (e.g. "2d_array")
    };
    const std::vector<DimInfo> sampleDims = {
        {WGPUTextureViewDimension_1D,        "1d",        "1d"},
        {WGPUTextureViewDimension_2D,        "2d",        "2d"},
        {WGPUTextureViewDimension_2DArray,   "2d-array",  "2d_array"},
        {WGPUTextureViewDimension_3D,        "3d",        "3d"},
        {WGPUTextureViewDimension_Cube,      "cube",      "cube"},
        {WGPUTextureViewDimension_CubeArray, "cube-array","cube_array"},
    };
    struct SampleTypeInfo {
        WGPUTextureSampleType type;
        std::string typeStr;
        std::string wgslElem;
    };
    const std::vector<SampleTypeInfo> sampleTypes = {
        {WGPUTextureSampleType_Float,            "float",            "f32"},
        {WGPUTextureSampleType_UnfilterableFloat, "unfilterable-float", "f32"},
        {WGPUTextureSampleType_Sint,              "sint",              "i32"},
        {WGPUTextureSampleType_Uint,              "uint",              "u32"},
    };

    for (const auto& di : sampleDims) {
        for (const auto& ti : sampleTypes) {
            Resource r;
            r.texture = TextureResource{ti.type, di.dim, false};
            r.key     = "texture_" + ti.typeStr + "_" + di.dimStr + "_false";
            r.wgslCode    = "var res : texture_" + di.wgslDim + "<" + ti.wgslElem + ">";
            resources.push_back(r);
        }
    }

    // --- Depth textures (non-multisampled) ---
    // dims: 2d, 2d-array, cube, cube-array
    const std::vector<DimInfo> depthDims = {
        {WGPUTextureViewDimension_2D,        "2d",        "2d"},
        {WGPUTextureViewDimension_2DArray,   "2d-array",  "2d_array"},
        {WGPUTextureViewDimension_Cube,      "cube",      "cube"},
        {WGPUTextureViewDimension_CubeArray, "cube-array","cube_array"},
    };
    for (const auto& di : depthDims) {
        Resource r;
        r.texture = TextureResource{WGPUTextureSampleType_Depth, di.dim, false};
        r.key     = "texture_depth_" + di.dimStr + "_false";
        r.wgslCode    = "var res : texture_depth_" + di.wgslDim;
        resources.push_back(r);
    }

    // --- Storage textures ---
    // dims: 1d, 2d, 2d-array, 3d
    // formats: r32float, r32sint, r32uint
    // accesses: write-only, read-only, read-write
    // WGSL access: write-only -> write, read-only -> read, read-write -> read_write
    //   (replaceFirstDash after removing "-only")
    const std::vector<DimInfo> storageDims = {
        {WGPUTextureViewDimension_1D,       "1d",       "1d"},
        {WGPUTextureViewDimension_2D,       "2d",       "2d"},
        {WGPUTextureViewDimension_2DArray,  "2d-array", "2d_array"},
        {WGPUTextureViewDimension_3D,       "3d",       "3d"},
    };
    struct FormatInfo {
        WGPUTextureFormat fmt;
        std::string fmtStr;
    };
    const std::vector<FormatInfo> storageFormats = {
        {WGPUTextureFormat_R32Float, "r32float"},
        {WGPUTextureFormat_R32Sint,  "r32sint"},
        {WGPUTextureFormat_R32Uint,  "r32uint"},
    };
    struct AccessInfo {
        WGPUStorageTextureAccess access;
        std::string accessStr;   // e.g. "write-only"
        std::string wgslAccess;  // e.g. "write"
    };
    const std::vector<AccessInfo> storageAccesses = {
        {WGPUStorageTextureAccess_WriteOnly, "write-only", "write"},
        {WGPUStorageTextureAccess_ReadOnly,  "read-only",  "read"},
        {WGPUStorageTextureAccess_ReadWrite, "read-write", "read_write"},
    };

    for (const auto& di : storageDims) {
        for (const auto& fi : storageFormats) {
            for (const auto& ai : storageAccesses) {
                Resource r;
                r.storageTexture = StorageTextureResource{ai.access, fi.fmt, di.dim};
                r.key            = "storage_texture_" + di.dimStr + "_" + fi.fmtStr + "_" + ai.accessStr;
                r.wgslCode       = "var res : texture_storage_" + di.wgslDim
                                   + "<" + fi.fmtStr + "," + ai.wgslAccess + ">";
                resources.push_back(r);
            }
        }
    }

    return resources;
}

// ---------------------------------------------------------------------------
// Global resource list (initialised once)
// ---------------------------------------------------------------------------
static const std::vector<Resource>& allResources() {
    static const std::vector<Resource> s_resources = buildResources();
    return s_resources;
}

// ---------------------------------------------------------------------------
// Look up a resource by key
// ---------------------------------------------------------------------------
static const Resource& findResource(const std::string& key) {
    for (const auto& r : allResources()) {
        if (r.key == key) return r;
    }
    std::abort(); // should never happen
}

// ---------------------------------------------------------------------------
// getWGSLShaderForResource — mirrors utils.ts
// ---------------------------------------------------------------------------
static std::string getWGSLShaderForResource(const std::string& stage, const Resource& res) {
    std::string code = "@group(0) @binding(0) " + res.wgslCode + ";\n";

    code += "@" + stage;
    // (no compute in this test)

    std::string retTy;
    std::string retVal;
    if (stage == "vertex") {
        retTy  = " -> @builtin(position) vec4f";
        retVal = "return vec4f();";
    } else if (stage == "fragment") {
        retTy  = " -> @location(0) vec4f";
        retVal = "return vec4f();";
    }

    const std::string use = res.staticUse.empty() ? "res" : res.staticUse;
    code += "\nfn main()" + retTy + " {\n";
    code += "  _ = " + use + ";\n";
    code += "  " + retVal + "\n";
    code += "}\n";

    return code;
}

// ---------------------------------------------------------------------------
// getAPIBindGroupLayoutForResource — mirrors utils.ts
// ---------------------------------------------------------------------------
static WGPUBindGroupLayout getAPIBindGroupLayoutForResource(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUShaderStage stage,
    const Resource& res) {

    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding    = 0;
    entry.visibility = stage;

    if (res.buffer) {
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = res.buffer->type;
    } else if (res.sampler) {
        entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry.sampler.type = res.sampler->type;
    } else if (res.texture) {
        entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry.texture.sampleType   = res.texture->sampleType;
        entry.texture.viewDimension = res.texture->viewDimension;
        entry.texture.multisampled  = res.texture->multisampled ? WGPU_TRUE : WGPU_FALSE;
    } else if (res.storageTexture) {
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry.storageTexture.access      = res.storageTexture->access;
        entry.storageTexture.format      = res.storageTexture->format;
        entry.storageTexture.viewDimension = res.storageTexture->viewDimension;
    }

    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = 1;
    bglDesc.entries    = &entry;
    return t.createBindGroupLayoutTracked(bglDesc);
}

// ---------------------------------------------------------------------------
// doSampleTypesMatch — mirrors utils.ts
// ---------------------------------------------------------------------------
static bool doSampleTypesMatch(WGPUTextureSampleType api, WGPUTextureSampleType wgsl) {
    if (api == WGPUTextureSampleType_Float || api == WGPUTextureSampleType_UnfilterableFloat) {
        return wgsl == WGPUTextureSampleType_Float || wgsl == WGPUTextureSampleType_UnfilterableFloat;
    }
    return api == wgsl;
}

// ---------------------------------------------------------------------------
// doAccessesMatch — mirrors utils.ts
// ---------------------------------------------------------------------------
static bool doAccessesMatch(WGPUStorageTextureAccess api, WGPUStorageTextureAccess wgsl) {
    if (api == WGPUStorageTextureAccess_ReadWrite) {
        return wgsl == WGPUStorageTextureAccess_ReadWrite || wgsl == WGPUStorageTextureAccess_WriteOnly;
    }
    return api == wgsl;
}

// ---------------------------------------------------------------------------
// doResourcesMatch — mirrors utils.ts
// ---------------------------------------------------------------------------
static bool doResourcesMatch(const Resource& api, const Resource& wgsl) {
    if (api.buffer) {
        if (!wgsl.buffer) return false;
        return api.buffer->type == wgsl.buffer->type;
    }
    if (api.sampler) {
        if (!wgsl.sampler) return false;
        // Compatible if same type, or neither is comparison
        return (api.sampler->type == wgsl.sampler->type) ||
               (api.sampler->type != WGPUSamplerBindingType_Comparison &&
                wgsl.sampler->type != WGPUSamplerBindingType_Comparison);
    }
    if (api.texture) {
        if (!wgsl.texture) return false;
        return doSampleTypesMatch(api.texture->sampleType, wgsl.texture->sampleType) &&
               api.texture->viewDimension == wgsl.texture->viewDimension &&
               api.texture->multisampled  == wgsl.texture->multisampled;
    }
    if (api.storageTexture) {
        if (!wgsl.storageTexture) return false;
        return doAccessesMatch(api.storageTexture->access, wgsl.storageTexture->access) &&
               api.storageTexture->format        == wgsl.storageTexture->format &&
               api.storageTexture->viewDimension == wgsl.storageTexture->viewDimension;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Build resource key Value list for param builder
// ---------------------------------------------------------------------------
static std::vector<Value> resourceKeyValues() {
    std::vector<Value> vals;
    for (const auto& r : allResources()) {
        vals.push_back(Value(r.key));
    }
    return vals;
}

// ---------------------------------------------------------------------------
// test: resource_compatibility
// ---------------------------------------------------------------------------
CTS_TEST(g, "resource_compatibility")
    .desc("Tests validation of resource (bind group) compatibility between pipeline layout and WGSL shader")
    .params([](ParamsBuilder u) {
        return u
            .combine("stage",       {Value(std::string("vertex")), Value(std::string("fragment"))})
            .combine("apiResource", resourceKeyValues())
            .filter([](const ParamRecord& p) {
                const std::string stage      = valueAs<std::string>(*findParam(p, "stage"));
                const std::string apiResKey  = valueAs<std::string>(*findParam(p, "apiResource"));
                const Resource&   apiRes     = findResource(apiResKey);
                if (stage == "vertex") {
                    if (apiRes.buffer && apiRes.buffer->type == WGPUBufferBindingType_Storage) {
                        return false;
                    }
                    if (apiRes.storageTexture &&
                        apiRes.storageTexture->access != WGPUStorageTextureAccess_ReadOnly) {
                        return false;
                    }
                }
                return true;
            })
            .beginSubcases()
            .combine("isAsync",     {Value(false), Value(true)})
            .combine("wgslResource", resourceKeyValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stage       = t.param<std::string>("stage");
        const std::string apiResKey   = t.param<std::string>("apiResource");
        const std::string wgslResKey  = t.param<std::string>("wgslResource");
        // isAsync: harness has no async pipeline creation wrapper; both paths use synchronous validation
        (void)t.param<bool>("isAsync");

        const Resource& apiRes  = findResource(apiResKey);
        const Resource& wgslRes = findResource(wgslResKey);

        // Gate: vertex stage with read-write/storage wgsl resource
        // (some of these are already filtered at the case level for apiResource,
        //  but wgslResource is a subcase so filter here too)
        if (stage == "vertex") {
            if (wgslRes.buffer && wgslRes.buffer->type == WGPUBufferBindingType_Storage) {
                t.skip("Read-Write Storage buffers cannot be used in vertex shaders");
            }
            if (wgslRes.storageTexture &&
                wgslRes.storageTexture->access != WGPUStorageTextureAccess_ReadOnly) {
                t.skip("Non-read-only storage textures cannot be used in vertex shaders");
            }
        }

        // Gate: texture view dimension support
        if (wgslRes.texture) {
            t.skipIfTextureViewDimensionNotSupported(wgslRes.texture->viewDimension);
        }
        if (wgslRes.storageTexture) {
            t.skipIfTextureViewDimensionNotSupported(wgslRes.storageTexture->viewDimension);
        }

        // Empty vertex / fragment shaders used when the resource is in the other stage.
        static const char* kEmptyVS =
            "@vertex\n"
            "fn main() -> @builtin(position) vec4f {\n"
            "  return vec4f();\n"
            "}\n";
        static const char* kEmptyFS =
            "@fragment\n"
            "fn main() -> @location(0) vec4f {\n"
            "  return vec4f();\n"
            "}\n";

        // Build the WGSL code for the tested stage.
        const std::string resourceShader = getWGSLShaderForResource(stage, wgslRes);
        const std::string vsCode = (stage == "vertex")   ? resourceShader : kEmptyVS;
        const std::string fsCode = (stage == "fragment") ? resourceShader : kEmptyFS;

        const WGPUShaderStage gpuStage = (stage == "vertex")
            ? WGPUShaderStage_Vertex
            : WGPUShaderStage_Fragment;

        // Build pipeline layout from the API resource descriptor.
        WGPUBindGroupLayout bgl = getAPIBindGroupLayoutForResource(t, gpuStage, apiRes);
        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts     = &bgl;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(plDesc);

        // Build shader modules.
        WGPUShaderModule vsModule = t.createShaderModuleTracked(vsCode);
        WGPUShaderModule fsModule = t.createShaderModuleTracked(fsCode);

        // Determine expected outcome.
        const bool shouldError = !doResourcesMatch(apiRes, wgslRes);

        // Create render pipeline: error fires at createRenderPipeline (eager model).
        t.expectValidationError([&] {
            WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
            colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            fragment.module      = fsModule;
            fragment.entryPoint  = sv("main");
            fragment.targetCount = 1;
            fragment.targets     = &colorTarget;

            WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            desc.layout             = layout;
            desc.vertex.module      = vsModule;
            desc.vertex.entryPoint  = sv("main");
            desc.fragment           = &fragment;
            t.createRenderPipelineTracked(desc);
        }, shouldError);
    });

} // namespace
