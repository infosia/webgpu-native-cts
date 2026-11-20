// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/check_texture/by_sampling.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "webgpu/api/operation/resource_init/check_texture/by_copy.h"
#include "webgpu/api/operation/resource_init/check_texture/texture_zero_init_test.h"

namespace cts::texture_zero {

// Per-device cache of the textureLoad compute pipelines, keyed by their WGSL (upstream bakes the
// mip size, component order and texture type into the shader).
struct TextureZeroSamplingPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<std::string, WGPUComputePipeline>> byDevice;

    ~TextureZeroSamplingPipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                wgpuComputePipelineRelease(pipeline);
            }
        }
    }
};

inline WGPUComputePipeline getSamplingPipeline(TextureZeroInitTest& t, const std::string& code) {
    auto& pipelines = deviceScoped<TextureZeroSamplingPipelineCache>().byDevice[t.device()];
    auto it = pipelines.find(code);
    if (it != pipelines.end()) {
        return it->second;
    }
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = t.createShaderModuleTracked(code);
    desc.compute.entryPoint = tzSv("main");
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(t.device(), &desc);
    if (pipeline == nullptr) {
        t.fail("failed to create the sampling compute pipeline");
    }
    pipelines.emplace(code, pipeline);
    return pipeline;
}

inline char componentLetter(TexelComponent c) {
    switch (c) {
        case TexelComponent::R:
            return 'r';
        case TexelComponent::G:
            return 'g';
        case TexelComponent::B:
            return 'b';
        case TexelComponent::A:
            return 'a';
    }
    return 'r';
}

inline double componentValue(const PerTexelComponents& c, TexelComponent component) {
    switch (component) {
        case TexelComponent::R:
            return c.R;
        case TexelComponent::G:
            return c.G;
        case TexelComponent::B:
            return c.B;
        case TexelComponent::A:
            return c.A;
    }
    return 0;
}

