// Ported from gpuweb/cts src/webgpu/api/validation/layout_shader_compat.spec.ts

// Note: t.isCompatibility guards (maxStorageBuffersInVertexStage,
// maxStorageTexturesInVertexStage, etc.) are omitted — AllFeaturesMaxLimitsGpuTest
// always runs with max limits so those limits are never 0 in this harness.
// Note: isAsync=true sub-cases are not parameterized separately because the C
// harness has no async pipeline-creation wrapper; both paths use the same
// synchronous validation path.

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,layout_shader_compat",
    "TODO:\n"
    "- interface matching between pipeline layout and shader\n"
    "    - x= bind group index values, binding index values, multiple bindings\n"
    "    - x= {superset, subset}");

// ---------------------------------------------------------------------------
// Bindable resource type (mirrors BindableResourceType in the upstream spec)
// ---------------------------------------------------------------------------
enum class BindableResourceType {
    uniformBuf,
    storageBuf,
    readonlyStorageBuf,
    filtSamp,
    nonFiltSamp,
    compareSamp,
    sampledTex,
    sampledTexMS,
    readonlyStorageTex,
    writeonlyStorageTex,
    readwriteStorageTex,
};

static const char* bindableResourceTypeName(BindableResourceType r) {
    switch (r) {
        case BindableResourceType::uniformBuf:          return "uniformBuf";
        case BindableResourceType::storageBuf:          return "storageBuf";
        case BindableResourceType::readonlyStorageBuf:  return "readonlyStorageBuf";
        case BindableResourceType::filtSamp:            return "filtSamp";
        case BindableResourceType::nonFiltSamp:         return "nonFiltSamp";
        case BindableResourceType::compareSamp:         return "compareSamp";
        case BindableResourceType::sampledTex:          return "sampledTex";
        case BindableResourceType::sampledTexMS:        return "sampledTexMS";
        case BindableResourceType::readonlyStorageTex:  return "readonlyStorageTex";
        case BindableResourceType::writeonlyStorageTex: return "writeonlyStorageTex";
        case BindableResourceType::readwriteStorageTex: return "readwriteStorageTex";
    }
    return "unknown";
}

// All bindable resource types (mirrors kBindableResources).
static const BindableResourceType kBindableResources[] = {
    BindableResourceType::uniformBuf,
    BindableResourceType::storageBuf,
    BindableResourceType::readonlyStorageBuf,
    BindableResourceType::filtSamp,
    BindableResourceType::nonFiltSamp,
    BindableResourceType::compareSamp,
    BindableResourceType::sampledTex,
    BindableResourceType::sampledTexMS,
    BindableResourceType::readonlyStorageTex,
    BindableResourceType::writeonlyStorageTex,
    BindableResourceType::readwriteStorageTex,
};

// ---------------------------------------------------------------------------
// Helper: build WGPUBindGroupLayout for a binding type
// Mirrors F.createPipelineLayout in the upstream.
// ---------------------------------------------------------------------------
static WGPUBindGroupLayout createBindGroupLayoutForResource(
    AllFeaturesMaxLimitsGpuTest& t,
    BindableResourceType resourceType,
    WGPUShaderStage visibility)
{
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding    = 0;
    entry.visibility = visibility;

    switch (resourceType) {
        case BindableResourceType::uniformBuf:
            entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type = WGPUBufferBindingType_Uniform;
            break;
        case BindableResourceType::storageBuf:
            entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type = WGPUBufferBindingType_Storage;
            break;
        case BindableResourceType::readonlyStorageBuf:
            entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            break;
        case BindableResourceType::compareSamp:
            entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
            entry.sampler.type = WGPUSamplerBindingType_Comparison;
            break;
        case BindableResourceType::filtSamp:
            entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
            entry.sampler.type = WGPUSamplerBindingType_Filtering;
            break;
        case BindableResourceType::nonFiltSamp:
            entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
            entry.sampler.type = WGPUSamplerBindingType_NonFiltering;
            break;
        case BindableResourceType::sampledTex:
            entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
            entry.texture.sampleType  = WGPUTextureSampleType_UnfilterableFloat;
            entry.texture.multisampled = WGPU_FALSE;
            break;
        case BindableResourceType::sampledTexMS:
            entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
            entry.texture.sampleType  = WGPUTextureSampleType_UnfilterableFloat;
            entry.texture.multisampled = WGPU_TRUE;
            break;
        case BindableResourceType::readonlyStorageTex:
            entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
            entry.storageTexture.format = WGPUTextureFormat_R32Float;
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
            break;
        case BindableResourceType::writeonlyStorageTex:
            entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
            entry.storageTexture.format = WGPUTextureFormat_R32Float;
            entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            break;
        case BindableResourceType::readwriteStorageTex:
            entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
            entry.storageTexture.format = WGPUTextureFormat_R32Float;
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
            break;
    }

    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = 1;
    bglDesc.entries    = &entry;
    return t.createBindGroupLayoutTracked(bglDesc);
}

