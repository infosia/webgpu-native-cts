// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/shader_builtin_utils.ts
// (and the format_info.ts subset the texture builtin validation specs depend on:
// kPossibleStorageTextureFormats, getTextureFormatColorType, and the
// gpu_test.ts skip helpers skipIfTextureLoadNotSupportedForTextureType /
// skipIfTextureFormatNotUsableWithStorageAccessMode) @
// b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Shared helpers for the texture* builtin validation specs (textureDimensions,
// textureLoad, textureSample*, textureGather*, textureStore, textureNumLevels/
// Layers/Samples). Faithful port of:
//   - kEntryPointsToValidateFragmentOnlyBuiltins
//   - kNonStorageTextureTypeInfo (+ getNonStorageTextureTypeWGSL)
//   - kTestTextureTypes (+ getSampleAndBaseTextureTypeForTextureType)
//   - isUnsignedType / stringToTexelType (conversion.ts subset)
//   - kPossibleStorageTextureFormats (WGSL format name + color type) and the
//     name->WGPUTextureFormat map, plus the skip helpers above implemented on
//     the C++ GpuTest API (skipIfTextureFormatNotSupported /
//     isTextureFormatUsableWithStorageAccessMode). On our all-features non-compat
//     device skipIfTextureLoadNotSupportedForTextureType is a no-op (matches
//     upstream: it only skips in compatibility mode).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

