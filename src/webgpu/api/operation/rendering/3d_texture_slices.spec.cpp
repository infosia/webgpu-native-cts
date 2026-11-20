// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/rendering/3d_texture_slices.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// checkTextureContent reads every mip level back with a textureLoad compute pass (upstream
// texture_utils.ts readTextureToTexelViews; the unused uniform/sampler bindings are omitted) and
// compares with upstream compareTexelViews/texelsApproximatelyEqual: a component fails when it is
// more than 3 ULPs and more than 0 (maxFractionalDiff) away from the expected value.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,3d_texture_slices",
    R"(
Test rendering to 3d texture slices.
- Render to same slice on different render pass, different textures, or texture [1, 1, N]'s different mip levels
- Render to different slices at mip levels on same texture in render pass
)");

constexpr uint32_t kSize = 4;

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

bool isSintOrUintFormat(WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    return id.find("sint") != std::string_view::npos || id.find("uint") != std::string_view::npos;
}

enum class ComponentType { F32 = 0, U32 = 1, I32 = 2 };

ComponentType componentTypeOf(WGPUTextureFormat format) {
    const std::string_view id = textureFormatIdentifier(format);
    if (id.find("sint") != std::string_view::npos) {
        return ComponentType::I32;
    }
    if (id.find("uint") != std::string_view::npos) {
        return ComponentType::U32;
    }
    return ComponentType::F32;
}

// getTextureFormatTypeInfo(format).resultType
std::string_view resultTypeOf(WGPUTextureFormat format) {
    switch (componentTypeOf(format)) {
        case ComponentType::I32:
            return "vec4i";
        case ComponentType::U32:
            return "vec4u";
        case ComponentType::F32:
            break;
    }
    return "vec4f";
}

// kPossibleColorRenderableTextureFormats filtered by textureFormatAndDimensionPossiblyCompatible('3d').
std::vector<Value> formatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kColorRenderableTextureFormats) {
        if (textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension_3D, format)) {
            values.emplace_back(std::string(textureFormatIdentifier(format)));
        }
    }
    return values;
}

std::array<double, 4> getClearValueForFormat(WGPUTextureFormat format) {
    return isSintOrUintFormat(format) ? std::array<double, 4>{11, 22, 33, 44} : std::array<double, 4>{0.3, 0.4, 0.5, 0.6};
}

// generates:
//    11, 22, 33, 44 for location 0, ch 0, 1, 2, 3
//    21, 32, 43, 54 for location 1, ch 0, 1, 2, 3
//    31, 42, 53, 64 for location 2, ch 0, 1, 2, 3
uint32_t outputForLocationByChannel(uint32_t location, uint32_t ch) {
    return (ch + location + 1) * 10 + 1 + location;
}

// Creates a shader module that outputs different values for each location.
WGPUShaderModule createShaderModule(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format, uint32_t attachmentCount = 1) {
    const std::string resultType(resultTypeOf(format));
    std::string locations;
    std::string outputs;
    for (uint32_t i = 0; i < attachmentCount; ++i) {
        if (i != 0) {
            locations += ",\n        ";
            outputs += "\n        ";
        }
        locations += "@location(" + std::to_string(i) + ") color" + std::to_string(i) + " : " + resultType;
        std::string output;
        for (uint32_t ch = 0; ch < 4; ++ch) {
            if (ch != 0) {
                output += ", ";
            }
            output += std::to_string(outputForLocationByChannel(i, ch)) + " / d";
        }
        outputs += "output.color" + std::to_string(i) + " = " + resultType + "(" + output + ");";
    }

    const std::string code = R"(
      struct Output {
        )" + locations + R"(
      }

      @vertex
      fn main_vs(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f {
        //  -1,1
        //    +-----+
        //    |\   |
        //    |.\  |
        //    |..\ |
        //    |...\|
        //    +-----+
        // -1,-1   1,-1

        let pos = array(
            // Triangle is slightly extended so its edge doesn't cut through pixel centers.
            vec2f(-1.0, 1.01),
            vec2f(1.01, -1.0),
            vec2f(-1.0, -1.0),
        );
        return vec4f(pos[VertexIndex], 0.0, 1.0);
      }

      const d = )" + std::string(isSintOrUintFormat(format) ? "1" : "255.0") + R"(;

      @fragment
      fn main_fs() -> Output {
        var output : Output;
        )" + outputs + R"(
        return output;
      }
  )";
    return t.createShaderModuleTracked(code);
}

WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t,
                                  WGPUShaderModule module,
                                  WGPUTextureFormat format,
                                  uint32_t attachmentCount) {
    std::vector<WGPUColorTargetState> targets(attachmentCount, WGPUColorTargetState(WGPU_COLOR_TARGET_STATE_INIT));
    for (WGPUColorTargetState& target : targets) {
        target.format = format;
    }
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("main_fs");
    fragment.targetCount = targets.size();
    fragment.targets = targets.data();

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // 'auto'
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView("main_vs");
    desc.fragment = &fragment;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    return t.createRenderPipelineTracked(desc);
}

WGPUTextureView createMipView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, uint32_t mipLevel) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.baseMipLevel = mipLevel;
    desc.mipLevelCount = 1;
    return t.createViewTracked(texture, desc);
}

WGPURenderPassColorAttachment colorAttachmentFor(WGPUTextureView view,
                                                 uint32_t depthSlice,
                                                 const std::array<double, 4>& clearValue) {
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthSlice = depthSlice;
    attachment.clearValue = WGPUColor{clearValue[0], clearValue[1], clearValue[2], clearValue[3]};
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    return attachment;
}

void encodeDrawPass(AllFeaturesMaxLimitsGpuTest& t,
                    WGPUCommandEncoder encoder,
                    WGPURenderPipeline pipeline,
                    const WGPURenderPassColorAttachment* attachments,
                    size_t attachmentCount) {
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = attachmentCount;
    passDesc.colorAttachments = attachments;
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// --- readTextureToTexelViews for a 3d texture ---------------------------------------------------

// Per-device textureLoad readback pipelines, one per component type (f32 / u32 / i32). Not tracked
// per case: released with the cached devices.
struct ReadTexture3DPipelines {
    std::array<WGPUBindGroupLayout, 3> bindGroupLayouts = {nullptr, nullptr, nullptr};
    std::array<WGPUComputePipeline, 3> pipelines = {nullptr, nullptr, nullptr};
};

struct ReadTexture3DPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, ReadTexture3DPipelines> byDevice;

    ~ReadTexture3DPipelineCache() override {
        for (auto& [device, entry] : byDevice) {
            (void)device;
            for (WGPUComputePipeline pipeline : entry.pipelines) {
                if (pipeline != nullptr) {
                    wgpuComputePipelineRelease(pipeline);
                }
            }
            for (WGPUBindGroupLayout layout : entry.bindGroupLayouts) {
                if (layout != nullptr) {
                    wgpuBindGroupLayoutRelease(layout);
                }
            }
        }
    }
};

const ReadTexture3DPipelines& getReadPipeline(AllFeaturesMaxLimitsGpuTest& t, ComponentType type) {
    ReadTexture3DPipelines& entry = deviceScoped<ReadTexture3DPipelineCache>().byDevice[t.device()];
    const size_t index = static_cast<size_t>(type);
    if (entry.pipelines[index] != nullptr) {
        return entry;
    }
    const char* componentType = type == ComponentType::F32 ? "f32" : (type == ComponentType::U32 ? "u32" : "i32");
    const char* resultType = type == ComponentType::F32 ? "vec4f" : (type == ComponentType::U32 ? "vec4u" : "vec4i");
    const std::string code = std::string(R"(
        @group(0) @binding(0) var tex: texture_3d<)") + componentType + R"(>;
        @group(0) @binding(1) var<storage, read_write> data: array<)" + resultType + R"(>;

        @compute @workgroup_size(1) fn cs(
          @builtin(global_invocation_id) global_invocation_id : vec3<u32>) {
          let size = textureDimensions(tex, 0);
          let ndx = global_invocation_id.z * size.x * size.y +
                    global_invocation_id.y * size.x +
                    global_invocation_id.x;
          data[ndx] = textureLoad(tex, global_invocation_id, 0);
        }
      )";
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = stringView(code);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(t.device(), &moduleDesc);

    std::array<WGPUBindGroupLayoutEntry, 2> entries = {WGPUBindGroupLayoutEntry(WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT),
                                                       WGPUBindGroupLayoutEntry(WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT)};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].texture.sampleType = type == ComponentType::F32
        ? WGPUTextureSampleType_UnfilterableFloat
        : (type == ComponentType::U32 ? WGPUTextureSampleType_Uint : WGPUTextureSampleType_Sint);
    entries[0].texture.viewDimension = WGPUTextureViewDimension_3D;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer.type = WGPUBufferBindingType_Storage;
    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = entries.size();
    bglDesc.entries = entries.data();
    WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(t.device(), &bglDesc);

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(t.device(), &layoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = layout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = stringView("cs");
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(t.device(), &pipelineDesc);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);

    entry.bindGroupLayouts[index] = bindGroupLayout;
    entry.pipelines[index] = pipeline;
    return entry;
}

