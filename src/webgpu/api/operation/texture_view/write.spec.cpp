// Ported from gpuweb/cts src/webgpu/api/operation/texture_view/write.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_layout.h"
#include "webgpu/util/texture_ok.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,texture_view,write",
    "\n"
    "Test the result of writing textures through texture views with various options.\n"
    "\n"
    "Reads value from a shader array, writes the value via various write methods.\n"
    "Check the texture result with the expected texel view.\n");

constexpr uint32_t kTextureSize = 16;

constexpr std::array<std::string_view, 4> kTextureViewWriteMethods = {{
    "storage-write-fragment",
    "storage-write-compute",
    "render-pass-store",
    "render-pass-resolve",
}};

constexpr std::array<std::string_view, 2> kTextureViewUsageMethods = {{
    "inherit",
    "minimal",
}};

constexpr std::array<std::array<double, 4>, 10> kColorsFloat = {{
    {{1.0, 0.0, 0.0, 0.8}},
    {{0.0, 1.0, 0.0, 0.7}},
    {{0.0, 0.0, 0.0, 0.6}},
    {{0.0, 0.0, 0.0, 0.5}},
    {{1.0, 1.0, 1.0, 0.4}},
    {{0.7, 0.0, 0.0, 0.3}},
    {{0.0, 0.8, 0.0, 0.2}},
    {{0.0, 0.0, 0.9, 0.1}},
    {{0.1, 0.2, 0.0, 0.3}},
    {{0.4, 0.3, 0.6, 0.8}},
}};

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) / alignment * alignment;
}

template <typename Values>
std::vector<Value> stringViewValues(const Values& values) {
    std::vector<Value> result;
    result.reserve(values.size());
    for (std::string_view value : values) {
        result.emplace_back(std::string(value));
    }
    return result;
}

std::vector<Value> regularTextureFormatValues() {
    std::vector<Value> result;
    result.reserve(kRegularTextureFormats.size());
    for (WGPUTextureFormat format : kRegularTextureFormats) {
        result.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return result;
}

bool isStorageWriteMethod(std::string_view method) {
    return method == "storage-write-fragment" || method == "storage-write-compute";
}

WGPUTextureUsage getTextureViewUsage(std::string_view viewUsageMethod, WGPUTextureUsage minimalUsageForTest) {
    if (viewUsageMethod == "inherit") {
        return WGPUTextureUsage_None;
    }
    if (viewUsageMethod == "minimal") {
        return minimalUsageForTest;
    }
    std::abort();
}

void skipIfTextureFormatNotMultisampled(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    if (!t.isTextureFormatMultisampled(format)) {
        t.skip("texture format is not multisampled");
    }
}

bool isTextureFormatResolvable(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
            return true;
        default:
            return false;
    }
}

void skipIfTextureFormatNotResolvable(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    if (!isTextureFormatResolvable(format)) {
        t.skip("texture format is not resolvable");
    }
}

bool isFloatTextureFormatType(WGPUTextureFormat format) {
    const TexelRepresentation& repr = texelRepresentation(format);
    for (TexelComponent component : repr.componentOrder) {
        const ComponentDataType type = repr.dataTypes[static_cast<uint32_t>(component)];
        if (type == ComponentDataType::Uint || type == ComponentDataType::Sint) {
            return false;
        }
    }
    return true;
}

bool isSintTextureFormatType(WGPUTextureFormat format) {
    const TexelRepresentation& repr = texelRepresentation(format);
    for (TexelComponent component : repr.componentOrder) {
        if (repr.dataTypes[static_cast<uint32_t>(component)] == ComponentDataType::Sint) {
            return true;
        }
    }
    return false;
}

std::string wgslVecType(WGPUTextureFormat format) {
    if (isFloatTextureFormatType(format)) {
        return "vec4f";
    }
    return isSintTextureFormatType(format) ? "vec4i" : "vec4u";
}

std::string wgslNumber(double value, bool isFloat, bool isSigned) {
    std::ostringstream out;
    if (isFloat) {
        out << value;
        const std::string text = out.str();
        return text.find('.') == std::string::npos ? text + ".0" : text;
    }
    const int64_t integer = static_cast<int64_t>(std::floor(value * 100.0));
    out << integer;
    if (!isSigned) {
        out << "u";
    }
    return out.str();
}

