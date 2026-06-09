// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/misc.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: the async=true sub-cases are exercised via the same synchronous pipeline-creation path
// (the harness has no async pipeline-creation wrapper); validation behaviour is identical.
// Note: pipeline_layout,device_mismatch — mismatchedDevice() is available on GpuTest directly.
// Note: external_texture — texture_external WGSL type compiled in the shader; no GPUExternalTexture
//   object needed for pipeline creation itself, so ported faithfully.

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,misc",
    "misc createRenderPipeline and createRenderPipelineAsync validation tests.");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns a WGPUStringView for a null-terminated C string.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

// Default vertex shader: mirrors kDefaultVertexShaderCode from upstream.
static const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Default fragment shader: mirrors getFragmentShaderCodeWithOutput([{values:[0,1,0,1],
// plainType:'f32', componentCount:4}]) for rgba8unorm.
static const char* kDefaultFragmentShaderCode =
    "struct Outputs {\n"
    "  @location(0) o0 : vec4<f32>,\n"
    "}\n"
    "\n"
    "@fragment fn main() -> Outputs {\n"
    "    return Outputs(vec4<f32>(0.0000, 1.0000, 0.0000, 1.0000));\n"
    "}\n";

// Holder to keep all objects alive for the duration of a pipeline creation call.
struct RenderPipelineDescriptorHolder {
    WGPUShaderModule vertexModule    = nullptr;
    WGPUShaderModule fragmentModule  = nullptr;
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState fragment       = WGPU_FRAGMENT_STATE_INIT;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPUPipelineLayout pipelineLayout  = nullptr;
    WGPURenderPipelineDescriptor desc  = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

// Build a default render pipeline descriptor.
// Mirrors CreateRenderPipelineValidationTest::getDescriptor with default options:
//   targets: [{ format: 'rgba8unorm' }], no depthStencil, not vertex-only.
RenderPipelineDescriptorHolder buildDefaultDescriptor(AllFeaturesMaxLimitsGpuTest& t) {
    RenderPipelineDescriptorHolder h;

    h.vertexModule   = t.createShaderModuleTracked(kDefaultVertexShaderCode);
    h.fragmentModule = t.createShaderModuleTracked(kDefaultFragmentShaderCode);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts     = nullptr;
    h.pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    h.colorTarget        = WGPU_COLOR_TARGET_STATE_INIT;
    h.colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    h.fragment             = WGPU_FRAGMENT_STATE_INIT;
    h.fragment.module      = h.fragmentModule;
    h.fragment.entryPoint  = sv("main");
    h.fragment.targetCount = 1;
    h.fragment.targets     = &h.colorTarget;

    h.desc                   = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout            = h.pipelineLayout;
    h.desc.vertex.module     = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.fragment          = &h.fragment;

    return h;
}

// Build a vertex-only render pipeline descriptor with optional depthStencil and optional color.
// Mirrors getDescriptor({ noFragment: true, depthStencil: ..., targets: ... }).
// Note: When noFragment=true, the upstream ignores the targets list (sets fragment=undefined).
struct VertexOnlyDescriptorHolder {
    WGPUShaderModule vertexModule     = nullptr;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    WGPUPipelineLayout pipelineLayout  = nullptr;
    WGPURenderPipelineDescriptor desc  = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

VertexOnlyDescriptorHolder buildVertexOnlyDescriptor(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat depthStencilFormat,  // WGPUTextureFormat_Undefined means no depthStencil
    bool /*hasColor*/)  // ignored in vertex-only pipelines per upstream
{
    VertexOnlyDescriptorHolder h;

    h.vertexModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts     = nullptr;
    h.pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    h.desc                   = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout            = h.pipelineLayout;
    h.desc.vertex.module     = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.fragment          = nullptr;  // vertex-only: no fragment state

    if (depthStencilFormat != WGPUTextureFormat_Undefined) {
        h.depthStencil               = WGPU_DEPTH_STENCIL_STATE_INIT;
        h.depthStencil.format        = depthStencilFormat;
        h.depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        h.depthStencil.depthCompare  = WGPUCompareFunction_Always;
        h.desc.depthStencil          = &h.depthStencil;
    }

    return h;
}

// Mirrors vtu.doCreateRenderPipelineTest for the synchronous path.
// For isAsync=true the C++ harness has no async wrapper; both paths use expectValidationError.
void doCreateRenderPipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    const WGPURenderPipelineDescriptor& desc)
{
    t.expectValidationError([&] {
        t.createRenderPipelineTracked(desc);
    }, !success);
}

// Build the list of "possible storage texture formats" matching kPossibleStorageTextureFormats from
// upstream format_info.ts:
//   [...kRegularTextureFormats.filter(f => color?.storage), 'bgra8unorm',
//    ...kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly]
// In C++: kStorageTextureFormats + BGRA8Unorm + kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly.
std::vector<WGPUTextureFormat> possibleStorageTextureFormats() {
    std::vector<WGPUTextureFormat> result;
    for (WGPUTextureFormat f : kStorageTextureFormats) {
        result.push_back(f);
    }
    result.push_back(WGPUTextureFormat_BGRA8Unorm);
    for (WGPUTextureFormat f : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        result.push_back(f);
    }
    return result;
}

std::vector<Value> possibleStorageTextureFormatValues() {
    const std::vector<WGPUTextureFormat> formats = possibleStorageTextureFormats();
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat f : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(f)));
    }
    return values;
}

// ---------------------------------------------------------------------------
// test: basic
// ---------------------------------------------------------------------------

CTS_TEST(g, "basic")
    .desc("Test basic usage of createRenderPipeline.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // isAsync: both paths use the same synchronous expectValidationError
        auto h = buildDefaultDescriptor(t);
        doCreateRenderPipelineTest(t, /*success=*/true, h.desc);
    });

