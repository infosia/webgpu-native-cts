// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/shadow_builtins.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,shadow_builtins",
    "Validation tests for identifiers");

struct BuiltinTest {
    const char* name;     // case key
    const char* keyword;  // builtin keyword to shadow
    const char* src;      // statement exercising it
};

static std::vector<Value> testNames(const std::vector<BuiltinTest>& tests) {
    std::vector<Value> values;
    for (const BuiltinTest& bt : tests) {
        values.emplace_back(std::string(bt.name));
    }
    return values;
}

static const BuiltinTest& findTest(const std::vector<BuiltinTest>& tests, const std::string& name) {
    for (const BuiltinTest& bt : tests) {
        if (name == bt.name) {
            return bt;
        }
    }
    static const BuiltinTest dummy{"", "", ""};
    return dummy;
}

CTS_TEST(g, "function_param")
    .desc("Test that a function param can shadow a builtin, but the builtin is available for other "
          "params.")
    .fn([](ShaderValidationTest& t) {
        const std::string code = "\nfn f(f: i32, i32: i32, t: i32) -> i32 { return i32; }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kTests (object key order preserved).
static const std::vector<BuiltinTest>& kTests() {
    static const std::vector<BuiltinTest> v = {
        {"abs", "abs", "_ = abs(1);"},
        {"acos", "acos", "_ = acos(.2);"},
        {"acosh", "acosh", "_ = acosh(1.2);"},
        {"all", "all", "_ = all(true);"},
        {"any", "any", "_ = any(true);"},
        {"array_templated", "array", "_ = array<i32, 2>(1, 2);"},
        {"array", "array", "_ = array(1, 2);"},
        {"array_length", "arrayLength", "_ = arrayLength(&placeholder.rt_arr);"},
        {"asin", "asin", "_ = asin(.2);"},
        {"asinh", "asinh", "_ = asinh(1.2);"},
        {"atan", "atan", "_ = atan(1.2);"},
        {"atanh", "atanh", "_ = atanh(.2);"},
        {"atan2", "atan2", "_ = atan2(1.2, 2.3);"},
        {"bool", "bool", "_ = bool(1);"},
        {"bitcast", "bitcast", "_ = bitcast<f32>(1i);"},
        {"ceil", "ceil", "_ = ceil(1.23);"},
        {"clamp", "clamp", "_ = clamp(1, 2, 3);"},
        {"cos", "cos", "_ = cos(2);"},
        {"cosh", "cosh", "_ = cosh(2.2);"},
        {"countLeadingZeros", "countLeadingZeros", "_ = countLeadingZeros(1);"},
        {"countOneBits", "countOneBits", "_ = countOneBits(1);"},
        {"countTrailingZeros", "countTrailingZeros", "_ = countTrailingZeros(1);"},
        {"cross", "cross", "_ = cross(vec3(1, 2, 3), vec3(4, 5, 6));"},
        {"degrees", "degrees", "_ = degrees(1);"},
        {"determinant", "determinant", "_ = determinant(mat2x2(1, 2, 3, 4));"},
        {"distance", "distance", "_ = distance(1, 2);"},
        {"dot", "dot", "_ = dot(vec2(1, 2,), vec2(2, 3));"},
        {"dot4U8Packed", "dot4U8Packed", "_ = dot4U8Packed(1, 2);"},
        {"dot4I8Packed", "dot4I8Packed", "_ = dot4I8Packed(1, 2);"},
        {"dpdx", "dpdx", "_ = dpdx(2);"},
        {"dpdxCoarse", "dpdxCoarse", "_ = dpdxCoarse(2);"},
        {"dpdxFine", "dpdxFine", "_ = dpdxFine(2);"},
        {"dpdy", "dpdy", "_ = dpdy(2);"},
        {"dpdyCoarse", "dpdyCoarse", "_ = dpdyCoarse(2);"},
        {"dpdyFine", "dpdyFine", "_ = dpdyFine(2);"},
        {"exp", "exp", "_ = exp(1);"},
        {"exp2", "exp2", "_ = exp2(2);"},
        {"extractBits", "extractBits", "_ = extractBits(1, 2, 3);"},
        {"f32", "f32", "_ = f32(1i);"},
        {"faceForward", "faceForward", "_ = faceForward(vec2(1, 2), vec2(3, 4), vec2(5, 6));"},
        {"firstLeadingBit", "firstLeadingBit", "_ = firstLeadingBit(1);"},
        {"firstTrailingBit", "firstTrailingBit", "_ = firstTrailingBit(1);"},
        {"floor", "floor", "_ = floor(1.2);"},
        {"fma", "fma", "_ = fma(1, 2, 3);"},
        {"fract", "fract", "_ = fract(1);"},
        {"frexp", "frexp", "_ = frexp(1);"},
        {"fwidth", "fwidth", "_ = fwidth(2);"},
        {"fwidthCoarse", "fwidthCoarse", "_ = fwidthCoarse(2);"},
        {"fwidthFine", "fwidthFine", "_ = fwidthFine(2);"},
        {"i32", "i32", "_ = i32(2u);"},
        {"insertBits", "insertBits", "_ = insertBits(1, 2, 3, 4);"},
        {"inverseSqrt", "inverseSqrt", "_ = inverseSqrt(1);"},
        {"ldexp", "ldexp", "_ = ldexp(1, 2);"},
        {"length", "length", "_ = length(1);"},
        {"log", "log", "_ = log(2);"},
        {"log2", "log2", "_ = log2(2);"},
        {"mat2x2_templated", "mat2x2", "_ = mat2x2<f32>(1, 2, 3, 4);"},
        {"mat2x2", "mat2x2", "_ = mat2x2(1, 2, 3, 4);"},
        {"mat2x3_templated", "mat2x3", "_ = mat2x3<f32>(1, 2, 3, 4, 5, 6);"},
        {"mat2x3", "mat2x3", "_ = mat2x3(1, 2, 3, 4, 5, 6);"},
        {"mat2x4_templated", "mat2x4", "_ = mat2x4<f32>(1, 2, 3, 4, 5, 6, 7, 8);"},
        {"mat2x4", "mat2x4", "_ = mat2x4(1, 2, 3, 4, 5, 6, 7, 8);"},
        {"mat3x2_templated", "mat3x2", "_ = mat3x2<f32>(1, 2, 3, 4, 5, 6);"},
        {"mat3x2", "mat3x2", "_ = mat3x2(1, 2, 3, 4, 5, 6);"},
        {"mat3x3_templated", "mat3x3", "_ = mat3x3<f32>(1, 2, 3, 4, 5, 6, 7, 8, 9);"},
        {"mat3x3", "mat3x3", "_ = mat3x3(1, 2, 3, 4, 5, 6, 7, 8, 9);"},
        {"mat3x4_templated", "mat3x4", "_ = mat3x4<f32>(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);"},
        {"mat3x4", "mat3x4", "_ = mat3x4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);"},
        {"mat4x2_templated", "mat4x2", "_ = mat4x2<f32>(1, 2, 3, 4, 5, 6, 7, 8);"},
        {"mat4x2", "mat4x2", "_ = mat4x2(1, 2, 3, 4, 5, 6, 7, 8);"},
        {"mat4x3_templated", "mat4x3", "_ = mat4x3<f32>(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);"},
        {"mat4x3", "mat4x3", "_ = mat4x3(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);"},
        {"mat4x4_templated", "mat4x4",
         "_ = mat4x4<f32>(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);"},
        {"mat4x4", "mat4x4",
         "_ = mat4x4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);"},
        {"max", "max", "_ = max(1, 2);"},
        {"min", "min", "_ = min(1, 2);"},
        {"mix", "mix", "_ = mix(1, 2, 3);"},
        {"modf", "modf", "_ = modf(1.2);"},
        {"normalize", "normalize", "_ = normalize(vec2(1, 2));"},
        {"pack2x16snorm", "pack2x16snorm", "_ = pack2x16snorm(vec2(1, 2));"},
        {"pack2x16unorm", "pack2x16unorm", "_ = pack2x16unorm(vec2(1, 2));"},
        {"pack2x16float", "pack2x16float", "_ = pack2x16float(vec2(1, 2));"},
        {"pack4x8snorm", "pack4x8snorm", "_ = pack4x8snorm(vec4(1, 2, 3, 4));"},
        {"pack4x8unorm", "pack4x8unorm", "_ = pack4x8unorm(vec4(1, 2, 3, 4));"},
        {"pack4xI8", "pack4xI8", "_ = pack4xI8(vec4(1, 2, 3, 4));"},
        {"pack4xU8", "pack4xU8", "_ = pack4xU8(vec4(1, 2, 3, 4));"},
        {"pack4xI8Clamp", "pack4xI8Clamp", "_ = pack4xI8Clamp(vec4(1, 2, 3, 4));"},
        {"pack4xU8Clamp", "pack4xU8Clamp", "_ = pack4xU8Clamp(vec4(1, 2, 3, 4));"},
        {"pow", "pow", "_ = pow(1, 2);"},
        {"quantizeToF16", "quantizeToF16", "_ = quantizeToF16(1.2);"},
        {"radians", "radians", "_ = radians(1.2);"},
        {"reflect", "reflect", "_ = reflect(vec2(1, 2), vec2(3, 4));"},
        {"refract", "refract", "_ = refract(vec2(1, 1), vec2(2, 2), 3);"},
        {"reverseBits", "reverseBits", "_ = reverseBits(1);"},
        {"round", "round", "_ = round(1.2);"},
        {"saturate", "saturate", "_ = saturate(1);"},
        {"select", "select", "_ = select(1, 2, false);"},
        {"sign", "sign", "_ = sign(1);"},
        {"sin", "sin", "_ = sin(2);"},
        {"sinh", "sinh", "_ = sinh(3);"},
        {"smoothstep", "smoothstep", "_ = smoothstep(1, 2, 3);"},
        {"sqrt", "sqrt", "_ = sqrt(24);"},
        {"step", "step", "_ = step(4, 5);"},
        {"tan", "tan", "_ = tan(2);"},
        {"tanh", "tanh", "_ = tanh(2);"},
        {"transpose", "transpose", "_ = transpose(mat2x2(1, 2, 3, 4));"},
        {"trunc", "trunc", "_ = trunc(2);"},
        {"u32", "u32", "_ = u32(1i);"},
        {"unpack2x16snorm", "unpack2x16snorm", "_ = unpack2x16snorm(2);"},
        {"unpack2x16unorm", "unpack2x16unorm", "_ = unpack2x16unorm(2);"},
        {"unpack2x16float", "unpack2x16float", "_ = unpack2x16float(2);"},
        {"unpack4x8snorm", "unpack4x8snorm", "_ = unpack4x8snorm(4);"},
        {"unpack4x8unorm", "unpack4x8unorm", "_ = unpack4x8unorm(4);"},
        {"unpack4xI8", "unpack4xI8", "_ = unpack4xI8(4);"},
        {"unpack4xU8", "unpack4xU8", "_ = unpack4xU8(4);"},
        {"vec2_templated", "vec2", "_ = vec2<f32>(1, 2);"},
        {"vec2", "vec2", "_ = vec2(1, 2);"},
        {"vec3_templated", "vec3", "_ = vec3<f32>(1, 2, 3);"},
        {"vec3", "vec3", "_ = vec3(1, 2, 3);"},
        {"vec4_templated", "vec4", "_ = vec4<f32>(1, 2, 3, 4);"},
        {"vec4", "vec4", "_ = vec4(1, 2, 3, 4);"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_builtin")
    .desc("Test that shadows hide builtins.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "sibling", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kTests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : i32;" : "";
        const std::string sibling_func = inject == "sibling" ? local : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\nstruct Data {"
            "\n  rt_arr: array<i32>,"
            "\n}"
            "\n@group(0) @binding(0) var<storage> placeholder: Data;"
            "\n"
            "\n" + module_shadow +
            "\n"
            "\nfn sibling() {"
            "\n  " + sibling_func +
            "\n}"
            "\n"
            "\n@fragment"
            "\nfn main() -> @location(0) vec4f {"
            "\n  " + func +
            "\n  " + data.src +
            "\n  return vec4f(1);"
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "sibling";
        t.expectCompileResult(pass, code);
    });

// Mirrors upstream kFloat16Tests.
static const std::vector<BuiltinTest>& kFloat16Tests() {
    static const std::vector<BuiltinTest> v = {
        {"f16", "f16", "_ = f16(2);"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_builtin_f16")
    .desc("Test that shadows hide builtins when shader-f16 is enabled.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "sibling", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kFloat16Tests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kFloat16Tests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : f16;" : "";
        const std::string sibling_func = inject == "sibling" ? local : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\nenable f16;"
            "\n"
            "\n" + module_shadow +
            "\n"
            "\nfn sibling() {"
            "\n  " + sibling_func +
            "\n}"
            "\n"
            "\n@vertex"
            "\nfn vtx() -> @builtin(position) vec4f {"
            "\n  " + func +
            "\n  " + data.src +
            "\n  return vec4f(1);"
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "sibling";
        t.expectCompileResult(pass, code);
    });

// Mirrors upstream kTextureTypeTests.
static const std::vector<BuiltinTest>& kTextureTypeTests() {
    static const std::vector<BuiltinTest> v = {
        {"texture_1d", "texture_1d", "var t: texture_1d<f32>;"},
        {"texture_2d", "texture_2d", "var t: texture_2d<f32>;"},
        {"texture_2d_array", "texture_2d_array", "var t: texture_2d_array<f32>;"},
        {"texture_3d", "texture_3d", "var t: texture_3d<f32>;"},
        {"texture_cube", "texture_cube", "var t: texture_cube<f32>;"},
        {"texture_cube_array", "texture_cube_array", "var t: texture_cube_array<f32>;"},
        {"texture_multisampled_2d", "texture_multisampled_2d",
         "var t: texture_multisampled_2d<f32>;"},
        {"texture_depth_multisampled_2d", "texture_depth_multisampled_2d",
         "var t: texture_depth_multisampled_2d;"},
        {"texture_external", "texture_external", "var t: texture_external;"},
        {"texture_storage_1d", "texture_storage_1d",
         "var t: texture_storage_1d<rgba8unorm, read_write>;"},
        {"texture_storage_2d", "texture_storage_2d",
         "var t: texture_storage_2d<rgba8unorm, read_write>;"},
        {"texture_storage_2d_array", "texture_storage_2d_array",
         "var t: texture_storage_2d_array<rgba8unorm, read_write>;"},
        {"texture_storage_3d", "texture_storage_3d",
         "var t: texture_storage_3d<rgba8unorm, read_write>;"},
        {"texture_depth_2d", "texture_depth_2d", "var t: texture_depth_2d;"},
        {"texture_depth_2d_array", "texture_depth_2d_array", "var t: texture_depth_2d_array;"},
        {"texture_depth_cube", "texture_depth_cube", "var t: texture_depth_cube;"},
        {"texture_depth_cube_array", "texture_depth_cube_array",
         "var t: texture_depth_cube_array;"},
        {"sampler", "sampler", "var s: sampler;"},
        {"sampler_comparison", "sampler_comparison", "var s: sampler_comparison;"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_builtin_handle_type")
    .desc("Test that shadows hide builtins when handle address space types are used.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kTextureTypeTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kTextureTypeTests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : f32;" : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\n" + module_shadow +
            "\n@group(0) @binding(0) " + data.src +
            "\n"
            "\nfn func() {"
            "\n  " + func +
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "function";
        t.expectCompileResult(pass, code);
    });

// Mirrors upstream kTextureTests.
static const std::vector<BuiltinTest>& kTextureTests() {
    static const std::vector<BuiltinTest> v = {
        {"textureDimensions", "textureDimensions", "_ = textureDimensions(t_2d);"},
        {"textureGather", "textureGather", "_ = textureGather(1, t_2d, s, vec2(1, 2));"},
        {"textureGatherCompare", "textureGatherCompare",
         "_ = textureGatherCompare(t_2d_depth, sc, vec2(1, 2), 3);"},
        {"textureLoad", "textureLoad", "_ = textureLoad(t_2d, vec2(1, 2), 1);"},
        {"textureNumLayers", "textureNumLayers", "_ = textureNumLayers(t_2d_array);"},
        {"textureNumLevels", "textureNumLevels", "_ = textureNumLevels(t_2d);"},
        {"textureNumSamples", "textureNumSamples", "_ = textureNumSamples(t_2d_ms);"},
        {"textureSample", "textureSample", "_ = textureSample(t_2d, s, vec2(1, 2));"},
        {"textureSampleBias", "textureSampleBias",
         "_ = textureSampleBias(t_2d, s, vec2(1, 2), 2);"},
        {"textureSampleCompare", "textureSampleCompare",
         "_ = textureSampleCompare(t_2d_depth, sc, vec2(1, 2), 2);"},
        {"textureSampleCompareLevel", "textureSampleCompareLevel",
         "_ = textureSampleCompareLevel(t_2d_depth, sc, vec2(1, 2), 3, vec2(1, 2));"},
        {"textureSampleGrad", "textureSampleGrad",
         "_ = textureSampleGrad(t_2d, s, vec2(1, 2), vec2(1, 2), vec2(1, 2));"},
        {"textureSampleLevel", "textureSampleLevel",
         "_ = textureSampleLevel(t_2d, s, vec2(1, 2), 3);"},
        {"textureSampleBaseClampToEdge", "textureSampleBaseClampToEdge",
         "_ = textureSampleBaseClampToEdge(t_2d, s, vec2(1, 2));"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_builtin_texture")
    .desc("Test that shadows hide texture builtins.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "sibling", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kTextureTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kTextureTests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : i32;" : "";
        const std::string sibling_func = inject == "sibling" ? local : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\n@group(0) @binding(0) var t_2d: texture_2d<f32>;"
            "\n@group(0) @binding(1) var t_2d_depth: texture_depth_2d;"
            "\n@group(0) @binding(2) var t_2d_array: texture_2d_array<f32>;"
            "\n@group(0) @binding(3) var t_2d_ms: texture_multisampled_2d<f32>;"
            "\n"
            "\n@group(1) @binding(0) var s: sampler;"
            "\n@group(1) @binding(1) var sc: sampler_comparison;"
            "\n"
            "\n" + module_shadow +
            "\n"
            "\nfn sibling() {"
            "\n  " + sibling_func +
            "\n}"
            "\n"
            "\n@fragment"
            "\nfn main() -> @location(0) vec4f {"
            "\n  " + func +
            "\n  " + data.src +
            "\n  return vec4f(1);"
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "sibling";
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "shadow_hides_builtin_atomic_type")
    .desc("Test that shadows hide builtins when atomic types are used.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "module"}).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string inject = t.param<std::string>("inject");
        const std::string local = "let atomic = 4;";
        const std::string module_shadow =
            inject == "module" ? std::string("var<private> atomic: i32;") : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\n" + module_shadow +
            "\n"
            "\nvar<workgroup> val: atomic<i32>;"
            "\n"
            "\nfn func() {"
            "\n  " + func +
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "function";
        t.expectCompileResult(pass, code);
    });

// Mirrors upstream kAtomicTests.
static const std::vector<BuiltinTest>& kAtomicTests() {
    static const std::vector<BuiltinTest> v = {
        {"atomicLoad", "atomicLoad", "_ = atomicLoad(&a);"},
        {"atomicStore", "atomicStore", "atomicStore(&a, 1);"},
        {"atomicAdd", "atomicAdd", "_ = atomicAdd(&a, 1);"},
        {"atomicSub", "atomicSub", "_ = atomicSub(&a, 1);"},
        {"atomicMax", "atomicMax", "_ = atomicMax(&a, 1);"},
        {"atomicMin", "atomicMin", "_ = atomicMin(&a, 1);"},
        {"atomicAnd", "atomicAnd", "_ = atomicAnd(&a, 1);"},
        {"atomicOr", "atomicOr", "_ = atomicOr(&a, 1);"},
        {"atomicXor", "atomicXor", "_ = atomicXor(&a, 1);"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_builtin_atomic")
    .desc("Test that shadows hide builtin atomic methods.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "sibling", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kAtomicTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kAtomicTests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : i32;" : "";
        const std::string sibling_func = inject == "sibling" ? local : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\nvar<workgroup> a: atomic<i32>;"
            "\n"
            "\n" + module_shadow +
            "\n"
            "\nfn sibling() {"
            "\n  " + sibling_func +
            "\n}"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  " + func +
            "\n  " + data.src +
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "sibling";
        t.expectCompileResult(pass, code);
    });

// Mirrors upstream kBarrierTests.
static const std::vector<BuiltinTest>& kBarrierTests() {
    static const std::vector<BuiltinTest> v = {
        {"storageBarrier", "storageBarrier", "storageBarrier();"},
        {"textureBarrier", "textureBarrier", "textureBarrier();"},
        {"workgroupBarrier", "workgroupBarrier", "workgroupBarrier();"},
        {"workgroupUniformLoad", "workgroupUniformLoad", "_ = workgroupUniformLoad(&u);"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_builtin_barriers")
    .desc("Test that shadows hide builtin barrier methods.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "sibling", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kBarrierTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kBarrierTests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : i32;" : "";
        const std::string sibling_func = inject == "sibling" ? local : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\nvar<workgroup> u: u32;"
            "\n"
            "\n" + module_shadow +
            "\n"
            "\nfn sibling() {"
            "\n  " + sibling_func +
            "\n}"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  " + func +
            "\n  " + data.src +
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "sibling";
        t.expectCompileResult(pass, code);
    });

// Mirrors upstream kAccessModeTests.
static const std::vector<BuiltinTest>& kAccessModeTests() {
    static const std::vector<BuiltinTest> v = {
        {"read", "read", "var<storage, read> a: i32;"},
        {"read_write", "read_write", "var<storage, read_write> a: i32;"},
        {"write", "write", "var t: texture_storage_1d<rgba8unorm, write>;"},
    };
    return v;
}

CTS_TEST(g, "shadow_hides_access_mode")
    .desc("Test that shadows hide access modes.")
    .params([](ParamsBuilder u) {
        return u.combine("inject", {"none", "function", "module"})
            .beginSubcases()
            .combine("builtin", testNames(kAccessModeTests()));
    })
    .fn([](ShaderValidationTest& t) {
        const BuiltinTest& data = findTest(kAccessModeTests(), t.param<std::string>("builtin"));
        const std::string inject = t.param<std::string>("inject");
        const std::string local = std::string("let ") + data.keyword + " = 4;";

        const std::string module_shadow =
            inject == "module" ? std::string("var<private> ") + data.keyword + " : i32;" : "";
        const std::string func = inject == "function" ? local : "";

        const std::string code =
            "\n" + module_shadow +
            "\n"
            "\n@group(0) @binding(0) " + data.src +
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  " + func +
            "\n}"
            "\n    ";
        const bool pass = inject == "none" || inject == "function";
        t.expectCompileResult(pass, code);
    });

}  // namespace
