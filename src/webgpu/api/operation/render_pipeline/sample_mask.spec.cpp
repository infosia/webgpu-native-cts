// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/sample_mask.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,sample_mask",
    "Tests that the final sample mask is the logical AND of rasterization, pipeline, fragment output, and alpha-to-coverage masks.");

constexpr uint32_t kRenderTargetSize = 1;
constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr WGPUTextureFormat kDepthStencilFormat = WGPUTextureFormat_Depth24PlusStencil8;
constexpr float kDepthClearValue = 1.0f;
constexpr float kDepthWriteValue = 0.0f;
constexpr uint32_t kStencilClearValue = 0;
constexpr uint32_t kStencilReferenceValue = 0xff;

constexpr std::array<std::array<uint8_t, 4>, 4> kColors = {{
    {{0xff, 0x00, 0x00, 0xff}},
    {{0x00, 0xff, 0x00, 0xff}},
    {{0x00, 0x00, 0xff, 0xff}},
    {{0xff, 0xff, 0x00, 0xff}},
}};

WGPUStringView sv(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

bool hasSample(uint32_t rasterizationMask, uint32_t sampleMask, uint32_t fragmentMask, uint32_t sampleIndex = 0) {
    return (rasterizationMask & sampleMask & fragmentMask & (1u << sampleIndex)) != 0;
}

std::vector<float> expectedColor(uint32_t sampleCount, uint32_t rasterizationMask, uint32_t sampleMask, uint32_t fragmentMask) {
    std::vector<float> expected(sampleCount * 4, 0.0f);
    if (sampleCount == 1) {
        if (hasSample(rasterizationMask, sampleMask, fragmentMask)) {
            for (uint32_t c = 0; c < 4; ++c) expected[c] = static_cast<float>(kColors[3][c]) / 255.0f;
        }
        return expected;
    }
    for (uint32_t i = 0; i < sampleCount; ++i) {
        if (hasSample(rasterizationMask, sampleMask, fragmentMask, i)) {
            for (uint32_t c = 0; c < 4; ++c) expected[i * 4 + c] = static_cast<float>(kColors[i][c]) / 255.0f;
        }
    }
    return expected;
}

std::vector<float> expectedDepth(uint32_t sampleCount, uint32_t rasterizationMask, uint32_t sampleMask, uint32_t fragmentMask) {
    std::vector<float> expected(sampleCount);
    for (uint32_t i = 0; i < sampleCount; ++i) {
        expected[i] = hasSample(rasterizationMask, sampleMask, fragmentMask, i) ? kDepthWriteValue : kDepthClearValue;
    }
    return expected;
}

std::vector<uint32_t> expectedStencil(uint32_t sampleCount, uint32_t rasterizationMask, uint32_t sampleMask, uint32_t fragmentMask) {
    std::vector<uint32_t> expected(sampleCount);
    for (uint32_t i = 0; i < sampleCount; ++i) {
        expected[i] = hasSample(rasterizationMask, sampleMask, fragmentMask, i) ? kStencilReferenceValue : kStencilClearValue;
    }
    return expected;
}

constexpr std::string_view kSampleMaskShader = R"(
struct Varyings {
  @builtin(position) Position : vec4<f32>,
  @location(0) @interpolate(flat, either) uvFlat : vec2<f32>,
  @location(1) @interpolate(perspective, sample) uvInterpolated : vec2<f32>,
}

@vertex
fn vmain(@builtin(vertex_index) VertexIndex : u32,
    @builtin(instance_index) InstanceIndex : u32) -> Varyings {
  var sampleCenters = array(
      vec2f(0, 0),
      vec2f(-2,  6) / 8,
      vec2f( 6,  2) / 8,
      vec2f(-6, -2) / 8,
      vec2f( 2, -6) / 8,
    );
  let kTinyQuadRadius = 1.0 / 32;
  var tinyQuad = array(
    vec2f( kTinyQuadRadius,  kTinyQuadRadius),
    vec2f( kTinyQuadRadius, -kTinyQuadRadius),
    vec2f(-kTinyQuadRadius, -kTinyQuadRadius),
    vec2f( kTinyQuadRadius,  kTinyQuadRadius),
    vec2f(-kTinyQuadRadius, -kTinyQuadRadius),
    vec2f(-kTinyQuadRadius,  kTinyQuadRadius),
    );
  var uvsFlat = array(
      vec2f(0.51, 0.51),
      vec2f(0.25, 0.25),
      vec2f(0.75, 0.25),
      vec2f(0.25, 0.75),
      vec2f(0.75, 0.75),
    );
  var uvsInterpolated = array(
      vec2f(1.0, 0.0),
      vec2f(1.0, 1.0),
      vec2f(0.0, 1.0),
      vec2f(1.0, 0.0),
      vec2f(0.0, 1.0),
      vec2f(0.0, 0.0),
      vec2f(0.5, 0.0),
      vec2f(0.5, 0.5),
      vec2f(0.0, 0.5),
      vec2f(0.5, 0.0),
      vec2f(0.0, 0.5),
      vec2f(0.0, 0.0),
      vec2f(1.0, 0.0),
      vec2f(1.0, 0.5),
      vec2f(0.5, 0.5),
      vec2f(1.0, 0.0),
      vec2f(0.5, 0.5),
      vec2f(0.5, 0.0),
      vec2f(0.5, 0.5),
      vec2f(0.5, 1.0),
      vec2f(0.0, 1.0),
      vec2f(0.5, 0.5),
      vec2f(0.0, 1.0),
      vec2f(0.0, 0.5),
      vec2f(1.0, 0.5),
      vec2f(1.0, 1.0),
      vec2f(0.5, 1.0),
      vec2f(1.0, 0.5),
      vec2f(0.5, 1.0),
      vec2f(0.5, 0.5)
    );
  var output : Varyings;
  let pos = sampleCenters[InstanceIndex] + tinyQuad[VertexIndex];
  output.Position = vec4(pos, 0.0, 1.0);
  output.uvFlat = uvsFlat[InstanceIndex];
  output.uvInterpolated = uvsInterpolated[InstanceIndex * 6 + VertexIndex];
  return output;
}

@group(0) @binding(0) var mySampler: sampler;
@group(0) @binding(1) var myTexture: texture_2d<f32>;

@group(0) @binding(2) var<uniform> fragMask: u32;
struct FragmentOutput1 {
  @builtin(sample_mask) mask : u32,
  @location(0) color : vec4<f32>,
}
@fragment fn fmain__fragment_output_mask__flat(varyings: Varyings) -> FragmentOutput1 {
  return FragmentOutput1(fragMask, textureSample(myTexture, mySampler, varyings.uvFlat));
}
@fragment fn fmain__fragment_output_mask__interp(varyings: Varyings) -> FragmentOutput1 {
  return FragmentOutput1(fragMask, textureSample(myTexture, mySampler, varyings.uvInterpolated));
}

struct FragmentOutput2 {
  @location(0) color0 : vec4<f32>,
  @location(1) color1 : vec4<f32>,
}
@group(0) @binding(2) var<uniform> alpha: vec2<f32>;
@fragment fn fmain__alpha_to_coverage_mask__flat(varyings: Varyings) -> FragmentOutput2 {
  var c = textureSample(myTexture, mySampler, varyings.uvFlat);
  return FragmentOutput2(vec4(c.xyz, alpha[0]), vec4(c.xyz, alpha[1]));
}
@fragment fn fmain__alpha_to_coverage_mask__interp(varyings: Varyings) -> FragmentOutput2 {
  var c = textureSample(myTexture, mySampler, varyings.uvInterpolated);
  return FragmentOutput2(vec4(c.xyz, alpha[0]), vec4(c.xyz, alpha[1]));
}
)";