// ---------------------------------------------------------------------------
// test: no_attachment
// ---------------------------------------------------------------------------

CTS_TEST(g, "no_attachment")
    .desc("Test that createRenderPipeline fails without any attachment.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Descriptor: noFragment=true, depthStencil=undefined → no attachments → must fail.
        WGPUShaderModule vertexModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);

        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 0;
        plDesc.bindGroupLayouts     = nullptr;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(plDesc);

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                  = layout;
        desc.vertex.module           = vertexModule;
        desc.vertex.entryPoint       = sv("main");
        desc.fragment                = nullptr;
        desc.depthStencil            = nullptr;

        doCreateRenderPipelineTest(t, /*success=*/false, desc);
    });

// ---------------------------------------------------------------------------
// test: vertex_state_only
// ---------------------------------------------------------------------------

// Maps depthStencilFormat string param to WGPUTextureFormat.
// Empty string means "undefined" (no depthStencil).
WGPUTextureFormat depthStencilFormatFromString(const std::string& s) {
    if (s == "depth24plus")          return WGPUTextureFormat_Depth24Plus;
    if (s == "depth24plus-stencil8") return WGPUTextureFormat_Depth24PlusStencil8;
    if (s == "depth32float")         return WGPUTextureFormat_Depth32Float;
    return WGPUTextureFormat_Undefined;  // empty string → no depthStencil
}

CTS_TEST(g, "vertex_state_only")
    .desc(
        "Tests creating vertex-state-only render pipeline. A vertex-only render pipeline has no "
        "fragment state (and thus has no color state), and must have a depth-stencil state as an "
        "attachment is required.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("depthStencilFormat", {
                Value(std::string("depth24plus")),
                Value(std::string("depth24plus-stencil8")),
                Value(std::string("depth32float")),
                Value(std::string("")),
            })
            .combine("hasColor", {Value(false), Value(true)})
            .filter([](const ParamRecord& params) {
                // Render pipeline needs at least one attachment.
                // Filter out: hasColor==false && depthStencilFormat==""
                const std::string dsFormat = valueAs<std::string>(
                    *findParam(params, "depthStencilFormat"));
                const bool hasColor = valueAs<bool>(*findParam(params, "hasColor"));
                return !(hasColor == false && dsFormat.empty());
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string dsFormatStr = t.param<std::string>("depthStencilFormat");
        // hasColor is explicitly present in the upstream params matrix for documentation, but
        // has no effect on the descriptor since noFragment=true ignores targets.
        // We still read it to faithfully mirror the params, but we do nothing with it.

        const WGPUTextureFormat dsFormat = depthStencilFormatFromString(dsFormatStr);
        // success iff a depthStencil attachment is provided
        const bool success = (dsFormat != WGPUTextureFormat_Undefined);

        auto h = buildVertexOnlyDescriptor(t, dsFormat, /*hasColor=*/t.param<bool>("hasColor"));
        doCreateRenderPipelineTest(t, success, h.desc);
    });

// ---------------------------------------------------------------------------
// test: pipeline_layout,device_mismatch
// ---------------------------------------------------------------------------