namespace cts::shader_validation::builtin {

namespace bt = cts::shader_validation::binary;
using bt::ScalarKind;
using bt::Type;

// ---------------------------------------------------------------------------
// kEntryPointsToValidateFragmentOnlyBuiltins
// ---------------------------------------------------------------------------
struct EntryPointConfig {
    bool expectSuccess;
    std::string code;
};
inline const EntryPointConfig& entryPointConfig(const std::string& name) {
    static const EntryPointConfig none{true, ""};
    static const EntryPointConfig fragment{true,
                                           "\n      @fragment\n      fn main() {\n        foo();\n "
                                           "     }\n    "};
    static const EntryPointConfig vertex{
        false,
        "\n      @vertex\n      fn main() -> @builtin(position) vec4f {\n        foo();\n        "
        "return vec4f();\n      }\n    "};
    static const EntryPointConfig compute{
        false,
        "\n      @compute @workgroup_size(1)\n      fn main() {\n        foo();\n      }\n    "};
    static const EntryPointConfig fragment_and_compute{
        false,
        "\n      @fragment\n      fn main1() {\n        foo();\n      }\n\n      @compute "
        "@workgroup_size(1)\n      fn main2() {\n        foo();\n      }\n    "};
    static const EntryPointConfig compute_without_call{
        true, "\n      @compute @workgroup_size(1)\n      fn main() {\n      }\n    "};
    if (name == "none") return none;
    if (name == "fragment") return fragment;
    if (name == "vertex") return vertex;
    if (name == "compute") return compute;
    if (name == "fragment_and_compute") return fragment_and_compute;
    return compute_without_call;
}
// Order preserved from the upstream object literal.
inline std::vector<cts::Value> kEntryPointsToValidateFragmentOnlyBuiltins() {
    return {cts::Value(std::string("none")),    cts::Value(std::string("fragment")),
            cts::Value(std::string("vertex")),  cts::Value(std::string("compute")),
            cts::Value(std::string("fragment_and_compute")),
            cts::Value(std::string("compute_without_call"))};
}

// ---------------------------------------------------------------------------
// kNonStorageTextureTypeInfo (+ getNonStorageTextureTypeWGSL)
// ---------------------------------------------------------------------------
struct TextureTypeInfo {
    std::string name;
    std::vector<Type> texelTypes;
    bool noSuffix;
};
// kCommonTexelTypes = [vec4f, vec4i, vec4u]; kDepthTexelTypes = [f32];
// kExternalTexelTypes = [vec4f]. Order: common textures, depth textures, external.
inline const std::vector<TextureTypeInfo>& kNonStorageTextureTypeInfo() {
    static const std::vector<Type> common = {bt::vec(4, ScalarKind::F32), bt::vec(4, ScalarKind::I32),
                                             bt::vec(4, ScalarKind::U32)};
    static const std::vector<Type> depth = {bt::scalar(ScalarKind::F32)};
    static const std::vector<Type> external = {bt::vec(4, ScalarKind::F32)};
    static const std::vector<TextureTypeInfo> v = {
        {"texture_1d", common, false},
        {"texture_2d", common, false},
        {"texture_2d_array", common, false},
        {"texture_3d", common, false},
        {"texture_cube", common, false},
        {"texture_cube_array", common, false},
        {"texture_multisampled_2d", common, false},
        {"texture_depth_2d", depth, true},
        {"texture_depth_2d_array", depth, true},
        {"texture_depth_cube", depth, true},
        {"texture_depth_cube_array", depth, true},
        {"texture_depth_multisampled_2d", depth, true},
        {"texture_external", external, true},
    };
    return v;
}
inline const TextureTypeInfo* nonStorageInfo(const std::string& name) {
    for (const TextureTypeInfo& i : kNonStorageTextureTypeInfo()) {
        if (i.name == name) {
            return &i;
        }
    }
    return nullptr;
}
// getNonStorageTextureTypeWGSL(textureType, texelType): noSuffix => textureType,
// else `${textureType}<${scalarTypeOf(texelType)}>`.
inline std::string getNonStorageTextureTypeWGSL(const std::string& textureType,
                                                const Type& texelType) {
    const TextureTypeInfo* info = nonStorageInfo(textureType);
    if (info != nullptr && info->noSuffix) {
        return textureType;
    }
    return textureType + "<" + bt::scalarKindString(bt::scalarTypeOf(texelType).kind) + ">";
}

// ---------------------------------------------------------------------------
// kTestTextureTypes (+ getSampleAndBaseTextureTypeForTextureType)
// ---------------------------------------------------------------------------
inline std::vector<cts::Value> kTestTextureTypes() {
    return {
        cts::Value(std::string("texture_1d<f32>")),
        cts::Value(std::string("texture_1d<u32>")),
        cts::Value(std::string("texture_2d<f32>")),
        cts::Value(std::string("texture_2d<u32>")),
        cts::Value(std::string("texture_2d_array<f32>")),
        cts::Value(std::string("texture_2d_array<u32>")),
        cts::Value(std::string("texture_3d<f32>")),
        cts::Value(std::string("texture_3d<u32>")),
        cts::Value(std::string("texture_cube<f32>")),
        cts::Value(std::string("texture_cube<u32>")),
        cts::Value(std::string("texture_cube_array<f32>")),
        cts::Value(std::string("texture_cube_array<u32>")),
        cts::Value(std::string("texture_multisampled_2d<f32>")),
        cts::Value(std::string("texture_multisampled_2d<u32>")),
        cts::Value(std::string("texture_depth_multisampled_2d")),
        cts::Value(std::string("texture_external")),
        cts::Value(std::string("texture_storage_1d<rgba8unorm, read>")),
        cts::Value(std::string("texture_storage_1d<r32uint, read>")),
        cts::Value(std::string("texture_storage_2d<rgba8unorm, read>")),
        cts::Value(std::string("texture_storage_2d<r32uint, read>")),
        cts::Value(std::string("texture_storage_2d_array<rgba8unorm, read>")),
        cts::Value(std::string("texture_storage_2d_array<r32uint, read>")),
        cts::Value(std::string("texture_storage_3d<rgba8unorm, read>")),
        cts::Value(std::string("texture_storage_3d<r32uint, read>")),
        cts::Value(std::string("texture_depth_2d")),
        cts::Value(std::string("texture_depth_2d_array")),
        cts::Value(std::string("texture_depth_cube")),
        cts::Value(std::string("texture_depth_cube_array")),
    };
}
// kTextureTypeSuffixToType: f32 -> vec4f, u32 -> vec4i (sic, matches upstream),
// 'rgba8unorm, read' -> vec4f, 'r32uint, read' -> vec4u.
inline Type textureTypeSuffixToType(const std::string& suffix) {
    if (suffix == "f32") return bt::vec(4, ScalarKind::F32);
    if (suffix == "u32") return bt::vec(4, ScalarKind::I32);
    if (suffix == "rgba8unorm, read") return bt::vec(4, ScalarKind::F32);
    if (suffix == "r32uint, read") return bt::vec(4, ScalarKind::U32);
    return bt::vec(4, ScalarKind::F32);
}
// getSampleAndBaseTextureTypeForTextureType: split on `^(.*?)<(.*?)>`; returns the
// base texture type and the sample Type (defaults to vec4f when no `<...>`).
struct BaseAndSample {
    std::string base;
    Type sample;
};
inline BaseAndSample getSampleAndBaseTextureTypeForTextureType(const std::string& textureType) {
    const std::string::size_type lt = textureType.find('<');
    if (lt == std::string::npos) {
        return BaseAndSample{textureType, bt::vec(4, ScalarKind::F32)};
    }
    const std::string::size_type gt = textureType.find('>', lt);
    const std::string base = textureType.substr(0, lt);
    const std::string suffix = textureType.substr(lt + 1, gt - lt - 1);
    return BaseAndSample{base, textureTypeSuffixToType(suffix)};
}

// ---------------------------------------------------------------------------
// conversion.ts subset
// ---------------------------------------------------------------------------
// isUnsignedType(ty): element kind is u32 (scalar or vector).
inline bool isUnsignedType(const Type& ty) { return ty.kind == ScalarKind::U32; }
// stringToType for the texel-type strings emitted by the specs (e.g. "vec4<f32>",
// "f32"). Reuses typeByName (keys == Type.toString()).
inline Type stringToTexelType(const std::string& name) { return bt::typeByName(name); }

// ---------------------------------------------------------------------------
// kPossibleStorageTextureFormats (format_info.ts): WGSL name + color type +
// WGPUTextureFormat enum. = regular formats with color.storage (22) ++
// 'bgra8unorm' ++ kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly (17).
// ---------------------------------------------------------------------------
struct StorageFormatInfo {
    std::string name;            // WGSL format name
    std::string colorType;       // "float"|"sint"|"uint"|"unfilterable-float"
    WGPUTextureFormat format;    // C enum
};
inline const std::vector<StorageFormatInfo>& kPossibleStorageTextureFormatsInfo() {
    static const std::vector<StorageFormatInfo> v = {
        // regular formats with color.storage === true (22)
        {"rgba8unorm", "float", WGPUTextureFormat_RGBA8Unorm},
        {"rgba8snorm", "float", WGPUTextureFormat_RGBA8Snorm},
        {"rgba8uint", "uint", WGPUTextureFormat_RGBA8Uint},
        {"rgba8sint", "sint", WGPUTextureFormat_RGBA8Sint},
        {"r16unorm", "unfilterable-float", WGPUTextureFormat_R16Unorm},
        {"r16snorm", "unfilterable-float", WGPUTextureFormat_R16Snorm},
        {"rg16unorm", "unfilterable-float", WGPUTextureFormat_RG16Unorm},
        {"rg16snorm", "unfilterable-float", WGPUTextureFormat_RG16Snorm},
        {"rgba16unorm", "unfilterable-float", WGPUTextureFormat_RGBA16Unorm},
        {"rgba16snorm", "unfilterable-float", WGPUTextureFormat_RGBA16Snorm},
        {"rgba16uint", "uint", WGPUTextureFormat_RGBA16Uint},
        {"rgba16sint", "sint", WGPUTextureFormat_RGBA16Sint},
        {"rgba16float", "float", WGPUTextureFormat_RGBA16Float},
        {"r32uint", "uint", WGPUTextureFormat_R32Uint},
        {"r32sint", "sint", WGPUTextureFormat_R32Sint},
        {"r32float", "unfilterable-float", WGPUTextureFormat_R32Float},
        {"rg32uint", "uint", WGPUTextureFormat_RG32Uint},
        {"rg32sint", "sint", WGPUTextureFormat_RG32Sint},
        {"rg32float", "unfilterable-float", WGPUTextureFormat_RG32Float},
        {"rgba32uint", "uint", WGPUTextureFormat_RGBA32Uint},
        {"rgba32sint", "sint", WGPUTextureFormat_RGBA32Sint},
        {"rgba32float", "unfilterable-float", WGPUTextureFormat_RGBA32Float},
        // 'bgra8unorm'
        {"bgra8unorm", "float", WGPUTextureFormat_BGRA8Unorm},
        // kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly (17)
        {"r8unorm", "float", WGPUTextureFormat_R8Unorm},
        {"r8snorm", "float", WGPUTextureFormat_R8Snorm},
        {"r8uint", "uint", WGPUTextureFormat_R8Uint},
        {"r8sint", "sint", WGPUTextureFormat_R8Sint},
        {"rg8unorm", "float", WGPUTextureFormat_RG8Unorm},
        {"rg8snorm", "float", WGPUTextureFormat_RG8Snorm},
        {"rg8uint", "uint", WGPUTextureFormat_RG8Uint},
        {"rg8sint", "sint", WGPUTextureFormat_RG8Sint},
        {"r16uint", "uint", WGPUTextureFormat_R16Uint},
        {"r16sint", "sint", WGPUTextureFormat_R16Sint},
        {"r16float", "float", WGPUTextureFormat_R16Float},
        {"rg16uint", "uint", WGPUTextureFormat_RG16Uint},
        {"rg16sint", "sint", WGPUTextureFormat_RG16Sint},
        {"rg16float", "float", WGPUTextureFormat_RG16Float},
        {"rgb10a2uint", "uint", WGPUTextureFormat_RGB10A2Uint},
        {"rgb10a2unorm", "float", WGPUTextureFormat_RGB10A2Unorm},
        {"rg11b10ufloat", "float", WGPUTextureFormat_RG11B10Ufloat},
    };
    return v;
}
inline std::vector<cts::Value> kPossibleStorageTextureFormats() {
    std::vector<cts::Value> out;
    for (const StorageFormatInfo& f : kPossibleStorageTextureFormatsInfo()) {
        out.emplace_back(f.name);
    }
    return out;
}
inline const StorageFormatInfo* storageFormatByName(const std::string& name) {
    for (const StorageFormatInfo& f : kPossibleStorageTextureFormatsInfo()) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}
// getTextureFormatColorType: 'float' | 'unfilterable-float' -> vec4f, 'uint' ->
// vec4u, 'sint' -> vec4i. (kTextureColorTypeToType in textureStore.)
inline Type textureColorTypeToType(const std::string& colorType) {
    if (colorType == "sint") return bt::vec(4, ScalarKind::I32);
    if (colorType == "uint") return bt::vec(4, ScalarKind::U32);
    return bt::vec(4, ScalarKind::F32);  // float / unfilterable-float
}

// ---------------------------------------------------------------------------
// gpu_test.ts skip helpers (texture builtin specs)
// ---------------------------------------------------------------------------
// Upstream skipIfTextureLoadNotSupportedForTextureType only skips for depth
// textures in compatibility mode. Our private device is non-compat, so this is a
// no-op (kept for parity / so the call sites read identically to upstream).
inline void skipIfTextureLoadNotSupportedForTextureType(ShaderValidationTest&, const std::string&) {}

// skipIfTextureFormatNotSupported(name) — resolve WGSL format name to the C enum
// and defer to the base GpuTest helper.
inline void skipIfTextureFormatNotSupported(ShaderValidationTest& t, const std::string& name) {
    const StorageFormatInfo* f = storageFormatByName(name);
    if (f != nullptr) {
        t.skipIfTextureFormatNotSupported(f->format);
    }
}
// skipIfTextureFormatNotUsableWithStorageAccessMode(access, name) where access is
// "read-only"/"write-only"/"read-write".
inline void skipIfTextureFormatNotUsableWithStorageAccessMode(ShaderValidationTest& t,
                                                              const std::string& access,
                                                              const std::string& name) {
    const StorageFormatInfo* f = storageFormatByName(name);
    if (f == nullptr) {
        return;
    }
    WGPUStorageTextureAccess mode = WGPUStorageTextureAccess_WriteOnly;
    if (access == "read-only" || access == "read") {
        mode = WGPUStorageTextureAccess_ReadOnly;
    } else if (access == "read-write" || access == "read_write") {
        mode = WGPUStorageTextureAccess_ReadWrite;
    } else {
        mode = WGPUStorageTextureAccess_WriteOnly;
    }
    if (!t.isTextureFormatUsableWithStorageAccessMode(f->format, mode)) {
        t.skip("Texture with " + name + " is not usable as a storage texture with access " + access);
    }
}

}  // namespace cts::shader_validation::builtin
