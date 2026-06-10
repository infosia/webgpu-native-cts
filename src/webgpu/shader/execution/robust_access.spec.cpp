// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/robust_access.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//  - generateTypes() (upstream: webgpu/shader/types.ts) is inlined as a C++ helper.
//  - supportedScalarTypes() and supportsAtomics() (upstream: webgpu/shader/types.ts)
//    are inlined.
//  - align() (upstream: webgpu/util/math.ts) is inlined.
//  - expandWithParams(generateTypes) (upstream TypeScript generator) has no direct
//    C++ harness equivalent. Ported as expand("type", ...) subcases: the expander
//    returns WGSL type strings, and the fn body re-derives _kTypeInfo from
//    (type, baseType, containerType, addressSpace, isAtomic). The subcase query
//    identity is therefore "type=<wgsl-type-string>" rather than the upstream
//    object representation, which is an unavoidable deviation.
//  - Float16Array fill (upstream JS) is approximated: the test buffer sentinel
//    (42 as uint16 = 0x002A, not the same as f16(42)) is irrelevant to the test
//    logic which only checks that the bound region is all-zero and the shader
//    returns 0. The sentinel fill is done with the appropriate element size.
//  - makeBufferWithContents / testFillArrayBuffer: output buffers are zero-filled
//    in the bound region and sentinel-filled elsewhere — never pre-filled with
//    expected values.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ============================================================
// Test group
// ============================================================

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,robust_access",
    R"(
Tests to check datatype clamping in shaders is correctly implemented for all indexable types
(vectors, matrices, sized/unsized arrays) visible to shaders in various ways.

TODO: add tests to check that textureLoad operations stay in-bounds.
)");

// ============================================================
// Helper: WGPUStringView from std::string_view
// ============================================================
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ============================================================
// align(n, alignment) — round n up to the next multiple of alignment.
// Ported from webgpu/util/math.ts
// ============================================================
static uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

// ============================================================
// Layout info for scalar and composite types.
// Mirrors kScalarTypeInfo / kVectorContainerTypeInfo / kMatrixContainerTypeLayoutInfo
// from webgpu/shader/types.ts
// ============================================================

struct AlignAndSize {
    uint32_t alignment;
    uint32_t size;
};

// Returns the AlignAndSize for a scalar type, or nullopt for bool (no layout).
static std::optional<AlignAndSize> scalarLayout(const std::string& baseType) {
    if (baseType == "i32") return AlignAndSize{4, 4};
    if (baseType == "u32") return AlignAndSize{4, 4};
    if (baseType == "f32") return AlignAndSize{4, 4};
    if (baseType == "f16") return AlignAndSize{2, 2};
    // bool has no layout
    return std::nullopt;
}

static bool scalarSupportsAtomics(const std::string& baseType) {
    return (baseType == "i32" || baseType == "u32");
}

// Returns vector layout for vecN<baseType>, or nullopt if baseType has no layout.
static std::optional<AlignAndSize> vectorLayout(int n, const std::string& baseType) {
    auto sl = scalarLayout(baseType);
    if (!sl) return std::nullopt;
    // vec3 has alignment = 4 * scalar alignment
    if (n == 3) {
        return AlignAndSize{sl->alignment * 4u, sl->size * 3u};
    }
    return AlignAndSize{sl->alignment * static_cast<uint32_t>(n),
                        sl->size * static_cast<uint32_t>(n)};
}

// Returns matrix layout for matCxR<f16 or f32>.
// Mirrors kMatrixContainerTypeLayoutInfo from types.ts
static std::optional<AlignAndSize> matrixLayout(int cols, int rows, const std::string& baseType) {
    // Only f16 and f32 matrices are valid
    if (baseType != "f16" && baseType != "f32") return std::nullopt;

    // f16 matrix layouts (alignment and size in bytes)
    // matCxR means C columns, R rows; each column is vec<R, f16>
    if (baseType == "f16") {
        // vec2<f16> column: align=4, size=4
        // vec3<f16> column: align=8, size=6 padded to 8 within matrix? Actually upstream:
        // mat2x2<f16>: {4, 8}, mat3x2<f16>: {4,12}, mat4x2<f16>: {4,16}
        // mat2x3<f16>: {8,16}, mat3x3<f16>: {8,24}, mat4x3<f16>: {8,32}
        // mat2x4<f16>: {8,16}, mat3x4<f16>: {8,24}, mat4x4<f16>: {8,32}
        //
        // Pattern: column vec alignment:
        //   rows=2 -> vecR align = 4 (2*2)
        //   rows=3 -> vecR align = 8 (4*2, padded to next pow2)
        //   rows=4 -> vecR align = 8 (4*2)
        // size = cols * col_stride where col_stride = align of col vec
        //   rows=2: col_stride = 4, size = cols*4
        //   rows=3: col_stride = 8, size = cols*8
        //   rows=4: col_stride = 8, size = cols*8
        uint32_t colAlign;
        uint32_t colStride;
        if (rows == 2) { colAlign = 4; colStride = 4; }
        else if (rows == 3) { colAlign = 8; colStride = 8; }
        else { colAlign = 8; colStride = 8; } // rows==4
        return AlignAndSize{colAlign, static_cast<uint32_t>(cols) * colStride};
    }
    // f32 matrix layouts:
    // mat2x2<f32>: {8,16}, mat3x2<f32>: {8,24}, mat4x2<f32>: {8,32}
    // mat2x3<f32>: {16,32}, mat3x3<f32>: {16,48}, mat4x3<f32>: {16,64}
    // mat2x4<f32>: {16,32}, mat3x4<f32>: {16,48}, mat4x4<f32>: {16,64}
    //
    // Pattern: column vec alignment = vecR<f32> alignment
    //   rows=2: vec2<f32> align=8
    //   rows=3: vec3<f32> align=16
    //   rows=4: vec4<f32> align=16
    // col_stride = alignment (since each column is aligned to its vector alignment)
    //   rows=2: col_stride = 8, size = cols*8
    //   rows=3: col_stride = 16, size = cols*16
    //   rows=4: col_stride = 16, size = cols*16
    {
        uint32_t colAlign;
        uint32_t colStride;
        if (rows == 2) { colAlign = 8; colStride = 8; }
        else if (rows == 3) { colAlign = 16; colStride = 16; }
        else { colAlign = 16; colStride = 16; } // rows==4
        return AlignAndSize{colAlign, static_cast<uint32_t>(cols) * colStride};
    }
}

