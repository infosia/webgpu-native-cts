// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/value_init.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ============================================================
// Test group
// ============================================================

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,value_init",
    "Test that variables in the shader are value initialized");

// ============================================================
// Helper: WGPUStringView from std::string_view
// ============================================================
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ============================================================
// scalarWgslValue(type, numericValue)
//   Returns the WGSL literal produced by Type[type].create(numericValue).wgsl()
//   for the four scalar types used by this file (value = 5):
//     bool  -> "true"
//     f32   -> "5.0f"
//     f16   -> "5.0h"
//     i32   -> "i32(5)"
//     u32   -> "5u"
// ============================================================
static std::string scalarWgslValue(std::string_view type) {
    if (type == "bool") return "true";
    if (type == "f32")  return "5.0f";
    if (type == "f16")  return "5.0h";
    if (type == "i32")  return "i32(5)";
    if (type == "u32")  return "5u";
    return "0";
}

// ============================================================
// generateShader: faithful port of the upstream JS function.
//   addressSpace in {"private", "function"}
// ============================================================
static std::string generateShader(
    bool isF16,
    std::string_view addressSpace,
    std::string_view typeDecl,
    std::string_view testValue,
    std::string_view comparison)
{
    std::string moduleScope =
        std::string(isF16 ? "enable f16;\n" : "") +
        R"(
    struct Output {
      failed : atomic<u32>
    }
    @group(0) @binding(0) var<storage, read_write> output : Output;
)";

    std::string functionScope;
    if (addressSpace == "private") {
        moduleScope += "\nvar<private> testVar: " + std::string(typeDecl) +
                       " = " + std::string(testValue) + ";";
    } else if (addressSpace == "function") {
        functionScope = "\nvar testVar: " + std::string(typeDecl) +
                        " = " + std::string(testValue) + ";";
    }

    return moduleScope + R"(
    @compute @workgroup_size(1, 1, 1)
    fn main() {
      )" + functionScope + "\n      " + std::string(comparison) + R"(
    }
  )";
}

// ============================================================
// run: compile the WGSL, dispatch, verify output.failed == 0
// The result buffer is created zero-filled (no mappedAtCreation).
// ============================================================
static void run(AllFeaturesMaxLimitsGpuTest& t, const std::string& wgsl) {
    // Create compute pipeline with auto layout.
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto
    pipeDesc.compute.module     = shaderModule;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    // Result buffer: 4 bytes, zero-filled (not pre-filled with expected).
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = 4;
    bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer resultBuffer = t.createBufferTracked(bufDesc);

    // Bind group: binding 0 -> resultBuffer
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer  = resultBuffer;
    entry.offset  = 0;
    entry.size    = 4;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Encode + dispatch
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    // Expect output.failed == 0
    const uint32_t expected = 0u;
    t.expectGPUBufferValuesEqual(resultBuffer, &expected, sizeof(expected));
}

// ============================================================
// Test: scalars
// ============================================================