std::string colorArrayShaderString(WGPUTextureFormat format) {
    const bool isFloat = isFloatTextureFormatType(format);
    const bool isSigned = isSintTextureFormatType(format);
    const std::string vecType = wgslVecType(format);
    std::ostringstream out;
    out << "array<" << vecType << ", " << kColorsFloat.size() << ">(";
    for (size_t i = 0; i < kColorsFloat.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << vecType << "("
            << wgslNumber(kColorsFloat[i][0], isFloat, isSigned) << ", "
            << wgslNumber(kColorsFloat[i][1], isFloat, isSigned) << ", "
            << wgslNumber(kColorsFloat[i][2], isFloat, isSigned) << ", "
            << wgslNumber(kColorsFloat[i][3], isFloat, isSigned) << ")";
    }
    out << ")";
    return out.str();
}

TexelComponents expectedTexel(WGPUTextureFormat format, uint32_t x, uint32_t y) {
    const bool isFloat = isFloatTextureFormatType(format);
    const std::array<double, 4>& source = kColorsFloat[(y * kTextureSize + x) % kColorsFloat.size()];
    TexelComponents texel;
    for (size_t i = 0; i < source.size(); ++i) {
        texel.values[i] = isFloat ? source[i] : std::floor(source[i] * 100.0);
    }
    return texel;
}

std::vector<uint8_t> makeExpectedTexelBytes(WGPUTextureFormat format) {
    const TexelRepresentation& repr = texelRepresentation(format);
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(kTextureSize) * kTextureSize * repr.bytesPerBlock);
    for (uint32_t y = 0; y < kTextureSize; ++y) {
        for (uint32_t x = 0; x < kTextureSize; ++x) {
            const TexelComponents texel = expectedTexel(format, x, y);
            const std::vector<uint8_t> texelBytes = repr.packBits(repr.numberToBits(texel));
            bytes.insert(bytes.end(), texelBytes.begin(), texelBytes.end());
        }
    }
    return bytes;
}

constexpr std::string_view kFullscreenQuadVertexShaderCode = R"(
struct VertexOutput {
  @builtin(position) Position : vec4<f32>
};

@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> VertexOutput {
  var pos = array<vec2<f32>, 6>(
      vec2<f32>( 1.0,  1.0),
      vec2<f32>( 1.0, -1.0),
      vec2<f32>(-1.0, -1.0),
      vec2<f32>( 1.0,  1.0),
      vec2<f32>(-1.0, -1.0),
      vec2<f32>(-1.0,  1.0));

  var output : VertexOutput;
  output.Position = vec4<f32>(pos[VertexIndex], 0.0, 1.0);
  return output;
}
)";

std::string storageWriteComputeShader(WGPUTextureFormat format) {
    std::ostringstream code;
    code << "@group(0) @binding(0) var dst: texture_storage_2d<"
         << textureFormatIdentifier(format) << ", write>;\n"
         << "@compute @workgroup_size(1, 1) fn main(\n"
         << "  @builtin(global_invocation_id) global_id: vec3<u32>,\n"
         << ") {\n"
         << "  const src = " << colorArrayShaderString(format) << ";\n"
         << "  let coord = vec2u(global_id.xy);\n"
         << "  let idx = coord.x + coord.y * " << kTextureSize << "u;\n"
         << "  textureStore(dst, coord, src[idx % " << kColorsFloat.size() << "u]);\n"
         << "}\n";
    return code.str();
}

std::string storageWriteFragmentShader(WGPUTextureFormat format) {
    std::ostringstream code;
    code << "@group(0) @binding(0) var dst: texture_storage_2d<"
         << textureFormatIdentifier(format) << ", write>;\n"
         << "@fragment fn main(@builtin(position) fragCoord: vec4<f32>) {\n"
         << "  const src = " << colorArrayShaderString(format) << ";\n"
         << "  let coord = vec2u(fragCoord.xy);\n"
         << "  let idx = coord.x + coord.y * " << kTextureSize << "u;\n"
         << "  textureStore(dst, coord, src[idx % " << kColorsFloat.size() << "u]);\n"
         << "}\n";
    return code.str();
}