// ============================================================
// TypeInfo: the _kTypeInfo fields we need from generateTypes.
// ============================================================
struct TypeInfo {
    std::string type;               // WGSL type name
    std::string elementBaseType;    // base element type name
    uint32_t arrayLength;           // number of indexed elements
    int innerLength;                // 0 for scalar/vector; row count for matrix
    std::optional<AlignAndSize> layout; // nullopt for bool/workgroup types
    bool supportsAtomics;
    std::vector<std::string> accessSuffixes; // empty = [""]
};

// ============================================================
// generateTypesForParams: C++ port of generateTypes() from types.ts.
// Returns all TypeInfo entries for the given parameter combination.
// ============================================================
static std::vector<TypeInfo> generateTypesForParams(
    const std::string& addressSpace,
    const std::string& baseType,
    const std::string& containerType,
    bool isAtomic)
{
    std::vector<TypeInfo> result;

    auto sl = scalarLayout(baseType);
    bool hostSharable = (sl.has_value());
    bool isStorageOrUniform = (addressSpace == "storage" || addressSpace == "uniform");

    // Storage and uniform require host-sharable types.
    if (isStorageOrUniform && !hostSharable) {
        return result;
    }

    // isAtomic requires supportsAtomics for the base type and scalar/array container.
    if (isAtomic) {
        if (!scalarSupportsAtomics(baseType)) return result;
        if (containerType != "scalar" && containerType != "array") return result;
    }

    std::string scalarType = isAtomic ? ("atomic<" + baseType + ">") : baseType;

    // Scalar
    if (containerType == "scalar") {
        TypeInfo ti;
        ti.type = scalarType;
        ti.elementBaseType = scalarType;
        ti.arrayLength = 1;
        ti.innerLength = 0;
        ti.layout = sl;
        ti.supportsAtomics = scalarSupportsAtomics(baseType);
        result.push_back(std::move(ti));
        return result;
    }

    // Vector
    if (containerType == "vector") {
        static const int kVecSizes[] = {2, 3, 4};
        for (int n : kVecSizes) {
            TypeInfo ti;
            ti.type = "vec" + std::to_string(n) + "<" + scalarType + ">";
            ti.elementBaseType = baseType;
            ti.arrayLength = static_cast<uint32_t>(n);
            ti.innerLength = 0;
            ti.layout = vectorLayout(n, baseType);
            ti.supportsAtomics = false;
            result.push_back(std::move(ti));
        }
        return result;
    }

    // Matrix — only f16 or f32
    if (containerType == "matrix") {
        if (baseType != "f16" && baseType != "f32") return result;
        // Upstream iterates kMatrixContainerTypes in order:
        // mat2x2, mat3x2, mat4x2, mat2x3, mat3x3, mat4x3, mat2x4, mat3x4, mat4x4
        // kMatrixContainerTypeInfo.arrayLength=cols, innerLength=rows
        static const int kMatCols[] = {2,3,4, 2,3,4, 2,3,4};
        static const int kMatRows[] = {2,2,2, 3,3,3, 4,4,4};
        for (int i = 0; i < 9; ++i) {
            int c = kMatCols[i];
            int r = kMatRows[i];
            TypeInfo ti;
            ti.type = "mat" + std::to_string(c) + "x" + std::to_string(r) +
                      "<" + scalarType + ">";
            // elementBaseType is the column vector type
            ti.elementBaseType = "vec" + std::to_string(r) + "<" + scalarType + ">";
            ti.arrayLength = static_cast<uint32_t>(c);
            ti.innerLength = r;
            ti.layout = matrixLayout(c, r, baseType);
            ti.supportsAtomics = false;
            result.push_back(std::move(ti));
        }
        return result;
    }

    // Array
    if (containerType == "array") {
        static const uint32_t kDefaultArrayLength = 3;

        bool validLayoutForAddressSpace = true;
        std::string arrayElemType = scalarType;
        uint32_t arrayElementCount = kDefaultArrayLength;
        bool arraySupportsAtomics = scalarSupportsAtomics(baseType);
        std::optional<AlignAndSize> layout;
        std::vector<std::string> accessSuffixes;

        if (sl) {
            if (addressSpace == "uniform") {
                // Use vec4<baseType> as array element to achieve 16-byte alignment.
                // f16 vec4 is only 8-byte aligned, so skip it (validLayoutForAddressSpace=false).
                arrayElemType = "vec4<" + baseType + ">";
                arraySupportsAtomics = false;
                accessSuffixes = {".x", ".y", ".z", ".w"};
                auto elemLayout = vectorLayout(4, baseType);
                // elemLayout.alignment must be a multiple of 16 for uniform
                if (elemLayout && (elemLayout->alignment % 16 == 0)) {
                    uint32_t count = alignUp(kDefaultArrayLength, 4) / 4;
                    arrayElementCount = count;
                    uint32_t arrayByteSize = count * elemLayout->size;
                    layout = AlignAndSize{elemLayout->alignment, arrayByteSize};
                    validLayoutForAddressSpace = true;
                } else {
                    validLayoutForAddressSpace = false;
                }
            } else {
                // Ordinary case: use scalarType as element type.
                // arrayStride = align(size, alignment)
                uint32_t stride = alignUp(sl->size, sl->alignment);
                uint32_t count = kDefaultArrayLength;
                uint32_t arrayByteSize = count * stride;
                if (addressSpace == "storage") {
                    // Buffer effective binding size must be a multiple of 4.
                    while (arrayByteSize % 4 != 0) {
                        ++count;
                        arrayByteSize = count * stride;
                    }
                }
                arrayElementCount = count;
                layout = AlignAndSize{sl->alignment, arrayByteSize};
            }
        }

        if (!validLayoutForAddressSpace) {
            return result;
        }

        TypeInfo ti;
        ti.type = "array<" + arrayElemType + "," + std::to_string(arrayElementCount) + ">";
        ti.elementBaseType = baseType;
        ti.arrayLength = arrayElementCount;
        ti.innerLength = 0;
        ti.layout = layout;
        ti.supportsAtomics = arraySupportsAtomics;
        ti.accessSuffixes = accessSuffixes;
        result.push_back(ti);

        // Also unsized array for storage address space
        if (addressSpace == "storage") {
            TypeInfo ti2 = ti;
            ti2.type = "array<" + arrayElemType + ">";
            result.push_back(std::move(ti2));
        }
        return result;
    }

    return result;
}

