// Ported from gpuweb/cts src/webgpu/shader/execution/expression/access/matrix/index.spec.ts
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix indexing expressions. m[c] extracts a column vector (vecR); m[c][r]
// extracts a single scalar element. The element type is a concrete float (f32/f16) or abstract
// float; the index is an i32/u32 scalar. Value-EXACT: each result element is the real stored
// element, compared bit-for-bit (no tolerance). f16 element type is skipped when 'shader-f16' is
// absent. The abstract_float variants are const-input-source only.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::access;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,access,matrix,index",
    "Execution Tests for matrix indexing expressions");

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

// Builds the index scalar (i32 or u32) for value i.
Scalar makeIndex(ScalarKind kind, int i) {
    return kind == ScalarKind::U32 ? u32(static_cast<uint32_t>(i)) : i32(i);
}

// Expression builders.
ExpressionBuilder colBuilder() {
    return [](const std::vector<std::string>& ops) { return ops[0] + "[" + ops[1] + "]"; };
}
ExpressionBuilder elemBuilder() {
    return [](const std::vector<std::string>& ops) {
        return ops[0] + "[" + ops[1] + "][" + ops[2] + "]";
    };
}
ExpressionBuilder abstractColBuilder() {
    return [](const std::vector<std::string>& ops) {
        return ops[0] + "[" + ops[1] + "] / 0x100000000";
    };
}
ExpressionBuilder abstractElemBuilder() {
    return [](const std::vector<std::string>& ops) {
        return ops[0] + "[" + ops[1] + "][" + ops[2] + "] / 0x100000000";
    };
}

ParamsBuilder concreteParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("elementType", {"f32", "f16"})
        .combine("indexType", {"i32", "u32"})
        .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                             Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}

ParamsBuilder abstractParams(ParamsBuilder u) {
    return u.combine("indexType", {"i32", "u32"})
        .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                             Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}

// Column-major flattened matrix payload with element (c,r) = (c+1)*10 + (r+1).
std::vector<Scalar> buildMatrix(ScalarKind elemKind, int cols, int rows) {
    std::vector<Scalar> out;
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            out.push_back(makeConcrete(elemKind, (c + 1) * 10 + (r + 1)));
        }
    }
    return out;
}

// Column-major abstract matrix payload (value * 0x100000000).
std::vector<Scalar> buildAbstractMatrix(int cols, int rows) {
    std::vector<Scalar> out;
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            out.push_back(makeAbstractScaled(ScalarKind::AbstractFloat, (c + 1) * 10 + (r + 1)));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// concrete_float_column: m[c] -> vecR
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "concrete_float_column")
    .desc("Test indexing a column vector from a concrete matrix")
    .params(concreteParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }

        const std::vector<Scalar> mat = buildMatrix(elemKind, cols, rows);
        const ExprType matTy = matType(cols, rows, elemKind);
        const ExprType colTy = vecType(rows, elemKind);

        std::vector<Case> cases;
        for (int c = 0; c < cols; ++c) {
            std::vector<Scalar> col(mat.begin() + c * rows, mat.begin() + c * rows + rows);
            cases.push_back({{CaseValue::composite(mat), CaseValue(makeIndex(idxKind, c))},
                             CaseValue::vec(col)});
        }

        run(t, colBuilder(), {matTy, scalarType(idxKind)}, colTy,
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

// ---------------------------------------------------------------------------
// concrete_float_element: m[c][r] -> scalar
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "concrete_float_element")
    .desc("Test indexing a single element from a concrete matrix")
    .params(concreteParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }

        const std::vector<Scalar> mat = buildMatrix(elemKind, cols, rows);
        const ExprType matTy = matType(cols, rows, elemKind);

        std::vector<Case> cases;
        for (int c = 0; c < cols; ++c) {
            for (int r = 0; r < rows; ++r) {
                cases.push_back({{CaseValue::composite(mat), CaseValue(makeIndex(idxKind, c)),
                                  CaseValue(makeIndex(idxKind, r))},
                                 CaseValue(mat[static_cast<size_t>(c * rows + r)])});
            }
        }

        run(t, elemBuilder(), {matTy, scalarType(idxKind), scalarType(idxKind)},
            scalarType(elemKind), inputSourceFromParam(t.param<std::string>("inputSource")), 0,
            cases);
    });

