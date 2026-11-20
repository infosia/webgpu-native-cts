// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/pipeline_output_targets.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Native spellings: the array-valued subcase params of color,component_count,blend (`_result`,
// `output`) are comma-separated strings ("0,1,0,0.498"), as elsewhere in the suite.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texture_test_utils.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,pipeline_output_targets",
    R"(
- Test pipeline outputs with different color attachment number, formats, component counts, etc.
)");

constexpr std::string_view kVertexShader = R"(
@vertex fn main(
@builtin(vertex_index) VertexIndex : u32
) -> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
      vec2<f32>(-1.0, -3.0),
      vec2<f32>(3.0, 1.0),
      vec2<f32>(-1.0, 1.0));
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

// Values to write into each attachment. Different for each attachment index and each channel so
// they don't get mixed up. Alpha is clamped to 3 to fit rgb10a2uint.
constexpr std::array<std::array<double, 4>, 4> kAttachmentsIntWriteValues = {{
    {1, 2, 3, 1},
    {5, 6, 7, 2},
    {9, 10, 11, 3},
    {13, 14, 15, 0},
}};
constexpr std::array<std::array<double, 4>, 4> kAttachmentsFloatWriteValues = {{
    {0.12, 0.34, 0.56, 0},
    {0.78, 0.9, 0.19, 1},
    {0.28, 0.37, 0.46, 0.3},
    {0.55, 0.64, 0.73, 1},
}};

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

bool isSintOrUintFormat(WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    return id.find("sint") != std::string_view::npos || id.find("uint") != std::string_view::npos;
}

// getPlainTypeInfo(getTextureFormatType(format)).
std::string_view plainTypeForFormat(WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    if (id.find("sint") != std::string_view::npos) {
        return "i32";
    }
    if (id.find("uint") != std::string_view::npos) {
        return "u32";
    }
    return "f32";
}

uint32_t componentCountOf(WGPUTextureFormat format) {
    return static_cast<uint32_t>(texelRepresentation(format).componentOrder.size());
}

struct ShaderOutput {
    std::vector<double> values;
    std::string_view plainType;
    uint32_t componentCount = 4;
};

// Port of util/shader.ts getFragmentShaderCodeWithOutput (without frag_depth / dual-source).
std::string getFragmentShaderCodeWithOutput(const std::vector<std::optional<ShaderOutput>>& outputs) {
    if (outputs.empty()) {
        return "\n        @fragment fn main() {\n        }";
    }
    std::vector<std::string> resultStrings;
    std::string outputStructString;
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (!outputs[i].has_value()) {
            continue;
        }
        const ShaderOutput& o = *outputs[i];
        const bool isF32 = o.plainType == "f32";
        const std::string suffix = o.plainType == "u32" ? "u" : "";
        std::vector<std::string> v;
        for (double n : o.values) {
            std::ostringstream text;
            text << std::fixed << std::setprecision(isF32 ? 4 : 0) << n;
            v.push_back(text.str());
        }
        std::string outputType;
        std::ostringstream result;
        switch (o.componentCount) {
            case 1:
                outputType = std::string(o.plainType);
                result << v[0] << suffix;
                break;
            case 2:
                outputType = "vec2<" + std::string(o.plainType) + ">";
                result << outputType << "(" << v[0] << suffix << ", " << v[1] << suffix << ")";
                break;
            case 3:
                outputType = "vec3<" + std::string(o.plainType) + ">";
                result << outputType << "(" << v[0] << suffix << ", " << v[1] << suffix << ", " << v[2] << suffix
                       << ")";
                break;
            case 4:
                outputType = "vec4<" + std::string(o.plainType) + ">";
                result << outputType << "(" << v[0] << suffix << ", " << v[1] << suffix << ", " << v[2] << suffix
                       << ", " << v[3] << suffix << ")";
                break;
            default:
                std::abort();
        }
        resultStrings.push_back(result.str());
        outputStructString += "@location(" + std::to_string(i) + ") o" + std::to_string(i) + " : " + outputType + ",\n";
    }
    std::string joined;
    for (size_t i = 0; i < resultStrings.size(); ++i) {
        if (i != 0) {
            joined += ",";
        }
        joined += resultStrings[i];
    }
    return "\n    struct Outputs {\n      " + outputStructString + "\n    }\n\n    @fragment fn main() -> Outputs {\n"
        + "        return Outputs(" + joined + ");\n    }";
}