std::string readbackShader(std::string_view textureType, std::string_view valueType, uint32_t components, uint32_t sampleCount) {
    std::ostringstream code;
    code << "@group(0) @binding(0) var src: " << textureType << ";\n"
         << "@group(0) @binding(1) var<storage, read_write> dst: array<" << valueType << ">;\n"
         << "@compute @workgroup_size(1) fn main() {\n";
    for (uint32_t s = 0; s < sampleCount; ++s) {
        if (textureType.find("multisampled") != std::string_view::npos) {
            code << "  let v" << s << " = textureLoad(src, vec2<i32>(0, 0), " << s << ");\n";
        } else if (textureType == "texture_depth_2d") {
            code << "  let v" << s << " = textureLoad(src, vec2<i32>(0, 0), 0);\n";
        } else {
            code << "  let v" << s << " = textureLoad(src, vec2<i32>(0, 0), 0);\n";
        }
        if (components == 1) {
            if (textureType == "texture_depth_2d" || textureType == "texture_depth_multisampled_2d") {
                code << "  dst[" << s << "] = v" << s << ";\n";
            } else {
                code << "  dst[" << s << "] = v" << s << ".r;\n";
            }
        } else {
            for (uint32_t c = 0; c < components; ++c) {
                code << "  dst[" << (s * components + c) << "] = v" << s << "[" << c << "];\n";
            }
        }
    }
    code << "}\n";
    return code.str();
}

