// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/buffer_view_utils.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
// - Upstream's Record tables become ordered vectors, so keysOf() order and query identities match.
//   They are function-local statics because test params are built during static initialization.
// - runReadWriteTest takes the element type name and element count, as the port has no Type class.
//   wrapAlignment(), wrapTypeName() and wrapElementCount() replace Type.vec/array/mat.
// - The params filter, skips and buffer declaration repeated by the bufferView and bufferArrayView
//   specs live here.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"
#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

namespace cts::buffer_view {

inline constexpr int kBufferSizes[] = {128, 256, 512, 1024};
inline constexpr int kOffsets[] = {0, 1, 2, 3, 4, 8, 12, 16, 32, 100, 156, 480, 768};
inline constexpr int kSizes[] = {128, 256, 512, 1024, 32, 48, 64};

inline std::vector<Value> bufferSizeValues() {
    return std::vector<Value>(std::begin(kBufferSizes), std::end(kBufferSizes));
}

inline std::vector<Value> offsetValues() {
    return std::vector<Value>(std::begin(kOffsets), std::end(kOffsets));
}

inline std::vector<Value> callValues() {
    return {"unsized", "sized", "sized_indirect", "sized_sized_indirect"};
}

struct ArrayLengthType {
    std::string name;
    std::string type;
    std::string access = "";
    int align = 0;
    int stride = 0;
    int arrayOffset = 0;
    bool f16 = false;
    bool uniformStdLayout = false;
};

/**
 * Return true if the case is valid
 *
 * @param type The testcase
 * @param offset The offset to the view call
 * @param size The size to the view call
 * @param bufferSize The size of the buffer
 * @returns true if case is valid.
 */
inline bool isValidArrayLengthCase(const ArrayLengthType& type, int offset, int size, int bufferSize) {
    if (size == 0) {
        // bufferView case
        return offset + type.arrayOffset + type.stride <= bufferSize;
    }
    // bufferArrayView case
    if (type.arrayOffset + type.stride > size) {
        return false;
    }
    return offset + size <= bufferSize;
}

/**
 * Calculates the return length in elements
 *
 * @param type The testcase
 * @param offset The offset to the view call
 * @param size The size to the view call
 * @param bufferSize The buffer size
 * @returns The number of elements in the arrayLength call
 */
inline uint32_t calculateArrayLength(const ArrayLengthType& type, int offset, int size, int bufferSize) {
    const int alignOffset = offset & ~(type.align - 1);
    if (size == 0) {
        // bufferView case
        return static_cast<uint32_t>((bufferSize - alignOffset - type.arrayOffset) / type.stride);
    }
    // bufferArrayView case
    return static_cast<uint32_t>((size - type.arrayOffset) / type.stride);
}

inline constexpr const char* kStructDecls = R"(
struct S {
  a: vec4u,
  b: u32,
}
struct T {
  a : u32,
  b : array<u32, 5>
}
struct S_arr {
  a : vec4u,
  b : array<u32>,
}
struct T_arr {
  a : S,
  b : array<T>,
}
)";

