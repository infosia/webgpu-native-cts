// Ported from gpuweb/cts src/webgpu/shader/execution/statement/swizzle_assignment.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
//
// 1. Language-feature guard (swizzle_assignment):
//    Upstream calls t.skipIfLanguageFeatureNotSupported('swizzle_assignment')
//    (wgpuInstanceHasWGSLLanguageFeature). The GpuTest harness does not expose
//    the shared WGPUInstance to test bodies, so this port creates a one-shot
//    probe instance via createInstance() (precedent:
//    api/validation/error_scope.spec.cpp creates its own instance) and queries
//    wgpuInstanceHasWGSLLanguageFeature once, caching the result. On non-Dawn
//    backends the webgpu-headers do not define a WGPUWGSLLanguageFeatureName
//    value for swizzle_assignment, so the feature is treated as unsupported
//    and the tests skip with a reason (spec rule: prefer skip-with-reason).
//
// 2. eval_order / compound_eval_order: these tests do not go through
//    runSwizzleAssignmentTest; they invoke runFlowControlTest from the
//    flow_control harness. As in upstream, their generated shaders contain no
//    `requires swizzle_assignment;` directive (FlowControlWgsl::extra is
//    emitted after the entrypoint function, where a directive would be a parse
//    error anyway); the language-feature guard from note (1) is applied at the
//    top of each test body, mirroring upstream's skip call.
//
// 3. f16 bit-pattern comparison: the JS port used Float16Array to produce the
//    expected byte sequence. The C++ port uses a local floatToF16() helper that
//    converts IEEE 754 single precision to IEEE 754 half precision (exact for
//    all finite values used in this file: 1.0, 2.0, 4.0, 5.0).
//
// 4. Buffer comparison offset for the "storage" address-space variant:
//    In that case the Outputs struct has `v : vecN<T>` at offset 0 followed by
//    `data : array<T>`.  The upstream comparison always starts at offset 0;
//    for the storage case this hits the `v` field (which holds the final
//    vector after the assignment), and for the non-storage cases the `data`
//    array starts at offset 0.  Both are correct per the upstream test design.
//
// 5. Upstream uses `keysOf()` to iterate the case table in definition order.
//    The C++ port uses an explicit ordered pair array that mirrors the JS
//    object-literal order exactly, so query identities match.

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "common/webgpu/backend.h"
#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/flow_control/harness.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Language-feature query (see port note (1)).
//
// Mirrors upstream t.skipIfLanguageFeatureNotSupported('swizzle_assignment').
// WGSL language features are an instance-level property, so a one-shot probe
// instance gives the same answer as the harness's shared instance.
// ---------------------------------------------------------------------------
static bool hasSwizzleAssignmentLanguageFeature() {
#if defined(CTS_BACKEND_DAWN)
    static const bool supported = [] {
        WGPUInstance probeInstance = createInstance();
        if (probeInstance == nullptr) {
            return false;
        }
        const bool has = wgpuInstanceHasWGSLLanguageFeature(
                             probeInstance,
                             WGPUWGSLLanguageFeatureName_SwizzleAssignment) != 0u;
        wgpuInstanceRelease(probeInstance);
        return has;
    }();
    return supported;
#else
    // The webgpu-headers used by the non-Dawn backends do not define a
    // WGPUWGSLLanguageFeatureName value for swizzle_assignment, so the
    // feature cannot be queried; treat it as unsupported (the tests skip,
    // matching upstream behavior on implementations without the feature).
    return false;
#endif
}

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,statement,swizzle_assignment",
    "\nSwizzle assignment execution.\n");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Converts a double-precision value to its IEEE 754 half-precision (f16) bit