// ============================================================
// supportsAtomics: Ported from webgpu/shader/types.ts
// ============================================================
static bool supportsAtomics(
    const std::string& addressSpace,
    const std::string& storageMode, // empty string if not applicable
    const std::string& containerType)
{
    bool storageReadWrite = (addressSpace == "storage" && storageMode == "read_write");
    bool isWorkgroup = (addressSpace == "workgroup");
    bool scalarOrArray = (containerType == "scalar" || containerType == "array");
    return (storageReadWrite || isWorkgroup) && scalarOrArray;
}

// ============================================================
// supportedScalarTypes: Ported from webgpu/shader/types.ts
// Returns scalar types supported for the given (isAtomic, addressSpace) combo.
// ============================================================
static std::vector<std::string> supportedScalarTypes(
    bool isAtomic,
    const std::string& addressSpace)
{
    static const std::string kAllTypes[] = {"i32", "u32", "f16", "f32", "bool"};
    bool isHostShared = (addressSpace == "storage" || addressSpace == "uniform");

    std::vector<std::string> result;
    for (const auto& t : kAllTypes) {
        // If atomic, only types that support atomics
        if (isAtomic && !scalarSupportsAtomics(t)) continue;
        // Storage/uniform require host-sharable (have a layout)
        if (isHostShared && !scalarLayout(t).has_value()) continue;
        result.push_back(t);
    }
    return result;
}