CTS_TEST(g, "scalars")
    .desc("Test that scalars in private, and function storage classes can be initialized to a value.")
    .params([](ParamsBuilder u) {
        return u
            .combine("addressSpace", {"private", "function"})
            .combine("type", {"bool", "f32", "f16", "i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace = t.param<std::string>("addressSpace");
        auto type         = t.param<std::string>("type");

        if (type == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available");
            }
        }

        std::string testValue  = scalarWgslValue(type);
        std::string comparison = "if (testVar != " + testValue + ") {\n"
                                 "      atomicStore(&output.failed, 1u);\n"
                                 "    }";

        std::string wgsl = generateShader(
            type == "f16",
            addressSpace,
            type,
            testValue,
            comparison);

        run(t, wgsl);
    });

// ============================================================
// Test: vec
// ============================================================

CTS_TEST(g, "vec")
    .desc("Test that vectors in private, and function storage classes can be initialized to a value.")
    .params([](ParamsBuilder u) {
        return u
            .combine("addressSpace", {"private", "function"})
            .combine("type", {"bool", "f32", "f16", "i32", "u32"})
            .combine("count", {2, 3, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace = t.param<std::string>("addressSpace");
        auto type         = t.param<std::string>("type");
        auto count        = t.param<int64_t>("count");

        if (type == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available");
            }
        }

        std::string scalarValue = scalarWgslValue(type);
        std::string typeDecl    = "vec" + std::to_string(count) + "<" + type + ">";
        std::string testValue   = typeDecl + "(" + scalarValue + ")";
        std::string comparison  = "if (!all(testVar == " + testValue + ")) {\n"
                                  "      atomicStore(&output.failed, 1u);\n"
                                  "    }";

        std::string wgsl = generateShader(
            type == "f16",
            addressSpace,
            typeDecl,
            testValue,
            comparison);

        run(t, wgsl);
    });

// ============================================================
// Test: mat
// ============================================================

CTS_TEST(g, "mat")
    .desc("Test that matrices in private, and function storage classes can be initialized to a value.")
    .params([](ParamsBuilder u) {
        return u
            .combine("addressSpace", {"private", "function"})
            .combine("type", {"f32", "f16"})
            .beginSubcases()
            .combine("c", {2, 3, 4})
            .combine("r", {2, 3, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace = t.param<std::string>("addressSpace");
        auto type         = t.param<std::string>("type");
        auto c            = t.param<int64_t>("c");
        auto r            = t.param<int64_t>("r");

        if (type == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available");
            }
        }

        std::string scalarValue = scalarWgslValue(type);
        std::string typeDecl    = "mat" + std::to_string(c) + "x" + std::to_string(r) +
                                  "<" + type + ">";

        // Build the matrix constructor: c columns * r rows scalar values.
        std::string testValue = typeDecl + "(";
        for (int64_t ci = 0; ci < c; ++ci) {
            for (int64_t ri = 0; ri < r; ++ri) {
                testValue += scalarValue + ",";
            }
        }
        testValue += ")";

        // Comparison: nested loop over c columns and r rows.
        std::string comparison =
            "for ( var i = 0; i < " + std::to_string(c) + "; i++) {\n"
            "      for (var k = 0; k < " + std::to_string(r) + "; k++) {\n"
            "        if (testVar[i][k] != " + scalarValue + ") {\n"
            "          atomicStore(&output.failed, 1u);\n"
            "        }\n"
            "      }\n"
            "    }";

        std::string wgsl = generateShader(
            type == "f16",
            addressSpace,
            typeDecl,
            testValue,
            comparison);

        run(t, wgsl);
    });

// ============================================================
// Test: array
// ============================================================

CTS_TEST(g, "array")
    .desc("Test that arrays in private, and function storage classes can be initialized to a value.")
    .params([](ParamsBuilder u) {
        return u
            .combine("addressSpace", {"private", "function"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace = t.param<std::string>("addressSpace");
        auto type         = t.param<std::string>("type");

        if (type == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available");
            }
        }

        const int arraySize = 4;
        std::string scalarValue = scalarWgslValue(type);
        std::string typeDecl    = "array<" + type + ", " + std::to_string(arraySize) + ">";

        // Build the array constructor.
        std::string testValue = typeDecl + "(";
        for (int i = 0; i < arraySize; ++i) {
            testValue += scalarValue + ",";
        }
        testValue += ")";

        std::string comparison =
            "for ( var i = 0; i < " + std::to_string(arraySize) + "; i++) {\n"
            "      if (testVar[i] != " + scalarValue + ") {\n"
            "        atomicStore(&output.failed, 1u);\n"
            "      }\n"
            "    }";

        std::string wgsl = generateShader(
            type == "f16",
            addressSpace,
            typeDecl,
            testValue,
            comparison);

        run(t, wgsl);
    });

// ============================================================
// Test: array,nested
// ============================================================

CTS_TEST(g, "array,nested")
    .desc("Test that arrays in private, and function storage classes can be initialized to a value.")
    .params([](ParamsBuilder u) {
        return u
            .combine("addressSpace", {"private", "function"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace = t.param<std::string>("addressSpace");
        auto type         = t.param<std::string>("type");

        if (type == "f16") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
                t.skip("shader-f16 feature not available");
            }
        }

        const int arraySize = 4;
        std::string scalarValue = scalarWgslValue(type);
        std::string innerDecl   = "array<" + type + ", " + std::to_string(arraySize) + ">";
        std::string typeDecl    = "array<" + innerDecl + ", " + std::to_string(arraySize) + ">";

        // Build the nested array constructor.
        std::string testValue = typeDecl + "(";
        for (int i = 0; i < arraySize; ++i) {
            testValue += innerDecl + "(";
            for (int j = 0; j < arraySize; ++j) {
                testValue += scalarValue + ",";
            }
            testValue += "),";
        }
        testValue += ")";

        std::string comparison =
            "for ( var i = 0; i < " + std::to_string(arraySize) + "; i++) {\n"
            "      for ( var k = 0; k < " + std::to_string(arraySize) + "; k++) {\n"
            "        if (testVar[i][k] != " + scalarValue + ") {\n"
            "          atomicStore(&output.failed, 1u);\n"
            "        }\n"
            "      }\n"
            "    }";

        std::string wgsl = generateShader(
            type == "f16",
            addressSpace,
            typeDecl,
            testValue,
            comparison);

        run(t, wgsl);
    });

// ============================================================
// Test: struct
// Note: this test builds its own WGSL (not using generateShader)
// to match the upstream exactly — it defines structs A and S
// in module scope directly alongside the Output struct.
// ============================================================

CTS_TEST(g, "struct")
    .desc("Test that structs in private, and function storage classes can be initialized to a value.")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", {"private", "function"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto addressSpace = t.param<std::string>("addressSpace");

        std::string moduleScope = R"(
    struct Output {
      failed : atomic<u32>
    }
    @group(0) @binding(0) var<storage, read_write> output : Output;

    struct A {
        a: i32,
        b: f32,
    }

    struct S {
        c: f32,
        d: A,
        e: array<i32, 2>,
    }
  )";

        const std::string typeDecl  = "S";
        const std::string testValue = "S(5.f, A(5i, 5.f), array<i32, 2>(5i, 5i))";

        std::string functionScope;
        if (addressSpace == "private") {
            moduleScope += "\nvar<private> testVar: " + typeDecl + " = " + testValue + ";";
        } else if (addressSpace == "function") {
            functionScope = "\nvar testVar: " + typeDecl + " = " + testValue + ";";
        }

        const std::string comparison = R"(
    if (testVar.c != 5f || testVar.d.a != 5i || testVar.d.b != 5.f || testVar.e[0] != 5i || testVar.e[1] != 5i) {
      atomicStore(&output.failed, 1u);
    }
    )";

        std::string wgsl =
            moduleScope +
            R"(
      @compute @workgroup_size(1, 1, 1)
      fn main() {
        )" + functionScope + "\n        " + comparison + R"(
      }
    )";

        run(t, wgsl);
    });

} // namespace
