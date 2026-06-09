// Ported from gpuweb/cts src/webgpu/api/validation/non_filterable_texture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,non_filterable_texture",
    "Tests that non-filterable textures used with filtering samplers generate a validation error.");

// Returns the WGPUStringView sentinel used when passing null-terminated strings to WGPU C API.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// Build the WGSL shader code used by the test.
// The shader exposes:
//   @group(0) @binding(0) var t : <textureType>
//   @group(groupNdx) @binding(1) var s : sampler
// and uses textureGather to reference both, forcing the validation engine to
// check that the sampler and texture types are compatible.
std::string buildShader(WGPUTextureSampleType sampleType,
                        WGPUTextureViewDimension viewDimension,
                        int groupNdx) {
    // Map sample type to WGSL element type.
    std::string elemType;
    bool isDepth = false;
    switch (sampleType) {
        case WGPUTextureSampleType_Sint:
            elemType = "i32";
            break;
        case WGPUTextureSampleType_Uint:
            elemType = "u32";
            break;
        case WGPUTextureSampleType_Float:
        case WGPUTextureSampleType_UnfilterableFloat:
            elemType = "f32";
            break;
        case WGPUTextureSampleType_Depth:
            isDepth = true;
            break;
        default:
            break;
    }

    // Map view dimension to WGSL texture type suffix and coord type.
    // Upstream: dimensionSuffix = viewDimension.replace('-', '_')
    std::string dimSuffix;
    std::string coordExpr;
    std::string layerSuffix;
    switch (viewDimension) {
        case WGPUTextureViewDimension_2D:
            dimSuffix  = "2d";
            coordExpr  = "vec2f(0)";
            layerSuffix = "";
            break;
        case WGPUTextureViewDimension_2DArray:
            dimSuffix  = "2d_array";
            coordExpr  = "vec2f(0)";
            layerSuffix = ", 0";
            break;
        case WGPUTextureViewDimension_Cube:
            dimSuffix  = "cube";
            coordExpr  = "vec3f(0)";
            layerSuffix = "";
            break;
        case WGPUTextureViewDimension_CubeArray:
            dimSuffix  = "cube_array";
            coordExpr  = "vec3f(0)";
            layerSuffix = ", 0";
            break;
        default:
            break;
    }

    std::string textureType;
    std::string gatherComponent;
    if (isDepth) {
        textureType     = "texture_depth_" + dimSuffix;
        gatherComponent = ""; // no component argument for depth textureGather
    } else {
        textureType     = "texture_" + dimSuffix + "<" + elemType + ">";
        gatherComponent = "0, "; // component argument (e.g. 0,)
    }

    std::string groupNdxStr = std::to_string(groupNdx);

    return std::string(R"(
      @group(0) @binding(0) var t: )") + textureType + R"(;
      @group()" + groupNdxStr + R"() @binding(1) var s: sampler;

      fn test() {
        _ = textureGather()" + gatherComponent + R"(t, s, )" + coordExpr + layerSuffix + R"();
      }

      @compute @workgroup_size(1) fn cs() {
        test();
      }

      @vertex fn vs() -> @builtin(position) vec4f {
        return vec4f(0);
      }

      @fragment fn fs() -> @location(0) vec4f {
        test();
        return vec4f(0);
      }
      )";
}

