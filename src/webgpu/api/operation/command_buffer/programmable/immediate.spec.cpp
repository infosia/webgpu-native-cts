// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/programmable/immediate.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 The webgpu-native-cts Authors, BSD-3-Clause.
//
// Operation tests for immediate data usage in RenderPassEncoder, ComputePassEncoder, and
// RenderBundleEncoder.
//
// PORTING NOTE — runtime-skipped on every backend at this revision.
//   Every test in this file exercises the immediate-data write path
//   (encoder.setImmediates). That path requires the C entry points
//   wgpuComputePassEncoderSetImmediates / wgpuRenderPassEncoderSetImmediates /
//   wgpuRenderBundleEncoderSetImmediates. Those symbols are *declared* in the
//   webgpu-headers but are NOT exported by any of the three backends (yawgpu,
//   wgpu-native, Dawn) at the pinned revision — verified with `nm` on the
//   yawgpu Metal/Vulkan dylibs (only an internal naga `resolve_immediates`
//   symbol exists) and by the sibling validation port
//   src/webgpu/api/validation/encoding/cmds/setImmediates.spec.cpp, which is
//   stubbed for the same reason. Referencing those symbols here would break the
//   link of the entire `cts` binary on every backend, so this file does NOT
//   reference them.
//
//   The full upstream case/subcase parameter space is preserved (so query
//   identity matches upstream and the cases re-activate automatically once a
//   backend ships SetImmediates). Each test body applies the spec-mandated
//   `maxImmediateSize == 0` runtime gate (Dawn / wgpu-native report 0; yawgpu
//   reports 64) and then skips because the SetImmediates write path is
//   unavailable. File status: partial (registered + skipped, not executed).

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,programmable,immediate",
    "Operation tests for immediate data usage in RenderPassEncoder, ComputePassEncoder, and "
    "RenderBundleEncoder.");

// kProgrammableEncoderTypes = ['compute pass', 'render pass', 'render bundle']
// (compute pass + kRenderEncodeTypes). See webgpu/util/command_buffer_maker.ts.
const std::vector<Value> kProgrammableEncoderTypes = {
    "compute pass", "render pass", "render bundle"};

// HostSharableTypes = ['i32', 'u32', 'f16', 'f32']. See webgpu/shader/types.ts.
// kVectorContainerTypes = ['vec2', 'vec3', 'vec4'].
//
// kTypedArrayBufferViewKeys (constructor names, in upstream order). See common/util/util.ts.
const std::vector<Value> kTypedArrayBufferViewKeys = {
    "Uint8Array",  "Uint8ClampedArray", "Uint16Array",   "Uint32Array",
    "Int8Array",   "Int16Array",        "Int32Array",    "Float16Array",
    "Float32Array", "Float64Array",     "BigInt64Array", "BigUint64Array"};

// ---------------------------------------------------------------------------
// Runtime gate shared by every test.
//
// Upstream gates the whole group in init() via supportsImmediateData(gpu).
// In native we mirror that with the maxImmediateSize limit: Dawn / wgpu-native
// advertise 0, yawgpu advertises 64. Additionally, the SetImmediates write path
// is not exported by any backend yet, so even on yawgpu the operation cannot run.
// ---------------------------------------------------------------------------
[[noreturn]] void skipImmediateUnsupported(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxImmediateSize = limits.maxImmediateSize;
    if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
        t.skip("Immediate data not supported (maxImmediateSize == 0)");
    }
    // Backend advertises immediate data (yawgpu), but the SetImmediates write
    // entry points are not exported by any backend at this revision, so the
    // operation tests cannot drive the immediate-data path. See file header.
    t.skip("encoder.setImmediates not exported by any backend at this revision");
}