// pattern returned as uint16_t.  Handles the finite values used in this file
// (magnitudes in [0, 65504]).  NaN/Inf inputs produce unspecified results.
static uint16_t floatToF16(double v) {
    // Handle exact zero (positive and negative).
    if (v == 0.0) {
        return 0u;
    }

    uint16_t sign = 0u;
    if (v < 0.0) {
        sign = 0x8000u;
        v = -v;
    }

    // Extract the float32 representation for easier bit-twiddling.
    float fv = static_cast<float>(v);
    uint32_t bits32 = 0u;
    std::memcpy(&bits32, &fv, sizeof(bits32));

    // IEEE 754 f32: 1 sign + 8 exp + 23 mantissa.
    // IEEE 754 f16: 1 sign + 5 exp + 10 mantissa.
    const uint32_t exp32  = (bits32 >> 23u) & 0xFFu;
    const uint32_t mant32 = bits32 & 0x7FFFFFu;

    // Rebias the exponent: f32 bias=127, f16 bias=15.
    const int32_t exp16i = static_cast<int32_t>(exp32) - 127 + 15;

    if (exp16i <= 0) {
        // Result is subnormal or zero in f16.
        if (exp16i < -10) {
            return sign;
        }
        // Subnormal: shift the mantissa right by (1 - exp16i) places.
        const uint32_t shifted = (mant32 | 0x800000u) >> static_cast<uint32_t>(14 - exp16i);
        return static_cast<uint16_t>(sign | (shifted >> 13u));
    }
    if (exp16i >= 31) {
        // Overflow: return infinity.
        return static_cast<uint16_t>(sign | 0x7C00u);
    }

    const uint16_t exp16 = static_cast<uint16_t>(exp16i);
    const uint16_t mant16 = static_cast<uint16_t>(mant32 >> 13u);
    return static_cast<uint16_t>(sign | (exp16 << 10u) | mant16);
}

// ---------------------------------------------------------------------------
// SwizzleAssignmentCase table (mirrors upstream kSwizzleAssignmentCases)
// ---------------------------------------------------------------------------

struct SwizzleAssignmentCase {
    const char* name;
    const char* elemType;   // "u32", "i32", "f32", "f16", "bool"
    int         vecSize;    // 2, 3, or 4
    std::initializer_list<double> initial;
    const char* swizzle;
    const char* rhs;
    std::initializer_list<double> expected;
};

// Case table in definition order (mirrors keysOf(kSwizzleAssignmentCases)).
static const SwizzleAssignmentCase kSwizzleAssignmentCases[] = {
    {
        "vec4u_w_literal",
        "u32", 4,
        {1, 2, 3, 4},
        "w",
        "5",
        {1, 2, 3, 5},
    },
    {
        "vec4u_xy_vec2u",
        "u32", 4,
        {1, 2, 3, 5},
        "xy",
        "vec2u(6, 7)",
        {6, 7, 3, 5},
    },
    {
        "vec4u_zx_vec2u",
        "u32", 4,
        {6, 7, 3, 5},
        "zx",
        "vec2u(8, 9)",
        {9, 7, 8, 5},
    },
    {
        "vec4u_xyzw_vec4u",
        "u32", 4,
        {1, 1, 1, 1},
        "xyzw",
        "vec4u(10, 11, 12, 13)",
        {10, 11, 12, 13},
    },
    {
        "vec4u_xy_vec2_yx",
        "u32", 4,
        {10, 11, 12, 13},
        "xy",
        "vec2(v.y, v.x)",
        {11, 10, 12, 13},
    },
    {
        "vec3i_y_literal",
        "i32", 3,
        {-10, -20, -30},
        "y",
        "-50",
        {-10, -50, -30},
    },
    {
        "vec3i_zx_vec2i",
        "i32", 3,
        {10, 20, 30},
        "zx",
        "vec2i(40, 60)",
        {60, 20, 40},
    },
    {
        "vec3f_xy_vec2f",
        "f32", 3,
        {1.0, 2.0, 3.0},
        "xy",
        "vec2f(4.0, 5.0)",
        {4.0, 5.0, 3.0},
    },
    {
        "vec2f_yx_v_plus_v",
        "f32", 2,
        {1.0, 2.0},
        "yx",
        "v + v",
        {4.0, 2.0},
    },
    {
        "vec4f_rgb_vec3f_div_10",
        "f32", 4,
        {10.0, 20.0, 30.0, 100.0},
        "rgb",
        "vec3f(v.r, v.g, v.b) / 10.0",
        {1.0, 2.0, 3.0, 100.0},
    },
    {
        "vec2h_yx_vec2h",
        "f16", 2,
        {1.0, 2.0},
        "yx",
        "vec2h(4.0, 5.0)",
        {5.0, 4.0},
    },
    {
        "vec2_bool_y_true",
        "bool", 2,
        {1, 0},
        "y",
        "true",
        {1, 1},
    },
    {
        "vec3_bool_xz_vec2bool",
        "bool", 3,
        {1, 1, 1},
        "xz",
        "vec2<bool>(false, false)",
        {0, 1, 0},
    },
    {
        "vec4u_xy_x_literal",
        "u32", 4,
        {1, 2, 3, 4},
        "xy.x",
        "5",
        {5, 2, 3, 4},
    },
    {
        "vec3f_zyx_yz_vec2f",
        "f32", 3,
        {1.0, 2.0, 3.0},
        "zyx.yz",
        "vec2f(5.0, 6.0)",
        {6.0, 5.0, 3.0},
    },
    {
        "vec2i_xz_yx_vec2i",
        "i32", 3,
        {-1, 0, -1},
        "xz.yx",
        "vec2i(2,3)",
        {3, 0, 2},
    },
};