// format_info.ts computeBytesPerSampleFromFormats.
uint32_t computeBytesPerSampleFromFormats(const std::vector<WGPUTextureFormat>& formats) {
    uint32_t bytesPerSample = 0;
    for (WGPUTextureFormat format : formats) {
        const uint32_t alignment = getColorRenderAlignment(format);
        const uint32_t aligned = (bytesPerSample + alignment - 1) / alignment * alignment;
        bytesPerSample = aligned + getColorRenderByteCost(format);
    }
    return bytesPerSample;
}

std::vector<Value> colorRenderableFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kColorRenderableTextureFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

WGPUTextureFormat formatParam(const ParamRecord& p) {
    return parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
}

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// layout 'auto', kVertexShader, triangle-list; `targets` may contain Undefined-format holes.
WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t,
                                  const std::string& fragmentCode,
                                  const std::vector<WGPUColorTargetState>& targets) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentCode);

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = targets.size();
    fragment.targets = targets.data();

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // 'auto'
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.fragment = &fragment;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    return t.createRenderPipelineTracked(desc);
}

void drawFullscreen(AllFeaturesMaxLimitsGpuTest& t,
                    WGPURenderPipeline pipeline,
                    const std::vector<WGPURenderPassColorAttachment>& colorAttachments) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colorAttachments.size();
    passDesc.colorAttachments = colorAttachments.data();
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPURenderPassColorAttachment clearStoreAttachment(AllFeaturesMaxLimitsGpuTest& t,
                                                   WGPUTexture texture,
                                                   WGPUColor clearValue) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = t.createViewTracked(texture, viewDesc);
    attachment.clearValue = clearValue;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    return attachment;
}

std::vector<double> parseNumberList(std::string_view text) {
    std::vector<double> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string_view item = text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                                          : comma - start);
        out.push_back(std::stod(std::string(item)));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

WGPUBlendFactor parseBlendFactor(std::string_view name) {
    if (name == "zero") return WGPUBlendFactor_Zero;
    if (name == "one") return WGPUBlendFactor_One;
    if (name == "src") return WGPUBlendFactor_Src;
    if (name == "src-alpha") return WGPUBlendFactor_SrcAlpha;
    if (name == "one-minus-src-alpha") return WGPUBlendFactor_OneMinusSrcAlpha;
    if (name == "dst-alpha") return WGPUBlendFactor_DstAlpha;
    if (name == "one-minus-dst-alpha") return WGPUBlendFactor_OneMinusDstAlpha;
    std::abort();
}

