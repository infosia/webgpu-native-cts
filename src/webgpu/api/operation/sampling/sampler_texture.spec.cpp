// Ported from gpuweb/cts src/webgpu/api/operation/sampling/sampler_texture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2025 Kota Iguchi, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,sampling,sampler_texture",
    R"(
Tests samplers with textures.

- test that you can use the maximum number of textures
  with the maximum number of samplers.
)");

// Returns aligned value (round up value to a multiple of alignment).
static uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

static WGPUStringView stringView(std::string_view sv) {
    return WGPUStringView{sv.data(), sv.size()};
}

// Decode a floating-point rgba8unorm channel value (0..1) to an integer in 0..255.
// pack4x8unorm(f) = round(f * 255) stored as the low byte of each lane.
// The upstream JS does: let bytes = pack4x8unorm(f); then bytes & 0xffff, bytes >> 16
// which reconstructs two u16s that were stored in the rg channels of an rgba8unorm texture.
//
// Here we read the rg16uint render target directly from the GPU buffer, so we bypass the
// rgba8unorm-to-rg16uint pack/unpack entirely — see readback logic below.

// Per-stage expected output: a list of (textureId, samplerId) pairs.
// textureId = texelValue | (stage << 15),  samplerId = (smpNum % maxSamplers) + 1.
// Storage textures have samplerId = 0.

// Information about one bind group's resources.
struct BindGroupResources {
    std::vector<WGPUBindGroupLayoutEntry> layoutEntries;
    std::vector<WGPUBindGroupEntry>       bindEntries;
};