// ---------------------------------------------------------------------------
// SwizzleCompoundAssignmentCase table (mirrors kSwizzleCompoundAssignmentCases)
// ---------------------------------------------------------------------------

struct SwizzleCompoundAssignmentCase {
    const char* name;
    const char* elemType;
    int         vecSize;
    std::initializer_list<double> initial;
    const char* swizzle;
    const char* op;
    const char* rhs;
    std::initializer_list<double> expected;
};

static const SwizzleCompoundAssignmentCase kSwizzleCompoundAssignmentCases[] = {
    {
        "vec4u_w_add_5",
        "u32", 4,
        {1, 2, 3, 4},
        "w", "+=", "5",
        {1, 2, 3, 9},
    },
    {
        "vec4u_xy_mul_vec2u",
        "u32", 4,
        {1, 2, 3, 4},
        "xy", "*=", "vec2u(6, 7)",
        {6, 14, 3, 4},
    },
    {
        "vec3i_zx_add_vec2i",
        "i32", 3,
        {10, 20, 30},
        "zx", "+=", "vec2i(100)",
        {110, 20, 130},
    },
    {
        "vec3f_xy_mul_vec2f",
        "f32", 3,
        {1.0, 2.0, 3.0},
        "xy", "*=", "vec2f(0.5, 2.0)",
        {0.5, 4.0, 3.0},
    },
};

// ---------------------------------------------------------------------------
// runSwizzleAssignmentTest: builds, dispatches, and verifies a swizzle test.
//
// See port note (1) regarding the swizzle_assignment language-feature guard.
// ---------------------------------------------------------------------------
static void runSwizzleAssignmentTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const char* elemType,
    int vecSize,
    const std::initializer_list<double>& expectedValues,
    const std::string& wgsl)
{
    // Mirrors upstream t.skipIfLanguageFeatureNotSupported('swizzle_assignment').
    if (!hasSwizzleAssignmentLanguageFeature()) {
        t.skip("swizzle_assignment WGSL language feature not supported");
    }

    // Skip if the device does not have the shader-f16 feature when elemType == f16.
    if (std::string(elemType) == "f16" &&
        !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }

    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // auto
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = WGPUStringView{"main", 4};
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    // Output buffer: zero-initialized by WebGPU (never pre-filled with
    // expected values per readback-buffers-zero-init rule).
    const uint32_t maxOutputValues = 1000u;
    const uint64_t outputBufferSize = 4ull * (1u + maxOutputValues);
    WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputBufferDesc.size = outputBufferSize;
    outputBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputBufferDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 1;
    entry.buffer  = outputBuffer;
    entry.offset  = 0;
    entry.size    = outputBufferSize;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    // Build the expected byte sequence, mirroring upstream's
    //   new outputArrayConstructor(expectedValues)
    // For f16: each element is 2 bytes (uint16_t f16 bit pattern).
    // For all others: each element is 4 bytes.
    const std::string elemTypeStr(elemType);
    if (elemTypeStr == "f16") {
        std::vector<uint16_t> expected;
        expected.reserve(static_cast<size_t>(vecSize));
        for (double v : expectedValues) {
            expected.push_back(floatToF16(v));
        }
        t.expectGPUBufferValuesEqual(
            outputBuffer,
            expected.data(),
            expected.size() * sizeof(uint16_t));
    } else if (elemTypeStr == "f32") {
        std::vector<float> expected;
        expected.reserve(static_cast<size_t>(vecSize));
        for (double v : expectedValues) {
            expected.push_back(static_cast<float>(v));
        }
        t.expectGPUBufferValuesEqual(
            outputBuffer,
            expected.data(),
            expected.size() * sizeof(float));
    } else if (elemTypeStr == "i32") {
        std::vector<int32_t> expected;
        expected.reserve(static_cast<size_t>(vecSize));
        for (double v : expectedValues) {
            expected.push_back(static_cast<int32_t>(v));
        }
        t.expectGPUBufferValuesEqual(
            outputBuffer,
            expected.data(),
            expected.size() * sizeof(int32_t));
    } else {
        // "u32" and "bool" (stored as u32)
        std::vector<uint32_t> expected;
        expected.reserve(static_cast<size_t>(vecSize));
        for (double v : expectedValues) {
            expected.push_back(static_cast<uint32_t>(v));
        }
        t.expectGPUBufferValuesEqual(
            outputBuffer,
            expected.data(),
            expected.size() * sizeof(uint32_t));
    }
}

