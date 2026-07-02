// Ported from gpuweb/cts src/webgpu/shader/execution/expression/access/array/index.spec.ts
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for array indexing expressions (a[i]). The operand is an array<T, N> of concrete
// scalars/bools/vectors/matrices or abstract scalars; the index is an i32/u32 scalar; the result is
// the indexed element. Value-EXACT: the expected result is the real stored element, compared
// bit-for-bit (no tolerance). f16 element types are skipped when 'shader-f16' is absent. The
// abstract_scalar variant is const-input-source only. The 'uniform' input source requires the
// 'uniform_buffer_standard_layout' WGSL language feature for non-16-aligned array strides; matching
// upstream's t.skipIfLanguageFeatureNotSupported, those cases are skipped when unsupported.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "common/webgpu/backend.h"
#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::access;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,access,array,index",
    "Execution Tests for array indexing expressions");

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

// Mirrors upstream t.hasLanguageFeature('uniform_buffer_standard_layout'). WGSL language features
// are an instance-level property, so a one-shot probe instance gives the same answer as the
// harness's shared instance. Dawn and yawgpu both export wgpuInstanceHasWGSLLanguageFeature and
// define WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout, so both query the real
// instance-level answer. wgpu-native does not export the query, so the feature is treated as
// unsupported there (the affected uniform cases skip, matching upstream behavior on
// implementations without the feature).
bool hasUniformBufferStandardLayout() {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    static const bool supported = [] {
        WGPUInstance probe = createInstance();
        if (probe == nullptr) {
            return false;
        }
        const bool has = wgpuInstanceHasWGSLLanguageFeature(
                             probe, WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout) != 0u;
        wgpuInstanceRelease(probe);
        return has;
    }();
    return supported;
#else
    return false;
#endif
}

// Builds the index scalar (i32 or u32) for value i.
Scalar makeIndex(ScalarKind kind, int i) {
    return kind == ScalarKind::U32 ? u32(static_cast<uint32_t>(i)) : i32(i);
}

// Expression builder for a[i]: ops[0][ops[1]].
ExpressionBuilder indexBuilder() {
    return [](const std::vector<std::string>& ops) { return ops[0] + "[" + ops[1] + "]"; };
}

// Expression builder for the abstract variant: (a[i]) / 0x100000000.
ExpressionBuilder abstractIndexBuilder() {
    return [](const std::vector<std::string>& ops) {
        return ops[0] + "[" + ops[1] + "] / 0x100000000";
    };
}

// ---------------------------------------------------------------------------
// concrete_scalar: array<scalar, 3>
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "concrete_scalar")
    .desc("Test indexing of an array of concrete scalars")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("elementType", {"i32", "u32", "f32", "f16"})
            .combine("indexType", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const InputSource source = inputSourceFromParam(t.param<std::string>("inputSource"));
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }
        if (source == InputSource::Uniform && !hasUniformBufferStandardLayout()) {
            t.skip("uniform array stride requires uniform_buffer_standard_layout");
        }

        const ExprType elemTy = scalarType(elemKind);
        std::vector<Case> cases;
        for (int base = 0; base < 3; ++base) {
            const int firstVal = (base + 1) * 10; // 10, 20, 30
            std::vector<Scalar> arr = {makeConcrete(elemKind, firstVal),
                                       makeConcrete(elemKind, firstVal + 1),
                                       makeConcrete(elemKind, firstVal + 2)};
            const int idx = base; // 0, 1, 2
            cases.push_back({{CaseValue::composite(arr), CaseValue(makeIndex(idxKind, idx))},
                             CaseValue(arr[static_cast<size_t>(idx)])});
        }

        run(t, indexBuilder(), {arrayType(3, elemTy), scalarType(idxKind)}, elemTy, source, 0,
            cases);
    });

// ---------------------------------------------------------------------------
// bool: array<bool, 3>
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "bool")
    .desc("Test indexing of an array of booleans")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("indexType", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const InputSource source = inputSourceFromParam(t.param<std::string>("inputSource"));
        if (source == InputSource::Uniform && !hasUniformBufferStandardLayout()) {
            t.skip("uniform array stride requires uniform_buffer_standard_layout");
        }

        const ExprType elemTy = scalarType(ScalarKind::Bool);
        // array(True, False, True)
        std::vector<Scalar> arr = {boolean(true), boolean(false), boolean(true)};
        std::vector<Case> cases;
        for (int idx = 0; idx < 3; ++idx) {
            cases.push_back({{CaseValue::composite(arr), CaseValue(makeIndex(idxKind, idx))},
                             CaseValue(arr[static_cast<size_t>(idx)])});
        }

        run(t, indexBuilder(), {arrayType(3, elemTy), scalarType(idxKind)}, elemTy, source, 0,
            cases);
    });