// ---------------------------------------------------------------------------
// basic_execution
// ---------------------------------------------------------------------------
// Upstream params:
//   u.combine('encoderType', kProgrammableEncoderTypes).expandWithParams(function*() {
//     for (const s of HostSharableTypes) yield { dataType: s, scalarType: s, vectorSize: 1 };
//     for (const v of kVectorContainerTypes) {
//       const size = parseInt(v[3]);
//       for (const s of HostSharableTypes) yield { dataType: `${v}<${s}>`, scalarType: s, vectorSize: size };
//     }
//     yield { dataType: 'struct', scalarType: undefined, vectorSize: undefined };
//   })
// Mirrored as combine('encoderType') x combineWithParams(<dataType rows>), preserving order.
std::vector<ParamRecord> basicExecutionDataTypeRows() {
    std::vector<ParamRecord> rows;
    const std::vector<std::string> hostSharable = {"i32", "u32", "f16", "f32"};

    // Scalars.
    for (const std::string& s : hostSharable) {
        rows.push_back(ParamRecord{
            {"dataType", s}, {"scalarType", s}, {"vectorSize", 1}});
    }
    // Vectors.
    const std::vector<std::pair<std::string, int>> vectorTypes = {
        {"vec2", 2}, {"vec3", 3}, {"vec4", 4}};
    for (const auto& v : vectorTypes) {
        for (const std::string& s : hostSharable) {
            rows.push_back(ParamRecord{
                {"dataType", v.first + "<" + s + ">"},
                {"scalarType", s},
                {"vectorSize", v.second}});
        }
    }
    // Struct.
    rows.push_back(ParamRecord{
        {"dataType", "struct"},
        {"scalarType", Value::undef()},
        {"vectorSize", Value::undef()}});
    return rows;
}

CTS_TEST(g, "basic_execution")
    .desc("Verify immediate data is correctly passed to shaders.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes)
            .combineWithParams(basicExecutionDataTypeRows());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Upstream additionally skips f16 immediate blocks; the gate below skips
        // before any setImmediates work, so the f16 skip is subsumed.
        skipImmediateUnsupported(t);
    });