// ============================================================
// testFillArrayBuffer: fill a byte buffer with sentinel (42 repeated for element
// size), except zero out [zeroByteStart, zeroByteStart+zeroByteCount).
// Ported from testFillArrayBuffer() in robust_access.spec.ts.
// The element size depends on the scalar base type.
// ============================================================
static void testFillBuffer(
    std::vector<uint8_t>& buf,
    const std::string& baseType,
    uint32_t zeroByteStart,
    uint32_t zeroByteCount)
{
    // Determine byte-per-element for sentinel fill (42 in native byte pattern)
    uint32_t bytesPerElement = 4; // default (i32, u32, f32)
    if (baseType == "f16") bytesPerElement = 2;

    // Fill entire buffer with 42 (as appropriate typed array)
    // For u32/i32/f32: fill as uint32 = 42u = 0x0000002A
    // For f16: fill as uint16 = 42u = 0x002A
    // We just fill byte-by-byte with the pattern repeated by element size.
    // uint32_t 42 = {0x2A, 0x00, 0x00, 0x00} (little-endian)
    // uint16_t 42 = {0x2A, 0x00} (little-endian)
    // Note: upstream fills with typed 42 (not 42 bytes), but the exact sentinel
    // value doesn't affect correctness — what matters is that the bound region is
    // zeroed and writes outside the bound region are detected.
    std::vector<uint8_t> pattern;
    if (bytesPerElement == 2) {
        uint16_t v = 42u;
        uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        pattern = {bytes[0], bytes[1]};
    } else {
        uint32_t v = 42u;
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        pattern = {bytes[0], bytes[1], bytes[2], bytes[3]};
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(buf.size()); ++i) {
        buf[i] = pattern[i % bytesPerElement];
    }

    // Zero out the bound region
    for (uint32_t i = zeroByteStart; i < zeroByteStart + zeroByteCount; ++i) {
        buf[i] = 0;
    }
}