inline void checkContentsBySampling(TextureZeroInitTest& t,
                                    WGPUTexture texture,
                                    InitializedState state,
                                    const SubresourceRange& subresourceRange) {
    const TextureZeroParams& params = t.p;
    const TexelRepresentation& rep = texelRepresentation(params.format);

    // getSingleDataType + getComponentReadbackTraits.
    const ComponentDataType dataType = rep.dataTypes[static_cast<size_t>(rep.componentOrder[0])];
    for (TexelComponent c : rep.componentOrder) {
        if (rep.dataTypes[static_cast<size_t>(c)] != dataType) {
            t.fail("format components do not share a single data type");
        }
    }
    std::string shaderType;
    switch (dataType) {
        case ComponentDataType::Ufloat:
        case ComponentDataType::Float:
        case ComponentDataType::Unorm:
        case ComponentDataType::Snorm:
            shaderType = "f32";
            break;
        case ComponentDataType::Uint:
            shaderType = "u32";
            break;
        case ComponentDataType::Sint:
            shaderType = "i32";
            break;
    }

    const std::vector<TexelComponent>& componentOrder = rep.componentOrder;
    const size_t componentCount = componentOrder.size();

    // For single-component textures, generates .r
    // For multi-component textures, generates ex.) .rgba[i], .bgra[i], .rgb[i]
    std::string indexExpression;
    if (componentCount == 1) {
        indexExpression = std::string(1, componentLetter(componentOrder[0]));
    } else {
        for (TexelComponent c : componentOrder) {
            indexExpression += componentLetter(c);
        }
        indexExpression += "[i]";
    }

    // This port never runs compatibility mode, so the view dimension is the texture dimension.
    const std::string viewDimension(textureDimensionIdentifier(params.dimension));
    const std::string xd = "_" + viewDimension;
    const std::string multisampled = params.sampleCount > 1 ? "_multisampled" : "";
    std::string texelIndexExpression;
    WGPUTextureViewDimension wgpuViewDimension = WGPUTextureViewDimension_2D;
    switch (params.dimension) {
        case WGPUTextureDimension_2D:
            texelIndexExpression = "vec2<i32>(GlobalInvocationID.xy)";
            wgpuViewDimension = WGPUTextureViewDimension_2D;
            break;
        case WGPUTextureDimension_3D:
            texelIndexExpression = "vec3<i32>(GlobalInvocationID.xyz)";
            wgpuViewDimension = WGPUTextureViewDimension_3D;
            break;
        case WGPUTextureDimension_1D:
            texelIndexExpression = "i32(GlobalInvocationID.x)";
            wgpuViewDimension = WGPUTextureViewDimension_1D;
            break;
        default:
            t.fail("unreachable texture dimension");
    }

    for (uint32_t level = subresourceRange.mipRange.begin; level < subresourceRange.mipRange.end; ++level) {
        const std::array<uint32_t, 3> mip = virtualMipSize(params.dimension, t.textureSize(), level);
        const uint32_t width = mip[0];
        const uint32_t height = mip[1];
        const uint32_t depth = mip[2];

        std::ostringstream code;
        code << R"(
            struct Constants {
              level : i32,
              layer : i32,
            };

            @group(0) @binding(0) var<uniform> constants : Constants;
            @group(0) @binding(1) var myTexture : texture)"
             << multisampled << xd << "<" << shaderType << R"(>;

            struct Result {
              values : array<)"
             << shaderType << R"(>
            };
            @group(0) @binding(3) var<storage, read_write> result : Result;

            @compute @workgroup_size(1)
            fn main(@builtin(global_invocation_id) GlobalInvocationID : vec3<u32>) {
              let flatIndex : u32 = )"
             << componentCount << "u * (\n                " << width << "u * " << height
             << "u * GlobalInvocationID.z +\n                " << width << R"(u * GlobalInvocationID.y +
                GlobalInvocationID.x
              );
              let texel : vec4<)"
             << shaderType << R"(> = textureLoad(
                myTexture, )"
             << texelIndexExpression << R"(, constants.level);

              for (var i : u32 = 0u; i < )"
             << componentCount << R"(u; i = i + 1u) {
                result.values[flatIndex + i] = texel.)"
             << indexExpression << R"(;
              }
            })";
        WGPUComputePipeline computePipeline = getSamplingPipeline(t, code.str());

        for (uint32_t layer = subresourceRange.layerRange.begin; layer < subresourceRange.layerRange.end; ++layer) {
            const int32_t uboData[2] = {static_cast<int32_t>(level), static_cast<int32_t>(layer)};
            WGPUBufferDescriptor uboDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            uboDesc.mappedAtCreation = WGPU_TRUE;
            uboDesc.size = 8;
            uboDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            WGPUBuffer ubo = t.createBufferTracked(uboDesc);
            void* uboMapping = wgpuBufferGetMappedRange(ubo, 0, 8);
            if (uboMapping == nullptr) {
                t.fail("failed to map the uniform buffer");
            }
            std::memcpy(uboMapping, uboData, sizeof(uboData));
            wgpuBufferUnmap(ubo);

            const uint64_t byteLength = static_cast<uint64_t>(width) * height * depth * 4u * componentCount;
            WGPUBufferDescriptor resultDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            resultDesc.size = byteLength;
            resultDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
            WGPUBuffer resultBuffer = t.createBufferTracked(resultDesc);

            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            viewDesc.baseArrayLayer = layer;
            viewDesc.arrayLayerCount = 1;
            viewDesc.dimension = wgpuViewDimension;
            WGPUTextureView view = t.createViewTracked(texture, viewDesc);

            WGPUBindGroupEntry entries[3] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                             WGPU_BIND_GROUP_ENTRY_INIT};
            entries[0].binding = 0;
            entries[0].buffer = ubo;
            entries[0].size = WGPU_WHOLE_SIZE;
            entries[1].binding = 1;
            entries[1].textureView = view;
            entries[2].binding = 3;
            entries[2].buffer = resultBuffer;
            entries[2].size = WGPU_WHOLE_SIZE;

            WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(computePipeline, 0);
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout = layout;
            bgDesc.entryCount = 3;
            bgDesc.entries = entries;
            WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
            wgpuBindGroupLayoutRelease(layout);

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, computePipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, width, height, depth);
            wgpuComputePassEncoderEnd(pass);
            t.submit(encoder);
            wgpuBufferDestroy(ubo);

            // Expected values: each texel's components in `componentOrder`, as f32/u32/i32.
            const PerTexelComponents expectedState = t.stateToTexelComponents(state);
            std::vector<uint8_t> expected(static_cast<size_t>(byteLength));
            size_t i = 0;
            for (uint32_t d = 0; d < depth; ++d) {
                for (uint32_t h = 0; h < height; ++h) {
                    for (uint32_t w = 0; w < width; ++w) {
                        for (TexelComponent c : componentOrder) {
                            const double value = componentValue(expectedState, c);
                            if (shaderType == "f32") {
                                const float f = static_cast<float>(value);
                                std::memcpy(expected.data() + i, &f, 4);
                            } else if (shaderType == "u32") {
                                const uint32_t u = static_cast<uint32_t>(value);
                                std::memcpy(expected.data() + i, &u, 4);
                            } else {
                                const int32_t s = static_cast<int32_t>(value);
                                std::memcpy(expected.data() + i, &s, 4);
                            }
                            i += 4;
                        }
                    }
                }
            }

            t.expectGPUBufferValuesPassCheck(
                resultBuffer,
                [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                    if (len < expected.size()) {
                        return std::string("readback too short");
                    }
                    for (size_t e = 0; e < expected.size(); e += 4) {
                        if (std::memcmp(actual + e, expected.data() + e, 4) != 0) {
                            const size_t element = e / 4;
                            const size_t texel = element / componentCount;
                            std::ostringstream msg;
                            msg << subresourceContext("checkContentsBySampling", state, level, layer) << ": texel (x="
                                << texel % width << ", y=" << (texel / width) % height
                                << ", z=" << texel / (static_cast<size_t>(width) * height) << ") component "
                                << componentLetter(componentOrder[element % componentCount]) << ": expected ";
                            if (shaderType == "f32") {
                                float a = 0;
                                float x = 0;
                                std::memcpy(&a, actual + e, 4);
                                std::memcpy(&x, expected.data() + e, 4);
                                msg << x << ", got " << a;
                            } else if (shaderType == "u32") {
                                uint32_t a = 0;
                                uint32_t x = 0;
                                std::memcpy(&a, actual + e, 4);
                                std::memcpy(&x, expected.data() + e, 4);
                                msg << x << ", got " << a;
                            } else {
                                int32_t a = 0;
                                int32_t x = 0;
                                std::memcpy(&a, actual + e, 4);
                                std::memcpy(&x, expected.data() + e, 4);
                                msg << x << ", got " << a;
                            }
                            return msg.str();
                        }
                    }
                    return std::nullopt;
                },
                0,
                static_cast<size_t>(byteLength));
        }
    }
}

}  // namespace cts::texture_zero