WGPUBuffer readTextureToBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureView view,
    uint32_t sampleCount,
    uint32_t components,
    bool depth,
    bool stencil) {
    const uint64_t bytes = uint64_t(sampleCount) * components * sizeof(uint32_t);
    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = bytes;
    bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

    std::string textureType;
    std::string valueType = stencil ? "u32" : "f32";
    if (sampleCount == 1) {
        if (depth) textureType = "texture_depth_2d";
        else if (stencil) textureType = "texture_2d<u32>";
        else textureType = "texture_2d<f32>";
    } else {
        if (depth) textureType = "texture_depth_multisampled_2d";
        else if (stencil) textureType = "texture_multisampled_2d<u32>";
        else textureType = "texture_multisampled_2d<f32>";
    }
    WGPUShaderModule module = t.createShaderModuleTracked(readbackShader(textureType, valueType, components, sampleCount));
    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::array<WGPUBindGroupEntry, 2> entries;
    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].buffer = buffer;
    entries[1].size = bytes;
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);
    return buffer;
}

WGPUTexture createSampleTexture(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = {2, 2, 1};
    desc.format = kFormat;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(desc);
    const std::array<uint8_t, 16> texels = {{
        kColors[0][0], kColors[0][1], kColors[0][2], kColors[0][3],
        kColors[1][0], kColors[1][1], kColors[1][2], kColors[1][3],
        kColors[2][0], kColors[2][1], kColors[2][2], kColors[2][3],
        kColors[3][0], kColors[3][1], kColors[3][2], kColors[3][3],
    }};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = 8;
    layout.rowsPerImage = 2;
    t.queueWriteTexture(texture, {2, 2, 1}, layout, texels.data(), texels.size());
    return texture;
}

struct RenderResult {
    WGPUTexture color = nullptr;
    WGPUTexture depthStencil = nullptr;
};

RenderResult drawTarget(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t sampleCount,
    uint32_t rasterizationMask,
    WGPURenderPipeline pipeline,
    WGPUBuffer uniformBuffer,
    uint32_t colorTargetsCount) {
    WGPUTexture sampleTexture = createSampleTexture(t);
    WGPUTextureViewDescriptor sampleViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView sampleView = t.createViewTracked(sampleTexture, sampleViewDesc);
    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    WGPUSampler sampler = t.createSamplerTracked(samplerDesc);
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    std::array<WGPUBindGroupEntry, 3> entries;
    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].sampler = sampler;
    entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].textureView = sampleView;
    entries[2] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[2].binding = 2;
    entries[2].buffer = uniformBuffer;
    entries[2].size = WGPU_WHOLE_SIZE;
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    std::array<WGPUTexture, 2> colorTextures{};
    std::array<WGPUTextureView, 2> colorViews{};
    std::array<WGPURenderPassColorAttachment, 2> colorAttachments{};
    for (uint32_t i = 0; i < colorTargetsCount; ++i) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {kRenderTargetSize, kRenderTargetSize, 1};
        desc.format = kFormat;
        desc.sampleCount = sampleCount;
        desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        colorTextures[i] = t.createTextureTracked(desc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        colorViews[i] = t.createViewTracked(colorTextures[i], viewDesc);
        colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachments[i].view = colorViews[i];
        colorAttachments[i].loadOp = WGPULoadOp_Clear;
        colorAttachments[i].storeOp = WGPUStoreOp_Store;
        colorAttachments[i].clearValue = WGPUColor{0, 0, 0, 0};
    }
    WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    dsDesc.size = {kRenderTargetSize, kRenderTargetSize, 1};
    dsDesc.format = kDepthStencilFormat;
    dsDesc.sampleCount = sampleCount;
    dsDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture depthStencil = t.createTextureTracked(dsDesc);
    WGPUTextureViewDescriptor dsViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView dsView = t.createViewTracked(depthStencil, dsViewDesc);
    WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    dsAttachment.view = dsView;
    dsAttachment.depthLoadOp = WGPULoadOp_Clear;
    dsAttachment.depthStoreOp = WGPUStoreOp_Store;
    dsAttachment.depthClearValue = kDepthClearValue;
    dsAttachment.stencilLoadOp = WGPULoadOp_Clear;
    dsAttachment.stencilStoreOp = WGPUStoreOp_Store;
    dsAttachment.stencilClearValue = kStencilClearValue;
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colorTargetsCount;
    passDesc.colorAttachments = colorAttachments.data();
    passDesc.depthStencilAttachment = &dsAttachment;
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetStencilReference(pass, kStencilReferenceValue);
    if (sampleCount == 1) {
        if ((rasterizationMask & 1u) != 0) wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    } else {
        if ((rasterizationMask & 1u) != 0) wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 1);
        if ((rasterizationMask & 2u) != 0) wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 2);
        if ((rasterizationMask & 4u) != 0) wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 3);
        if ((rasterizationMask & 8u) != 0) wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 4);
    }
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);
    return RenderResult{colorTextures[0], depthStencil};
}

void checkFloatBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, const std::vector<float>& expected, std::string_view label) {
    t.expectGPUBufferValuesPassCheck(buffer, [expected, label](const uint8_t* actual, size_t len) -> std::optional<std::string> {
        if (len < expected.size() * sizeof(float)) return std::string("readback buffer too small");
        for (size_t i = 0; i < expected.size(); ++i) {
            float got = 0;
            std::memcpy(&got, actual + i * sizeof(float), sizeof(float));
            if (std::fabs(got - expected[i]) > 1.0f / 255.0f + 1e-5f) {
                std::ostringstream msg;
                msg << label << " index " << i << ": got " << got << ", expected " << expected[i];
                return msg.str();
            }
        }
        return std::nullopt;
    }, 0, expected.size() * sizeof(float));
}

void checkStencilBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, const std::vector<uint32_t>& expected) {
    t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size() * sizeof(uint32_t));
}

std::vector<Value> sampleMaskValues() {
    return {uint64_t(0), uint64_t(0b0001), uint64_t(0b0010), uint64_t(0b0111), uint64_t(0b1011),
            uint64_t(0b1101), uint64_t(0b1110), uint64_t(0b1111), uint64_t(0b11110)};
}

std::vector<Value> rasterizationMaskValuesFor(const ParamRecord& p) {
    const uint64_t sampleCount = valueAs<uint64_t>(*findParam(p, "sampleCount"));
    std::vector<Value> values;
    for (uint64_t i = 0; i <= ((uint64_t(1) << sampleCount) - 1); ++i) values.emplace_back(i);
    return values;
}

WGPURenderPipeline makePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view entryPoint,
    uint32_t sampleCount,
    uint32_t sampleMask,
    bool alphaToCoverage,
    uint32_t colorTargetCount) {
    WGPUShaderModule module = t.createShaderModuleTracked(kSampleMaskShader);
    std::array<WGPUColorTargetState, 2> targets{};
    for (uint32_t i = 0; i < colorTargetCount; ++i) {
        targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
        targets[i].format = kFormat;
    }
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv(entryPoint);
    fragment.targetCount = colorTargetCount;
    fragment.targets = targets.data();
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = kDepthStencilFormat;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Always;
    depthStencil.stencilFront.compare = WGPUCompareFunction_Always;
    depthStencil.stencilFront.passOp = WGPUStencilOperation_Replace;
    depthStencil.stencilBack.compare = WGPUCompareFunction_Always;
    depthStencil.stencilBack.passOp = WGPUStencilOperation_Replace;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vmain");
    desc.fragment = &fragment;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = sampleCount;
    desc.multisample.mask = sampleMask;
    desc.multisample.alphaToCoverageEnabled = alphaToCoverage;
    desc.depthStencil = &depthStencil;
    return t.createRenderPipelineTracked(desc);
}

