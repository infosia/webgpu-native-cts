// Ported from gpuweb/cts src/webgpu/api/operation/texture_view/format_reinterpretation.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// Port notes / deviations:
// - Upstream's `kDifferentBaseFormatRegularTextureFormats` is the set of regular
//   (non-compressed) formats whose baseFormat differs from the format itself:
//   {"rgba8unorm-srgb", "bgra8unorm-srgb"}. The list is inlined here.
// - Upstream `.beforeAllSubcases(t => t.skipIf(t.isCompatibility))`: this harness
//   always runs core (non-compatibility) native backends, so the compatibility-mode
//   skip has no C analog and is intentionally omitted.
// - Upstream compares via TexelView + expectTexelViewComparisonIsOkInTexture with a
//   ULP threshold for norm formats (default 1 ULP; 2 ULPs for the render/resolve
//   checks). Every format involved here is a 4x8-bit unorm format, so one ULP of the
//   normalized value space equals exactly one step of the encoded byte; the port
//   therefore compares the physical encoded bytes with an integer per-channel
//   tolerance (1 or 2), which is equivalent.
// - Expected values are computed on the CPU with the texel_data pack/unpack helpers
//   (which apply the spec sRGB transfer functions), mirroring upstream's
//   TexelView.fromTexelsAsColors / fromTexelsAsBytes round-trips.

#include <array>
#include <cstddef>
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
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,texture_view,format_reinterpretation",
    "\n"
    "Test texture views can reinterpret the format of the original texture.\n"
    "\n"
    "- TODO: test compressed texture reinterpretation\n");

constexpr uint32_t kTextureSize = 16;
constexpr uint32_t kBytesPerPixel = 4;