// ---------------------------------------------------------------------------
// abstract_float_column: m[c] -> vecR<f32> (const only)
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "abstract_float_column")
    .desc("Test indexing a column vector from an abstract-float matrix")
    .params(abstractParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));

        const std::vector<Scalar> mat = buildAbstractMatrix(cols, rows);
        const ExprType matTy = matType(cols, rows, ScalarKind::AbstractFloat);
        const ExprType colTy = vecType(rows, ScalarKind::F32);

        std::vector<Case> cases;
        for (int c = 0; c < cols; ++c) {
            std::vector<Scalar> col;
            for (int r = 0; r < rows; ++r) {
                col.push_back(f32FromInt((c + 1) * 10 + (r + 1)));
            }
            cases.push_back({{CaseValue::composite(mat), CaseValue(makeIndex(idxKind, c))},
                             CaseValue::vec(col)});
        }

        run(t, abstractColBuilder(), {matTy, scalarType(idxKind)}, colTy, InputSource::Const, 0,
            cases);
    });

// ---------------------------------------------------------------------------
// abstract_float_element: m[c][r] -> f32 (const only)
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "abstract_float_element")
    .desc("Test indexing a single element from an abstract-float matrix")
    .params(abstractParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));

        const std::vector<Scalar> mat = buildAbstractMatrix(cols, rows);
        const ExprType matTy = matType(cols, rows, ScalarKind::AbstractFloat);

        std::vector<Case> cases;
        for (int c = 0; c < cols; ++c) {
            for (int r = 0; r < rows; ++r) {
                cases.push_back({{CaseValue::composite(mat), CaseValue(makeIndex(idxKind, c)),
                                  CaseValue(makeIndex(idxKind, r))},
                                 CaseValue(f32FromInt((c + 1) * 10 + (r + 1)))});
            }
        }

        run(t, abstractElemBuilder(), {matTy, scalarType(idxKind), scalarType(idxKind)},
            scalarType(ScalarKind::F32), InputSource::Const, 0, cases);
    });

// ---------------------------------------------------------------------------
// non_const_index: custom compute (matrix indexed by a non-const index)
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "non_const_index")
    .desc("Test indexing of a matrix using non-const index")
    .params([](ParamsBuilder u) {
        return u
            .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                 Value(static_cast<int64_t>(4))})
            .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                              Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        const int total = cols * rows;

        // m = matCxRf(0, 1, 2, ..., cols*rows-1) (column-major constructor args).
        std::string valuesJoined;
        for (int i = 0; i < total; ++i) {
            if (i > 0) {
                valuesJoined += ", ";
            }
            valuesJoined += std::to_string(i);
        }

        std::string wgsl;
        wgsl += "@group(0) @binding(0) var<storage, read_write> output : array<f32, " +
                std::to_string(total) + ">;\n";
        wgsl += "@compute @workgroup_size(" + std::to_string(cols) + ", " + std::to_string(rows) +
                ")\n";
        wgsl += "fn main(@builtin(local_invocation_id) invocation_id : vec3<u32>) {\n";
        wgsl += "  let m = mat" + std::to_string(cols) + "x" + std::to_string(rows) + "f(" +
                valuesJoined + ");\n";
        wgsl += "  output[invocation_id.x*" + std::to_string(rows) +
                " + invocation_id.y] = m[invocation_id.x][invocation_id.y];\n";
        wgsl += "}\n";

        WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
        WGPUComputePipelineDescriptor pdesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pdesc.layout = nullptr;
        pdesc.compute.module = module;
        pdesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pdesc);

        // Expected: output[c*rows + r] = m[c][r] = (c*rows + r) (the constructor value at that
        // column-major slot). f32 packed tightly (array<f32, N>).
        std::vector<uint8_t> expected(static_cast<size_t>(total) * 4, 0);
        for (int i = 0; i < total; ++i) {
            const Scalar s = makeConcrete(ScalarKind::F32, i);
            std::memcpy(&expected[static_cast<size_t>(i) * 4], &s.bits, 4);
        }

        WGPUBufferDescriptor odesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        odesc.size = static_cast<uint64_t>(total) * 4;
        odesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(odesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry e0 = WGPU_BIND_GROUP_ENTRY_INIT;
        e0.binding = 0;
        e0.buffer = outputBuffer;
        e0.size = odesc.size;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries = &e0;
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

        t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size());
    });

} // namespace
