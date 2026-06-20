// Ported from gpuweb/cts src/webgpu/shader/execution/expression/precedence.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for operator precedence.

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

struct Expression {
    const char* name;
    const char* expr;
    int32_t result;
};

const std::vector<Expression>& kExpressions() {
    static const std::vector<Expression> v = {
        {"add_mul", "kThree + kSeven * kEleven", 80},
        {"mul_add", "kThree * kSeven + kEleven", 32},
        {"sub_neg", "kThree - - kSeven", 10},
        {"neg_shl", "- kThree << u32(kSeven)", -384},
        {"neg_shr", "- kThree >> u32(kSeven)", -1},
        {"neg_add", "- kThree + kSeven", 4},
        {"neg_mul", "- kThree * kSeven", -21},
        {"neg_and", "- kThree & kSeven", 5},
        {"neg_or", "- kThree | kSeven", -1},
        {"neg_xor", "- kThree ^ kSeven", -6},
        {"comp_add", "~ kThree + kSeven", 3},
        {"mul_deref", "kThree * * ptr_five", 15},
        {"not_and", "i32(! kFalse && kFalse)", 0},
        {"not_or", "i32(! kTrue || kTrue)", 1},
        {"eq_and", "i32(kFalse == kTrue && kFalse)", 0},
        {"and_eq", "i32(kFalse && kTrue == kFalse)", 0},
        {"eq_or", "i32(kFalse == kFalse || kTrue)", 1},
        {"or_eq", "i32(kTrue || kFalse == kFalse)", 1},
        {"add_swizzle", "(vec + vec . y) . z", 8},
    };
    return v;
}

// Upstream uses the base GPUTest fixture; we use AllFeaturesMaxLimitsGpuTest (a superset) so the
// per-process device is shared with the other expression ports (the base fixture re-requests a
// device from an already-consumed adapter, which fails when run alongside other groups).
TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup =
    MakeTestGroup<AllFeaturesMaxLimitsGpuTest>("shader,execution,expression,precedence",
                                               "Execution tests for operator precedence.");

CTS_TEST(testGroup, "precedence")
    .desc("Test that operator precedence rules are correctly implemented.")
    .params([](ParamsBuilder u) {
        std::vector<Value> exprNames;
        for (const Expression& e : kExpressions()) {
            exprNames.push_back(Value(std::string(e.name)));
        }
        return u.combine("expr", exprNames)
            .combine("decl", {"literal", "const", "override", "var<private>"})
            .combine("strip_spaces", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string exprName = t.param<std::string>("expr");
        const Expression* expr = nullptr;
        for (const Expression& e : kExpressions()) {
            if (exprName == e.name) {
                expr = &e;
                break;
            }
        }
        t.expect(expr != nullptr, "unknown expr param");
        if (expr == nullptr) {
            return;
        }

        std::string decl = t.param<std::string>("decl");
        std::string exprWgsl = expr->expr;
        if (decl == "literal") {
            decl = "const";
            exprWgsl = replaceAll(exprWgsl, "kThree", "3");
            exprWgsl = replaceAll(exprWgsl, "kSeven", "7");
            exprWgsl = replaceAll(exprWgsl, "kEleven", "11");
            exprWgsl = replaceAll(exprWgsl, "kFalse", "false");
            exprWgsl = replaceAll(exprWgsl, "kTrue", "true");
        }
        if (t.param<bool>("strip_spaces")) {
            exprWgsl = replaceAll(exprWgsl, " ", "");
        }

        const std::string wgsl =
            "@group(0) @binding(0) var<storage, read_write> buffer : i32;\n\n" + decl +
            " kFalse = false;\n" + decl + " kTrue = true;\n" + decl + " kThree = 3;\n" + decl +
            " kSeven = 7;\n" + decl + " kEleven = 11;\n\n"
            "@compute @workgroup_size(1)\n"
            "fn main() {\n"
            "  var five = 5;\n"
            "  var vec = vec4(1, kThree, 5, kSeven);\n"
            "  let ptr_five = &five;\n"
            "  buffer = " + exprWgsl + ";\n"
            "}\n";

        WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr;
        pipeDesc.compute.module = module;
        pipeDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        const uint32_t init = 0xDEADBEEFu;
        WGPUBuffer outputBuffer = t.makeBufferWithContents(
            &init, sizeof(init), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = outputBuffer;
        entry.offset = 0;
        entry.size = 4;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
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

        const int32_t expected = expr->result;
        t.expectGPUBufferValuesEqual(outputBuffer, &expected, sizeof(expected));
    });

} // namespace