void checkRenderResult(
    AllFeaturesMaxLimitsGpuTest& t,
    const RenderResult& result,
    uint32_t sampleCount,
    uint32_t rasterizationMask,
    uint32_t sampleMask,
    uint32_t fragmentMask) {
    WGPUTextureViewDescriptor colorViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(result.color, colorViewDesc);
    WGPUBuffer color = readTextureToBuffer(t, colorView, sampleCount, 4, false, false);
    checkFloatBuffer(t, color, expectedColor(sampleCount, rasterizationMask, sampleMask, fragmentMask), "color");
    WGPUTextureViewDescriptor depthViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTextureView depthView = t.createViewTracked(result.depthStencil, depthViewDesc);
    WGPUBuffer depth = readTextureToBuffer(t, depthView, sampleCount, 1, true, false);
    checkFloatBuffer(t, depth, expectedDepth(sampleCount, rasterizationMask, sampleMask, fragmentMask), "depth");
    WGPUTextureViewDescriptor stencilViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    stencilViewDesc.aspect = WGPUTextureAspect_StencilOnly;
    WGPUTextureView stencilView = t.createViewTracked(result.depthStencil, stencilViewDesc);
    WGPUBuffer stencil = readTextureToBuffer(t, stencilView, sampleCount, 1, false, true);
    checkStencilBuffer(t, stencil, expectedStencil(sampleCount, rasterizationMask, sampleMask, fragmentMask));
}

CTS_TEST(testGroup, "fragment_output_mask")
    .params([](ParamsBuilder u) {
        return u.combine("interpolated", {false, true})
            .combine("sampleCount", {uint64_t(1), uint64_t(4)})
            .expand("rasterizationMask", rasterizationMaskValuesFor)
            .beginSubcases()
            .combine("sampleMask", sampleMaskValues())
            .combine("fragmentShaderOutputMask", sampleMaskValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool interpolated = t.param<bool>("interpolated");
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<uint64_t>("sampleCount"));
        const uint32_t rasterizationMask = static_cast<uint32_t>(t.param<uint64_t>("rasterizationMask"));
        const uint32_t sampleMask = static_cast<uint32_t>(t.param<uint64_t>("sampleMask"));
        const uint32_t fragmentMask = static_cast<uint32_t>(t.param<uint64_t>("fragmentShaderOutputMask"));
        WGPUBufferDescriptor uniformDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        uniformDesc.size = 4;
        uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer uniform = t.createBufferTracked(uniformDesc);
        wgpuQueueWriteBuffer(t.queue(), uniform, 0, &fragmentMask, sizeof(fragmentMask));
        std::string entry = std::string("fmain__fragment_output_mask__") + (interpolated ? "interp" : "flat");
        WGPURenderPipeline pipeline = makePipeline(t, entry, sampleCount, sampleMask, false, 1);
        RenderResult result = drawTarget(t, sampleCount, rasterizationMask, pipeline, uniform, 1);
        checkRenderResult(t, result, sampleCount, rasterizationMask, sampleMask, fragmentMask);
    });

std::vector<float> alphaValues() {
    std::vector<float> values;
    values.push_back(-0.1f);
    for (uint32_t i = 0; i < 16; ++i) values.push_back(static_cast<float>(i) / 16.0f);
    values.push_back(1.0f);
    values.push_back(1.1f);
    return values;
}

template <typename T>
std::optional<std::string> checkAlphaSequence(
    const std::vector<std::vector<T>>& results,
    const std::vector<std::vector<T>>& exactLow,
    const std::vector<std::vector<T>>& exactHigh,
    bool positive) {
    const std::vector<float> alphas = alphaValues();
    for (size_t i = 0; i < results.size(); ++i) {
        if (alphas[i] <= 0.0f) {
            if (results[i] != exactLow[i]) return std::string("alpha <= 0 result did not match zero coverage");
        } else if (alphas[i] >= 1.0f) {
            if (results[i] != exactHigh[i]) return std::string("alpha >= 1 result did not match full coverage");
        } else {
            for (size_t j = 0; j < results[i].size(); ++j) {
                if (positive) {
                    if (results[i][j] < results[i - 1][j]) return std::string("alpha-to-coverage result decreased");
                } else {
                    if (results[i][j] > results[i - 1][j]) return std::string("alpha-to-coverage result increased");
                }
            }
        }
    }
    return std::nullopt;
}

std::vector<float> readFloatVector(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, size_t count) {
    std::vector<float> values(count);
    t.expectGPUBufferValuesPassCheck(buffer, [&values](const uint8_t* actual, size_t len) -> std::optional<std::string> {
        if (len < values.size() * sizeof(float)) return std::string("readback buffer too small");
        std::memcpy(values.data(), actual, values.size() * sizeof(float));
        return std::nullopt;
    }, 0, count * sizeof(float));
    return values;
}