CTS_TEST(g, "non_filterable_texture_with_filtering_sampler")
    .desc("test that createXXXPipeline generates a validation error if a depth/u32/i32 texture binding is used with a filtering sampler binding")
    .params([](ParamsBuilder u) {
        return u
            .combine("pipeline",       {Value("compute"), Value("render")})
            .combine("async",          {Value(true), Value(false)})
            .combine("sampleType",     {
                Value(std::string(textureSampleTypeIdentifier(WGPUTextureSampleType_Sint))),
                Value(std::string(textureSampleTypeIdentifier(WGPUTextureSampleType_Uint))),
                Value(std::string(textureSampleTypeIdentifier(WGPUTextureSampleType_Float))),
                Value(std::string(textureSampleTypeIdentifier(WGPUTextureSampleType_UnfilterableFloat))),
                Value(std::string(textureSampleTypeIdentifier(WGPUTextureSampleType_Depth))),
            })
            .combine("viewDimension", {
                Value(std::string(textureViewDimensionIdentifier(WGPUTextureViewDimension_2D))),
                Value(std::string(textureViewDimensionIdentifier(WGPUTextureViewDimension_2DArray))),
                Value(std::string(textureViewDimensionIdentifier(WGPUTextureViewDimension_Cube))),
                Value(std::string(textureViewDimensionIdentifier(WGPUTextureViewDimension_CubeArray))),
            })
            .combine("sameGroup", {Value(true), Value(false)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string pipelineStr   = t.param<std::string>("pipeline");
        const bool        async         = t.param<bool>("async");
        const WGPUTextureSampleType  sampleType  = parseTextureSampleType(t.param<std::string>("sampleType"));
        const WGPUTextureViewDimension viewDimension = parseTextureViewDimension(t.param<std::string>("viewDimension"));
        const bool sameGroup = t.param<bool>("sameGroup");

        // Upstream: t.skipIfTextureViewDimensionNotSupported(viewDimension)
        t.skipIfTextureViewDimensionNotSupported(viewDimension);

        // Upstream: const success = sampleType === 'float'
        const bool success = (sampleType == WGPUTextureSampleType_Float);
        const bool shouldError = !success;

        // Sampler BGL entry (binding 1, filtering sampler)
        WGPUBindGroupLayoutEntry samplerEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        samplerEntry.binding    = 1;
        samplerEntry.visibility = static_cast<WGPUShaderStage>(
            WGPUShaderStage_Compute | WGPUShaderStage_Fragment);
        samplerEntry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        samplerEntry.sampler.type = WGPUSamplerBindingType_Filtering;

        // Texture BGL entry (binding 0)
        WGPUBindGroupLayoutEntry textureEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        textureEntry.binding    = 0;
        textureEntry.visibility = static_cast<WGPUShaderStage>(
            WGPUShaderStage_Compute | WGPUShaderStage_Fragment);
        textureEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        textureEntry.texture.sampleType    = sampleType;
        textureEntry.texture.viewDimension = viewDimension;
        textureEntry.texture.multisampled  = WGPU_FALSE;

        // Build bindGroup0 entries
        std::vector<WGPUBindGroupLayoutEntry> bg0Entries;
        bg0Entries.push_back(textureEntry);
        if (sameGroup) {
            bg0Entries.push_back(samplerEntry);
        }

        WGPUBindGroupLayoutDescriptor bg0Desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bg0Desc.entryCount = bg0Entries.size();
        bg0Desc.entries    = bg0Entries.data();
        WGPUBindGroupLayout bg0Layout = t.createBindGroupLayoutTracked(bg0Desc);

        // Build pipeline layout
        std::vector<WGPUBindGroupLayout> bgLayouts;
        bgLayouts.push_back(bg0Layout);

        if (!sameGroup) {
            WGPUBindGroupLayoutDescriptor bg1Desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bg1Desc.entryCount = 1;
            bg1Desc.entries    = &samplerEntry;
            WGPUBindGroupLayout bg1Layout = t.createBindGroupLayoutTracked(bg1Desc);
            bgLayouts.push_back(bg1Layout);
        }

        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = bgLayouts.size();
        plDesc.bindGroupLayouts     = bgLayouts.data();
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(plDesc);

        // Shader source — groupNdx is 0 if sameGroup, 1 otherwise
        const int groupNdx = sameGroup ? 0 : 1;
        const std::string wgsl = buildShader(sampleType, viewDimension, groupNdx);
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

        // doCreateComputePipelineTest / doCreateRenderPipelineTest
        // For async=true the upstream uses createXxxPipelineAsync (promise rejection).
        // The C++ harness has no async pipeline-creation wrapper, so both async=true
        // and async=false use the synchronous expectValidationError path.
        // The validation outcome is identical; only the API path differs.
        (void)async;

        if (pipelineStr == "compute") {
            t.expectValidationError([&] {
                WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                desc.layout          = layout;
                desc.compute.module  = shaderModule;
                desc.compute.entryPoint = sv("cs");
                t.createComputePipelineTracked(desc);
            }, shouldError);
        } else {
            t.expectValidationError([&] {
                WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
                colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
                colorTarget.writeMask = WGPUColorWriteMask_All;

                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                fragment.module      = shaderModule;
                fragment.entryPoint  = sv("fs");
                fragment.targetCount = 1;
                fragment.targets     = &colorTarget;

                WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                desc.layout               = layout;
                desc.vertex.module        = shaderModule;
                desc.vertex.entryPoint    = sv("vs");
                desc.primitive.topology   = WGPUPrimitiveTopology_TriangleList;
                desc.multisample.count    = 1;
                desc.fragment             = &fragment;
                t.createRenderPipelineTracked(desc);
            }, shouldError);
        }
    });

} // namespace