inline const std::vector<ArrayLengthType>& arrayLengthTypes() {
    static const std::vector<ArrayLengthType> types = {
        // Scalars and vectors
        {.name = "u32", .type = "array<u32>", .align = 4, .stride = 4, .uniformStdLayout = true},
        {.name = "vec2f", .type = "array<vec2f>", .align = 8, .stride = 8, .uniformStdLayout = true},
        {.name = "vec3i", .type = "array<vec3i>", .align = 16, .stride = 16},
        {.name = "vec4u", .type = "array<vec4u>", .align = 16, .stride = 16},
        {.name = "f16", .type = "array<f16>", .align = 2, .stride = 2, .f16 = true, .uniformStdLayout = true},
        {.name = "vec2h", .type = "array<vec2h>", .align = 4, .stride = 4, .f16 = true, .uniformStdLayout = true},
        {.name = "vec3h", .type = "array<vec3h>", .align = 8, .stride = 8, .f16 = true, .uniformStdLayout = true},
        {.name = "vec4h", .type = "array<vec4h>", .align = 8, .stride = 8, .f16 = true, .uniformStdLayout = true},
        // Matrices
        {.name = "mat2x2f", .type = "array<mat2x2f>", .align = 8, .stride = 16},
        {.name = "mat2x3f", .type = "array<mat2x3f>", .align = 16, .stride = 32},
        {.name = "mat2x4f", .type = "array<mat2x4f>", .align = 16, .stride = 32},
        {.name = "mat4x2f", .type = "array<mat4x2f>", .align = 8, .stride = 32},
        {.name = "mat4x3f", .type = "array<mat4x3f>", .align = 16, .stride = 64},
        {.name = "mat4x4f", .type = "array<mat4x4f>", .align = 16, .stride = 64},
        {.name = "mat3x2h", .type = "array<mat3x2h>", .align = 4, .stride = 12, .f16 = true},
        {.name = "mat3x3h", .type = "array<mat3x3h>", .align = 8, .stride = 24, .f16 = true, .uniformStdLayout = true},
        {.name = "mat3x4h", .type = "array<mat3x4h>", .align = 8, .stride = 24, .f16 = true, .uniformStdLayout = true},
        // Structs without arrays
        {.name = "S", .type = "array<S>", .align = 16, .stride = 32},
        {.name = "T", .type = "array<T>", .align = 4, .stride = 24, .uniformStdLayout = true},
        // Sized arrays
        {.name = "array_u32_4", .type = "array<array<u32, 4>>", .align = 4, .stride = 16, .uniformStdLayout = true},
        {.name = "array_f16_6",
         .type = "array<array<f16, 6>>",
         .align = 2,
         .stride = 12,
         .f16 = true,
         .uniformStdLayout = true},
        {.name = "array_vec2f_2", .type = "array<array<vec2f, 2>>", .align = 8, .stride = 16, .uniformStdLayout = true},
        {.name = "array_S_2", .type = "array<array<S, 2>>", .align = 16, .stride = 64},
        {.name = "array_T_2", .type = "array<array<T, 2>>", .align = 4, .stride = 48},
        // Structs with runtime arrays
        {.name = "S_arr", .type = "S_arr", .access = ".b", .align = 16, .stride = 4, .arrayOffset = 16},
        {.name = "T_arr", .type = "T_arr", .access = ".b", .align = 16, .stride = 24, .arrayOffset = 32},
    };
    return types;
}

inline const ArrayLengthType& arrayLengthType(const std::string& name) {
    for (const ArrayLengthType& type : arrayLengthTypes()) {
        if (type.name == name) {
            return type;
        }
    }
    std::abort();
}

/** keysOf(kArrayLengthTypes) */
inline std::vector<Value> arrayLengthTypeNames() {
    std::vector<Value> names;
    for (const ArrayLengthType& type : arrayLengthTypes()) {
        names.emplace_back(type.name);
    }
    return names;
}

inline void skipIfBufferViewNotSupported(GpuTest& t) {
    if (!hasBufferViewLanguageFeature()) {
        t.skip("Missing buffer_view language feature");
    }
}

inline void skipIfUnrestrictedPointerParametersNotSupported(GpuTest& t) {
    if (!hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters)) {
        t.skip("Missing unrestricted_pointer_parameters language feature");
    }
}

inline void skipIfUniformBufferStandardLayoutNotSupported(GpuTest& t) {
    if (!hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout)) {
        t.skip("Missing uniform_buffer_standard_layout language feature");
    }
}

inline void skipIfNoShaderF16(GpuTest& t) {
    if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16) == WGPU_FALSE) {
        t.skip("Missing shader-f16 feature");
    }
}