std::array<uint32_t, 3> mipSize3D(const std::array<uint32_t, 3>& size, uint32_t mipLevel) {
    return {std::max(1u, size[0] >> mipLevel), std::max(1u, size[1] >> mipLevel), std::max(1u, size[2] >> mipLevel)};
}

// One texture plus, per mip level, the slice -> location map of the slices that were rendered.
struct TextureToCheck {
    WGPUTexture texture = nullptr;
    std::string label;
    std::array<uint32_t, 3> size = {0, 0, 0};
    uint32_t mipLevelCount = 1;
    std::map<uint32_t, std::map<uint32_t, uint32_t>> mipLevelToSliceToLocationMap;
};

// Port of checkTextureContent: reads all mip levels with textureLoad and compares every texel with
// the value the shader writes (render texel where x <= y, clear texel elsewhere) in rendered
// slices and 0 elsewhere.
void checkTextureContent(AllFeaturesMaxLimitsGpuTest& t,
                         WGPUTextureFormat format,
                         const TextureToCheck& check,
                         const std::array<double, 4>& clearValue) {
    const ComponentType type = componentTypeOf(format);
    const ReadTexture3DPipelines& read = getReadPipeline(t, type);
    const size_t index = static_cast<size_t>(type);
    const double d = isSintOrUintFormat(format) ? 1.0 : 255.0;
    const TexelRepresentation& repr = texelRepresentation(format);

    std::vector<WGPUBuffer> storageBuffers;
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    for (uint32_t mipLevel = 0; mipLevel < check.mipLevelCount; ++mipLevel) {
        const std::array<uint32_t, 3> size = mipSize3D(check.size, mipLevel);
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.label = stringView("readTextureToTexelViews:storageBuffer");
        bufferDesc.size = static_cast<uint64_t>(size[0]) * size[1] * size[2] * 4 * 4;
        bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer storageBuffer = t.createBufferTracked(bufferDesc);
        storageBuffers.push_back(storageBuffer);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = WGPUTextureViewDimension_3D;
        viewDesc.baseMipLevel = mipLevel;
        viewDesc.mipLevelCount = 1;
        std::array<WGPUBindGroupEntry, 2> entries = {WGPUBindGroupEntry(WGPU_BIND_GROUP_ENTRY_INIT),
                                                     WGPUBindGroupEntry(WGPU_BIND_GROUP_ENTRY_INIT)};
        entries[0].binding = 0;
        entries[0].textureView = t.createViewTracked(check.texture, viewDesc);
        entries[1].binding = 1;
        entries[1].buffer = storageBuffer;
        entries[1].size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = read.bindGroupLayouts[index];
        bgDesc.entryCount = entries.size();
        bgDesc.entries = entries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, read.pipelines[index]);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, size[0], size[1], size[2]);
        wgpuComputePassEncoderEnd(pass);
    }
    submit(t, encoder);

    std::vector<std::string> errors;
    for (uint32_t mipLevel = 0; mipLevel < check.mipLevelCount; ++mipLevel) {
        const std::array<uint32_t, 3> size = mipSize3D(check.size, mipLevel);
        const auto sliceMap = check.mipLevelToSliceToLocationMap.find(mipLevel);
        const size_t byteLength = static_cast<size_t>(size[0]) * size[1] * size[2] * 16;
        t.expectGPUBufferValuesPassCheck(
            storageBuffers[mipLevel],
            [&](const uint8_t* data, size_t len) -> std::optional<std::string> {
                std::vector<std::string> mipErrors;
                for (uint32_t z = 0; z < size[2]; ++z) {
                    std::optional<uint32_t> location;
                    if (sliceMap != check.mipLevelToSliceToLocationMap.end()) {
                        const auto it = sliceMap->second.find(z);
                        if (it != sliceMap->second.end()) {
                            location = it->second;
                        }
                    }
                    for (uint32_t y = 0; y < size[1]; ++y) {
                        for (uint32_t x = 0; x < size[0]; ++x) {
                            std::array<double, 4> expected = {0, 0, 0, 0};
                            if (location.has_value()) {
                                if (x <= y) {
                                    for (uint32_t ch = 0; ch < 4; ++ch) {
                                        expected[ch] = outputForLocationByChannel(*location, ch) / d;
                                    }
                                } else {
                                    expected = clearValue;
                                }
                            }
                            const size_t offset = ((static_cast<size_t>(z) * size[1] + y) * size[0] + x) * 16;
                            if (offset + 16 > len) {
                                return std::string("readback too short");
                            }
                            std::array<double, 4> actual = {0, 0, 0, 0};
                            for (uint32_t ch = 0; ch < 4; ++ch) {
                                const uint8_t* p = data + offset + ch * 4;
                                if (type == ComponentType::F32) {
                                    float v = 0;
                                    std::memcpy(&v, p, 4);
                                    actual[ch] = v;
                                } else if (type == ComponentType::U32) {
                                    uint32_t v = 0;
                                    std::memcpy(&v, p, 4);
                                    actual[ch] = v;
                                } else {
                                    int32_t v = 0;
                                    std::memcpy(&v, p, 4);
                                    actual[ch] = v;
                                }
                            }
                            // texelsApproximatelyEqual with maxFractionalDiff 0: components the
                            // format lacks are the same default on both sides.
                            for (TexelComponent component : repr.componentOrder) {
                                const uint32_t c = static_cast<uint32_t>(component);
                                const double absDiff = std::fabs(actual[c] - expected[c]);
                                const int64_t ulpDiff = repr.ulpFromZero(c, actual[c]) - repr.ulpFromZero(c, expected[c]);
                                if ((ulpDiff > 3 || ulpDiff < -3) && absDiff > 0) {
                                    std::ostringstream msg;
                                    msg << "texel at " << x << ", " << y << ", " << z << ", sampleIndex: 0 expected: "
                                        << expected[0] << "," << expected[1] << "," << expected[2] << ","
                                        << expected[3] << ", actual: " << actual[0] << "," << actual[1] << ","
                                        << actual[2] << "," << actual[3];
                                    mipErrors.push_back(msg.str());
                                    break;
                                }
                            }
                        }
                    }
                }
                if (mipErrors.empty()) {
                    return std::nullopt;
                }
                std::ostringstream msg;
                msg << "errors in texture: (" << check.label << ")\n  errors in mipLevel: " << mipLevel;
                const size_t shown = std::min<size_t>(mipErrors.size(), 16);
                for (size_t i = 0; i < shown; ++i) {
                    msg << "\n  " << mipErrors[i];
                }
                if (mipErrors.size() > shown) {
                    msg << "\n  ... (" << (mipErrors.size() - shown) << " more)";
                }
                return msg.str();
            },
            0, byteLength);
    }
}