CTS_TEST(g, "color,attachments")
    .desc("Test that pipeline with sparse color attachments write values correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("format", colorRenderableFormatValues())
            .beginSubcases()
            .combine("attachmentCount", {2, 3, 4})
            .expand("emptyAttachmentId", [](const ParamRecord& p) {
                const int64_t count = valueAs<int64_t>(*findParam(p, "attachmentCount"));
                std::vector<Value> ids;
                for (int64_t i = 0; i < count; ++i) {
                    ids.emplace_back(i);
                }
                return ids;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t attachmentCount = static_cast<uint32_t>(t.param<int64_t>("attachmentCount"));
        const uint32_t emptyAttachmentId = static_cast<uint32_t>(t.param<int64_t>("emptyAttachmentId"));

        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(format);

        const uint32_t componentCount = componentCountOf(format);

        // We only need to test formats that have a valid color attachment bytes per sample.
        const std::vector<WGPUTextureFormat> formats(attachmentCount, format);
        if (computeBytesPerSampleFromFormats(formats) > t.getLimits().maxColorAttachmentBytesPerSample) {
            t.skip("color attachment bytes per sample exceed maxColorAttachmentBytesPerSample");
        }

        const auto& writeValues = isSintOrUintFormat(format) ? kAttachmentsIntWriteValues : kAttachmentsFloatWriteValues;

        std::vector<WGPUTexture> renderTargets;
        for (uint32_t i = 0; i < attachmentCount; ++i) {
            renderTargets.push_back(createRenderTarget(t, format));
        }

        std::vector<std::optional<ShaderOutput>> outputs;
        std::vector<WGPUColorTargetState> targets;
        for (uint32_t i = 0; i < attachmentCount; ++i) {
            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            if (i == emptyAttachmentId) {
                outputs.emplace_back(std::nullopt);
                target.format = WGPUTextureFormat_Undefined;
            } else {
                ShaderOutput o;
                o.values.assign(writeValues[i].begin(), writeValues[i].end());
                o.plainType = plainTypeForFormat(format);
                o.componentCount = componentCount;
                outputs.emplace_back(o);
                target.format = format;
            }
            targets.push_back(target);
        }
        WGPURenderPipeline pipeline = createPipeline(t, getFragmentShaderCodeWithOutput(outputs), targets);

        std::vector<WGPURenderPassColorAttachment> colorAttachments;
        for (uint32_t i = 0; i < attachmentCount; ++i) {
            if (i == emptyAttachmentId) {
                WGPURenderPassColorAttachment empty = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                empty.view = nullptr;
                colorAttachments.push_back(empty);
            } else {
                colorAttachments.push_back(clearStoreAttachment(t, renderTargets[i], WGPUColor{0.5, 0.5, 0.5, 0.5}));
            }
        }
        drawFullscreen(t, pipeline, colorAttachments);

        for (uint32_t i = 0; i < attachmentCount; ++i) {
            if (i == emptyAttachmentId) {
                continue;
            }
            expectSinglePixelComparisonsAreOkInTexture(
                t, renderTargets[i], format, 0, {PixelComparison{WGPUOrigin3D{0, 0, 0}, writeValues[i]}});
        }
    });

CTS_TEST(g, "color,component_count")
    .desc("Test that extra components of the output (e.g. f32, vec2<f32>, vec3<f32>, vec4<f32>) are discarded.")
    .params([](ParamsBuilder u) {
        return u.combine("format", colorRenderableFormatValues())
            .beginSubcases()
            .combine("componentCount", {1, 2, 3, 4})
            .filter([](const ParamRecord& p) {
                return valueAs<int64_t>(*findParam(p, "componentCount"))
                    >= static_cast<int64_t>(componentCountOf(formatParam(p)));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t componentCount = static_cast<uint32_t>(t.param<int64_t>("componentCount"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(format);

        // Expected RGBA values; extra channels are discarded.
        const std::vector<double> values = {0, 1, 0, 1};

        WGPUTexture renderTarget = createRenderTarget(t, format);

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        ShaderOutput o;
        o.values = values;
        o.plainType = plainTypeForFormat(format);
        o.componentCount = componentCount;
        WGPURenderPipeline pipeline = createPipeline(t, getFragmentShaderCodeWithOutput({o}), {target});

        drawFullscreen(t, pipeline, {clearStoreAttachment(t, renderTarget, WGPUColor{1.0, 0.0, 0.0, 1.0})});

        expectSingleColor(t, renderTarget, format, {1, 1, 1},
                          colorTexelValue({values[0], values[1], values[2], values[3]}));
    });

ParamRecord blendRecord(const char* result,
                        const char* output,
                        const char* colorSrcFactor,
                        const char* colorDstFactor,
                        const char* alphaSrcFactor,
                        const char* alphaDstFactor) {
    return ParamRecord{
        {"_result", Value(result)},
        {"output", Value(output)},
        {"colorSrcFactor", Value(colorSrcFactor)},
        {"colorDstFactor", Value(colorDstFactor)},
        {"alphaSrcFactor", Value(alphaSrcFactor)},
        {"alphaDstFactor", Value(alphaDstFactor)},
    };
}

CTS_TEST(g, "color,component_count,blend")
    .desc(R"(Test that blending behaves correctly when:
- fragment output has no alpha, but the src alpha is not used for the blend operation indicated by blend factors
- attachment format has no alpha, and the dst alpha should be assumed as 1

The attachment has a load value of [1, 0, 0, 1]
)")
    .params([](ParamsBuilder u) {
        return u.combine("format", {"r8unorm", "rg8unorm", "rgba8unorm", "bgra8unorm"})
            .beginSubcases()
            // _result is expected values in the color attachment (extra channels are discarded)
            // output is the fragment shader output vector
            // 0.498 -> 0x7f, 0.502 -> 0x80
            .combineWithParams({
                // fragment output has no alpha
                blendRecord("0,0,0,0", "0", "one", "zero", "zero", "zero"),
                blendRecord("0,0,0,0", "0", "dst-alpha", "zero", "zero", "zero"),
                blendRecord("1,0,0,0", "0", "one-minus-dst-alpha", "dst-alpha", "zero", "one"),
                blendRecord("0.498,0,0,0", "0.498", "dst-alpha", "zero", "zero", "one"),
                blendRecord("0,1,0,0", "0,1", "one", "zero", "zero", "zero"),
                blendRecord("0,1,0,0", "0,1", "dst-alpha", "zero", "zero", "zero"),
                blendRecord("1,0,0,0", "0,1", "one-minus-dst-alpha", "dst-alpha", "zero", "one"),
                blendRecord("0,1,0,0", "0,1,0", "one", "zero", "zero", "zero"),
                blendRecord("0,1,0,0", "0,1,0", "dst-alpha", "zero", "zero", "zero"),
                blendRecord("1,0,0,0", "0,1,0", "one-minus-dst-alpha", "dst-alpha", "zero", "one"),
                // fragment output has alpha
                blendRecord("0.502,1,0,0.498", "0,1,0,0.498", "one", "one-minus-src-alpha", "one", "zero"),
                blendRecord("0.502,0.498,0,0.498", "0,1,0,0.498", "src-alpha", "one-minus-src-alpha", "one", "zero"),
                blendRecord("0,1,0,0.498", "0,1,0,0.498", "dst-alpha", "zero", "one", "zero"),
                blendRecord("0,1,0,0.498", "0,1,0,0.498", "dst-alpha", "zero", "zero", "src"),
                blendRecord("1,0,0,1", "0,1,0,0.498", "one-minus-dst-alpha", "dst-alpha", "zero", "dst-alpha"),
            })
            .filter([](const ParamRecord& p) {
                return parseNumberList(valueAs<std::string>(*findParam(p, "output"))).size()
                    >= componentCountOf(formatParam(p));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::vector<double> result = parseNumberList(t.param<std::string>("_result"));
        const std::vector<double> output = parseNumberList(t.param<std::string>("output"));
        t.skipIfTextureFormatNotSupported(format);
        if (!t.isTextureFormatBlendable(format)) {
            t.skip("texture format is not blendable");
        }

        const uint32_t componentCount = static_cast<uint32_t>(output.size());
        WGPUTexture renderTarget = createRenderTarget(t, format);

        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = parseBlendFactor(t.param<std::string>("colorSrcFactor"));
        blend.color.dstFactor = parseBlendFactor(t.param<std::string>("colorDstFactor"));
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = parseBlendFactor(t.param<std::string>("alphaSrcFactor"));
        blend.alpha.dstFactor = parseBlendFactor(t.param<std::string>("alphaDstFactor"));
        blend.alpha.operation = WGPUBlendOperation_Add;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.blend = &blend;

        ShaderOutput o;
        o.values = output;
        o.plainType = plainTypeForFormat(format);
        o.componentCount = componentCount;
        WGPURenderPipeline pipeline = createPipeline(t, getFragmentShaderCodeWithOutput({o}), {target});

        drawFullscreen(t, pipeline, {clearStoreAttachment(t, renderTarget, WGPUColor{1.0, 0.0, 0.0, 1.0})});

        expectSingleColor(t, renderTarget, format, {1, 1, 1},
                          colorTexelValue({result[0], result[1], result[2], result[3]}));
    });

} // namespace
