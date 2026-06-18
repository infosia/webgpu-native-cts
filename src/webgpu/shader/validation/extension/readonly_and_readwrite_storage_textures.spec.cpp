// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/extension/readonly_and_readwrite_storage_textures.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - kPossibleStorageTextureFormats is reconstructed (kStorageTextureFormats +
//     bgra8unorm + kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly), matching
//     upstream format_info.ts and render_pipeline/misc.spec.cpp.
//   - hasLanguageFeature('readonly_and_readwrite_storage_textures') and
//     hasLanguageFeature('texture_formats_tier1') return the TRUE per-backend
//     answer: Dawn via wgpuInstanceHasWGSLLanguageFeature; non-Dawn via a canonical
//     trial-compile probe (tier1 stays conservatively false on non-Dawn). Used
//     (per upstream) to set the expected compile result.
//   - hasFeature(device.features, 'texture-formats-tier1') uses the private device.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"
#include "webgpu/texture_format.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,extension,readonly_and_readwrite_storage_textures",
    "Validation tests for the readonly_and_readwrite_storage_textures language feature");

// kPossibleStorageTextureFormats (upstream format_info.ts):
//   [...kRegularTextureFormats.filter(color?.storage), 'bgra8unorm',
//    ...kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly]
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

bool formatInTier1List(WGPUTextureFormat format) {
    for (WGPUTextureFormat f : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        if (f == format) {
            return true;
        }
    }
    return false;
}

CTS_TEST(g, "var_decl")
    .desc(R"(Checks that the read and read_write access modes are only allowed with the language feature present

    TODO(https://github.com/gpuweb/cts/issues/4612): Stop checking the device feature
    )")
    .params([](ParamsBuilder u) {
        // Upstream uses paramsSubcasesOnly -> all params are subcases under one case.
        return u
            .beginSubcases()
            .combine("type", {Value(std::string("texture_storage_1d")),
                              Value(std::string("texture_storage_2d")),
                              Value(std::string("texture_storage_2d_array")),
                              Value(std::string("texture_storage_3d"))})
            .combine("format", possibleStorageTextureFormatValues())
            .combine("access", {Value(std::string("read")),
                                Value(std::string("write")),
                                Value(std::string("read_write"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string formatStr = t.param<std::string>("format");
        const std::string access = t.param<std::string>("access");
        const WGPUTextureFormat format = parseTextureFormat(formatStr);

        // hasLanguageFeature returns the TRUE per-backend answer (Dawn via the
        // instance query; non-Dawn via a canonical trial-compile probe / conservative
        // false for tier1), so the expected result tracks actual feature support.
        // Mirrors upstream `valid &&= t.hasLanguageFeature(...)`.
        bool valid = true;
        if (access != "write") {
            valid = valid &&
                t.hasLanguageFeature("readonly_and_readwrite_storage_textures");
        }

        if (formatInTier1List(format)) {
            // Their WGSL validity should depend only on the tier1 language feature.
            // However, because the language feature is new, upstream also checks the
            // device feature (MAINTENANCE_TODO gpuweb/cts#4612: stop doing this).
            if (!t.deviceHasFeature(WGPUFeatureName_TextureFormatsTier1)) {
                valid = valid &&
                    t.hasLanguageFeature("texture_formats_tier1");
            }
        }

        const std::string source =
            "@group(0) @binding(0) var t : " + type + "<" + formatStr + ", " + access + ">;";
        t.expectCompileResult(valid, source);
    });

CTS_TEST(g, "textureBarrier")
    .desc("Checks that the textureBarrier() builtin is only allowed with the language feature present")
    .fn([](ShaderValidationTest& t) {
        // hasLanguageFeature returns the TRUE per-backend answer (see var_decl above).
        const bool valid = t.hasLanguageFeature(
            "readonly_and_readwrite_storage_textures");
        t.expectCompileResult(
            valid,
            "\n        @workgroup_size(1) @compute fn main() {"
            "\n            textureBarrier();"
            "\n        }"
            "\n    ");
    });

} // namespace