WGPUTexture create3DTexture(AllFeaturesMaxLimitsGpuTest& t,
                            WGPUTextureFormat format,
                            const std::array<uint32_t, 3>& size,
                            uint32_t mipLevelCount,
                            std::string_view label) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = stringView(label);
    desc.size = WGPUExtent3D{size[0], size[1], size[2]};
    desc.dimension = WGPUTextureDimension_3D;
    desc.format = format;
    desc.mipLevelCount = mipLevelCount;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    return t.createTextureTracked(desc);
}

uint32_t maxAttachmentCount(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxAttachmentCountPerSample = limits.maxColorAttachmentBytesPerSample / getColorRenderByteCost(format);
    return std::min(maxAttachmentCountPerSample, limits.maxColorAttachments);
}

CTS_TEST(g, "one_color_attachment,mip_levels")
    .desc("Render to a 3d texture slice with mip levels.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues()).combine("mipLevel", {0, 1, 2}).combine("depthSlice", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        const uint32_t depthSlice = static_cast<uint32_t>(t.param<int64_t>("depthSlice"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatDoesNotSupportUsage(WGPUTextureUsage_RenderAttachment, format);

        const std::array<double, 4> clearValue = getClearValueForFormat(format);

        TextureToCheck check;
        check.size = {kSize << mipLevel, kSize << mipLevel, 2u << mipLevel};
        check.mipLevelCount = mipLevel + 1;
        check.texture = create3DTexture(t, format, check.size, check.mipLevelCount, "");

        WGPUShaderModule module = createShaderModule(t, format);
        WGPURenderPipeline pipeline = createPipeline(t, module, format, 1);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        const WGPURenderPassColorAttachment attachment =
            colorAttachmentFor(createMipView(t, check.texture, mipLevel), depthSlice, clearValue);
        encodeDrawPass(t, encoder, pipeline, &attachment, 1);
        submit(t, encoder);

        check.mipLevelToSliceToLocationMap[mipLevel][depthSlice] = 0;
        checkTextureContent(t, format, check, clearValue);
    });