// ---------------------------------------------------------------------------
// Helper: build WGPUPipelineLayout wrapping the BGL.
// Mirrors F.createPipelineLayout in the upstream.
// ---------------------------------------------------------------------------
static WGPUPipelineLayout createPipelineLayoutForResource(
    AllFeaturesMaxLimitsGpuTest& t,
    BindableResourceType resourceType,
    WGPUShaderStage visibility)
{
    WGPUBindGroupLayout bgl = createBindGroupLayoutForResource(t, resourceType, visibility);
    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts     = &bgl;
    return t.createPipelineLayoutTracked(plDesc);
}

// ---------------------------------------------------------------------------
// Helper: WGSL variable declaration for @group(0) @binding(0).
// Mirrors F.getBindableResourceShaderDeclaration in the upstream.
// ---------------------------------------------------------------------------
static std::string bindableResourceShaderDeclaration(BindableResourceType r) {
    switch (r) {
        case BindableResourceType::compareSamp:
            return "var tmp : sampler_comparison";
        case BindableResourceType::filtSamp:
        case BindableResourceType::nonFiltSamp:
            return "var tmp : sampler";
        case BindableResourceType::sampledTex:
            return "var tmp : texture_2d<f32>";
        case BindableResourceType::sampledTexMS:
            return "var tmp : texture_multisampled_2d<f32>";
        case BindableResourceType::storageBuf:
            return "var<storage, read_write> tmp : vec4u";
        case BindableResourceType::readonlyStorageBuf:
            return "var<storage, read> tmp : vec4u";
        case BindableResourceType::uniformBuf:
            return "var<uniform> tmp : vec4u;";
        case BindableResourceType::readonlyStorageTex:
            return "var tmp : texture_storage_2d<r32float, read>";
        case BindableResourceType::writeonlyStorageTex:
            return "var tmp : texture_storage_2d<r32float, write>";
        case BindableResourceType::readwriteStorageTex:
            return "var tmp : texture_storage_2d<r32float, read_write>";
    }
    return "";
}

