// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/textures.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for texture types in shaders. All tests are compile-only
// (expectCompileResult).
//
// Porting notes:
//   - kPossibleStorageTextureFormats is reconstructed (kStorageTextureFormats +
//     bgra8unorm + kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly), matching
//     upstream format_info.ts and the readonly_and_readwrite_storage_textures port.
//   - getPlainTypeInfo(getTextureFormatType(format)) maps a storage format to its
//     scalar shader type. None of kPossibleStorageTextureFormats are depth, so the
//     scalar type is derived directly from the format-identifier suffix:
//     `*uint` -> u32, `*sint` -> i32, otherwise (unorm/snorm/float) -> f32.
//   - skipIfTextureFormatNotSupported / the read-only storage-access usability skip
//     use the inherited AllFeaturesMaxLimitsGpuTest helpers (all-features device).
//   - isTextureFormatUsableAsStorageFormatInCreateShaderModule(features, format) is
//     feature-independent membership in kPossibleStorageTextureFormats (upstream).

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"
#include "webgpu/texture_format.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,textures",
    "Validation tests for various texture types in shaders.");

// kPossibleStorageTextureFormats (upstream format_info.ts):
//   [...kRegularTextureFormats.filter(color?.storage), 'bgra8unorm',
//    ...kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly]
static std::vector<WGPUTextureFormat> possibleStorageTextureFormats() {
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

static std::vector<Value> possibleStorageTextureFormatValues() {
    const std::vector<WGPUTextureFormat> formats = possibleStorageTextureFormats();
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat f : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(f)));
    }
    return values;
}

static std::vector<Value> allTextureFormatValues() {
    std::vector<Value> values;
    values.reserve(kAllTextureFormats.size());
    for (WGPUTextureFormat f : kAllTextureFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(f)));
    }
    return values;
}

static bool isPossibleStorageTextureFormat(WGPUTextureFormat format) {
    for (WGPUTextureFormat f : possibleStorageTextureFormats()) {
        if (f == format) {
            return true;
        }
    }
    return false;
}

static bool stringEndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// getPlainTypeInfo(getTextureFormatType(format)) for storage formats: depth never
// applies here, so derive from the identifier suffix.
static std::string validShaderScalarTypeForStorageFormat(const std::string& formatStr) {
    if (stringEndsWith(formatStr, "uint")) {
        return "u32";
    }
    if (stringEndsWith(formatStr, "sint")) {
        return "i32";
    }
    return "f32";
}

CTS_TEST(g, "texel_formats")
    .desc("Test channels and channel format of various texel formats when using as the storage "
          "texture format")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormatValues())
            .beginSubcases()
            .combine("shaderScalarType", {"f32", "u32", "i32", "bool", "f16"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string formatStr = t.param<std::string>("format");
        const std::string shaderScalarType = t.param<std::string>("shaderScalarType");
        const WGPUTextureFormat format = parseTextureFormat(formatStr);
        t.skipIfTextureFormatNotSupported(format);
        if (!t.isTextureFormatUsableWithStorageAccessMode(format,
                                                          WGPUStorageTextureAccess_ReadOnly)) {
            t.skip("texture format is not usable as read-only storage texture");
        }
        const std::string validShaderScalarType = validShaderScalarTypeForStorageFormat(formatStr);
        const std::string shaderValueType = "vec4<" + shaderScalarType + ">";
        const std::string wgsl =
            "\n    @group(0) @binding(0) var tex: texture_storage_2d<" + formatStr +
            ", read>;\n    @compute @workgroup_size(1) fn main() {\n        let v : " +
            shaderValueType + " = textureLoad(tex, vec2u(0));\n        _ = v;\n    }\n";
        t.expectCompileResult(validShaderScalarType == shaderScalarType, wgsl);
    });

CTS_TEST(g, "texel_formats,as_value")
    .desc("Test that texel format cannot be used as value")
    .fn([](ShaderValidationTest& t) {
        const std::string wgsl =
            "\n    @compute @workgroup_size(1) fn main() {\n        var i = rgba8unorm;\n    }\n";
        t.expectCompileResult(false, wgsl);
    });

CTS_TEST(g, "sampled_texture_types")
    .desc("Test that for texture_xx<T>\n- The sampled type T must be f32, i32, or u32\n")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", {"texture_2d", "texture_multisampled_2d"})
            .beginSubcases()
            .combine("sampledType",
                     {"f32", "i32", "u32", "bool", "vec2", "mat2x2", "1.0", "1", "1u"})
            .combine("comma", {"", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampledType = t.param<std::string>("sampledType");
        const std::string comma = t.param<std::string>("comma");
        const bool valid =
            sampledType == "f32" || sampledType == "i32" || sampledType == "u32";
        const std::string wgsl = "@group(0) @binding(0) var tex: " + textureType + "<" +
                                 sampledType + comma + ">;";
        t.expectCompileResult(valid, wgsl);
    });

CTS_TEST(g, "external_sampled_texture_types")
    .desc("Test that texture_external compiles and cannot specify address space\n")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "@group(0) @binding(0) var tex: texture_external;");
        t.expectCompileResult(false,
                              "@group(0) @binding(0) var<private> tex: texture_external;");
    });

CTS_TEST(g, "storage_texture_types")
    .desc("Test that for texture_storage_xx<format, access>\n- format must be an enumerant for one "
          "of the texel formats for storage textures\n- access must be an enumerant for one of the "
          "access modes\n\nBesides, the shader compilation should always pass regardless of whether "
          "the format supports the usage indicated by the access or not.\n")
    .params([](ParamsBuilder u) {
        return u.combine("access", {"read", "write", "read_write", "storage"})
            .combine("format", allTextureFormatValues())
            .combine("comma", {"", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string access = t.param<std::string>("access");
        const std::string formatStr = t.param<std::string>("format");
        const std::string comma = t.param<std::string>("comma");
        const WGPUTextureFormat format = parseTextureFormat(formatStr);
        const bool isFormatValid = isPossibleStorageTextureFormat(format);
        const bool isAccessValid =
            access == "read" || access == "write" || access == "read_write";
        const std::string wgsl = "@group(0) @binding(0) var tex: texture_storage_2d<" + formatStr +
                                 ", " + access + comma + ">;";
        t.expectCompileResult(isFormatValid && isAccessValid, wgsl);
    });

CTS_TEST(g, "depth_texture_types")
    .desc("Test that for texture_depth_xx\n- must not specify an address space\n")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", {"texture_depth_2d", "texture_depth_2d_array",
                                         "texture_depth_cube", "texture_depth_cube_array"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        t.expectCompileResult(true, "@group(0) @binding(0) var t: " + textureType + ";");
        t.expectCompileResult(false,
                              "@group(0) @binding(0) var<private> t: " + textureType + ";");
        t.expectCompileResult(
            false, "@group(0) @binding(0) var<storage, read> t: " + textureType + ";");
    });

CTS_TEST(g, "sampler_types")
    .desc("Test that for sampler and sampler_comparison\n- cannot specify address space\n- cannot "
          "be declared in WGSL function scope\n")
    .params([](ParamsBuilder u) {
        return u.combine("samplerType", {"sampler", "sampler_comparison"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string samplerType = t.param<std::string>("samplerType");
        t.expectCompileResult(true, "@group(0) @binding(0) var s: " + samplerType + ";");
        t.expectCompileResult(false,
                              "@group(0) @binding(0) var<private> s: " + samplerType + ";");
        t.expectCompileResult(
            false, "\n      @compute @workgroup_size(1) fn main() {\n        var s: " + samplerType +
                       ";\n      }\n    ");
    });

} // namespace