// Upstream kColors (RGBA, normalized values).
constexpr std::array<std::array<double, 4>, 10> kColors = {{
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

// Formats in upstream kDifferentBaseFormatRegularTextureFormats.
std::vector<Value> differentBaseFormatRegularTextureFormatValues() {
    return {
        Value("rgba8unorm-srgb"),
        Value("bgra8unorm-srgb"),
    };
}

// Upstream getBaseFormatForTextureFormat restricted to the formats above.
WGPUTextureFormat baseFormatForTextureFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
            return WGPUTextureFormat_RGBA8Unorm;
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return WGPUTextureFormat_BGRA8Unorm;
        default:
            std::abort();
    }
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

// Mirrors upstream makeInputTexelView's color generator: the kColors pattern,
// clamped to the format range (all formats here are unorm => clamp to [0, 1],
// which is a no-op for kColors but kept for fidelity to clampToFormatRange).
std::vector<TexelComponents> makeInputTexels() {
    std::vector<TexelComponents> texels(static_cast<size_t>(kTextureSize) * kTextureSize);
    for (uint32_t y = 0; y < kTextureSize; ++y) {
        for (uint32_t x = 0; x < kTextureSize; ++x) {
            const size_t pixelPos = static_cast<size_t>(y) * kTextureSize + x;
            const std::array<double, 4>& color = kColors[pixelPos % kColors.size()];
            TexelComponents texel;
            for (size_t c = 0; c < 4; ++c) {
                texel.values[c] = clamp01(color[c]);
            }
            texels[pixelPos] = texel;
        }
    }
    return texels;
}

// TexelView.fromTexelsAsColors(format, ..., { clampToFormatRange: true }).bytes:
// encode colors into the physical byte representation of `format` (applies the
// sRGB transfer function for -srgb formats).
std::vector<uint8_t> encodeTexels(WGPUTextureFormat format, const std::vector<TexelComponents>& texels) {
    const TexelRepresentation& repr = texelRepresentation(format);
    std::vector<uint8_t> bytes;
    bytes.reserve(texels.size() * repr.bytesPerBlock);
    for (const TexelComponents& texel : texels) {
        TexelComponents clamped = texel;
        for (size_t c = 0; c < 4; ++c) {
            clamped.values[c] = clamp01(clamped.values[c]);
        }
        const std::vector<uint8_t> texelBytes = repr.packBits(repr.numberToBits(clamped));
        bytes.insert(bytes.end(), texelBytes.begin(), texelBytes.end());
    }
    return bytes;
}

// TexelView.fromTexelsAsBytes(format, bytes).color: decode physical bytes as
// `format` (applies sRGB decode for -srgb formats).
std::vector<TexelComponents> decodeTexels(WGPUTextureFormat format, const std::vector<uint8_t>& bytes) {
    const TexelRepresentation& repr = texelRepresentation(format);
    const size_t texelCount = bytes.size() / repr.bytesPerBlock;
    std::vector<TexelComponents> texels(texelCount);
    for (size_t i = 0; i < texelCount; ++i) {
        const uint8_t* texelData = bytes.data() + i * repr.bytesPerBlock;
        texels[i] = repr.bitsToNumber(repr.unpackBits(texelData, repr.bytesPerBlock));
    }
    return texels;
}

// Port of upstream expectTexelViewComparisonIsOkInTexture for the 4x8-bit unorm
// formats used in this file: copy the texture to a buffer and compare the encoded
// bytes against `expected` (tightly packed kTextureSize x kTextureSize x 4) with a
// per-channel integer tolerance (== ULPs of the 8-bit normalized representation).
void expectTexturePixelBytes(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const std::vector<uint8_t>& expected,
    uint32_t toleranceUlps,
    const std::string& context) {
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(alignTo(static_cast<uint64_t>(kTextureSize) * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kTextureSize - 1)
            + static_cast<uint64_t>(kTextureSize) * kBytesPerPixel,
        kBufferCopyAlignment);

    // Readback buffer is created zero-filled (never pre-filled with expected values).
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
        [&expected, bytesPerRow, toleranceUlps, context](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t y = 0; y < kTextureSize; ++y) {
                for (uint32_t x = 0; x < kTextureSize; ++x) {
                    const uint64_t actualOffset = static_cast<uint64_t>(y) * bytesPerRow
                        + static_cast<uint64_t>(x) * kBytesPerPixel;
                    const size_t expectedOffset =
                        (static_cast<size_t>(y) * kTextureSize + x) * kBytesPerPixel;
                    if (actualOffset + kBytesPerPixel > len) {
                        std::ostringstream message;
                        message << context << ": pixel offset out of range: " << actualOffset;
                        return message.str();
                    }
                    for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                        const int32_t got = actual[actualOffset + channel];
                        const int32_t want = expected[expectedOffset + channel];
                        const int32_t diff = got > want ? got - want : want - got;
                        if (diff > static_cast<int32_t>(toleranceUlps)) {
                            std::ostringstream message;
                            message << context << ": mismatch at (" << x << ", " << y << ") byte "
                                    << channel << ": expected " << want << " +/- " << toleranceUlps
                                    << ", got " << got;
                            return message.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

// Upstream createTextureFromTexelView: create the texture (with COPY_DST added so
// the contents can be uploaded) and write the encoded texels into it.
WGPUTexture createTextureFromTexelBytes(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    const std::vector<uint8_t>& bytes,
    WGPUTextureUsage usage,
    const WGPUTextureFormat* viewFormats,
    size_t viewFormatCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    desc.format = format;
    desc.usage = usage | WGPUTextureUsage_CopyDst;
    desc.viewFormatCount = viewFormatCount;
    desc.viewFormats = viewFormats;
    WGPUTexture texture = t.createTextureTracked(desc);

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = kTextureSize * kBytesPerPixel;
    layout.rowsPerImage = kTextureSize;
    t.queueWriteTexture(
        texture,
        WGPUExtent3D{kTextureSize, kTextureSize, 1},
        layout,
        bytes.data(),
        bytes.size());
    return texture;
}

constexpr std::string_view kBlitVertexShader = R"(
          @vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
            var pos = array<vec2<f32>, 6>(
                                        vec2<f32>(-1.0, -1.0),
                                        vec2<f32>(-1.0,  1.0),
                                        vec2<f32>( 1.0, -1.0),
                                        vec2<f32>(-1.0,  1.0),
                                        vec2<f32>( 1.0, -1.0),
                                        vec2<f32>( 1.0,  1.0));
            return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
          })";

constexpr std::string_view kBlitFragmentShaderSingleSample = R"(
            @group(0) @binding(0) var src: texture_2d<f32>;
            @fragment fn main(@builtin(position) coord: vec4<f32>) -> @location(0) vec4<f32> {
              return textureLoad(src, vec2<i32>(coord.xy), 0);
            })";

std::string makeBlitFragmentShaderMultisample(uint32_t sampleCount) {
    std::ostringstream out;
    out << "@group(0) @binding(0) var src: texture_multisampled_2d<f32>;\n"
        << "@fragment fn main(@builtin(position) coord: vec4<f32>) -> @location(0) vec4<f32> {\n"
        << "  var result : vec4<f32>;\n"
        << "  for (var i = 0; i < " << sampleCount << "; i = i + 1) {\n"
        << "    result = result + textureLoad(src, vec2<i32>(coord.xy), i);\n"
        << "  }\n"
        << "  return result * " << (1.0 / static_cast<double>(sampleCount)) << ";\n"
        << "}\n";
    return out.str();
}

// Upstream makeBlitPipeline(device, format, { sample, render }).
WGPURenderPipeline makeBlitPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat targetFormat,
    uint32_t sampleSampleCount,
    uint32_t renderSampleCount) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kBlitVertexShader);
    WGPUShaderModule fragmentModule = sampleSampleCount > 1
        ? t.createShaderModuleTracked(makeBlitFragmentShaderMultisample(sampleSampleCount))
        : t.createShaderModuleTracked(kBlitFragmentShaderSingleSample);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = targetFormat;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // 'auto'
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = renderSampleCount;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