std::string renderPassFragmentShader(WGPUTextureFormat format) {
    std::ostringstream code;
    code << "@fragment fn main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) "
         << wgslVecType(format) << " {\n"
         << "  const src = " << colorArrayShaderString(format) << ";\n"
         << "  let coord = vec2u(fragCoord.xy);\n"
         << "  let idx = coord.x + coord.y * " << kTextureSize << "u;\n"
         << "  return src[idx % " << kColorsFloat.size() << "u];\n"
         << "}\n";
    return code.str();
}

void writeTextureWithCompute(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureView view, WGPUTextureFormat format) {
    const std::string shader = storageWriteComputeShader(format);
    WGPUShaderModule module = t.createShaderModuleTracked(shader);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = stringView("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = view;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    wgpuBindGroupLayoutRelease(layout);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, kTextureSize, kTextureSize, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void writeTextureWithFragmentStorage(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureView view, WGPUTextureFormat format) {
    WGPUTextureDescriptor placeholderDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    placeholderDesc.format = WGPUTextureFormat_RGBA8Unorm;
    placeholderDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    placeholderDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture placeholder = t.createTextureTracked(placeholderDesc);

    WGPUTextureViewDescriptor placeholderViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView placeholderView = t.createViewTracked(placeholder, placeholderViewDesc);

    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kFullscreenQuadVertexShaderCode);
    const std::string fragmentShader = storageWriteFragmentShader(format);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentShader);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_None;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr;
    pipelineDesc.vertex.module = vertexModule;
    pipelineDesc.vertex.entryPoint = stringView("main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.fragment = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);

    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = view;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    wgpuBindGroupLayoutRelease(layout);

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = placeholderView;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Discard;
    color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPURenderPipeline makeRenderWritePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    uint32_t sampleCount) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kFullscreenQuadVertexShaderCode);
    const std::string fragmentShader = renderPassFragmentShader(format);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentShader);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr;
    pipelineDesc.vertex.module = vertexModule;
    pipelineDesc.vertex.entryPoint = stringView("main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = sampleCount;
    pipelineDesc.fragment = &fragment;
    return t.createRenderPipelineTracked(pipelineDesc);
}

void writeTextureWithRenderPass(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view method,
    WGPUTextureView view,
    WGPUTextureFormat format,
    uint32_t sampleCount) {
    WGPUTextureView targetView = view;
    WGPUTextureView resolveView = nullptr;
    uint32_t multisampleCount = sampleCount;

    if (method == "render-pass-resolve") {
        WGPUTextureDescriptor targetDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        targetDesc.format = format;
        targetDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
        targetDesc.usage = WGPUTextureUsage_RenderAttachment;
        targetDesc.sampleCount = 4;
        WGPUTexture targetTexture = t.createTextureTracked(targetDesc);
        WGPUTextureViewDescriptor targetViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        targetView = t.createViewTracked(targetTexture, targetViewDesc);
        resolveView = view;
        multisampleCount = 4;
    }

    WGPURenderPipeline pipeline = makeRenderWritePipeline(t, format, multisampleCount);

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = targetView;
    color.resolveTarget = resolveView;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

struct ExpectedTexelView {
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::vector<uint8_t> bytes;

    TexelView view() const {
        TexelViewConfig config;
        config.bytesPerRow = kTextureSize * texelRepresentation(format).bytesPerBlock;
        config.rowsPerImage = kTextureSize;
        config.subrectSize = WGPUExtent3D{kTextureSize, kTextureSize, 1};
        return TexelView::fromTextureDataByReference(format, bytes.data(), bytes.size(), config);
    }
};

ExpectedTexelView writeTextureAndGetExpectedTexelView(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view method,
    WGPUTextureView view,
    WGPUTextureFormat format,
    uint32_t sampleCount) {
    if (method == "storage-write-compute") {
        writeTextureWithCompute(t, view, format);
    } else if (method == "storage-write-fragment") {
        writeTextureWithFragmentStorage(t, view, format);
    } else if (method == "render-pass-store" || method == "render-pass-resolve") {
        writeTextureWithRenderPass(t, method, view, format, sampleCount);
    } else {
        std::abort();
    }

    return ExpectedTexelView{format, makeExpectedTexelBytes(format)};
}

void expectTexelViewComparisonIsOkInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const ExpectedTexelView& expected) {
    const uint32_t bytesPerBlock = texelRepresentation(expected.format).bytesPerBlock;
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(alignTo(static_cast<uint64_t>(kTextureSize) * bytesPerBlock, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kTextureSize - 1u)
            + static_cast<uint64_t>(kTextureSize) * bytesPerBlock,
        kBufferCopyAlignment);

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = byteLength;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, bytesPerRow, WGPUExtent3D{kTextureSize, kTextureSize, 1});
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&expected, bytesPerRow, byteLength](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < byteLength) {
                return std::string("readback buffer too small");
            }
            TexelViewConfig actualConfig;
            actualConfig.bytesPerRow = bytesPerRow;
            actualConfig.rowsPerImage = kTextureSize;
            actualConfig.subrectSize = WGPUExtent3D{kTextureSize, kTextureSize, 1};
            const TexelView actualView = TexelView::fromTextureDataByReference(
                expected.format,
                actual,
                len,
                actualConfig);
            return findFailedPixels(
                expected.format,
                WGPUOrigin3D{0, 0, 0},
                WGPUExtent3D{kTextureSize, kTextureSize, 1},
                actualView,
                expected.view(),
                1.0);
        },
        0,
        static_cast<size_t>(byteLength));
}