// ---------------------------------------------------------------------------
// Helper: build the WGSL initializer list for a vec of the given elemType.
// For bool, maps 0→"false", 1→"true".
// ---------------------------------------------------------------------------
static std::string buildInitialValues(
    const char* elemType,
    const std::initializer_list<double>& initial)
{
    const std::string elemTypeStr(elemType);
    std::string result;
    bool first = true;
    for (double v : initial) {
        if (!first) {
            result += ", ";
        }
        first = false;
        if (elemTypeStr == "bool") {
            result += (v == 0.0 ? "false" : "true");
        } else {
            // Format integer types without decimal, floats with.
            if (elemTypeStr == "f32" || elemTypeStr == "f16") {
                // Always emit a decimal point so WGSL parses as float literal.
                char buf[64];
                // Use snprintf for MSVC portability.
                if (v == static_cast<int64_t>(v)) {
                    // e.g. "1.0" rather than "1"
                    snprintf(buf, sizeof(buf), "%.1f", v);
                } else {
                    snprintf(buf, sizeof(buf), "%g", v);
                }
                result += buf;
            } else {
                // Integer types: cast to int64_t to get correct representation for negatives.
                result += std::to_string(static_cast<int64_t>(v));
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Helper: substitute all whole-word occurrences of "v" with varRef in rhs.
// Mirrors upstream's `rhs.replaceAll(/\bv\b/g, varRef)`.
// ---------------------------------------------------------------------------
static std::string substituteVarRef(const std::string& rhs, const std::string& varRef) {
    std::string result;
    result.reserve(rhs.size());
    for (size_t i = 0; i < rhs.size(); ) {
        if (rhs[i] == 'v') {
            // Check word boundaries: character before and after must not be \w.
            auto isWord = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_';
            };
            const bool prevOk = (i == 0) || !isWord(rhs[i - 1]);
            const bool nextOk = (i + 1 >= rhs.size()) || !isWord(rhs[i + 1]);
            if (prevOk && nextOk) {
                result += varRef;
                i++;
                continue;
            }
        }
        result += rhs[i];
        i++;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Test: swizzle_assignment_vars
// ---------------------------------------------------------------------------

CTS_TEST(g, "swizzle_assignment_vars")
    .desc("Tests the value of a vector after swizzle assignment on different variable types, "
          "address spaces, and on pointer and reference memory views.")
    .params([](ParamsBuilder u) {
        // "case" keys mirror keysOf(kSwizzleAssignmentCases) in definition order.
        return u
            .combine("case", {
                Value(std::string("vec4u_w_literal")),
                Value(std::string("vec4u_xy_vec2u")),
                Value(std::string("vec4u_zx_vec2u")),
                Value(std::string("vec4u_xyzw_vec4u")),
                Value(std::string("vec4u_xy_vec2_yx")),
                Value(std::string("vec3i_y_literal")),
                Value(std::string("vec3i_zx_vec2i")),
                Value(std::string("vec3f_xy_vec2f")),
                Value(std::string("vec2f_yx_v_plus_v")),
                Value(std::string("vec4f_rgb_vec3f_div_10")),
                Value(std::string("vec2h_yx_vec2h")),
                Value(std::string("vec2_bool_y_true")),
                Value(std::string("vec3_bool_xz_vec2bool")),
                Value(std::string("vec4u_xy_x_literal")),
                Value(std::string("vec3f_zyx_yz_vec2f")),
                Value(std::string("vec2i_xz_yx_vec2i")),
            })
            .beginSubcases()
            .combine("address_space", {
                Value(std::string("function")),
                Value(std::string("private")),
                Value(std::string("workgroup")),
                Value(std::string("storage")),
            })
            .combine("memory_view", {
                Value(std::string("ref")),
                Value(std::string("ptr")),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string caseName    = t.param<std::string>("case");
        const std::string addressSpace = t.param<std::string>("address_space");
        const std::string memoryView  = t.param<std::string>("memory_view");

        // Find the case entry.
        const SwizzleAssignmentCase* tc = nullptr;
        for (const SwizzleAssignmentCase& c : kSwizzleAssignmentCases) {
            if (c.name == caseName) {
                tc = &c;
                break;
            }
        }
        if (tc == nullptr) {
            t.fail("unknown case: " + caseName);
        }

        // upstream: t.skipIf(address_space === 'storage' && elemType === 'bool')
        if (addressSpace == "storage" && std::string(tc->elemType) == "bool") {
            t.skip("bool is not a host-shareable type; cannot use vec<bool> in storage address space");
        }

        const std::string elemType   = tc->elemType;
        const int         vecSize    = tc->vecSize;
        const std::string swizzle    = tc->swizzle;
        const std::string rhsStr     = tc->rhs;

        // vecType: e.g. "vec4<u32>"
        const std::string vecType =
            "vec" + std::to_string(vecSize) + "<" + elemType + ">";

        // outputElemType: bool is stored as u32
        const std::string outputElemType = (elemType == "bool") ? "u32" : elemType;

        // Initial value string: booleans use false/true literals.
        const std::string initialValues = buildInitialValues(tc->elemType, tc->initial);

        // var_ref: when storage, the vec lives in outputs.v; otherwise in local v.
        const std::string varRef = (addressSpace == "storage") ? "outputs.v" : "v";

        // lhs expression depends on memory_view (upstream names the let `ptr`;
        // WGSL allows shadowing the predeclared ptr type-generator).
        std::string lhs;
        if (memoryView == "ptr") {
            lhs = "let ptr = &" + varRef + "; ptr." + swizzle;
        } else {
            lhs = varRef + "." + swizzle;
        }

        // rhs: replace bare "v" with varRef (upstream: replaceAll(/\bv\b/g, varRef)).
        const std::string newRhs = substituteVarRef(rhsStr, varRef);

        // Private/workgroup module-scope variable declaration.
        std::string moduleScopeVar;
        if (addressSpace == "private" || addressSpace == "workgroup") {
            moduleScopeVar = "var<" + addressSpace + "> v : " + vecType + ";";
        }

        // Function-scope variable declaration.
        std::string funcScopeVar;
        if (addressSpace == "function") {
            funcScopeVar = "var v : " + vecType + ";";
        }

        // Outputs struct: storage variant includes v field.
        std::string outputsV;
        if (addressSpace == "storage") {
            outputsV = "  v : " + vecType + ",\n";
        }

        // Loop body for storing results.
        std::string storeLoop;
        if (elemType == "bool") {
            storeLoop = "outputs.data[i] = u32(" + varRef + "[i]);";
        } else {
            storeLoop = "outputs.data[i] = " + varRef + "[i];";
        }

        // Build the WGSL shader (upstream emits `requires swizzle_assignment;`
        // in the shader; the runtime skip guard lives in
        // runSwizzleAssignmentTest, see port note (1)).
        const std::string wgsl =
            std::string("requires swizzle_assignment;\n") +
            (elemType == "f16" ? "enable f16;\n" : "") +
            "\nstruct Outputs {\n" +
            outputsV +
            "  data : array<" + outputElemType + ">,\n" +
            "};\n\n" +
            "@group(0) @binding(1) var<storage, read_write> outputs : Outputs;\n\n" +
            (moduleScopeVar.empty() ? "" : moduleScopeVar + "\n\n") +
            "@compute @workgroup_size(1)\n" +
            "fn main() {\n" +
            (funcScopeVar.empty() ? "" : "  " + funcScopeVar + "\n") +
            "  " + varRef + " = " + vecType + "(" + initialValues + ");\n" +
            "  " + lhs + " = " + newRhs + ";\n\n" +
            "  // Store result to Output\n" +
            "  for (var i = 0; i < " + std::to_string(vecSize) + "; i++) {\n" +
            "    " + storeLoop + "\n" +
            "  }\n" +
            "}\n";

        runSwizzleAssignmentTest(t, tc->elemType, vecSize, tc->expected, wgsl);
    });

// ---------------------------------------------------------------------------
// Test: swizzle_compound_assignment
// ---------------------------------------------------------------------------

CTS_TEST(g, "swizzle_compound_assignment")
    .desc("Tests the value of a vector after compound swizzle assignment.")
    .params([](ParamsBuilder u) {
        return u
            .combine("case", {
                Value(std::string("vec4u_w_add_5")),
                Value(std::string("vec4u_xy_mul_vec2u")),
                Value(std::string("vec3i_zx_add_vec2i")),
                Value(std::string("vec3f_xy_mul_vec2f")),
            })
            .beginSubcases()
            .combine("address_space", {
                Value(std::string("function")),
                Value(std::string("private")),
                Value(std::string("workgroup")),
                Value(std::string("storage")),
            })
            .combine("memory_view", {
                Value(std::string("ref")),
                Value(std::string("ptr")),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string caseName     = t.param<std::string>("case");
        const std::string addressSpace = t.param<std::string>("address_space");
        const std::string memoryView   = t.param<std::string>("memory_view");

        const SwizzleCompoundAssignmentCase* tc = nullptr;
        for (const SwizzleCompoundAssignmentCase& c : kSwizzleCompoundAssignmentCases) {
            if (c.name == caseName) {
                tc = &c;
                break;
            }
        }
        if (tc == nullptr) {
            t.fail("unknown compound case: " + caseName);
        }

        const std::string elemType  = tc->elemType;
        const int         vecSize   = tc->vecSize;
        const std::string swizzle   = tc->swizzle;
        const std::string opStr     = tc->op;
        const std::string rhsStr    = tc->rhs;

        const std::string vecType =
            "vec" + std::to_string(vecSize) + "<" + elemType + ">";

        // For compound assignment the upstream output array type is elemType
        // (not outputElemType); compound cases have no bool variants.
        const std::string initialValues = buildInitialValues(tc->elemType, tc->initial);

        const std::string varRef = (addressSpace == "storage") ? "outputs.v" : "v";

        std::string lhs;
        if (memoryView == "ptr") {
            lhs = "let ptr = &" + varRef + "; ptr." + swizzle;
        } else {
            lhs = varRef + "." + swizzle;
        }

        const std::string newRhs = substituteVarRef(rhsStr, varRef);

        std::string moduleScopeVar;
        if (addressSpace == "private" || addressSpace == "workgroup") {
            moduleScopeVar = "var<" + addressSpace + "> v : " + vecType + ";";
        }

        std::string funcScopeVar;
        if (addressSpace == "function") {
            funcScopeVar = "var v : " + vecType + ";";
        }

        std::string outputsV;
        if (addressSpace == "storage") {
            outputsV = "  v : " + vecType + ",\n";
        }

        const std::string wgsl =
            std::string("requires swizzle_assignment;\n") +
            (elemType == "f16" ? "enable f16;\n" : "") +
            "\nstruct Outputs {\n" +
            outputsV +
            "  data : array<" + elemType + ">,\n" +
            "};\n\n" +
            "@group(0) @binding(1) var<storage, read_write> outputs : Outputs;\n\n" +
            (moduleScopeVar.empty() ? "" : moduleScopeVar + "\n\n") +
            "@compute @workgroup_size(1)\n" +
            "fn main() {\n" +
            (funcScopeVar.empty() ? "" : "  " + funcScopeVar + "\n") +
            "  " + varRef + " = " + vecType + "(" + initialValues + ");\n" +
            "  " + lhs + " " + opStr + " " + newRhs + ";\n\n" +
            "  // Store result to Output\n" +
            "  for (var i = 0; i < " + std::to_string(vecSize) + "; i++) {\n" +
            "    outputs.data[i] = " + varRef + "[i];\n" +
            "  }\n" +
            "}\n";

        runSwizzleAssignmentTest(t, tc->elemType, vecSize, tc->expected, wgsl);
    });

// ---------------------------------------------------------------------------
// Test: eval_order
//
// Tests that the vec pointer on the lhs of a swizzle assignment is evaluated
// before the rhs, and the load of the lhs vec happens after rhs.
//
// Port note (2): as in upstream, the flow-control shader has no `requires`
// directive; the language-feature skip guard is applied at the top of the
// test body.
// ---------------------------------------------------------------------------

CTS_TEST(g, "eval_order")
    .desc("Tests that the vec pointer on the lhs of a swizzle assignment is evaluated before "
          "the rhs, and the load of the lhs vec happens after rhs.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Mirrors upstream t.skipIfLanguageFeatureNotSupported('swizzle_assignment').
        if (!hasSwizzleAssignmentLanguageFeature()) {
            t.skip("swizzle_assignment WGSL language feature not supported");
        }
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(
R"(
  arr[0] = vec4u(1, 1, 1, 1);
  %%
  arr[foo()].xy = bar();
  %%
  if (all(arr[0] == vec4u(4, 5, 3, 8))) {
    %%
  } else {
    %%
  }
)",
                {
                    f.expect_order(0),
                    f.expect_order(3),
                    f.expect_order(4),
                    f.expect_not_reached(),
                });
            wgsl.extra = wgslTemplate(
R"(
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  %%
  arr[0].x = 6;       // overwritten by swizzle
  arr[0].z = 7;       // overwritten by bar()
  arr[0].w = 8;       // persists
  return 0;
}
fn bar() -> vec2u {
  %%
  arr[0].z = 3;       // persists
  return vec2u(4, 5); // will set x,y
}
)",
                {
                    f.expect_order(1),
                    f.expect_order(2),
                });
            return wgsl;
        });
    });

// ---------------------------------------------------------------------------
// Test: compound_eval_order
//
// Tests that the lhs of a swizzle compound assignment is evaluated before the
// rhs, and another load of the lhs vec happens after rhs evaluation, without
// re-evaluating the pointer to the lhs vec.
// ---------------------------------------------------------------------------

CTS_TEST(g, "compound_eval_order")
    .desc("Tests that the lhs of a swizzle compound assignment is evaluated before the rhs, "
          "and another load of the lhs vec happens after rhs evaluation, without "
          "re-evaluating the pointer to the lhs vec.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Mirrors upstream t.skipIfLanguageFeatureNotSupported('swizzle_assignment').
        if (!hasSwizzleAssignmentLanguageFeature()) {
            t.skip("swizzle_assignment WGSL language feature not supported");
        }
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(
R"(
  arr[0] = vec4u(1, 1, 1, 1);
  %%
  arr[foo()].xy += bar();
  %%
  if (all(arr[0] == vec4u(10, 6, 3, 8))) {
    %%
  } else {
    %%
  }
)",
                {
                    f.expect_order(0),
                    f.expect_order(3),
                    f.expect_order(4),
                    f.expect_not_reached(),
                });
            wgsl.extra = wgslTemplate(
R"(
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  %%
  arr[0].x = 6;       // modifies x before add
  arr[0].z = 7;       // overwritten by bar()
  arr[0].w = 8;       // persists
  return 0;
}
fn bar() -> vec2u {
  %%
  arr[0].x = 2;       // no visible effect
  arr[0].z = 3;       // persists
  return vec2u(4, 5); // will add to x,y
}
)",
                {
                    f.expect_order(1),
                    f.expect_order(2),
                });
            return wgsl;
        });
    });

} // namespace