// ---------------------------------------------------------------------------
// Compatibility filter: mirrors bindingResourceCompatibleWithShaderStages.
// Returns false if the resource type cannot be used in a vertex shader.
// ---------------------------------------------------------------------------
static bool bindingResourceCompatibleWithShaderStages(
    BindableResourceType bindingResource,
    WGPUShaderStage shaderStages)
{
    if (shaderStages & WGPUShaderStage_Vertex) {
        switch (bindingResource) {
            case BindableResourceType::writeonlyStorageTex:
            case BindableResourceType::readwriteStorageTex:
            case BindableResourceType::storageBuf:
                return false;
            default:
                break;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from a null-terminated string literal.
// ---------------------------------------------------------------------------
static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// ---------------------------------------------------------------------------
// test: pipeline_layout_shader_exact_match
// ---------------------------------------------------------------------------
CTS_TEST(g, "pipeline_layout_shader_exact_match")
    .desc(
        "Test that the binding type in the pipeline layout must match the related declaration in shader.\n"
        "Note that read-write storage textures in the pipeline layout can match write-only storage textures\n"
        "in the shader.")
    .params([](ParamsBuilder u) {
        // Build Values for bindingInPipelineLayout and bindingInShader.
        std::vector<Value> allResourceValues;
        for (BindableResourceType r : kBindableResources) {
            allResourceValues.push_back(Value(std::string(bindableResourceTypeName(r))));
        }

        // Build Values for pipelineLayoutVisibility (kShaderStageCombinations).
        std::vector<Value> visibilityValues;
        for (WGPUShaderStage v : kShaderStageCombinations) {
            visibilityValues.push_back(Value(static_cast<int>(v)));
        }

        // Build Values for shaderStageWithBinding (kShaderStages).
        std::vector<Value> stageValues;
        for (WGPUShaderStage s : kShaderStages) {
            stageValues.push_back(Value(static_cast<int>(s)));
        }

        return u
            .combine("bindingInPipelineLayout", allResourceValues)
            .combine("bindingInShader", allResourceValues)
            .filter([](const ParamRecord& p) {
                const std::string shaderStr = valueAs<std::string>(*findParam(p, "bindingInShader"));
                // We don't test using non-filtering sampler in shader because it has the same
                // declaration as filtering sampler.
                return shaderStr != "nonFiltSamp";
            })
            .beginSubcases()
            .combine("pipelineLayoutVisibility", visibilityValues)
            .combine("shaderStageWithBinding", stageValues)
            .combine("isBindingStaticallyUsed", {Value(true), Value(false)})
            .filter([](const ParamRecord& p) {
                const std::string layoutStr = valueAs<std::string>(*findParam(p, "bindingInPipelineLayout"));
                const std::string shaderStr = valueAs<std::string>(*findParam(p, "bindingInShader"));
                const int vis   = valueAs<int>(*findParam(p, "pipelineLayoutVisibility"));
                const int stage = valueAs<int>(*findParam(p, "shaderStageWithBinding"));

                // Resolve resource types.
                BindableResourceType layoutRes = BindableResourceType::uniformBuf;
                BindableResourceType shaderRes = BindableResourceType::uniformBuf;
                for (BindableResourceType r : kBindableResources) {
                    if (bindableResourceTypeName(r) == layoutStr) layoutRes = r;
                    if (bindableResourceTypeName(r) == shaderStr)  shaderRes = r;
                }

                if (!bindingResourceCompatibleWithShaderStages(
                        layoutRes, static_cast<WGPUShaderStage>(vis))) {
                    return false;
                }
                if (!bindingResourceCompatibleWithShaderStages(
                        shaderRes, static_cast<WGPUShaderStage>(stage))) {
                    return false;
                }
                return true;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string layoutStr = t.param<std::string>("bindingInPipelineLayout");
        const std::string shaderStr = t.param<std::string>("bindingInShader");
        const WGPUShaderStage pipelineLayoutVisibility =
            static_cast<WGPUShaderStage>(t.param<int>("pipelineLayoutVisibility"));
        const WGPUShaderStage shaderStageWithBinding =
            static_cast<WGPUShaderStage>(t.param<int>("shaderStageWithBinding"));
        const bool isBindingStaticallyUsed = t.param<bool>("isBindingStaticallyUsed");

        // Resolve resource types.
        BindableResourceType bindingInPipelineLayout = BindableResourceType::uniformBuf;
        BindableResourceType bindingInShader         = BindableResourceType::uniformBuf;
        for (BindableResourceType r : kBindableResources) {
            if (bindableResourceTypeName(r) == layoutStr) bindingInPipelineLayout = r;
            if (bindableResourceTypeName(r) == shaderStr) bindingInShader         = r;
        }

        // Build pipeline layout.
        WGPUPipelineLayout layout = createPipelineLayoutForResource(
            t, bindingInPipelineLayout, pipelineLayoutVisibility);

        // Build the @group(0) @binding(0) declaration string.
        const std::string bindResourceDeclaration =
            "@group(0) @binding(0) " + bindableResourceShaderDeclaration(bindingInShader);

        // Whether the binding is statically used in the shader body.
        const std::string staticallyUseBinding =
            isBindingStaticallyUsed ? "_ = tmp; " : "";

        // Determine success:
        // If not statically used the pipeline is always valid.
        // If statically used, the types must match (with the special cases below).
        bool success = true;
        if (isBindingStaticallyUsed) {
            success = (bindingInPipelineLayout == bindingInShader);

            // Filtering and non-filtering both have the same shader declaration (var tmp : sampler).
            if (!success) {
                success = (bindingInPipelineLayout == BindableResourceType::nonFiltSamp &&
                           bindingInShader          == BindableResourceType::filtSamp);
            }

            // read-write storage texture in layout can match write-only in shader.
            if (!success) {
                success = (bindingInPipelineLayout == BindableResourceType::readwriteStorageTex &&
                           bindingInShader          == BindableResourceType::writeonlyStorageTex);
            }

            // The shader stage using the resource must be included in the layout visibility.
            success = success && ((pipelineLayoutVisibility & shaderStageWithBinding) != 0);
        }

        switch (shaderStageWithBinding) {
            case WGPUShaderStage_Compute: {
                const std::string computeShader =
                    bindResourceDeclaration + ";\n"
                    "@compute @workgroup_size(1)\n"
                    "fn main() {\n"
                    "  " + staticallyUseBinding + "\n"
                    "}\n";

                t.expectValidationError([&] {
                    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                    desc.layout = layout;
                    desc.compute.module     = t.createShaderModuleTracked(computeShader);
                    desc.compute.entryPoint = sv("main");
                    t.createComputePipelineTracked(desc);
                }, !success);
                break;
            }
            case WGPUShaderStage_Vertex: {
                const std::string vertexShader =
                    bindResourceDeclaration + ";\n"
                    "@vertex\n"
                    "fn main() -> @builtin(position) vec4f {\n"
                    "  " + staticallyUseBinding + "\n"
                    "  return vec4f();\n"
                    "}\n";

                t.expectValidationError([&] {
                    WGPUShaderModule vsModule = t.createShaderModuleTracked(vertexShader);

                    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                    desc.layout             = layout;
                    desc.vertex.module      = vsModule;
                    desc.vertex.entryPoint  = sv("main");

                    // depth32float attachment so the render pipeline has a valid target
                    // (matches the upstream depthStencil: { format: 'depth32float', ... }).
                    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
                    ds.format              = WGPUTextureFormat_Depth32Float;
                    ds.depthWriteEnabled   = WGPUOptionalBool_True;
                    ds.depthCompare        = WGPUCompareFunction_Always;
                    desc.depthStencil      = &ds;

                    t.createRenderPipelineTracked(desc);
                }, !success);
                break;
            }
            case WGPUShaderStage_Fragment: {
                const std::string fragmentShader =
                    bindResourceDeclaration + ";\n"
                    "@fragment\n"
                    "fn main() -> @location(0) vec4f {\n"
                    "  " + staticallyUseBinding + "\n"
                    "  return vec4f();\n"
                    "}\n";

                t.expectValidationError([&] {
                    WGPUShaderModule vsModule = t.createShaderModuleTracked(
                        "@vertex\n"
                        "fn main() -> @builtin(position) vec4f {\n"
                        "  return vec4f();\n"
                        "}\n");

                    WGPUShaderModule fsModule = t.createShaderModuleTracked(fragmentShader);

                    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
                    colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
                    colorTarget.writeMask = WGPUColorWriteMask_All;

                    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                    fragment.module      = fsModule;
                    fragment.entryPoint  = sv("main");
                    fragment.targetCount = 1;
                    fragment.targets     = &colorTarget;

                    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                    desc.layout            = layout;
                    desc.vertex.module     = vsModule;
                    desc.vertex.entryPoint = sv("main");
                    desc.fragment          = &fragment;

                    t.createRenderPipelineTracked(desc);
                }, !success);
                break;
            }
            default:
                t.fail("unexpected shaderStageWithBinding");
        }
    });

} // namespace