CTS_TEST(testGroup, "format")
    .desc(
        "Views of every allowed format.\n"
        "\n"
        "Read values from color array in the shader, and write it to the texture view via different write methods.\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", stringViewValues(kTextureViewWriteMethods))
            .combine("format", regularTextureFormatValues())
            .combine("sampleCount", {uint64_t(1), uint64_t(4)})
            .filter([](const ParamRecord& p) {
                const std::string format = valueAs<std::string>(*findParam(p, "format"));
                const std::string method = valueAs<std::string>(*findParam(p, "method"));
                const uint64_t sampleCount = valueAs<uint64_t>(*findParam(p, "sampleCount"));
                if (format == "rgb10a2uint") {
                    return false;
                }
                if (method == "storage-write-compute" || method == "storage-write-fragment"
                    || method == "render-pass-resolve") {
                    return sampleCount == 1;
                }
                if (method == "render-pass-store" && sampleCount > 1) {
                    return false;
                }
                return true;
            })
            .combine("viewUsageMethod", stringViewValues(kTextureViewUsageMethods));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string method = t.param<std::string>("method");
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<uint64_t>("sampleCount"));
        const std::string viewUsageMethod = t.param<std::string>("viewUsageMethod");

        t.skipIfTextureFormatNotSupported(format);
        if (sampleCount > 1) {
            skipIfTextureFormatNotMultisampled(t, format);
        }

        if (isStorageWriteMethod(method)) {
            if (!t.isTextureFormatUsableWithStorageAccessMode(format, WGPUStorageTextureAccess_WriteOnly)) {
                t.skip("texture format is not usable as write-only storage texture");
            }
        } else if (method == "render-pass-store") {
            t.skipIfTextureFormatNotUsableAsRenderAttachment(format);
        } else if (method == "render-pass-resolve") {
            t.skipIfTextureFormatNotUsableAsRenderAttachment(format);
            skipIfTextureFormatNotResolvable(t, format);
        }

        // Upstream has a compatibility-mode storage-write-fragment limit guard.
        // Native CTS runs the core path here, so no compatibility-stage skip is needed.
        const WGPUTextureUsage textureUsageForMethod =
            isStorageWriteMethod(method) ? WGPUTextureUsage_StorageBinding : WGPUTextureUsage_RenderAttachment;
        const WGPUTextureUsage usage = WGPUTextureUsage_CopySrc | textureUsageForMethod;

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.format = format;
        textureDesc.usage = usage;
        textureDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
        textureDesc.sampleCount = sampleCount;
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.usage = getTextureViewUsage(viewUsageMethod, textureUsageForMethod);
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        const ExpectedTexelView expected =
            writeTextureAndGetExpectedTexelView(t, method, view, format, sampleCount);
        expectTexelViewComparisonIsOkInTexture(t, texture, expected);
    });

CTS_TEST(testGroup, "dimension")
    .desc("Views of every allowed dimension.")
    .unimplemented();

CTS_TEST(testGroup, "aspect")
    .desc("Views of every allowed aspect of depth/stencil textures.")
    .unimplemented();

} // namespace