std::vector<uint32_t> readU32Vector(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, size_t count) {
    std::vector<uint32_t> values(count);
    t.expectGPUBufferValuesPassCheck(buffer, [&values](const uint8_t* actual, size_t len) -> std::optional<std::string> {
        if (len < values.size() * sizeof(uint32_t)) return std::string("readback buffer too small");
        std::memcpy(values.data(), actual, values.size() * sizeof(uint32_t));
        return std::nullopt;
    }, 0, count * sizeof(uint32_t));
    return values;
}

CTS_TEST(testGroup, "alpha_to_coverage_mask")
    .params([](ParamsBuilder u) {
        return u.combine("interpolated", {false, true})
            .combine("sampleCount", {uint64_t(4)})
            .expand("rasterizationMask", rasterizationMaskValuesFor)
            .beginSubcases()
            .combine("alpha1", {0.0, 0.5, 1.0});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool interpolated = t.param<bool>("interpolated");
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<uint64_t>("sampleCount"));
        const uint32_t rasterizationMask = static_cast<uint32_t>(t.param<uint64_t>("rasterizationMask"));
        const float alpha1 = static_cast<float>(t.param<double>("alpha1"));
        const uint32_t sampleMask = 0xffffffffu;
        WGPUBufferDescriptor uniformDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        uniformDesc.size = 4 * sizeof(float);
        uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer uniform = t.createBufferTracked(uniformDesc);
        std::string entry = std::string("fmain__alpha_to_coverage_mask__") + (interpolated ? "interp" : "flat");
        WGPURenderPipeline pipeline = makePipeline(t, entry, sampleCount, sampleMask, true, 2);

        std::vector<std::vector<float>> colorResults;
        std::vector<std::vector<float>> depthResults;
        std::vector<std::vector<uint32_t>> stencilResults;
        for (float alpha0 : alphaValues()) {
            std::array<float, 4> alpha = {alpha0, alpha1, 0.0f, 0.0f};
            wgpuQueueWriteBuffer(t.queue(), uniform, 0, alpha.data(), alpha.size() * sizeof(float));
            RenderResult result = drawTarget(t, sampleCount, rasterizationMask, pipeline, uniform, 2);
            WGPUTextureViewDescriptor colorViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView colorView = t.createViewTracked(result.color, colorViewDesc);
            colorResults.push_back(readFloatVector(t, readTextureToBuffer(t, colorView, sampleCount, 4, false, false), sampleCount * 4));
            WGPUTextureViewDescriptor depthViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            WGPUTextureView depthView = t.createViewTracked(result.depthStencil, depthViewDesc);
            depthResults.push_back(readFloatVector(t, readTextureToBuffer(t, depthView, sampleCount, 1, true, false), sampleCount));
            WGPUTextureViewDescriptor stencilViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            stencilViewDesc.aspect = WGPUTextureAspect_StencilOnly;
            WGPUTextureView stencilView = t.createViewTracked(result.depthStencil, stencilViewDesc);
            stencilResults.push_back(readU32Vector(t, readTextureToBuffer(t, stencilView, sampleCount, 1, false, true), sampleCount));
        }
        std::vector<std::vector<float>> colorLow(colorResults.size(), expectedColor(sampleCount, rasterizationMask, sampleMask, 0));
        std::vector<std::vector<float>> colorHigh(colorResults.size(), expectedColor(sampleCount, rasterizationMask, sampleMask, 0xffffffffu));
        std::vector<std::vector<float>> depthLow(depthResults.size(), expectedDepth(sampleCount, rasterizationMask, sampleMask, 0));
        std::vector<std::vector<float>> depthHigh(depthResults.size(), expectedDepth(sampleCount, rasterizationMask, sampleMask, 0xffffffffu));
        std::vector<std::vector<uint32_t>> stencilLow(stencilResults.size(), expectedStencil(sampleCount, rasterizationMask, sampleMask, 0));
        std::vector<std::vector<uint32_t>> stencilHigh(stencilResults.size(), expectedStencil(sampleCount, rasterizationMask, sampleMask, 0xffffffffu));
        if (auto error = checkAlphaSequence(colorResults, colorLow, colorHigh, true)) t.fail(*error);
        if (auto error = checkAlphaSequence(depthResults, depthLow, depthHigh, false)) t.fail(*error);
        if (auto error = checkAlphaSequence(stencilResults, stencilLow, stencilHigh, true)) t.fail(*error);
    });

} // namespace