constexpr std::string_view kReinterpretComputeShader = R"(
            @group(0) @binding(0) var src: texture_2d<f32>;
            @group(0) @binding(1) var dst: texture_storage_2d<rgba8unorm, write>;
            @compute @workgroup_size(1, 1) fn main(
              @builtin(global_invocation_id) global_id: vec3<u32>,
            ) {
              var coord = vec2<i32>(global_id.xy);
              textureStore(dst, coord, textureLoad(src, coord, 0));
            })";

struct FormatPair {
    WGPUTextureFormat format;
    WGPUTextureFormat viewFormat;
};

CTS_TEST(g, "texture_binding")
    .desc("Test that a regular texture allocated as 'format' is correctly sampled as 'viewFormat'.")
    .params([](ParamsBuilder u) {
        return u.combine("format", differentBaseFormatRegularTextureFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat paramFormat = parseTextureFormat(t.param<std::string>("format"));
        const WGPUTextureFormat paramViewFormat = baseFormatForTextureFormat(paramFormat);

        t.skipIfTextureFormatNotSupported(paramFormat);
        t.skipIfTextureFormatNotSupported(paramViewFormat);

        const std::array<FormatPair, 2> cases = {{
            {paramFormat, paramViewFormat},
            {paramViewFormat, paramFormat},
        }};

        for (const FormatPair& formatPair : cases) {
            const WGPUTextureFormat format = formatPair.format;
            const WGPUTextureFormat viewFormat = formatPair.viewFormat;

            // Make the input texel data (encoded as |format|).
            const std::vector<uint8_t> inputBytes = encodeTexels(format, makeInputTexels());

            // Create the initial texture with the contents of the input texel view.
            // viewFormats array must outlive the create call (lifetime rule).
            const WGPUTextureFormat viewFormatsArr[1] = {viewFormat};
            WGPUTexture texture = createTextureFromTexelBytes(
                t, format, inputBytes, WGPUTextureUsage_TextureBinding, viewFormatsArr, 1);

            // Reinterpret the texture as the view format.
            WGPUTextureViewDescriptor reinterpretedViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            reinterpretedViewDesc.format = viewFormat;
            WGPUTextureView reinterpretedView = t.createViewTracked(texture, reinterpretedViewDesc);

            // Make a texel view of the view format that reinterprets the same data,
            // then re-encode it as rgba8unorm to compute the expected output bytes.
            const std::vector<TexelComponents> reinterpretedTexels = decodeTexels(viewFormat, inputBytes);
            const std::vector<uint8_t> expected =
                encodeTexels(WGPUTextureFormat_RGBA8Unorm, reinterpretedTexels);

            // Create a pipeline to write data out to rgba8unorm.
            WGPUShaderModule computeModule = t.createShaderModuleTracked(kReinterpretComputeShader);
            WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            pipelineDesc.layout = nullptr; // 'auto'
            pipelineDesc.compute.module = computeModule;
            pipelineDesc.compute.entryPoint = stringView("main");
            WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

            // Create an rgba8unorm output texture.
            WGPUTextureDescriptor outputDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            outputDesc.format = WGPUTextureFormat_RGBA8Unorm;
            outputDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
            outputDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
            WGPUTexture outputTexture = t.createTextureTracked(outputDesc);

            WGPUTextureViewDescriptor outputViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView outputView = t.createViewTracked(outputTexture, outputViewDesc);

            // getBindGroupLayout is a getter -> must be released manually.
            WGPUBindGroupLayout bindGroupLayout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

            // Bind group entries must outlive createBindGroupTracked (lifetime rule).
            std::array<WGPUBindGroupEntry, 2> bindGroupEntries = {{
                WGPU_BIND_GROUP_ENTRY_INIT,
                WGPU_BIND_GROUP_ENTRY_INIT,
            }};
            bindGroupEntries[0].binding = 0;
            bindGroupEntries[0].textureView = reinterpretedView;
            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].textureView = outputView;

            WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bindGroupDesc.layout = bindGroupLayout;
            bindGroupDesc.entryCount = bindGroupEntries.size();
            bindGroupDesc.entries = bindGroupEntries.data();
            WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
            wgpuBindGroupLayoutRelease(bindGroupLayout);

            // Execute a compute pass to load data from the reinterpreted view and
            // write out to the rgba8unorm texture.
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, kTextureSize, kTextureSize, 1);
            wgpuComputePassEncoderEnd(pass);
            WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

            // Upstream default comparison: 1 ULP for norm formats.
            expectTexturePixelBytes(
                t,
                outputTexture,
                expected,
                /*toleranceUlps=*/1,
                std::string("texture_binding format=") + std::string(textureFormatIdentifier(format))
                    + " viewFormat=" + std::string(textureFormatIdentifier(viewFormat)));
        }
    });