CTS_TEST(g, "sample_texture_combos")
    .desc(R"DESC(
Test that you can use the maximum number of textures with the maximum number of samplers.
and the maximum number of storage textures per stage.

The test works by making the maximum number of texture+sampler combos and the max storage
textures per stage. Each texture is [maxSamplersPerShaderStage + maxStorageTexturesInStage, 1]
in size and each texel is [textureId, samplerId]. A function "useCombo<StageNum>(comboId)" is
made that returns stage[stageNum].combo[comboId].texel[id, 0] or to put it another way, it
returns the nth texel from the nth combo for that stage.

These are read in both the vertex shader and fragment shader and written to a
[maxSamplerPerShaderStage + maxStorageTexturesInStage, 2] texture where the top row is the
values from the vertex shader and the bottom row from the fragment shader.

The result should be a texture that has a value in each texel unique to a particular combo
or storage texture.
)DESC")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Retrieve device limits.
        const WGPULimits lim = t.getLimits();

        // Retrieve compatibility-mode stage-specific storage-texture limits (may be UINT32_MAX if
        // the device is not in compatibility mode, i.e. they are "undefined" in WebGPU terms).
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();

        const uint32_t maxSampledTexturesPerShaderStage = lim.maxSampledTexturesPerShaderStage;
        const uint32_t maxSamplersPerShaderStage        = lim.maxSamplersPerShaderStage;
        const uint32_t maxBindingsPerBindGroup          = lim.maxBindingsPerBindGroup;
        const uint32_t maxStorageTexturesPerShaderStage = lim.maxStorageTexturesPerShaderStage;

        // Upstream: numStorageTexturesIn{Vertex,Fragment}Stage = compat limit ?? maxStorageTexturesPerShaderStage.
        // WGPU_LIMIT_U32_UNDEFINED == UINT32_MAX means "not supported / use fallback".
        const uint32_t numStorageTexturesInVertexStage =
            (compat.maxStorageTexturesInVertexStage != WGPU_LIMIT_U32_UNDEFINED)
                ? compat.maxStorageTexturesInVertexStage
                : maxStorageTexturesPerShaderStage;
        const uint32_t numStorageTexturesInFragmentStage =
            (compat.maxStorageTexturesInFragmentStage != WGPU_LIMIT_U32_UNDEFINED)
                ? compat.maxStorageTexturesInFragmentStage
                : maxStorageTexturesPerShaderStage;

        t.expect(maxSampledTexturesPerShaderStage < 0xfffe, "maxSampledTexturesPerShaderStage sanity");
        t.expect(maxSamplersPerShaderStage        < 0xfffe, "maxSamplersPerShaderStage sanity");

        // In compatibility mode upstream uses min(textures, samplers); otherwise textures*samplers.
        // We detect compatibility mode by checking whether the compat chain limits are defined.
        // Since there is no direct t.isCompatibility, we check whether the max storage texture
        // per-stage limits are defined as finite compat values (which only non-compat mode sets
        // to UINT32_MAX). A simpler heuristic: WebGPU native devices (non-compat) have
        // WGPU_LIMIT_U32_UNDEFINED for stage-specific storage limits.
        // Upstream logic: isCompatibility ? min(tex,smp) : tex*smp.
        const bool isCompatibility =
            (compat.maxStorageTexturesInVertexStage != WGPU_LIMIT_U32_UNDEFINED)
            || (compat.maxStorageTexturesInFragmentStage != WGPU_LIMIT_U32_UNDEFINED);

        const uint32_t maxTestableCombosPerStage = isCompatibility
            ? std::min(maxSampledTexturesPerShaderStage, maxSamplersPerShaderStage)
            : maxSampledTexturesPerShaderStage * maxSamplersPerShaderStage;

        // Width of each source texture = maxSamplers + max(storageV, storageF).
        const uint32_t maxStorageInAnyStage = std::max(
            numStorageTexturesInVertexStage, numStorageTexturesInFragmentStage);
        const uint32_t width = maxSamplersPerShaderStage + maxStorageInAnyStage;

        // ---- Build resources ----
        // We accumulate bind-group-resource records; each record maps to one @group().
        // When the current group reaches maxBindingsPerBindGroup, we start a new group.

        // The map from textureId string to already-created texture index.
        // textureId: "tex<stage>_<texNum>"
        struct TextureEntry {
            WGPUTexture     texture = nullptr;
            WGPUTextureView view    = nullptr;
            uint32_t        texelValue = 0; // unique value encoded in the texture
        };
        std::vector<TextureEntry> textureEntries;  // owns view handles
        // Map from textureId string -> index into textureEntries
        struct TexIdMap { std::string id; uint32_t index; };
        std::vector<TexIdMap> texIdMap;

        struct SamplerEntry {
            std::string  id;
            WGPUSampler  sampler = nullptr;
        };
        std::vector<SamplerEntry> samplerEntries;

        // Declaration lines for WGSL.
        std::vector<std::string> declarationLines;

        // Bind groups: parallel arrays (layout entries and bind entries).
        std::vector<BindGroupResources> groups(1);

        // per stage, per texel: [textureId_value, samplerId_value]
        // expected[stage] is a list of [tid, sid] pairs, one per combo/storage texture.
        std::vector<std::vector<std::array<uint32_t, 2>>> expected(2);

        // Helper: add a resource (sampler or texture view) to the bind group list.
        // Returns (group_index, binding_index).
        auto addResource = [&](uint32_t stage,
                                const std::string& resourceId,
                                bool isSampler,
                                bool isStorageTex,
                                WGPUSampler samplerHandle,
                                WGPUTextureView texViewHandle) {
            BindGroupResources& bg = groups.back();
            if (bg.bindEntries.size() == maxBindingsPerBindGroup) {
                groups.push_back(BindGroupResources{});
            }
            BindGroupResources& cur = groups.back();
            const uint32_t groupIndex   = static_cast<uint32_t>(groups.size() - 1);
            const uint32_t bindingIndex = static_cast<uint32_t>(cur.bindEntries.size());

            // WGSL declaration.
            std::string resourceType;
            if (isSampler) {
                resourceType = "sampler";
            } else if (isStorageTex) {
                resourceType = "texture_storage_2d<rgba8unorm, read>";
            } else {
                resourceType = "texture_2d<f32>";
            }
            {
                std::ostringstream decl;
                decl << "    @group(" << groupIndex << ") @binding(" << bindingIndex
                     << ") var " << resourceId << ": " << resourceType << ";";
                declarationLines.push_back(decl.str());
            }

            // Layout entry.
            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding    = bindingIndex;
            layoutEntry.visibility = (stage == 0) ? WGPUShaderStage_Vertex
                                                  : WGPUShaderStage_Fragment;
            if (isSampler) {
                layoutEntry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
                layoutEntry.sampler.type = WGPUSamplerBindingType_Filtering;
            } else if (isStorageTex) {
                layoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
                layoutEntry.storageTexture.access      = WGPUStorageTextureAccess_ReadOnly;
                layoutEntry.storageTexture.format      = WGPUTextureFormat_RGBA8Unorm;
                layoutEntry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            } else {
                layoutEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
                layoutEntry.texture.sampleType    = WGPUTextureSampleType_Float;
                layoutEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
                layoutEntry.texture.multisampled  = WGPU_FALSE;
            }
            cur.layoutEntries.push_back(layoutEntry);

            // Bind entry.
            WGPUBindGroupEntry bindEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bindEntry.binding = bindingIndex;
            if (isSampler) {
                bindEntry.sampler = samplerHandle;
            } else {
                bindEntry.textureView = texViewHandle;
            }
            cur.bindEntries.push_back(bindEntry);
        };

        // Address mode cycling helper, matching upstream's getAddressMode(hash, depth).
        // kAddressModes = ['repeat', 'clamp-to-edge', 'mirror-repeat']
        // getAddressMode(hash, depth) = kAddressModes[(hash / pow(3, depth) | 0) % 3]
        auto getAddressMode = [](uint32_t hash, uint32_t depth) -> WGPUAddressMode {
            // Compute kAddressModes.length^depth = 3^depth via integer power.
            uint32_t divisor = 1;
            for (uint32_t d = 0; d < depth; ++d) divisor *= 3;
            const uint32_t idx = (hash / divisor) % 3;
            // mapping: 0->repeat, 1->clamp-to-edge, 2->mirror-repeat (same order as upstream array)
            if (idx == 0) return WGPUAddressMode_Repeat;
            if (idx == 1) return WGPUAddressMode_ClampToEdge;
            return WGPUAddressMode_MirrorRepeat;
        };

        // addTexture: create or reuse a texture for (stage, textureNum, storageTexture).
        // Returns texelValue.
        auto addTexture = [&](uint32_t stage, uint32_t textureNum, bool storageTexture) -> uint32_t {
            std::ostringstream idss;
            idss << "tex" << stage << "_" << textureNum;
            const std::string textureId = idss.str();

            // Look up existing entry.
            for (const auto& e : texIdMap) {
                if (e.id == textureId) {
                    return textureEntries[e.index].texelValue;
                }
            }

            // Create new texture.
            const uint32_t texelValue = static_cast<uint32_t>(textureEntries.size()) + 1;

            WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            texDesc.size            = WGPUExtent3D{width, 1, 1};
            texDesc.mipLevelCount   = 1;
            texDesc.sampleCount     = 1;
            texDesc.dimension       = WGPUTextureDimension_2D;
            texDesc.format          = WGPUTextureFormat_RGBA8Unorm;
            texDesc.usage           = WGPUTextureUsage_StorageBinding
                                    | WGPUTextureUsage_TextureBinding
                                    | WGPUTextureUsage_CopyDst;
            WGPUTexture tex = t.createTextureTracked(texDesc);

            // Encode rgba8unorm texture with rg16uint data where each texel is
            // [texelValue | (stage << 15), {samplerId + 1}].
            // rgba8unorm stores each channel as a byte (0..255), scaled by 1/255.
            // Two u8 channels form a u16: value = low_byte | (high_byte << 8).
            // texelValue | (stage << 15) fits in 16 bits (asserted < 0xfffe).
            //
            // pack4x8unorm layout in WGSL:
            //   bits 0..7  = r channel (rg.x low byte)
            //   bits 8..15 = g channel (rg.x high byte)
            //   bits 16..23 = b channel (rg.y low byte)
            //   bits 24..31 = g channel (rg.y high byte)
            //
            // The texture is rgba8unorm and data is written as raw bytes.
            // For texel x: r = (rg_val & 0xff), g = (rg_val >> 8),
            //              b = (samplerNum & 0xff), a = (samplerNum >> 8).
            // rg_val = texelValue | (stage << 15).
            //
            // Width * 4 bytes per texel (rgba8unorm).
            std::vector<uint8_t> data(static_cast<size_t>(width) * 4, 0);
            const uint32_t rg = texelValue | (stage << 15);
            for (uint32_t x = 0; x < width; ++x) {
                const uint32_t samplerNum = storageTexture ? 0u : (x % maxSamplersPerShaderStage) + 1u;
                const size_t offset = static_cast<size_t>(x) * 4;
                // r = rg low byte, g = rg high byte
                data[offset + 0] = static_cast<uint8_t>(rg & 0xff);
                data[offset + 1] = static_cast<uint8_t>((rg >> 8) & 0xff);
                // b = samplerNum low byte, a = samplerNum high byte
                data[offset + 2] = static_cast<uint8_t>(samplerNum & 0xff);
                data[offset + 3] = static_cast<uint8_t>((samplerNum >> 8) & 0xff);
            }

            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.offset      = 0;
            layout.bytesPerRow = width * 4;
            layout.rowsPerImage = 1;
            t.queueWriteTexture(tex, WGPUExtent3D{width, 1, 1}, layout,
                                data.data(), data.size());

            // Create a default view.
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView view = t.createViewTracked(tex, viewDesc);

            textureEntries.push_back(TextureEntry{tex, view, texelValue});
            texIdMap.push_back(TexIdMap{textureId, static_cast<uint32_t>(textureEntries.size() - 1)});

            addResource(stage, textureId, false, storageTexture, nullptr, view);
            return texelValue;
        };

        // addSampler: create or reuse a sampler.
        auto addSampler = [&](uint32_t stage, uint32_t samplerNum) -> std::string {
            std::ostringstream idss;
            idss << "smp" << stage << "_" << samplerNum;
            const std::string samplerId = idss.str();

            for (const auto& e : samplerEntries) {
                if (e.id == samplerId) {
                    return samplerId;
                }
            }

            // samplerNum within addSampler is samplerEntries.size() at call time.
            const uint32_t smpIndex = static_cast<uint32_t>(samplerEntries.size());

            const uint32_t addressHash = smpIndex >> 3;
            WGPUSamplerDescriptor sampDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            sampDesc.minFilter    = (smpIndex & 1) ? WGPUFilterMode_Linear  : WGPUFilterMode_Nearest;
            sampDesc.magFilter    = (smpIndex & 2) ? WGPUFilterMode_Linear  : WGPUFilterMode_Nearest;
            sampDesc.mipmapFilter = (smpIndex & 4) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
            sampDesc.addressModeU = getAddressMode(addressHash, 0);
            sampDesc.addressModeV = getAddressMode(addressHash, 1);
            sampDesc.addressModeW = getAddressMode(addressHash, 2);
            WGPUSampler samp = t.createSamplerTracked(sampDesc);

            samplerEntries.push_back(SamplerEntry{samplerId, samp});
            addResource(stage, samplerId, true, false, samp, nullptr);
            return samplerId;
        };

        // Build texture/sampler combos for each stage (0=vertex, 1=fragment).
        const uint32_t numStorageTexturesInStage[2] = {
            numStorageTexturesInVertexStage,
            numStorageTexturesInFragmentStage,
        };

        // Per-stage WGSL useCombos function bodies.
        std::vector<std::string> useComboFns(2);
        for (uint32_t stage = 0; stage < 2; ++stage) {
            std::ostringstream fn;
            fn << "    fn useCombos" << stage << "(id: u32) -> vec4f {\n";
            fn << "      var c: vec4f;\n";

            // Texture+sampler combos.
            for (uint32_t i = 0; i < maxTestableCombosPerStage; ++i) {
                const uint32_t texNum   = i / maxSamplersPerShaderStage;
                const uint32_t texelVal = addTexture(stage, texNum, false);
                const uint32_t smpNum   = i % maxSamplersPerShaderStage;
                const std::string smpId = addSampler(stage, smpNum);
                // Reconstruct textureId from texIdMap.
                std::ostringstream texIdss;
                texIdss << "tex" << stage << "_" << texNum;
                const std::string texId = texIdss.str();

                expected[stage].push_back({texelVal | (stage << 15), smpNum + 1});

                fn << "      c = sample(" << texId << ", " << smpId
                   << ", " << i << "u, id, c);\n";
            }

            // Storage texture combos.
            for (uint32_t i = 0; i < numStorageTexturesInStage[stage]; ++i) {
                // texNum for storage textures starts at current textureEntries.size()
                // (i.e., after all sampled textures for this stage were created).
                // But upstream uses textures.length at call time, which is the global texture array
                // length. We mirror this: textureEntries.size() at the time we call addTexture.
                const uint32_t texNum   = static_cast<uint32_t>(textureEntries.size());
                const uint32_t texelVal = addTexture(stage, texNum, true);
                std::ostringstream texIdss;
                texIdss << "tex" << stage << "_" << texNum;
                const std::string texId = texIdss.str();

                expected[stage].push_back({texelVal | (stage << 15), 0});

                fn << "      c = load(" << texId << ", "
                   << (i + maxTestableCombosPerStage) << "u, id, c);\n";
            }

            fn << "      return c;\n";
            fn << "    }\n";
            useComboFns[stage] = fn.str();
        }

        // Total columns in the render target.
        const uint32_t numAcross = maxTestableCombosPerStage
            + numStorageTexturesInVertexStage
            + numStorageTexturesInFragmentStage;

        // Build the full WGSL shader.
        std::ostringstream shaderSS;
        shaderSS << "    // maxTestableCombosPerStage: " << maxTestableCombosPerStage << "\n";
        shaderSS << "    // numStorageTexturesPerVertexStage: " << numStorageTexturesInVertexStage << "\n";
        shaderSS << "    // numStorageTexturesPerFragmentStage: " << numStorageTexturesInFragmentStage << "\n";
        shaderSS << "\n";
        shaderSS << "    fn sample(tex: texture_2d<f32>, s: sampler, validId: u32, currentId: u32, c: vec4f) -> vec4f {\n";
        shaderSS << "      let size = textureDimensions(tex, 0);\n";
        shaderSS << "      let uv = vec2f((f32(currentId % " << maxSamplersPerShaderStage << "u) + 0.5) / f32(size.x), 0.5);\n";
        shaderSS << "      let v = textureSampleLevel(tex, s, uv, 0);\n";
        shaderSS << "      return select(c, v, currentId == validId);\n";
        shaderSS << "    }\n";
        shaderSS << "\n";
        shaderSS << "    fn load(tex: texture_storage_2d<rgba8unorm, read>, validId: u32, currentId: u32, c: vec4f) -> vec4f {\n";
        shaderSS << "      let size = textureDimensions(tex);\n";
        shaderSS << "      let uv = vec2u(currentId % size.x, 0u);\n";
        shaderSS << "      let v = textureLoad(tex, uv);\n";
        shaderSS << "      return select(c, v, currentId == validId);\n";
        shaderSS << "    }\n";
        shaderSS << "\n";
        for (const auto& fn : useComboFns) {
            shaderSS << fn << "\n";
        }
        for (const auto& decl : declarationLines) {
            shaderSS << decl << "\n";
        }
        shaderSS << "\n";
        shaderSS << "    struct VOut {\n";
        shaderSS << "      @builtin(position) pos: vec4f,\n";
        shaderSS << "      @location(0) value: vec4f,\n";
        shaderSS << "    };\n";
        shaderSS << "\n";
        shaderSS << "    @vertex fn vs(@builtin(instance_index) iNdx: u32) -> VOut {\n";
        shaderSS << "      return VOut(\n";
        shaderSS << "        vec4f(0, 0, 0, 1),\n";
        shaderSS << "        useCombos0(iNdx),\n";
        shaderSS << "      );\n";
        shaderSS << "    }\n";
        shaderSS << "\n";
        shaderSS << "    @fragment fn fs(vin: VOut) -> @location(0) vec4u {\n";
        shaderSS << "      let ndx = u32(vin.pos.x);\n";
        shaderSS << "      let f = select(vin.value, useCombos1(ndx), vin.pos.y > 1.0);\n";
        shaderSS << "\n";
        shaderSS << "      // We're putting two u16 values in the source data but as rgba8unorm.\n";
        shaderSS << "      // Convert them back to u32 then split them back into two u16s\n";
        shaderSS << "      let bytes = pack4x8unorm(f);\n";
        shaderSS << "      return vec4u(bytes & 0xffff, bytes >> 16, 0, 0);\n";
        shaderSS << "    }\n";

        const std::string shaderCode = shaderSS.str();

        // Create shader module.
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(shaderCode);

        // Create per-group bind group layouts.
        // Store layout and bind-group vectors for use in pipeline creation and rendering.
        // The BGL entries must outlive createBindGroupLayoutTracked calls.
        std::vector<WGPUBindGroupLayout> bindGroupLayouts;
        bindGroupLayouts.reserve(groups.size());
        for (const auto& bg : groups) {
            WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bglDesc.entryCount = static_cast<size_t>(bg.layoutEntries.size());
            bglDesc.entries    = bg.layoutEntries.data();
            bindGroupLayouts.push_back(t.createBindGroupLayoutTracked(bglDesc));
        }

        // Create pipeline layout.
        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = bindGroupLayouts.size();
        plDesc.bindGroupLayouts     = bindGroupLayouts.data();
        WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

        // Create render pipeline.
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RG16Uint;

        WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
        fragmentState.module      = shaderModule;
        fragmentState.entryPoint  = stringView("fs");
        fragmentState.targetCount = 1;
        fragmentState.targets     = &colorTarget;

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout                   = pipelineLayout;
        pipeDesc.vertex.module            = shaderModule;
        pipeDesc.vertex.entryPoint        = stringView("vs");
        pipeDesc.primitive.topology       = WGPUPrimitiveTopology_PointList;
        pipeDesc.multisample.count        = 1;
        pipeDesc.fragment                 = &fragmentState;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

        // Create bind groups (one per @group slot), using explicit layouts.
        // We use the explicit layouts we already created (not getBindGroupLayout getter)
        // so no manual release is needed.
        std::vector<WGPUBindGroup> bindGroups;
        bindGroups.reserve(groups.size());
        for (size_t i = 0; i < groups.size(); ++i) {
            const BindGroupResources& bg = groups[i];
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout     = bindGroupLayouts[i];
            bgDesc.entryCount = static_cast<size_t>(bg.bindEntries.size());
            bgDesc.entries    = bg.bindEntries.data();
            bindGroups.push_back(t.createBindGroupTracked(bgDesc));
        }

        // Create render target texture: rg16uint, [numAcross, 2].
        WGPUTextureDescriptor rtDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        rtDesc.size           = WGPUExtent3D{numAcross, 2, 1};
        rtDesc.mipLevelCount  = 1;
        rtDesc.sampleCount    = 1;
        rtDesc.dimension      = WGPUTextureDimension_2D;
        rtDesc.format         = WGPUTextureFormat_RG16Uint;
        rtDesc.usage          = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture renderTarget = t.createTextureTracked(rtDesc);

        WGPUTextureViewDescriptor rtViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView renderTargetView = t.createViewTracked(renderTarget, rtViewDesc);

        // Render pass.
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view      = renderTargetView;
        colorAttachment.loadOp    = WGPULoadOp_Clear;
        colorAttachment.storeOp   = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        for (size_t i = 0; i < bindGroups.size(); ++i) {
            wgpuRenderPassEncoderSetBindGroup(
                pass, static_cast<uint32_t>(i), bindGroups[i], 0, nullptr);
        }

        // For each row (y=0 vertex stage, y=1 fragment stage) and each column x,
        // draw 1 point with instance_index = x using a 1x1 viewport at (x, y).
        for (uint32_t y = 0; y < 2; ++y) {
            for (uint32_t x = 0; x < numAcross; ++x) {
                wgpuRenderPassEncoderSetViewport(
                    pass,
                    static_cast<float>(x), static_cast<float>(y),
                    1.0f, 1.0f,
                    0.0f, 1.0f);
                // draw(vertexCount=1, instanceCount=1, firstVertex=0, firstInstance=x)
                wgpuRenderPassEncoderDraw(pass, 1, 1, 0, x);
            }
        }
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer cb = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cb);

        // Build expected data as a flat array of uint16_t in rg16uint layout.
        // Layout: numAcross pixels * 2 rows * 2 uint16_t per pixel (rg channels).
        // Byte stride per row = numAcross * 4 (2 bytes r + 2 bytes g).
        // Row 0 (y=0): vertex stage expected; Row 1 (y=1): fragment stage expected.
        const uint32_t rg16BytesPerPixel = 4; // 2 channels * 2 bytes each
        const uint64_t rg16BytesPerRow   = static_cast<uint64_t>(numAcross) * rg16BytesPerPixel;
        // Align to 256 for copy.
        const uint32_t alignedBytesPerRow = static_cast<uint32_t>(
            alignTo(rg16BytesPerRow, kBytesPerRowAlignment));
        const uint64_t bufferSize = static_cast<uint64_t>(alignedBytesPerRow) * 2;
        const uint64_t alignedBufSize = alignTo(bufferSize, kBufferCopyAlignment);

        // Readback buffer (zero-filled). Needs CopyDst (written by CopyTextureToBuffer)
        // and CopySrc (expectGPUBufferValuesPassCheck copies it to an internal MapRead
        // staging buffer via CopyBufferToBuffer).
        WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.size  = alignedBufSize;
        readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readbackBuf = t.createBufferTracked(readbackDesc);

        // Copy render target to buffer.
        {
            WGPUCommandEncoder copyEnc = t.createCommandEncoderTracked();
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture  = renderTarget;
            src.mipLevel = 0;
            src.aspect   = WGPUTextureAspect_All;

            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer              = readbackBuf;
            dst.layout.offset       = 0;
            dst.layout.bytesPerRow  = alignedBytesPerRow;
            dst.layout.rowsPerImage = 2;

            WGPUExtent3D copySize{numAcross, 2, 1};
            wgpuCommandEncoderCopyTextureToBuffer(copyEnc, &src, &dst, &copySize);

            WGPUCommandBuffer copyCb = t.finishTracked(copyEnc);
            wgpuQueueSubmit(t.queue(), 1, &copyCb);
        }

        // Build expected buffer (rg16uint raw bytes matching the GPU layout).
        // expected[stage] is ordered: combos 0..maxTestableCombosPerStage-1, then storage textures.
        std::vector<uint8_t> expectedData(static_cast<size_t>(alignedBytesPerRow) * 2, 0);
        for (uint32_t stage = 0; stage < 2; ++stage) {
            const auto& stageExp = expected[stage];
            for (size_t i = 0; i < stageExp.size() && i < numAcross; ++i) {
                const uint32_t tid = stageExp[i][0];
                const uint32_t sid = stageExp[i][1];
                // Pixel at (x=i, y=stage) in rg16uint.
                // r channel = tid (u16 LE), g channel = sid (u16 LE).
                const size_t pixelOffset =
                    static_cast<size_t>(stage) * alignedBytesPerRow
                    + i * rg16BytesPerPixel;
                expectedData[pixelOffset + 0] = static_cast<uint8_t>(tid & 0xff);
                expectedData[pixelOffset + 1] = static_cast<uint8_t>((tid >> 8) & 0xff);
                expectedData[pixelOffset + 2] = static_cast<uint8_t>(sid & 0xff);
                expectedData[pixelOffset + 3] = static_cast<uint8_t>((sid >> 8) & 0xff);
            }
        }

        // Compare.
        t.expectGPUBufferValuesPassCheck(
            readbackBuf,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                for (uint32_t stage = 0; stage < 2; ++stage) {
                    const auto& stageExp = expected[stage];
                    for (size_t i = 0; i < stageExp.size() && i < numAcross; ++i) {
                        const size_t pixelOffset =
                            static_cast<size_t>(stage) * alignedBytesPerRow
                            + i * rg16BytesPerPixel;
                        if (pixelOffset + rg16BytesPerPixel > len) {
                            std::ostringstream msg;
                            msg << "readback buffer too small at pixel (" << i << ", " << stage << ")";
                            return msg.str();
                        }
                        // Read actual rg16uint pixel.
                        const uint32_t actualTid =
                            static_cast<uint32_t>(actual[pixelOffset + 0])
                            | (static_cast<uint32_t>(actual[pixelOffset + 1]) << 8);
                        const uint32_t actualSid =
                            static_cast<uint32_t>(actual[pixelOffset + 2])
                            | (static_cast<uint32_t>(actual[pixelOffset + 3]) << 8);
                        const uint32_t expTid = stageExp[i][0];
                        const uint32_t expSid = stageExp[i][1];
                        if (actualTid != expTid || actualSid != expSid) {
                            std::ostringstream msg;
                            msg << "rg16uint mismatch at pixel (" << i << ", stage=" << stage << "): "
                                << "expected [" << expTid << ", " << expSid << "] "
                                << "got [" << actualTid << ", " << actualSid << "]";
                            return msg.str();
                        }
                    }
                }
                return std::nullopt;
            },
            /*srcByteOffset=*/0,
            /*byteLength=*/static_cast<size_t>(alignedBufSize));
    });

} // namespace