// ============================================================
// runShaderTest: Ported from the runShaderTest() helper in robust_access.spec.ts.
//
// Compiles the given WGSL (which must define fn runTest() -> u32),
// binds testBuffer (if non-null) at group(0) binding(0) with the given
// offset/size/dynamicOffset, and a constants UBO + result buffer at group(1).
// Dispatches one workgroup and verifies result.value == 0.
//
// Non-buffer address spaces: pass nullptr for testBuffer.
// ============================================================
static void runShaderTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& enables,
    const std::string& testSource,
    WGPUBuffer testBuffer,           // nullptr if no buffer binding needed
    WGPUBufferBindingType testBindingType, // ignored if testBuffer==nullptr
    bool hasDynamicOffset,           // true if testBuffer uses a dynamic offset
    uint64_t testBufferStaticOffset, // static binding offset
    uint64_t testBufferSize,         // binding size
    uint32_t dynamicOffsetValue      // value used if hasDynamicOffset
)
{
    // Group(1) binding(0): constants buffer — just zero.
    WGPUBufferDescriptor constDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    constDesc.size  = 4;
    constDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer constantsBuffer = t.createBufferTracked(constDesc);

    // Group(1) binding(1): result buffer — zero-filled output.
    WGPUBufferDescriptor resDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    resDesc.size  = 4;
    resDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer resultBuffer = t.createBufferTracked(resDesc);

    // Build the full WGSL source wrapping testSource.
    std::string source =
        enables + "\n"
        "struct Constants {\n"
        "  zero: u32\n"
        "};\n"
        "@group(1) @binding(0) var<uniform> constants: Constants;\n"
        "\n"
        "struct Result {\n"
        "  value: u32\n"
        "};\n"
        "@group(1) @binding(1) var<storage, read_write> result: Result;\n"
        "\n" +
        testSource +
        "\n"
        "@compute @workgroup_size(1)\n"
        "fn main() {\n"
        "  _ = constants.zero; // Ensure constants buffer is statically-accessed\n"
        "  result.value = runTest();\n"
        "}\n";

    WGPUShaderModule shaderModule = t.createShaderModuleTracked(source);

    // Build pipeline layout with explicit bind group layouts.
    // Group 0: test data buffer (or empty BGL if no buffer)
    // Group 1: constants + result

    // BGL for group 0
    WGPUBindGroupLayout bgl0;
    {
        WGPUBindGroupLayoutDescriptor desc0 = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        // NOTE: entry0 must outlive the createBindGroupLayoutTracked() call below
        // (desc0.entries points at it), so it is declared outside the if block.
        WGPUBindGroupLayoutEntry entry0 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        if (testBuffer != nullptr) {
            entry0.binding    = 0;
            entry0.visibility = WGPUShaderStage_Compute;
            entry0.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry0.buffer.type = testBindingType;
            entry0.buffer.hasDynamicOffset = hasDynamicOffset ? WGPU_TRUE : WGPU_FALSE;
            entry0.buffer.minBindingSize = 0;
            desc0.entryCount = 1;
            desc0.entries    = &entry0;
        } else {
            desc0.entryCount = 0;
            desc0.entries    = nullptr;
        }
        bgl0 = t.createBindGroupLayoutTracked(desc0);
    }

    // BGL for group 1: constants (uniform) + result (storage)
    WGPUBindGroupLayout bgl1;
    {
        WGPUBindGroupLayoutEntry entries1[2];
        entries1[0] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries1[0].binding    = 0;
        entries1[0].visibility = WGPUShaderStage_Compute;
        entries1[0].buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entries1[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries1[1] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries1[1].binding    = 1;
        entries1[1].visibility = WGPUShaderStage_Compute;
        entries1[1].buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entries1[1].buffer.type = WGPUBufferBindingType_Storage;

        WGPUBindGroupLayoutDescriptor desc1 = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc1.entryCount = 2;
        desc1.entries    = entries1;
        bgl1 = t.createBindGroupLayoutTracked(desc1);
    }

    // Pipeline layout
    WGPUBindGroupLayout bgls[2] = {bgl0, bgl1};
    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts     = bgls;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    // Compute pipeline
    WGPUComputePipelineDescriptor cpDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cpDesc.layout             = pipelineLayout;
    cpDesc.compute.module     = shaderModule;
    cpDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(cpDesc);

    // Bind group 0: test buffer (or empty)
    WGPUBindGroup group0;
    {
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl0;
        // NOTE: entry0 must outlive the createBindGroupTracked() call below
        // (bgDesc.entries points at it), so it is declared outside the if block.
        WGPUBindGroupEntry entry0 = WGPU_BIND_GROUP_ENTRY_INIT;
        if (testBuffer != nullptr) {
            entry0.binding = 0;
            entry0.buffer  = testBuffer;
            entry0.offset  = testBufferStaticOffset;
            entry0.size    = testBufferSize;
            bgDesc.entryCount = 1;
            bgDesc.entries    = &entry0;
        } else {
            bgDesc.entryCount = 0;
            bgDesc.entries    = nullptr;
        }
        group0 = t.createBindGroupTracked(bgDesc);
    }

    // Bind group 1: constants + result
    WGPUBindGroup group1;
    {
        WGPUBindGroupEntry entries1[2];
        entries1[0] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries1[0].binding = 0;
        entries1[0].buffer  = constantsBuffer;
        entries1[0].offset  = 0;
        entries1[0].size    = 4;

        entries1[1] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries1[1].binding = 1;
        entries1[1].buffer  = resultBuffer;
        entries1[1].offset  = 0;
        entries1[1].size    = 4;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl1;
        bgDesc.entryCount = 2;
        bgDesc.entries    = entries1;
        group1 = t.createBindGroupTracked(bgDesc);
    }

    // NOTE: bgl0/bgl1 come from createBindGroupLayoutTracked(), so the fixture
    // releases them in finalize(). Do NOT release them manually here (that would
    // double-release and crash). Manual release is only for handles obtained via
    // wgpu*PipelineGetBindGroupLayout().

    // Encode + dispatch
    {
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);

        if (hasDynamicOffset && testBuffer != nullptr) {
            wgpuComputePassEncoderSetBindGroup(pass, 0, group0, 1, &dynamicOffsetValue);
        } else {
            wgpuComputePassEncoderSetBindGroup(pass, 0, group0, 0, nullptr);
        }
        wgpuComputePassEncoderSetBindGroup(pass, 1, group1, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    }

    // Verify result.value == 0
    const uint32_t expected = 0u;
    t.expectGPUBufferValuesEqual(resultBuffer, &expected, sizeof(expected));
}

// ============================================================
// linear_memory test
// ============================================================

CTS_TEST(g, "linear_memory")
    .desc(
        R"(For each indexable data type (vec, mat, sized/unsized array, of various scalar types), attempts
    to access (read, write, atomic load/store) a region of memory (buffer or internal) at various
    (signed/unsigned) indices. Checks that the accesses conform to robust access (OOB reads only
    return bound memory, OOB writes don't write OOB).

    TODO: Test in/out storage classes.
    TODO: Test vertex and fragment stages.
    TODO: Test using a dynamic offset instead of a static offset into uniform/storage bindings.
    TODO: Test types like vec2<atomic<i32>>, if that's allowed.
    TODO: Test exprIndexAddon as constexpr.
    TODO: Test exprIndexAddon as pipeline-overridable constant expression.
    TODO: Adjust test logic to support array of f16 in the uniform address space
  )"
    )
    .params([](ParamsBuilder u) {
        // 14 address space / storageMode / access / dynamicOffset combinations.
        // storageMode and dynamicOffset are absent (undefined) for non-storage/uniform spaces.
        return u
            .combineWithParams({
                ParamRecord{{"addressSpace", "storage"}, {"storageMode", "read"},       {"access", "read"},  {"dynamicOffset", false}},
                ParamRecord{{"addressSpace", "storage"}, {"storageMode", "read_write"}, {"access", "read"},  {"dynamicOffset", false}},
                ParamRecord{{"addressSpace", "storage"}, {"storageMode", "read_write"}, {"access", "write"}, {"dynamicOffset", false}},
                ParamRecord{{"addressSpace", "storage"}, {"storageMode", "read"},       {"access", "read"},  {"dynamicOffset", true}},
                ParamRecord{{"addressSpace", "storage"}, {"storageMode", "read_write"}, {"access", "read"},  {"dynamicOffset", true}},
                ParamRecord{{"addressSpace", "storage"}, {"storageMode", "read_write"}, {"access", "write"}, {"dynamicOffset", true}},
                ParamRecord{{"addressSpace", "uniform"}, {"storageMode", Value::undef()}, {"access", "read"},  {"dynamicOffset", false}},
                ParamRecord{{"addressSpace", "uniform"}, {"storageMode", Value::undef()}, {"access", "read"},  {"dynamicOffset", true}},
                ParamRecord{{"addressSpace", "private"},   {"storageMode", Value::undef()}, {"access", "read"},  {"dynamicOffset", Value::undef()}},
                ParamRecord{{"addressSpace", "private"},   {"storageMode", Value::undef()}, {"access", "write"}, {"dynamicOffset", Value::undef()}},
                ParamRecord{{"addressSpace", "function"},  {"storageMode", Value::undef()}, {"access", "read"},  {"dynamicOffset", Value::undef()}},
                ParamRecord{{"addressSpace", "function"},  {"storageMode", Value::undef()}, {"access", "write"}, {"dynamicOffset", Value::undef()}},
                ParamRecord{{"addressSpace", "workgroup"}, {"storageMode", Value::undef()}, {"access", "read"},  {"dynamicOffset", Value::undef()}},
                ParamRecord{{"addressSpace", "workgroup"}, {"storageMode", Value::undef()}, {"access", "write"}, {"dynamicOffset", Value::undef()}},
            })
            .combineWithParams({
                ParamRecord{{"containerType", "array"}},
                ParamRecord{{"containerType", "matrix"}},
                ParamRecord{{"containerType", "vector"}},
            })
            .combineWithParams({
                ParamRecord{{"shadowingMode", "none"}},
                ParamRecord{{"shadowingMode", "module-scope"}},
                ParamRecord{{"shadowingMode", "function-scope"}},
            })
            .expand("isAtomic", [](const ParamRecord& p) -> std::vector<Value> {
                auto addrSpace  = valueAs<std::string>(*findParam(p, "addressSpace"));
                auto storMode   = findParam(p, "storageMode");
                std::string sm  = (storMode && !std::holds_alternative<Value::Undefined>(storMode->data()))
                                  ? valueAs<std::string>(*storMode) : "";
                auto contType   = valueAs<std::string>(*findParam(p, "containerType"));
                if (supportsAtomics(addrSpace, sm, contType)) {
                    return {false, true};
                }
                return {false};
            })
            .expand("baseType", [](const ParamRecord& p) -> std::vector<Value> {
                auto addrSpace = valueAs<std::string>(*findParam(p, "addressSpace"));
                bool isAt      = valueAs<bool>(*findParam(p, "isAtomic"));
                auto types     = supportedScalarTypes(isAt, addrSpace);
                std::vector<Value> result;
                result.reserve(types.size());
                for (const auto& t : types) result.emplace_back(t);
                return result;
            })
            .beginSubcases()
            .expand("type", [](const ParamRecord& p) -> std::vector<Value> {
                auto addrSpace   = valueAs<std::string>(*findParam(p, "addressSpace"));
                auto baseType    = valueAs<std::string>(*findParam(p, "baseType"));
                auto contType    = valueAs<std::string>(*findParam(p, "containerType"));
                bool isAt        = valueAs<bool>(*findParam(p, "isAtomic"));
                auto typeInfos   = generateTypesForParams(addrSpace, baseType, contType, isAt);
                std::vector<Value> result;
                result.reserve(typeInfos.size());
                for (const auto& ti : typeInfos) result.emplace_back(ti.type);
                return result;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace  = t.param<std::string>("addressSpace");
        auto access        = t.param<std::string>("access");
        auto containerType = t.param<std::string>("containerType");
        auto shadowingMode = t.param<std::string>("shadowingMode");
        auto isAtomic      = t.param<bool>("isAtomic");
        auto baseType      = t.param<std::string>("baseType");
        auto type          = t.param<std::string>("type");

        // storageMode and dynamicOffset may be undefined for non-storage/uniform spaces.
        std::string storageMode;
        bool dynamicOffset = false;
        {
            auto smVal = findParam(t.params(), "storageMode");
            if (smVal && !std::holds_alternative<Value::Undefined>(smVal->data())) {
                storageMode = valueAs<std::string>(*smVal);
            }
            auto doVal = findParam(t.params(), "dynamicOffset");
            if (doVal && !std::holds_alternative<Value::Undefined>(doVal->data())) {
                dynamicOffset = valueAs<bool>(*doVal);
            }
        }

        // Runtime-skip f16 if the device does not support it.
        if (baseType == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available");
            }
        }

        // Recover TypeInfo for the current 'type' param.
        auto typeInfos = generateTypesForParams(addressSpace, baseType, containerType, isAtomic);
        const TypeInfo* kTypeInfo = nullptr;
        for (const auto& ti : typeInfos) {
            if (ti.type == type) {
                kTypeInfo = &ti;
                break;
            }
        }
        if (kTypeInfo == nullptr) {
            t.fail("Could not find TypeInfo for type=" + type);
            return;
        }

        // Upstream assert: _kTypeInfo must have arrayLength.
        // For scalar: arrayLength=1; for vector: arrayLength=N; for matrix: arrayLength=C.
        // For array: arrayLength=element count.
        uint32_t arrayLength = kTypeInfo->arrayLength;

        // Constants — mirrors kMaxU32=0xffff_ffff, kMaxI32=0x7fff_ffff, kMinI32=-0x8000_0000
        // from robust_access.spec.ts.
        constexpr uint32_t kMaxU32 = 0xffffffffu;
        constexpr int32_t  kMaxI32 = INT32_MAX;  // 0x7fffffff
        constexpr int32_t  kMinI32 = INT32_MIN;  // -0x80000000

        bool usesCanary = false;
        std::string globalSource;
        std::string testFunctionSource;

        constexpr uint32_t testBufferSize      = 512;
        constexpr uint32_t bufferBindingOffset = 256;
        uint32_t bufferBindingSize = 0;
        bool hasBufferBinding = false;

        // Struct declaration for canary-based address spaces.
        std::string structDecl =
            "struct S {\n"
            "  startCanary: array<u32, 10>,\n"
            "  data: " + type + ",\n"
            "  endCanary: array<u32, 10>,\n"
            "};\n";

        // BGL type needed for buffer binding
        WGPUBufferBindingType testBindingType = WGPUBufferBindingType_Storage;

        if (addressSpace == "storage" || addressSpace == "uniform") {
            // buffer binding case
            if (!kTypeInfo->layout.has_value()) {
                t.skip("type has no layout (bool in storage/uniform not supported)");
                return;
            }
            auto& layout = kTypeInfo->layout.value();
            bufferBindingSize = alignUp(layout.size, layout.alignment);
            hasBufferBinding = true;

            std::string qualifiers =
                (addressSpace == "storage")
                    ? ("storage, " + storageMode)
                    : addressSpace;

            globalSource +=
                "struct TestData {\n"
                "  data: " + type + ",\n"
                "};\n"
                "@group(0) @binding(0) var<" + qualifiers + "> s: TestData;\n";

            if (addressSpace == "uniform") {
                testBindingType = WGPUBufferBindingType_Uniform;
            } else if (storageMode == "read") {
                testBindingType = WGPUBufferBindingType_ReadOnlyStorage;
            } else {
                testBindingType = WGPUBufferBindingType_Storage;
            }

        } else if (addressSpace == "private" || addressSpace == "workgroup") {
            usesCanary = true;
            globalSource += structDecl;
            globalSource += "var<" + addressSpace + "> s: S;\n";

        } else if (addressSpace == "function") {
            usesCanary = true;
            globalSource += structDecl;
            testFunctionSource += "var s: S;\n";
        }

        // Initialize canary if needed
        if (usesCanary) {
            testFunctionSource +=
                "  for (var i = 0u; i < 10u; i = i + 1u) {\n"
                "    s.startCanary[i] = 0xFFFFFFFFu;\n"
                "    s.endCanary[i] = 0xFFFFFFFFu;\n"
                "  }\n";
        }

        // Error-return value counter (like __LINE__ for identifying which check failed).
        // Generates WGSL literals like "0x1001u", "0x1002u", ... matching upstream.
        uint32_t errorReturnValue = 0x1000;
        auto nextErrorReturnValue = [&]() -> std::string {
            ++errorReturnValue;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%xu", static_cast<unsigned>(errorReturnValue));
            return std::string(buf);
        };

        // Build access checks — loop over indexSigned (false, true)
        for (int signedPass = 0; signedPass < 2; ++signedPass) {
            bool indexSigned = (signedPass == 1);

            std::vector<std::string> indicesToTest;
            if (indexSigned) {
                indicesToTest = {
                    "0",
                    std::to_string(static_cast<int32_t>(arrayLength)) + " - 1",
                    "-1",
                    std::to_string(static_cast<int32_t>(arrayLength)),
                    "-1000000",
                    "1000000",
                    std::to_string(kMinI32),
                    std::to_string(kMaxI32),
                };
            } else {
                indicesToTest = {
                    "0u",
                    std::to_string(arrayLength) + "u - 1u",
                    std::to_string(arrayLength) + "u",
                    "1000000u",
                    std::to_string(kMaxU32) + "u",
                    std::to_string(static_cast<uint32_t>(kMaxI32)) + "u",
                };
            }

            std::string indexTypeLiteral = indexSigned ? "0" : "0u";
            std::string indexTypeCast    = indexSigned ? "i32" : "u32";

            std::vector<std::string> exprIndexAddons = {
                "",
                " + " + indexTypeLiteral,
                " + " + indexTypeCast + "(constants.zero)",
            };

            for (const auto& exprIndexAddon : exprIndexAddons) {
                for (const auto& indexToTest : indicesToTest) {
                    testFunctionSource +=
                        "  {\n"
                        "    let index = (" + indexToTest + ")" + exprIndexAddon + ";\n";

                    std::string exprZeroElement = kTypeInfo->elementBaseType + "()";
                    std::string exprElement = "s.data[index]";

                    // Determine suffices for element access
                    const std::vector<std::string>& suffices =
                        kTypeInfo->accessSuffixes.empty()
                            ? std::vector<std::string>{""}
                            : kTypeInfo->accessSuffixes;

                    if (access == "read") {
                        std::string exprLoadElement =
                            isAtomic ? ("atomicLoad(&" + exprElement + ")") : exprElement;

                        for (const auto& x : suffices) {
                            std::string condition =
                                exprLoadElement + x + " != " + exprZeroElement;
                            if (containerType == "matrix") {
                                condition = "any(" + condition + ")";
                            }
                            testFunctionSource +=
                                "    if (" + condition + ") { return " + nextErrorReturnValue() + "; }\n";
                        }
                    } else { // access == "write"
                        if (isAtomic) {
                            testFunctionSource +=
                                "    atomicStore(&s.data[index], " + exprZeroElement + ");\n";
                        } else {
                            for (const auto& x : suffices) {
                                testFunctionSource +=
                                    "    s.data[index]" + x + " = " + exprZeroElement + ";\n";
                            }
                        }
                    }

                    testFunctionSource += "  }\n";
                }
            }
        }

        // Check canaries haven't been modified
        if (usesCanary) {
            testFunctionSource +=
                "  for (var i = 0u; i < 10u; i = i + 1u) {\n"
                "    if (s.startCanary[i] != 0xFFFFFFFFu) {\n"
                "      return " + nextErrorReturnValue() + ";\n"
                "    }\n"
                "    if (s.endCanary[i] != 0xFFFFFFFFu) {\n"
                "      return " + nextErrorReturnValue() + ";\n"
                "    }\n"
                "  }\n";
        }

        // Shadowing declarations
        std::string moduleScopeShadowDecls;
        std::string functionScopeShadowDecls;

        if (shadowingMode == "module-scope") {
            moduleScopeShadowDecls =
                "var<private> min = 0;\n"
                "var<private> max = 0;\n"
                "var<private> arrayLength = 0;\n";
            functionScopeShadowDecls =
                "  _ = min;\n"
                "  _ = max;\n"
                "  _ = arrayLength;\n";
        } else if (shadowingMode == "function-scope") {
            functionScopeShadowDecls =
                "  let min = 0;\n"
                "  let max = 0;\n"
                "  let arrayLength = 0;\n";
        }

        // Assemble the full test source
        std::string testSource =
            globalSource + "\n" +
            moduleScopeShadowDecls + "\n"
            "fn runTest() -> u32 {\n" +
            functionScopeShadowDecls + "\n" +
            testFunctionSource +
            "  return 0u;\n"
            "}\n";

        std::string enables = (baseType == "f16") ? "enable f16;" : "";

        // Run the test
        if (hasBufferBinding && baseType != "bool") {
            // Create a buffer with sentinel in non-bound regions and zeros in the bound region.
            std::vector<uint8_t> expectedData(testBufferSize, 0);
            uint32_t bufferBindingEnd = bufferBindingOffset + bufferBindingSize;

            testFillBuffer(expectedData, baseType,
                           bufferBindingOffset, bufferBindingSize);

            // Create the test buffer with the sentinel/zero data.
            WGPUBuffer testBuffer = t.makeBufferWithContents(
                expectedData.data(),
                expectedData.size(),
                WGPUBufferUsage_CopySrc | WGPUBufferUsage_Uniform |
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);

            // Run shader, accessing the buffer
            uint64_t staticOffset  = dynamicOffset ? 0u : bufferBindingOffset;
            uint32_t dynamicOffsetVal = dynamicOffset ? bufferBindingOffset : 0u;

            runShaderTest(t, enables, testSource,
                          testBuffer, testBindingType,
                          dynamicOffset,
                          staticOffset,
                          bufferBindingSize,
                          dynamicOffsetVal);

            // Check that content outside the allowed area didn't change.
            // [0, bufferBindingOffset) and [bufferBindingEnd, testBufferSize)
            t.expectGPUBufferValuesEqual(
                testBuffer,
                expectedData.data(),
                bufferBindingOffset,
                /*srcByteOffset=*/0);
            t.expectGPUBufferValuesEqual(
                testBuffer,
                expectedData.data() + bufferBindingEnd,
                testBufferSize - bufferBindingEnd,
                /*srcByteOffset=*/bufferBindingEnd);
        } else {
            // No buffer binding — private/function/workgroup address spaces
            // or bool (no layout).
            runShaderTest(t, enables, testSource,
                          nullptr, WGPUBufferBindingType_Storage,
                          false, 0, 0, 0);
        }
    });

} // namespace