/** @returns the declaration of the variable 'v' of type 'bufferTy' used by the array length tests. */
inline std::string arrayLengthVarDecl(const std::string& aspace, const std::string& bufferTy) {
    if (aspace == "workgroup") {
        return "var<workgroup> v : " + bufferTy + ";\n@group(0) @binding(0) var<storage> dummy : array<u32>;";
    }
    if (aspace == "uniform") {
        return "@group(0) @binding(0) var<uniform> v : " + bufferTy + ";";
    }
    return "@group(0) @binding(0) var<storage> v : " + bufferTy + ";";
}

/**
 * Run arrayLength tests for bufferView and bufferArrayView
 *
 * @param t The test
 * @param wgsl The shader code. Interface requirements:
 *             * Overrides:
 *               * wgx: workgroup size x
 *               * bufferSize: size of the buffer
 *             * (0, 0): input buffer with 'usage' usage
 *             * (0, 1): read-only-storage-buffer for offsets
 *             * (0, 2): read-only-storage-buffer for sizes
 *             * (0, 3): storage-buffer for output
 * @param usage The test buffer usage
 * @param bufferSize The size of the test buffer
 * @param dynOffset The size of the dynamic offset for the test buffer
 * @param offsets The values for the offset buffer
 * @param sizes The values for the size buffer
 * @param values The expected results
 */