CTS_TEST(g, "render_and_resolve_attachment")
    .desc(
        "Test that a color render attachment allocated as 'format' is correctly rendered to as 'viewFormat',\n"
        "and resolved to an attachment allocated as 'format' viewed as 'viewFormat'.\n"
        "\n"
        "Other combinations aren't possible because the render and resolve targets must both match\n"
        "in view format and match in base format.")
    .params([](ParamsBuilder u) {
        return u.combine("format", differentBaseFormatRegularTextureFormatValues())
            .combine("sampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat paramFormat = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const WGPUTextureFormat paramViewFormat = baseFormatForTextureFormat(paramFormat);

        t.skipIfTextureFormatNotSupported(paramFormat);
        t.skipIfTextureFormatNotSupported(paramViewFormat);

        const std::array<FormatPair, 2> cases = {{
            {paramFormat, paramViewFormat},
            {paramViewFormat, paramFormat},
        }};

        for (const FormatPair& formatPair : cases) {
            const WGPUTextureFormat format = formatPair.format;
            const WGPUTextureFormat viewFormat = formatPair.viewFormat;

            // Make the input texel data (encoded as |format|).
            const std::vector<uint8_t> inputBytes = encodeTexels(format, makeInputTexels());

            // viewFormats array must outlive the create calls (lifetime rule).
            const WGPUTextureFormat viewFormatsArr[1] = {viewFormat};

            // Create the renderTexture as |format|.
            WGPUTextureDescriptor renderDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            renderDesc.format = format;
            renderDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
            renderDesc.usage = WGPUTextureUsage_RenderAttachment
                | (sampleCount > 1 ? WGPUTextureUsage_TextureBinding : WGPUTextureUsage_CopySrc);
            renderDesc.viewFormatCount = 1;
            renderDesc.viewFormats = viewFormatsArr;
            renderDesc.sampleCount = sampleCount;
            WGPUTexture renderTexture = t.createTextureTracked(renderDesc);

            WGPUTexture resolveTexture = nullptr;
            if (sampleCount > 1) {
                WGPUTextureDescriptor resolveDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                resolveDesc.format = format;
                resolveDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
                resolveDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
                resolveDesc.viewFormatCount = 1;
                resolveDesc.viewFormats = viewFormatsArr;
                resolveTexture = t.createTextureTracked(resolveDesc);
            }

            // Create the sample source with the contents of the input texel view.
            // We will sample this texture into |renderTexture|. It uses the same format
            // to keep the same number of bits of precision.
            WGPUTexture sampleSource = createTextureFromTexelBytes(
                t, format, inputBytes, WGPUTextureUsage_TextureBinding, nullptr, 0);

            // Reinterpret the renderTexture (and resolveTexture) as |viewFormat|.
            WGPUTextureViewDescriptor reinterpretedViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            reinterpretedViewDesc.format = viewFormat;
            WGPUTextureView reinterpretedRenderView = t.createViewTracked(renderTexture, reinterpretedViewDesc);
            WGPUTextureView reinterpretedResolveView = nullptr;
            if (resolveTexture != nullptr) {
                reinterpretedResolveView = t.createViewTracked(resolveTexture, reinterpretedViewDesc);
            }

            // Create a pipeline to blit a src texture to the render attachment.
            WGPURenderPipeline pipeline = makeBlitPipeline(t, viewFormat, /*sample=*/1, /*render=*/sampleCount);

            WGPUTextureViewDescriptor plainViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView sampleSourceView = t.createViewTracked(sampleSource, plainViewDesc);

            // getBindGroupLayout is a getter -> must be released manually.
            WGPUBindGroupLayout blitBindGroupLayout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
            WGPUBindGroupEntry blitEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            blitEntry.binding = 0;
            blitEntry.textureView = sampleSourceView;
            WGPUBindGroupDescriptor blitBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            blitBindGroupDesc.layout = blitBindGroupLayout;
            blitBindGroupDesc.entryCount = 1;
            blitBindGroupDesc.entries = &blitEntry;
            WGPUBindGroup blitBindGroup = t.createBindGroupTracked(blitBindGroupDesc);
            wgpuBindGroupLayoutRelease(blitBindGroupLayout);

            // Execute a render pass to sample |sampleSource| into |renderTexture|
            // viewed as |viewFormat|.
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            {
                WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                colorAttachment.view = reinterpretedRenderView;
                colorAttachment.resolveTarget = reinterpretedResolveView;
                colorAttachment.loadOp = WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;

                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;

                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, blitBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
            }

            // If the render target is multisampled, we'll manually resolve it to check
            // the contents.
            WGPUTexture singleSampleRenderTexture = renderTexture;
            if (resolveTexture != nullptr) {
                WGPUTextureDescriptor singleSampleDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                singleSampleDesc.format = format;
                singleSampleDesc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
                singleSampleDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
                singleSampleRenderTexture = t.createTextureTracked(singleSampleDesc);

                // Create a pipeline to blit the multisampled render texture to a
                // non-multisampled texture. This is a manual resolve step to the same
                // format as the original render texture to check its contents.
                WGPURenderPipeline resolvePipeline =
                    makeBlitPipeline(t, format, /*sample=*/sampleCount, /*render=*/1);

                WGPUTextureView singleSampleView = t.createViewTracked(singleSampleRenderTexture, plainViewDesc);
                WGPUTextureView renderTextureView = t.createViewTracked(renderTexture, plainViewDesc);

                WGPUBindGroupLayout resolveBindGroupLayout =
                    wgpuRenderPipelineGetBindGroupLayout(resolvePipeline, 0);
                WGPUBindGroupEntry resolveEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                resolveEntry.binding = 0;
                resolveEntry.textureView = renderTextureView;
                WGPUBindGroupDescriptor resolveBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                resolveBindGroupDesc.layout = resolveBindGroupLayout;
                resolveBindGroupDesc.entryCount = 1;
                resolveBindGroupDesc.entries = &resolveEntry;
                WGPUBindGroup resolveBindGroup = t.createBindGroupTracked(resolveBindGroupDesc);
                wgpuBindGroupLayoutRelease(resolveBindGroupLayout);

                WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                colorAttachment.view = singleSampleView;
                colorAttachment.loadOp = WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;

                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;

                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, resolvePipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, resolveBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
            }

            // Submit the commands.
            WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

            // Check the rendered contents. Upstream:
            //   renderViewTexels = TexelView.fromTexelsAsColors(viewFormat, inputTexelView.color)
            // where inputTexelView.color is the (clamped) kColors pattern, so the
            // expected bytes are the kColors pattern encoded as |viewFormat|.
            const std::vector<uint8_t> expectedRenderBytes = encodeTexels(viewFormat, makeInputTexels());
            expectTexturePixelBytes(
                t,
                singleSampleRenderTexture,
                expectedRenderBytes,
                /*toleranceUlps=*/2,
                std::string("render format=") + std::string(textureFormatIdentifier(format))
                    + " viewFormat=" + std::string(textureFormatIdentifier(viewFormat)));

            // Check the resolved contents. Upstream re-encodes renderViewTexels.color
            // (== the clamped kColors pattern) as viewFormat again, which yields the
            // same expected bytes.
            if (resolveTexture != nullptr) {
                expectTexturePixelBytes(
                    t,
                    resolveTexture,
                    expectedRenderBytes,
                    /*toleranceUlps=*/2,
                    std::string("resolve format=") + std::string(textureFormatIdentifier(format))
                        + " viewFormat=" + std::string(textureFormatIdentifier(viewFormat)));
            }
        }
    });

} // namespace