// ---------------------------------------------------------------------------
// abstract_scalar: array<abstract, 3> (const only)
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "abstract_scalar")
    .desc("Test indexing of an array of abstract scalars")
    .params([](ParamsBuilder u) {
        return u.combine("elementType", {"abstract-int", "abstract-float"})
            .combine("indexType", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));

        const ExprType elemTy = scalarType(elemKind);
        std::vector<Case> cases;
        // Upstream rows: base scales 0x10/0x11/0x12 (idx 0), 0x20/0x21/0x22 (idx 1),
        // 0x30/0x31/0x32 (idx 2); element = scale * 0x100000000, expected = f32(scale at idx).
        const int bases[3] = {0x10, 0x20, 0x30};
        for (int row = 0; row < 3; ++row) {
            const int b = bases[row];
            std::vector<Scalar> arr = {makeAbstractScaled(elemKind, b),
                                       makeAbstractScaled(elemKind, b + 1),
                                       makeAbstractScaled(elemKind, b + 2)};
            const int idx = row;
            cases.push_back({{CaseValue::composite(arr), CaseValue(makeIndex(idxKind, idx))},
                             CaseValue(f32FromInt(b + idx))});
        }

        run(t, abstractIndexBuilder(), {arrayType(3, elemTy), scalarType(idxKind)},
            scalarType(ScalarKind::F32), InputSource::Const, 0, cases);
    });

// ---------------------------------------------------------------------------
// vector: array<vec4<T>, 3>
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "vector")
    .desc("Test indexing of an array of vectors")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .expand("elementType",
                    [](const ParamRecord&) {
                        return std::vector<Value>{Value("vec4i"), Value("vec4u"), Value("vec4f"),
                                                  Value("vec4h")};
                    })
            .combine("indexType", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string elementType = t.param<std::string>("elementType");
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const InputSource source = inputSourceFromParam(t.param<std::string>("inputSource"));
        // Element scalar kind: vec4{i,u,f,h}.
        ScalarKind elemKind = ScalarKind::I32;
        if (elementType == "vec4u") {
            elemKind = ScalarKind::U32;
        } else if (elementType == "vec4f") {
            elemKind = ScalarKind::F32;
        } else if (elementType == "vec4h") {
            elemKind = ScalarKind::F16;
        }
        if (elementType == "vec4h") {
            skipIfNoF16(t);
            if (source == InputSource::Uniform && !hasUniformBufferStandardLayout()) {
                t.skip("uniform array stride requires uniform_buffer_standard_layout");
            }
        }

        const ExprType elemTy = vecType(4, elemKind);
        std::vector<Case> cases;
        // Upstream base values: 0x10.. for idx 0, 0x20.. for idx 1, 0x30.. for idx 2.
        const int bases[3] = {0x10, 0x20, 0x30};
        for (int row = 0; row < 3; ++row) {
            const int b = bases[row];
            // Three vec4 elements with consecutive values b..b+11.
            std::vector<Scalar> arr;
            for (int e = 0; e < 12; ++e) {
                arr.push_back(makeConcrete(elemKind, b + e));
            }
            const int idx = row;
            // Expected = the indexed vec4 (the idx-th group of 4).
            std::vector<Scalar> expected(arr.begin() + idx * 4, arr.begin() + idx * 4 + 4);
            cases.push_back({{CaseValue::composite(arr), CaseValue(makeIndex(idxKind, idx))},
                             CaseValue::vec(expected)});
        }

        run(t, indexBuilder(), {arrayType(3, elemTy), scalarType(idxKind)}, elemTy, source, 0,
            cases);
    });

// ---------------------------------------------------------------------------
// matrix: array<matCxR, 3>
// ---------------------------------------------------------------------------
// True iff a matCxR of the given element kind has a 16-byte-aligned stride (align(size, alignment)).
bool matrixStride16Aligned(ScalarKind elemKind, int cols, int rows) {
    const uint32_t eb = elemKind == ScalarKind::F16 ? 2u : 4u;
    const uint32_t n = rows == 3 ? 4u : static_cast<uint32_t>(rows);
    const uint32_t matAlign = eb * n;
    const uint32_t matSize = matAlign * static_cast<uint32_t>(cols);
    const uint32_t strideAligned = ((matSize + matAlign - 1) / matAlign) * matAlign;
    return (strideAligned & 15u) == 0;
}