inline void runLengthTest(
    GpuTest& t,
    const std::string& wgsl,
    WGPUBufferUsage usage,
    int bufferSize,
    int dynOffset,
    const std::vector<uint32_t>& offsets,
    const std::vector<uint32_t>& sizes,
    const std::vector<uint32_t>& values) {
    const uint64_t fullBufferSize = static_cast<uint64_t>(bufferSize) + static_cast<uint64_t>(dynOffset);
    WGPUBufferDescriptor inputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    inputDesc.size = fullBufferSize;
    inputDesc.usage = usage;
    WGPUBuffer inputBuffer = t.createBufferTracked(inputDesc);
    WGPUBuffer offsetBuffer = t.makeBufferWithContents(
        offsets.data(), offsets.size() * sizeof(uint32_t), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBuffer sizeBuffer = t.makeBufferWithContents(
        sizes.data(), sizes.size() * sizeof(uint32_t), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = values.size() * 4;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    WGPUBindGroupLayoutEntry layoutEntries[4] = {
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    };
    for (uint32_t i = 0; i < 4; ++i) {
        layoutEntries[i].binding = i;
        layoutEntries[i].visibility = WGPUShaderStage_Compute;
    }
    layoutEntries[0].buffer.type =
        usage == WGPUBufferUsage_Uniform ? WGPUBufferBindingType_Uniform : WGPUBufferBindingType_ReadOnlyStorage;
    layoutEntries[0].buffer.hasDynamicOffset = dynOffset != 0 ? WGPU_TRUE : WGPU_FALSE;
    layoutEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    layoutEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    layoutEntries[3].buffer.type = WGPUBufferBindingType_Storage;
    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.entryCount = 4;
    layoutDesc.entries = layoutEntries;
    WGPUBindGroupLayout bgLayout = t.createBindGroupLayoutTracked(layoutDesc);

    // Limit the number of invocations per workgroup to 128 to fit the default compat limits. Launch
    // extra workgroups to fit all the required invocations.
    const uint32_t valueCount = static_cast<uint32_t>(values.size());
    const uint32_t wgx = valueCount < 128u ? valueCount : 128u;
    const uint32_t numWgs = (valueCount + wgx - 1) / wgx;

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bgLayout;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(pipelineLayoutDesc);

    WGPUConstantEntry constants[2] = {WGPU_CONSTANT_ENTRY_INIT, WGPU_CONSTANT_ENTRY_INIT};
    constants[0].key = WGPUStringView{"wgx", 3};
    constants[0].value = wgx;
    constants[1].key = WGPUStringView{"bufferSize", 10};
    constants[1].value = bufferSize;

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = t.createShaderModuleTracked(wgsl);
    pipelineDesc.compute.constantCount = 2;
    pipelineDesc.compute.constants = constants;
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    WGPUBindGroup bg = atomics::makeAutoBindGroup(
        t,
        pipeline,
        {{inputBuffer, static_cast<uint64_t>(bufferSize)},
         {offsetBuffer, offsets.size() * sizeof(uint32_t)},
         {sizeBuffer, sizes.size() * sizeof(uint32_t)},
         {outputBuffer, outputDesc.size}});

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    const uint32_t dynamicOffset = static_cast<uint32_t>(dynOffset);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, dynOffset == 0 ? 0 : 1, dynOffset == 0 ? nullptr : &dynamicOffset);
    wgpuComputePassEncoderDispatchWorkgroups(pass, numWgs, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    t.expectGPUBufferValuesEqual(outputBuffer, values.data(), values.size() * sizeof(uint32_t));
}

struct LayoutCase {
    std::string name;
    std::string type;
    std::string decl = "";
    std::string access;
    int align = 0;
    int offset = 0;
    bool f16 = false;
    bool f32 = false;
    bool uniformStdLayoutView = false;
    bool uniformStdLayoutArrayView = false;
};

inline const std::vector<LayoutCase>& layoutCases() {
    static const std::vector<LayoutCase> cases = {
        {.name = "vec2u_align8",
         .type = "S_vec2u_align",
         .decl = "struct S_vec2u_align { x : u32, y : vec2u, }",
         .access = ".y[1]",
         .align = 8,
         .offset = 12,
         .uniformStdLayoutArrayView = true},
        {.name = "vec3u_align16",
         .type = "S_vec3u_align",
         .decl = "struct S_vec3u_align { x : u32, y : vec3u, }",
         .access = ".y[2]",
         .align = 16,
         .offset = 24},
        {.name = "vec4u_align16",
         .type = "S_vec4u_align",
         .decl = "struct S_vec4u_align { x : u32, y : vec4u, }",
         .access = ".y[0]",
         .align = 16,
         .offset = 16},
        {.name = "struct_align32",
         .type = "S_align32",
         .decl = "struct S_align32 { x : u32, @align(32) y : u32, }",
         .access = ".y",
         .align = 32,
         .offset = 32},
        {.name = "vec2h_align4",
         .type = "S_vec2h_align",
         .decl = "struct S_vec2h_align { x : f16, y : vec2h }",
         .access = ".y[0]",
         .align = 4,
         .offset = 4,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "vec3h_align8",
         .type = "S_vec3h_align",
         .decl = "struct S_vec3h_align { x : f16, y : vec3h }",
         .access = ".y.z",
         .align = 8,
         .offset = 12,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "vec4h_align8",
         .type = "S_vec4h_align",
         .decl = "struct S_vec4h_align { x : f16, y : vec4h }",
         .access = ".y.z",
         .align = 8,
         .offset = 12,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "vec3i_size12",
         .type = "S_vec3i_size",
         .decl = "struct S_vec3i_size { x : vec3i, y : u32 }",
         .access = ".y",
         .align = 16,
         .offset = 12},
        {.name = "vec3h_size6",
         .type = "S_vec3h_size",
         .decl = "struct S_vec3h_size { x : vec3h, y : f16, z : f16 }",
         .access = ".z",
         .align = 8,
         .offset = 8,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "size80",
         .type = "S_size80",
         .decl = "struct S_size80 { @size(80) x : u32, y : u32 }",
         .access = ".y",
         .align = 4,
         .offset = 80,
         .uniformStdLayoutArrayView = true},
        {.name = "mat2x2f_align8",
         .type = "S_mat2x2f_align",
         .decl = "struct S_mat2x2f_align { x : u32, y : mat2x2f }",
         .access = ".y[0][0]",
         .align = 8,
         .offset = 8,
         .f32 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat3x3f_align16",
         .type = "S_mat3x3f_align",
         .decl = "struct S_mat3x3f_align { x : u32, y : mat3x3f }",
         .access = ".y[0][0]",
         .align = 16,
         .offset = 16,
         .f32 = true},
        {.name = "mat4x4f_align16",
         .type = "S_mat4x4f_align",
         .decl = "struct S_mat4x4f_align { x : u32, y : mat4x4f }",
         .access = ".y[1][0]",
         .align = 16,
         .offset = 32,
         .f32 = true},
        {.name = "mat3x2h_align4",
         .type = "S_mat3x2h_align",
         .decl = "struct S_mat3x2h_align { x : f16, y : mat3x2h }",
         .access = ".y[0][0]",
         .align = 4,
         .offset = 4,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat4x3h_align8",
         .type = "S_mat4x3h_align",
         .decl = "struct S_mat4x3h_align { x : f16, y : mat4x3h }",
         .access = ".y[0][0]",
         .align = 8,
         .offset = 8,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat2x4h_align8",
         .type = "S_mat2x4h_align",
         .decl = "struct S_mat2x4h_align { x : f16, y : mat2x4h }",
         .access = ".y[0][0]",
         .align = 8,
         .offset = 8,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat2x2f_size",
         .type = "S_mat2x2f_size",
         .decl = "struct S_mat2x2f_size { x : mat2x2f, y : u32 }",
         .access = ".y",
         .align = 8,
         .offset = 16,
         .uniformStdLayoutArrayView = true},
        {.name = "mat3x2f_size",
         .type = "S_mat3x2f_size",
         .decl = "struct S_mat3x2f_size { x : mat3x2f, y : u32 }",
         .access = ".y",
         .align = 8,
         .offset = 24,
         .uniformStdLayoutArrayView = true},
        {.name = "mat2x3f_size",
         .type = "S_mat2x3f_size",
         .decl = "struct S_mat2x3f_size { x : mat2x3f, y : u32 }",
         .access = ".y",
         .align = 16,
         .offset = 32},
        {.name = "mat3x3f_size",
         .type = "S_mat3x3f_size",
         .decl = "struct S_mat3x3f_size { x : mat3x3f, y : u32 }",
         .access = ".y",
         .align = 16,
         .offset = 48},
        {.name = "mat2x4f_size",
         .type = "S_mat2x4f_size",
         .decl = "struct S_mat2x4f_size { x : mat2x4f, y : u32 }",
         .access = ".y",
         .align = 16,
         .offset = 32},
        {.name = "mat3x4f_size",
         .type = "S_mat3x4f_size",
         .decl = "struct S_mat3x4f_size { x : mat3x4f, y : u32 }",
         .access = ".y",
         .align = 16,
         .offset = 48},
        {.name = "mat2x2h_size",
         .type = "S_mat2x2h_size",
         .decl = "struct S_mat2x2h_size { x : mat2x2h, y : f16 }",
         .access = ".y",
         .align = 4,
         .offset = 8,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat4x2h_size",
         .type = "S_mat4x2h_size",
         .decl = "struct S_mat4x2h_size { x : mat4x2h, y : f16 }",
         .access = ".y",
         .align = 4,
         .offset = 16,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat2x3h_size",
         .type = "S_mat2x3h_size",
         .decl = "struct S_mat2x3h_size { x : mat2x3h, y : f16 }",
         .access = ".y",
         .align = 8,
         .offset = 16,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat4x3h_size",
         .type = "S_mat4x3h_size",
         .decl = "struct S_mat4x3h_size { x : mat4x3h, y : f16 }",
         .access = ".y",
         .align = 8,
         .offset = 32,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat2x4h_size",
         .type = "S_mat2x4h_size",
         .decl = "struct S_mat2x4h_size { x : mat2x4h, y : f16 }",
         .access = ".y",
         .align = 8,
         .offset = 16,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "mat4x4h_size",
         .type = "S_mat4x4h_size",
         .decl = "struct S_mat4x4h_size { x : mat4x4h, y : f16 }",
         .access = ".y",
         .align = 8,
         .offset = 32,
         .f16 = true,
         .uniformStdLayoutArrayView = true},
        {.name = "struct_size_roundup",
         .type = "S_struct_size_roundup",
         .decl = "struct Inner { x : vec3u }\n"
                 "           struct S_struct_size_roundup { x : Inner, y : u32 }",
         .access = ".y",
         .align = 16,
         .offset = 16},
        {.name = "struct_inner_size",
         .type = "S_struct_inner_size",
         .decl = "struct Inner { @size(112) x : u32 }\n"
                 "           struct S_struct_inner_size { x : Inner, y : u32 }",
         .access = ".y",
         .align = 4,
         .offset = 112,
         .uniformStdLayoutArrayView = true},
        {.name = "struct_inner_align",
         .type = "S_struct_inner_align",
         .decl = "struct Inner { @align(64) x : u32 }\n"
                 "           struct S_struct_inner_align { x : Inner, y : u32 }",
         .access = ".y",
         .align = 64,
         .offset = 64},
        {.name = "struct_inner_size_and_align",
         .type = "S_struct_inner_size_and_align",
         .decl = "struct Inner { @align(32) @size(33) x : u32 }\n"
                 "           struct S_struct_inner_size_and_align { x : Inner, y : u32 }",
         .access = ".y",
         .align = 32,
         .offset = 64},
        {.name = "struct_override_size",
         .type = "S_struct_override_size",
         .decl = "struct Inner { @size(32) x : u32 }\n"
                 "           struct S_struct_override_size { @size(64) x : Inner, y : u32 }",
         .access = ".y",
         .align = 4,
         .offset = 64,
         .uniformStdLayoutArrayView = true},
        {.name = "struct_double_align",
         .type = "S_struct_double_align",
         .decl = "struct Inner { x : u32, @align(32) y : u32 }\n"
                 "           struct S_struct_double_align { x : u32, @align(64) y : Inner }",
         .access = ".y.y",
         .align = 64,
         .offset = 96},
        {.name = "array_stride_size",
         .type = "array<S_stride, 4>",
         .decl = "struct S_stride { @size(16) x : u32 }",
         .access = "[2].x",
         .align = 4,
         .offset = 32},
    };
    return cases;
}

inline const LayoutCase& layoutCase(const std::string& name) {
    for (const LayoutCase& testcase : layoutCases()) {
        if (testcase.name == name) {
            return testcase;
        }
    }
    std::abort();
}

/** keysOf(kLayoutCases) */
inline std::vector<Value> layoutCaseNames() {
    std::vector<Value> names;
    for (const LayoutCase& testcase : layoutCases()) {
        names.emplace_back(testcase.name);
    }
    return names;
}

/** The params filter shared by the read_layout and write_layout tests. */
inline bool isValidLayoutCase(const ParamRecord& p) {
    const LayoutCase& testcase = layoutCase(valueAs<std::string>(*findParam(p, "case")));
    const int offset = static_cast<int>(valueAs<int64_t>(*findParam(p, "offset")));
    if ((offset & (testcase.align - 1)) != 0) {
        return false;
    }
    return testcase.offset + offset < 252;
}

/**
 * Runs a read test for memory layouts from bufferView and bufferArrayView
 *
 * @param t The GPUTest
 * @param testcase the LayoutCase
 * @param wgsl the shader code
 *             * (0, 0) - uniform or storage buffer
 *             * (0, 1) - storage buffer
 * @param aspace the address space being tested
 * @param offset the offset to the view call
 */
inline void runReadLayoutTest(
    GpuTest& t,
    const LayoutCase& testcase,
    const std::string& wgsl,
    const std::string& aspace,
    int offset) {
    const WGPUBufferUsage usage =
        WGPUBufferUsage_CopyDst | (aspace == "uniform" ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage);

    const int kBufferLength = 256;
    // Magic number is 42 in various representations.
    const uint32_t inMagicNumber = testcase.f16 ? 0x5140u : testcase.f32 ? 0x42280000u : 42u;
    std::vector<uint32_t> input(static_cast<size_t>(kBufferLength), 0);
    for (int x = 0; x < kBufferLength; ++x) {
        if (x * 4 == testcase.offset + offset) {
            input[static_cast<size_t>(x)] = inMagicNumber;
        }
    }
    const uint64_t inputSize = input.size() * sizeof(uint32_t);
    WGPUBuffer inputBuffer = t.makeBufferWithContents(input.data(), static_cast<size_t>(inputSize), usage);

    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = 4;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    WGPUComputePipeline pipeline = atomics::createComputePipelineAuto(t, wgsl);
    WGPUBindGroup bg =
        atomics::makeAutoBindGroup(t, pipeline, {{inputBuffer, inputSize}, {outputBuffer, outputDesc.size}});
    atomics::runComputePassX(t, pipeline, bg, 1);

    const uint32_t expected = 42;
    t.expectGPUBufferValuesEqual(outputBuffer, &expected, sizeof(expected));
}

/**
 * Runs a write test for memory layout from bufferView and bufferArrayView
 *
 * @param t The GPUTest
 * @param testcase the LayoutCase
 * @param wgsl The shader code
 *             * (0, 0) - a read-only storage buffer
 *             * (0, 1) - a read-write storage buffer
 * @param offset The offset for the view call
 */
inline void runWriteLayoutTest(GpuTest& t, const LayoutCase& testcase, const std::string& wgsl, int offset) {
    const uint32_t inputData = 42;
    WGPUBuffer inputBuffer = t.makeBufferWithContents(
        &inputData, sizeof(inputData), WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);

    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = 256 * 4;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    WGPUComputePipeline pipeline = atomics::createComputePipelineAuto(t, wgsl);
    WGPUBindGroup bg =
        atomics::makeAutoBindGroup(t, pipeline, {{inputBuffer, sizeof(inputData)}, {outputBuffer, outputDesc.size}});
    atomics::runComputePassX(t, pipeline, bg, 1);

    // Magic number is 42 in various representations.
    const uint32_t outMagicNumber = testcase.f16 ? 0x5140u : testcase.f32 ? 0x42280000u : 42u;
    std::vector<uint32_t> expected(128, 0);
    for (int x = 0; x < 128; ++x) {
        if (x * 4 == testcase.offset + offset) {
            expected[static_cast<size_t>(x)] = outMagicNumber;
        }
    }
    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size() * sizeof(uint32_t));
}

inline int scalarSize(const std::string& eleTy) {
    return eleTy == "f16" ? 2 : 4;
}

/** The alignment of the type formed by wrapping 'eleTy': Type.vec/array/mat(...).alignment. */
inline int wrapAlignment(const std::string& eleTy, const std::string& wrap, int width) {
    const int size = scalarSize(eleTy);
    if (wrap == "vector") {
        return (width == 2 ? 2 : 4) * size;
    }
    if (wrap == "matrix") {
        // Type.mat(width, 2, eleTy): the alignment of a vec2 column.
        return 2 * size;
    }
    return size;
}

/** The WGSL name of the type formed by wrapping 'eleTy': Type.vec/array/mat(...).toString(). */
inline std::string wrapTypeName(const std::string& eleTy, const std::string& wrap, int width) {
    const std::string w = std::to_string(width);
    if (wrap == "vector") {
        return "vec" + w + "<" + eleTy + ">";
    }
    if (wrap == "array") {
        return "array<" + eleTy + ", " + w + ">";
    }
    if (wrap == "matrix") {
        return "mat" + w + "x2<" + eleTy + ">";
    }
    return eleTy;
}

/** The number of scalars in the type formed by wrapping an element type. */
inline int wrapElementCount(const std::string& wrap, int width) {
    if (wrap == "vector" || wrap == "array") {
        return width;
    }
    if (wrap == "matrix") {
        return 2 * width;
    }
    return 1;
}

/** The parts of the params filter shared by the read and write tests. */
inline bool isValidReadWriteCase(const ParamRecord& p) {
    const std::string baseType = valueAs<std::string>(*findParam(p, "base_type"));
    const std::string wrap = valueAs<std::string>(*findParam(p, "wrap"));
    const int width = static_cast<int>(valueAs<int64_t>(*findParam(p, "width")));
    const int offset = static_cast<int>(valueAs<int64_t>(*findParam(p, "offset")));
    if (wrap != "none" && width == 1) {
        return false;
    }
    if (wrap == "none" && width != 1) {
        return false;
    }
    if (wrap == "matrix" && baseType != "f32" && baseType != "f16") {
        return false;
    }
    return offset % wrapAlignment(baseType, wrap, width) == 0;
}

/** Encodes 'values' as a typed array of the element type 'eleTy'. */
inline std::vector<uint8_t> encodeElements(const std::string& eleTy, const std::vector<int>& values) {
    std::vector<uint8_t> bytes(values.size() * static_cast<size_t>(scalarSize(eleTy)));
    for (size_t i = 0; i < values.size(); ++i) {
        if (eleTy == "f16") {
            const uint16_t bits = expression::access::f16BitsOfDouble(values[i]);
            std::memcpy(bytes.data() + i * sizeof(bits), &bits, sizeof(bits));
        } else if (eleTy == "f32") {
            const float value = static_cast<float>(values[i]);
            std::memcpy(bytes.data() + i * sizeof(value), &value, sizeof(value));
        } else {
            const int32_t value = values[i];
            std::memcpy(bytes.data() + i * sizeof(value), &value, sizeof(value));
        }
    }
    return bytes;
}

/**
 * Performs a type-based read or write test for bufferView and bufferArrayView
 *
 * Note: This only covers types with no padding. Padding is covered by the layout tests.
 * @param read Test read if true
 * @param t The GPUTest
 * @param wgsl The shader code
 * @param eleTy The base element type
 * @param numEles The number of base elements in the test type
 * @param aspace The address space to test
 * @param offset The offset for the call
 * @param bufferSize The test buffer size
 */
inline void runReadWriteTest(
    bool read,
    GpuTest& t,
    const std::string& wgsl,
    const std::string& eleTy,
    int numEles,
    const std::string& aspace,
    int offset,
    int bufferSize) {
    const int eleSize = scalarSize(eleTy);
    const int bufferElements = bufferSize / eleSize;

    const int start = offset / eleSize;
    const int end = start + numEles;
    std::vector<int> ins;
    std::vector<int> outs;
    for (int i = 0; i < bufferElements; ++i) {
        if (i >= start && i < end) {
            ins.push_back(i + 4);
            outs.push_back(i + 4);
        } else if (read) {
            ins.push_back(0);
        } else {
            outs.push_back(0);
        }
    }
    if (!read && eleTy == "f16" && ins.size() % 2 == 1) {
        ins.push_back(0);
    }
    if (read && eleTy == "f16" && outs.size() % 2 == 1) {
        outs.push_back(0);
    }

    const std::vector<uint8_t> inputData = encodeElements(eleTy, ins);
    const std::vector<uint8_t> outputData = encodeElements(eleTy, outs);

    WGPUBuffer inputBuffer = t.makeBufferWithContents(
        inputData.data(),
        inputData.size(),
        WGPUBufferUsage_CopySrc | (aspace == "uniform" ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage));
    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = outputData.size();
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    WGPUComputePipeline pipeline = atomics::createComputePipelineAuto(t, wgsl);
    WGPUBindGroup bg = atomics::makeAutoBindGroup(
        t, pipeline, {{inputBuffer, inputData.size()}, {outputBuffer, outputDesc.size}});
    atomics::runComputePassX(t, pipeline, bg, 1);

    t.expectGPUBufferValuesEqual(outputBuffer, outputData.data(), outputData.size());
}

} // namespace cts::buffer_view