CTS_TEST(g, "pipeline_layout,device_mismatch")
    .desc(
        "Tests createRenderPipeline(Async) cannot be called with a pipeline layout created from "
        "another device")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combine("isAsync", {Value(true), Value(false)})
            .combine("mismatched", {Value(true), Value(false)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        // Upstream: sourceDevice = mismatched ? t.mismatchedDevice : t.device
        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        // Create pipeline layout on the (possibly mismatched) device.
        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 0;
        plDesc.bindGroupLayouts     = nullptr;
        WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(sourceDevice, &plDesc);

        // Create shaders on the test device.
        WGPUShaderModule vertModule = t.createShaderModuleTracked(kDefaultVertexShaderCode);
        WGPUShaderModule fragModule = t.createShaderModuleTracked(kDefaultFragmentShaderCode);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format               = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment   = WGPU_FRAGMENT_STATE_INIT;
        fragment.module              = fragModule;
        fragment.entryPoint          = sv("main");
        fragment.targetCount         = 1;
        fragment.targets             = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                  = layout;
        desc.vertex.module           = vertModule;
        desc.vertex.entryPoint       = sv("main");
        desc.fragment                = &fragment;

        // success = !mismatched
        t.expectValidationError([&] {
            t.createRenderPipelineTracked(desc);
        }, mismatched);

        if (layout != nullptr) {
            wgpuPipelineLayoutRelease(layout);
        }
    });

// ---------------------------------------------------------------------------
// test: external_texture
// ---------------------------------------------------------------------------

CTS_TEST(g, "external_texture")
    .desc("Tests createRenderPipeline(Async) with an external_texture")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Shader uses texture_external — compiled from the verbatim upstream WGSL.
        // Pipeline creation must succeed (success=true).
        static const char* shaderCode =
            "@vertex\n"
            "fn vertexMain() -> @builtin(position) vec4f {\n"
            "  return vec4f(1);\n"
            "}\n"
            "\n"
            "@group(0) @binding(0) var myTexture: texture_external;\n"
            "\n"
            "@fragment\n"
            "fn fragmentMain() -> @location(0) vec4f {\n"
            "  let result = textureLoad(myTexture, vec2u(1, 1));\n"
            "  return vec4f(1);\n"
            "}\n";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(shaderCode);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format               = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment   = WGPU_FRAGMENT_STATE_INIT;
        fragment.module              = shaderModule;
        fragment.entryPoint          = sv("fragmentMain");
        fragment.targetCount         = 1;
        fragment.targets             = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                  = nullptr;  // layout: 'auto'
        desc.vertex.module           = shaderModule;
        desc.vertex.entryPoint       = sv("vertexMain");
        desc.fragment                = &fragment;

        doCreateRenderPipelineTest(t, /*success=*/true, desc);
    });

// ---------------------------------------------------------------------------
// test: storage_texture,format
// ---------------------------------------------------------------------------

CTS_TEST(g, "storage_texture,format")
    .desc(
        "Test that a pipeline with auto layout and storage texture access combo that is not "
        "supported generates a validation error at createComputePipeline(Async)")
    .params([](ParamsBuilder u) {
        return u
            .combine("format", possibleStorageTextureFormatValues())
            .beginSubcases()
            .combine("isAsync", {Value(true), Value(false)})
            .combine("access", {
                Value(std::string("read")),
                Value(std::string("write")),
                Value(std::string("read_write")),
            })
            .combine("dimension", {
                Value(std::string("1d")),
                Value(std::string("2d")),
                Value(std::string("3d")),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string formatStr = t.param<std::string>("format");
        const std::string access    = t.param<std::string>("access");
        const std::string dimension = t.param<std::string>("dimension");

        const WGPUTextureFormat format = parseTextureFormat(formatStr);

        t.skipIfTextureFormatNotSupported(format);

        // Build WGSL shader code using texture_storage_<dimension><format, access> syntax.
        // Verbatim from upstream.
        const std::string code =
            std::string("@group(0) @binding(0) var tex: texture_storage_") + dimension
            + "<" + formatStr + ", " + access + ">;\n"
            "@vertex fn vs() -> @builtin(position) vec4f {\n"
            "  return vec4f(0);\n"
            "}\n"
            "\n"
            "@fragment fn fs() -> @location(0) vec4f {\n"
            "  _ = tex;\n"
            "  return vec4f(0);\n"
            "}\n";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        // Map WGSL access mode string to WGPUStorageTextureAccess for the success check.
        WGPUStorageTextureAccess wgpuAccess;
        if (access == "read") {
            wgpuAccess = WGPUStorageTextureAccess_ReadOnly;
        } else if (access == "write") {
            wgpuAccess = WGPUStorageTextureAccess_WriteOnly;
        } else {
            wgpuAccess = WGPUStorageTextureAccess_ReadWrite;
        }

        const bool success = t.isTextureFormatUsableWithStorageAccessMode(format, wgpuAccess);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format               = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment   = WGPU_FRAGMENT_STATE_INIT;
        fragment.module              = shaderModule;
        fragment.entryPoint          = sv("fs");
        fragment.targetCount         = 1;
        fragment.targets             = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                  = nullptr;  // layout: 'auto'
        desc.vertex.module           = shaderModule;
        desc.vertex.entryPoint       = sv("vs");
        desc.fragment                = &fragment;

        doCreateRenderPipelineTest(t, success, desc);
    });

} // namespace