CTS_TEST(g, "multiple_color_attachments,same_mip_level")
    .desc(R"(
  Render to the different slices of 3d texture in multiple color attachments.
  - Same 3d texture with different slices at same mip level
  - Different 3d textures with same slice at same mip level
  )")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues())
            .combine("sameTexture", {true, false})
            .beginSubcases()
            .combine("samePass", {true, false})
            .combine("mipLevel", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool sameTexture = t.param<bool>("sameTexture");
        const bool samePass = t.param<bool>("samePass");
        const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatDoesNotSupportUsage(WGPUTextureUsage_RenderAttachment, format);

        const uint32_t attachmentCount = maxAttachmentCount(t, format);
        const std::array<uint32_t, 3> size = {kSize << mipLevel, kSize << mipLevel, (1u << attachmentCount) << mipLevel};
        const uint32_t mipLevelCount = mipLevel + 1;
        const std::array<double, 4> clearValue = getClearValueForFormat(format);

        std::vector<TextureToCheck> textures(1);
        textures[0].texture = create3DTexture(t, format, size, mipLevelCount, "texture-0");
        textures[0].label = "texture-0";
        textures[0].size = size;
        textures[0].mipLevelCount = mipLevelCount;
        textures[0].mipLevelToSliceToLocationMap[mipLevel] = {};

        std::vector<WGPURenderPassColorAttachment> colorAttachments;
        for (uint32_t i = 0; i < attachmentCount; ++i) {
            WGPUTexture target = nullptr;
            if (sameTexture) {
                target = textures[0].texture;
                textures[0].mipLevelToSliceToLocationMap[mipLevel][i] = samePass ? i : 0;
            } else {
                TextureToCheck diff;
                diff.label = "texture-" + std::to_string(i);
                diff.texture = create3DTexture(t, format, size, mipLevelCount, diff.label);
                diff.size = size;
                diff.mipLevelCount = mipLevelCount;
                diff.mipLevelToSliceToLocationMap[mipLevel][0] = samePass ? i : 0;
                target = diff.texture;
                textures.push_back(diff);
            }
            colorAttachments.push_back(
                colorAttachmentFor(createMipView(t, target, mipLevel), sameTexture ? i : 0, clearValue));
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (samePass) {
            WGPUShaderModule module = createShaderModule(t, format, attachmentCount);
            WGPURenderPipeline pipeline = createPipeline(t, module, format, attachmentCount);
            encodeDrawPass(t, encoder, pipeline, colorAttachments.data(), colorAttachments.size());
        } else {
            WGPUShaderModule module = createShaderModule(t, format);
            WGPURenderPipeline pipeline = createPipeline(t, module, format, 1);
            for (uint32_t i = 0; i < attachmentCount; ++i) {
                encodeDrawPass(t, encoder, pipeline, &colorAttachments[i], 1);
            }
        }
        submit(t, encoder);

        for (const TextureToCheck& check : textures) {
            checkTextureContent(t, format, check, getClearValueForFormat(format));
        }
    });

CTS_TEST(g, "multiple_color_attachments,same_slice_with_diff_mip_levels")
    .desc(R"(
  Render to the same slice of a 3d texture at different mip levels in multiple color attachments.
  - For texture size with 1x1xN, the same depth slice of different mip levels can be rendered.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues()).combine("depthSlice", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t depthSlice = static_cast<uint32_t>(t.param<int64_t>("depthSlice"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatDoesNotSupportUsage(WGPUTextureUsage_RenderAttachment, format);

        constexpr uint32_t kBaseSize = 1;
        const uint32_t attachmentCount = maxAttachmentCount(t, format);

        WGPUShaderModule module = createShaderModule(t, format, attachmentCount);
        WGPURenderPipeline pipeline = createPipeline(t, module, format, attachmentCount);

        const std::array<double, 4> clearValue = getClearValueForFormat(format);
        TextureToCheck check;
        check.size = {kBaseSize, kBaseSize, (depthSlice + 1) << attachmentCount};
        check.mipLevelCount = attachmentCount;
        check.texture = create3DTexture(t, format, check.size, check.mipLevelCount, "");

        std::vector<WGPURenderPassColorAttachment> colorAttachments;
        for (uint32_t i = 0; i < attachmentCount; ++i) {
            check.mipLevelToSliceToLocationMap[i][depthSlice] = i;
            colorAttachments.push_back(colorAttachmentFor(createMipView(t, check.texture, i), depthSlice, clearValue));
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        encodeDrawPass(t, encoder, pipeline, colorAttachments.data(), colorAttachments.size());
        submit(t, encoder);

        checkTextureContent(t, format, check, clearValue);
    });

} // namespace