// ---------------------------------------------------------------------------
// update_data
// ---------------------------------------------------------------------------
CTS_TEST(g, "update_data")
    .desc("Verify setImmediates updates data correctly within a pass, including partial updates.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { skipImmediateUnsupported(t); });

// ---------------------------------------------------------------------------
// pipeline_switch
// ---------------------------------------------------------------------------
// Upstream restricts encoderType to ['render pass', 'compute pass'] for this test.
CTS_TEST(g, "pipeline_switch")
    .desc(
        "Verify immediate data is correctly set after switching pipelines.\n"
        "    - sameImmediateSize=true: Both pipelines use the same immediateSize.\n"
        "    - sameImmediateSize=false: Pipelines use different immediateSize values.\n"
        "    In both cases, immediates must be set correctly between draws/dispatches.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {"render pass", "compute pass"})
            .combine("sameImmediateSize", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { skipImmediateUnsupported(t); });

// ---------------------------------------------------------------------------
// use_max_immediate_size
// ---------------------------------------------------------------------------
CTS_TEST(g, "use_max_immediate_size")
    .desc("Verify setImmediates with maxImmediateSize.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { skipImmediateUnsupported(t); });

// ---------------------------------------------------------------------------
// typed_array_arguments
// ---------------------------------------------------------------------------
// Upstream params:
//   u.combine('typedArray', kTypedArrayBufferViewKeys)
//    .combine('encoderType', kProgrammableEncoderTypes)
//    .beginSubcases()
//    .expandWithParams(function*(p) {
//      const elementSize = kTypedArrayBufferViews[p.typedArray].BYTES_PER_ELEMENT;
//      const smallCount = Math.max(1, Math.ceil(4 / elementSize));
//      yield { dataOffset: undefined, dataSize: undefined };
//      yield { dataOffset: 0,          dataSize: undefined };
//      yield { dataOffset: smallCount, dataSize: undefined };
//      yield { dataOffset: undefined,  dataSize: smallCount };
//      yield { dataOffset: 0,          dataSize: smallCount };
//      yield { dataOffset: smallCount, dataSize: smallCount };
//    })
//
// BYTES_PER_ELEMENT per typed array (upstream key order):
//   Uint8Array=1, Uint8ClampedArray=1, Uint16Array=2, Uint32Array=4,
//   Int8Array=1, Int16Array=2, Int32Array=4, Float16Array=2, Float32Array=4,
//   Float64Array=8, BigInt64Array=8, BigUint64Array=8.
int bytesPerElementForTypedArray(const std::string& key) {
    if (key == "Uint8Array" || key == "Uint8ClampedArray" || key == "Int8Array") {
        return 1;
    }
    if (key == "Uint16Array" || key == "Int16Array" || key == "Float16Array") {
        return 2;
    }
    if (key == "Uint32Array" || key == "Int32Array" || key == "Float32Array") {
        return 4;
    }
    // Float64Array, BigInt64Array, BigUint64Array.
    return 8;
}

CTS_TEST(g, "typed_array_arguments")
    .desc("Verify dataOffset and dataSize arguments work correctly for all TypedArray types.")
    .params([](ParamsBuilder u) {
        // expandWithParams yields six {dataOffset, dataSize} rows whose values
        // depend on the per-case typedArray element size. Those six rows are the
        // cross product dataOffset ∈ {undefined, 0, smallCount} ×
        // dataSize ∈ {undefined, smallCount}, so they are reproduced with two
        // subcase-level expand() calls that read `typedArray` from the case
        // record (mirroring expandWithParams). The subcase keys are only
        // dataOffset/dataSize — re-emitting the case keys would collide.
        return u.combine("typedArray", kTypedArrayBufferViewKeys)
            .combine("encoderType", kProgrammableEncoderTypes)
            .beginSubcases()
            .expand("dataOffset", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* ta = findParam(p, "typedArray");
                const std::string typedArray =
                    ta != nullptr ? std::get<std::string>(ta->data()) : std::string();
                int smallCount = (4 + bytesPerElementForTypedArray(typedArray) - 1) /
                                 bytesPerElementForTypedArray(typedArray);
                if (smallCount < 1) {
                    smallCount = 1;
                }
                return {Value::undef(), Value(0), Value(smallCount)};
            })
            .expand("dataSize", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* ta = findParam(p, "typedArray");
                const std::string typedArray =
                    ta != nullptr ? std::get<std::string>(ta->data()) : std::string();
                int smallCount = (4 + bytesPerElementForTypedArray(typedArray) - 1) /
                                 bytesPerElementForTypedArray(typedArray);
                if (smallCount < 1) {
                    smallCount = 1;
                }
                return {Value::undef(), Value(smallCount)};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Upstream additionally skips Float16Array (TODO #4297); subsumed by the gate.
        skipImmediateUnsupported(t);
    });

// ---------------------------------------------------------------------------
// multiple_updates_before_draw_or_dispatch
// ---------------------------------------------------------------------------
CTS_TEST(g, "multiple_updates_before_draw_or_dispatch")
    .desc(
        "Verify that multiple setImmediates calls before a draw or dispatch result in the latest "
        "content being used (merging updates).")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { skipImmediateUnsupported(t); });

// ---------------------------------------------------------------------------
// render_pass_and_bundle_mix (no params upstream)
// ---------------------------------------------------------------------------
CTS_TEST(g, "render_pass_and_bundle_mix")
    .desc("Verify interaction between executeBundles and direct render pass commands.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { skipImmediateUnsupported(t); });

// ---------------------------------------------------------------------------
// render_bundle_isolation (no params upstream)
// ---------------------------------------------------------------------------
CTS_TEST(g, "render_bundle_isolation")
    .desc("Verify that immediate data state is isolated between bundles executed in the same pass.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { skipImmediateUnsupported(t); });

} // namespace