CTS_TEST(testGroup, "matrix")
    .desc("Test indexing of an array of matrices")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("elementType", {"f16", "f32"})
            .beginSubcases()
            .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                 Value(static_cast<int64_t>(4))})
            .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                              Value(static_cast<int64_t>(4))})
            .combine("indexType", {"i32", "u32"})
            .filter([](const ParamRecord& p) {
                // Upstream: keep all non-uniform; for uniform keep only 16-aligned matrix strides.
                const Value* src = findParam(p, "inputSource");
                if (src == nullptr || valueAs<std::string>(*src) != "uniform") {
                    return true;
                }
                const ScalarKind ek = elementKind(valueAs<std::string>(*findParam(p, "elementType")));
                const int c = static_cast<int>(valueAs<int64_t>(*findParam(p, "columns")));
                const int r = static_cast<int>(valueAs<int64_t>(*findParam(p, "rows")));
                return matrixStride16Aligned(ek, c, r);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        const InputSource source = inputSourceFromParam(t.param<std::string>("inputSource"));
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }
        // Upstream additionally skips non-16-aligned uniform matrices when the language feature is
        // absent; the .filter() already removed those, so this guard is a defensive no-op.
        if (source == InputSource::Uniform && !matrixStride16Aligned(elemKind, cols, rows) &&
            !hasUniformBufferStandardLayout()) {
            t.skip("uniform matrix array stride requires uniform_buffer_standard_layout");
        }

        const ExprType matTy = matType(cols, rows, elemKind);
        // Build 3 matrices: element (c,r) of matrix `index` = index*100 + c*10 + r, where upstream's
        // create() consumes a row-major-by-(c,r) flat list and slices it column-major into columns.
        // Upstream's nested loop is `for c in 0..rows for r in 0..cols push(index*100 + c*10 + r)`,
        // i.e. it fills cols*rows values; create() then reshapes to columns of length rows.
        auto buildMat = [&](int index) {
            // Replicate upstream's create() reshape: a flat list of cols*rows values (filled by the
            // upstream double loop) is sliced into `cols` columns of `rows` values each.
            std::vector<int> flat;
            for (int a = 0; a < rows; ++a) {
                for (int b = 0; b < cols; ++b) {
                    flat.push_back(index * 100 + a * 10 + b);
                }
            }
            // Column-major flatten: column c uses flat[c*rows .. c*rows+rows).
            std::vector<Scalar> colMajor;
            for (int c = 0; c < cols; ++c) {
                for (int r = 0; r < rows; ++r) {
                    colMajor.push_back(
                        makeConcrete(elemKind, flat[static_cast<size_t>(c * rows + r)]));
                }
            }
            return colMajor;
        };

        // array(mat0, mat1, mat2): flatten all three matrices' column-major payloads in order.
        std::vector<Scalar> arr;
        for (int m = 0; m < 3; ++m) {
            std::vector<Scalar> mat = buildMat(m);
            arr.insert(arr.end(), mat.begin(), mat.end());
        }
        // Indexed result = matrix 1.
        std::vector<Scalar> expected = buildMat(1);

        std::vector<Case> cases;
        cases.push_back({{CaseValue::composite(arr), CaseValue(makeIndex(idxKind, 1))},
                         CaseValue::composite(expected)});

        run(t, indexBuilder(), {arrayType(3, matTy), scalarType(idxKind)}, matTy, source, 0, cases);
    });

// ---------------------------------------------------------------------------
// runtime_sized: custom 3-buffer compute (runtime-sized array indexing)
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "runtime_sized")
    .desc("Test indexing of a runtime sized array")
    .params([](ParamsBuilder u) {
        return u
            .combine("elementType", {"i32", "u32", "f32", "f16", "vec4i", "vec2u", "vec3f", "vec2h"})
            .combine("indexType", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string elementType = t.param<std::string>("elementType");
        const std::string indexType = t.param<std::string>("indexType");

        // Decode the element type into scalar kind + width.
        ScalarKind elemKind = ScalarKind::I32;
        int width = 1;
        if (elementType == "i32") {
            elemKind = ScalarKind::I32;
        } else if (elementType == "u32") {
            elemKind = ScalarKind::U32;
        } else if (elementType == "f32") {
            elemKind = ScalarKind::F32;
        } else if (elementType == "f16") {
            elemKind = ScalarKind::F16;
        } else if (elementType == "vec4i") {
            elemKind = ScalarKind::I32;
            width = 4;
        } else if (elementType == "vec2u") {
            elemKind = ScalarKind::U32;
            width = 2;
        } else if (elementType == "vec3f") {
            elemKind = ScalarKind::F32;
            width = 3;
        } else if (elementType == "vec2h") {
            elemKind = ScalarKind::F16;
            width = 2;
        }
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }

        const ExprType elemTy = width == 1 ? scalarType(elemKind) : vecType(width, elemKind);
        const ScalarKind idxKind = elementKind(indexType);

        // WGSL type spellings.
        auto wgslType = [](ScalarKind k, int w) -> std::string {
            const char* base = k == ScalarKind::I32   ? "i32"
                               : k == ScalarKind::U32  ? "u32"
                               : k == ScalarKind::F16  ? "f16"
                                                       : "f32";
            if (w == 1) {
                return base;
            }
            return "vec" + std::to_string(w) + "<" + std::string(base) + ">";
        };
        const std::string valueTypeName = wgslType(elemKind, width);
        const std::string indexTypeName = wgslType(idxKind, 1);

        std::string wgsl;
        if (elemKind == ScalarKind::F16) {
            wgsl += "enable f16;\n";
        }
        wgsl += "@group(0) @binding(0) var<storage, read> input_values : array<" + valueTypeName +
                ">;\n";
        wgsl += "@group(0) @binding(1) var<storage, read> input_indices : array<" + indexTypeName +
                ">;\n";
        wgsl += "@group(0) @binding(2) var<storage, read_write> output : array<" + valueTypeName +
                ">;\n";
        wgsl +=
            "@compute @workgroup_size(16)\n"
            "fn main(@builtin(local_invocation_index) invocation_id : u32) {\n"
            "  let index = input_indices[invocation_id];\n"
            "  output[invocation_id] = input_values[index];\n"
            "}\n";

        WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
        WGPUComputePipelineDescriptor pdesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pdesc.layout = nullptr;
        pdesc.compute.module = module;
        pdesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pdesc);

        const int values[16] = {2,  3,  5,  7,  11, 13, 17, 19,
                                23, 29, 31, 37, 41, 43, 47, 53};
        const int indices[16] = {9, 0, 14, 10, 12, 4, 15, 3, 5, 6, 11, 2, 8, 13, 7, 1};

        // Element byte size and array stride (align(size, alignment)). For vec3f: size 12, align 16.
        const uint32_t eb = elemKind == ScalarKind::F16 ? 2u : 4u;
        const uint32_t n = width == 3 ? 4u : static_cast<uint32_t>(width);
        const uint32_t elemSize = eb * (width == 1 ? 1u : static_cast<uint32_t>(width));
        const uint32_t elemAlign = width == 1 ? eb : eb * n;
        const uint32_t elemStride = ((elemSize + elemAlign - 1) / elemAlign) * elemAlign;
        auto bufSize = [&](int count) { return static_cast<uint64_t>(elemStride) * count; };

        // Serialize the value array (16 elements, each `width` copies of values[i]).
        auto makeValueBytes = [&](const int* vals) {
            std::vector<uint8_t> bytes(static_cast<size_t>(bufSize(16)), 0);
            for (int i = 0; i < 16; ++i) {
                Scalar s = makeConcrete(elemKind, vals[i]);
                const uint32_t baseOff = static_cast<uint32_t>(i) * elemStride;
                for (int e = 0; e < width; ++e) {
                    std::memcpy(&bytes[baseOff + static_cast<uint32_t>(e) * eb], &s.bits, eb);
                }
            }
            return bytes;
        };
        // Index array: 16 scalar indices (4 bytes each).
        std::vector<uint8_t> indexBytes(16 * 4, 0);
        for (int i = 0; i < 16; ++i) {
            Scalar s = makeIndex(idxKind, indices[i]);
            std::memcpy(&indexBytes[static_cast<size_t>(i) * 4], &s.bits, 4);
        }
        // Expected: output[i] = values[indices[i]].
        std::vector<int> expectedVals(16);
        for (int i = 0; i < 16; ++i) {
            expectedVals[static_cast<size_t>(i)] = values[indices[i]];
        }
        std::vector<uint8_t> expectedBytes = makeValueBytes(expectedVals.data());

        std::vector<uint8_t> valueBytes = makeValueBytes(values);
        WGPUBuffer valueBuffer = t.makeBufferWithContents(
            valueBytes.data(), valueBytes.size(), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBuffer indexBuffer = t.makeBufferWithContents(
            indexBytes.data(), indexBytes.size(), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBufferDescriptor odesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        odesc.size = bufSize(16);
        odesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(odesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry e0 = WGPU_BIND_GROUP_ENTRY_INIT;
        e0.binding = 0;
        e0.buffer = valueBuffer;
        e0.size = valueBytes.size();
        WGPUBindGroupEntry e1 = WGPU_BIND_GROUP_ENTRY_INIT;
        e1.binding = 1;
        e1.buffer = indexBuffer;
        e1.size = indexBytes.size();
        WGPUBindGroupEntry e2 = WGPU_BIND_GROUP_ENTRY_INIT;
        e2.binding = 2;
        e2.buffer = outputBuffer;
        e2.size = odesc.size;
        WGPUBindGroupEntry bgEntries[3] = {e0, e1, e2};
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 3;
        bgDesc.entries = bgEntries;
        WGPUBindGroup group = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        t.expectGPUBufferValuesEqual(outputBuffer, expectedBytes.data(), expectedBytes.size());
    });

} // namespace
