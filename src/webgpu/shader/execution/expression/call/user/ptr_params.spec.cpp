// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/user/ptr_params.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// User function call tests for pointer parameters. This file does not use the generic expression
// run() harness; mirroring upstream it builds a compute pipeline directly (with layout: auto), one
// uniform/storage input buffer + one read_write storage output buffer, dispatches one workgroup,
// and reads back the output. The override_array1/2/3 variants set the WGSL pipeline-overridable
// constants over_no_default / over_default via WGPUConstantEntry.
//
// Most g.tests gate on the WGSL language feature 'unrestricted_pointer_parameters'; we query it via
// wgpuInstanceHasWGSLLanguageFeature (a one-shot probe instance gives the same answer as the shared
// harness instance, since language features are an instance-level property). Dawn and yawgpu both
// export this query and define WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters, so both
// get the real instance-level answer; wgpu-native does not export the query, so the feature is
// treated as unsupported there (the gated cases skip, matching upstream behavior on implementations
// without the feature).

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "common/webgpu/backend.h"
#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> testGroup = MakeTestGroup<GpuTest>(
    "shader,execution,expression,call,user,ptr_params",
    "\nUser function call tests for pointer parameters.\n");

// ---------------------------------------------------------------------------
// Language-feature query: mirrors upstream
// t.skipIfLanguageFeatureNotSupported('unrestricted_pointer_parameters').
// ---------------------------------------------------------------------------
bool hasUnrestrictedPointerParameters() {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    static const bool supported = [] {
        WGPUInstance probe = createInstance();
        if (probe == nullptr) {
            return false;
        }
        const bool has = wgpuInstanceHasWGSLLanguageFeature(
                             probe, WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters) != 0u;
        wgpuInstanceRelease(probe);
        return has;
    }();
    return supported;
#else
    // Dawn and yawgpu both export wgpuInstanceHasWGSLLanguageFeature and define the
    // UnrestrictedPointerParameters enum value, so both query the real instance-level answer.
    // wgpu-native does not export the query, so the feature is treated as unsupported there (the
    // gated tests skip, matching upstream behavior on implementations without the feature).
    return false;
#endif
}

void skipIfUnrestrictedPointerParametersNotSupported(GpuTest& t) {
    if (!hasUnrestrictedPointerParameters()) {
        t.skip("WGSL language feature 'unrestricted_pointer_parameters' not supported");
    }
}

// ---------------------------------------------------------------------------
// Small WGSL/pipeline helpers (self-contained; do not depend on other harness headers).
// ---------------------------------------------------------------------------
WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

struct OverrideConstant {
    std::string key;
    double value;
};

WGPUComputePipeline createComputePipeline(GpuTest& t, const std::string& wgsl,
                                          const std::vector<OverrideConstant>& constants) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);
    std::vector<WGPUConstantEntry> entries(constants.size());
    for (size_t i = 0; i < constants.size(); ++i) {
        entries[i] = WGPU_CONSTANT_ENTRY_INIT;
        entries[i].key = sv(constants[i].key);
        entries[i].value = constants[i].value;
    }
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = sv("main");
    desc.compute.constantCount = entries.size();
    desc.compute.constants = entries.empty() ? nullptr : entries.data();
    return t.createComputePipelineTracked(desc);
}

struct BufferBinding {
    WGPUBuffer buffer;
    uint64_t size;
};

WGPUBindGroup makeAutoBindGroup(GpuTest& t, WGPUComputePipeline pipeline,
                                const std::vector<BufferBinding>& buffers) {
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::vector<WGPUBindGroupEntry> entries(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = static_cast<uint32_t>(i);
        entries[i].buffer = buffers[i].buffer;
        entries[i].offset = 0;
        entries[i].size = buffers[i].size;
    }
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = bgl;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(desc);
    wgpuBindGroupLayoutRelease(bgl);
    return bindGroup;
}

// Runs the WGSL with one input buffer (uniform or storage,read) + one read_write storage output,
// dispatches one workgroup, and asserts the output equals 'expected'. Mirrors upstream's local run().
void runPtr(GpuTest& t, const std::string& wgsl, bool uniformInput,
            const std::vector<uint32_t>& input, const std::vector<uint32_t>& expected,
            const std::vector<OverrideConstant>& constants = {}) {
    WGPUComputePipeline pipeline = createComputePipeline(t, wgsl, constants);

    const WGPUBufferUsage inputUsage =
        uniformInput ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage;
    WGPUBuffer inputBuffer =
        t.makeBufferWithContents(input.data(), input.size() * sizeof(uint32_t), inputUsage);

    WGPUBufferDescriptor outDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outDesc.size = expected.size() * sizeof(uint32_t);
    outDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outDesc);

    WGPUBindGroup bindGroup = makeAutoBindGroup(
        t, pipeline,
        {{inputBuffer, input.size() * sizeof(uint32_t)},
         {outputBuffer, expected.size() * sizeof(uint32_t)}});

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size() * sizeof(uint32_t));
}

// Per-'type' WGSL alias declarations (mirror upstream wgslTypeDecl).
std::string wgslTypeDecl(const std::string& kind) {
    if (kind == "vec4i") {
        return "\nalias T = vec4i;\nalias RT = T;\n";
    }
    if (kind == "array") {
        return "\nalias T = array<vec4f, 3>;\nalias RT = T;\n";
    }
    if (kind == "override_array1") {
        return "\nalias T = array<vec4f, over_no_default>;\nalias RT = array<vec4f, 3>;\n";
    }
    if (kind == "override_array2") {
        return "\nalias T = array<vec4f, over_default>;\nalias RT = array<vec4f, 3>;\n";
    }
    if (kind == "override_array3") {
        return "\nalias T = array<vec4f, over_expr>;\nalias RT = array<vec4f, 3>;\n";
    }
    // struct
    return "\nstruct S {\na : i32,\nb : u32,\nc : i32,\nd : u32,\n}\nalias T = S;\nalias RT = T;\n";
}

// Per-'type' input/expected values (mirror upstream valuesForType). All bit patterns are u32.
std::vector<uint32_t> valuesForType(const std::string& kind) {
    if (kind == "vec4i" || kind == "struct") {
        return {1u, 2u, 3u, 4u};
    }
    // array / override_array1/2/3: Float32Array([1..12]).
    std::vector<uint32_t> out(12);
    for (int i = 0; i < 12; ++i) {
        float f = static_cast<float>(i + 1);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        out[static_cast<size_t>(i)] = bits;
    }
    return out;
}

const std::vector<std::string> kTypes = {"vec4i", "array", "override_array1",
                                         "override_array2", "override_array3", "struct"};
bool isOverrideArrayType(const std::string& type) {
    return type == "override_array1" || type == "override_array2" || type == "override_array3";
}

const std::vector<OverrideConstant> kOverrideConstants = {{"over_no_default", 3.0},
                                                          {"over_default", 3.0}};

}  // namespace

// ---------------------------------------------------------------------------
// read_full_object
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "read_full_object")
    .desc("Test a pointer parameter can be read by a callee function")
    .params([](ParamsBuilder u) {
        return u.combine("address_space",
                         {"function", "private", "workgroup", "storage", "uniform"})
            .combine("call_indirection", {Value(static_cast<int64_t>(0)),
                                          Value(static_cast<int64_t>(1)),
                                          Value(static_cast<int64_t>(2))})
            .combine("type", {"vec4i", "array", "override_array1", "override_array2",
                              "override_array3", "struct"})
            .filter([](const ParamRecord& p) {
                const std::string type = valueAs<std::string>(*findParam(p, "type"));
                const std::string aspace = valueAs<std::string>(*findParam(p, "address_space"));
                if (type == "override_array1" || type == "override_array2" ||
                    type == "override_array3") {
                    return aspace == "workgroup";
                }
                return true;
            });
    })
    .fn([](GpuTest& t) {
        const std::string aspace = t.param<std::string>("address_space");
        const int64_t callIndirection = t.param<int64_t>("call_indirection");
        const std::string type = t.param<std::string>("type");

        if (aspace == "workgroup" || aspace == "storage" || aspace == "uniform") {
            skipIfUnrestrictedPointerParametersNotSupported(t);
        }

        std::string wgAssignInput = "W = input;";
        std::string outputAssign = "output = *p;";
        if (aspace == "workgroup" && isOverrideArrayType(type)) {
            wgAssignInput = "\nfor (var i = 0u; i < 3; i++) {\n  W[i] = input[i];\n}";
            outputAssign = "\nfor (var i = 0u; i < 3; i++) {\n  output[i] = (*p)[i];\n}";
        }

        std::string main;
        if (aspace == "function") {
            main =
                "\n@compute @workgroup_size(1)\nfn main() {\n  var F : T = input;\n  f0(&F);\n}\n";
        } else if (aspace == "private") {
            main = "\nvar<private> P : T;\n@compute @workgroup_size(1)\nfn main() {\n  P = input;\n  "
                   "f0(&P);\n}\n";
        } else if (aspace == "workgroup") {
            main = "\nvar<workgroup> W : T;\n@compute @workgroup_size(1)\nfn main() {\n  " +
                   wgAssignInput + "\n  f0(&W);\n}\n";
        } else {  // storage / uniform
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  f0(&input);\n}\n";
        }

        std::string callChain;
        for (int64_t i = 0; i < callIndirection; i++) {
            callChain += "\nfn f" + std::to_string(i) + "(p : ptr<" + aspace +
                         ", T>) {\n  f" + std::to_string(i + 1) + "(p);\n}\n";
        }

        const std::string inputVar =
            aspace == "uniform" ? "@binding(0) @group(0) var<uniform> input : RT;"
                                : "@binding(0) @group(0) var<storage, read> input : RT;";

        const std::string wgsl =
            "\noverride over_no_default : u32;\noverride over_default = 1u;\noverride over_expr = "
            "over_default + over_no_default - 3u;\n" +
            wgslTypeDecl(type) + "\n\n" + inputVar +
            "\n\n@binding(1) @group(0) var<storage, read_write> output : RT;\n\nfn f" +
            std::to_string(callIndirection) + "(p : ptr<" + aspace + ", T>) {\n    " +
            outputAssign + "\n}\n\n" + callChain + "\n" + main + "\n";

        const std::vector<uint32_t> values = valuesForType(type);
        runPtr(t, wgsl, aspace == "uniform", values, values, kOverrideConstants);
    });

// ---------------------------------------------------------------------------
// read_ptr_to_member
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "read_ptr_to_member")
    .desc("Test a pointer parameter to a member of a structure can be read by a callee function")
    .params([](ParamsBuilder u) {
        return u.combine("address_space",
                         {"function", "private", "workgroup", "storage", "uniform"});
    })
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);
        const std::string aspace = t.param<std::string>("address_space");

        std::string main;
        if (aspace == "function") {
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  var v : S = input;\n  output = "
                   "f0(&v);\n}\n";
        } else if (aspace == "private") {
            main = "\nvar<private> P : S;\n@compute @workgroup_size(1)\nfn main() {\n  P = input;\n  "
                   "output = f0(&P);\n}\n";
        } else if (aspace == "workgroup") {
            main = "\nvar<workgroup> W : S;\n@compute @workgroup_size(1)\nfn main() {\n  W = "
                   "input;\n  output = f0(&W);\n}\n";
        } else {  // storage / uniform
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  output = f0(&input);\n}\n";
        }

        const std::string inputVar =
            aspace == "uniform" ? "@binding(0) @group(0) var<uniform> input : S;"
                                : "@binding(0) @group(0) var<storage, read> input : S;";

        const std::string wgsl =
            "\nstruct S {\n  a : vec4i,\n  b : T,\n  c : vec4i,\n}\n\nstruct T {\n  a : vec4i,\n  b "
            ": vec4i,\n}\n\n\n" +
            inputVar +
            "\n@binding(1) @group(0) var<storage, read_write> output : T;\n\nfn f2(p : ptr<" +
            aspace + ", T>) -> T {\n  return *p;\n}\n\nfn f1(p : ptr<" + aspace +
            ", S>) -> T {\n  return f2(&(*p).b);\n}\n\nfn f0(p : ptr<" + aspace +
            ", S>) -> T {\n  return f1(p);\n}\n\n" + main + "\n";

        const std::vector<uint32_t> input = {/* S.a */ 1, 2, 3, 4,
                                             /* S.b.a */ 5, 6, 7, 8,
                                             /* S.b.b */ 9, 10, 11, 12,
                                             /* S.c */ 13, 14, 15, 16};
        const std::vector<uint32_t> expected = {/* S.b.a */ 5, 6, 7, 8,
                                                /* S.b.b */ 9, 10, 11, 12};
        runPtr(t, wgsl, aspace == "uniform", input, expected);
    });

// ---------------------------------------------------------------------------
// read_ptr_to_element
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "read_ptr_to_element")
    .desc("Test a pointer parameter to an element of an array can be read by a callee function")
    .params([](ParamsBuilder u) {
        return u.combine("address_space",
                         {"function", "private", "workgroup", "storage", "uniform"});
    })
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);
        const std::string aspace = t.param<std::string>("address_space");

        std::string main;
        if (aspace == "function") {
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  var v : T = input;\n  output = "
                   "f0(&v);\n}\n";
        } else if (aspace == "private") {
            main = "\nvar<private> P : T;\n@compute @workgroup_size(1)\nfn main() {\n  P = input;\n  "
                   "output = f0(&P);\n}\n";
        } else if (aspace == "workgroup") {
            main = "\nvar<workgroup> W : T;\n@compute @workgroup_size(1)\nfn main() {\n  W = "
                   "input;\n  output = f0(&W);\n}\n";
        } else {  // storage / uniform
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  output = f0(&input);\n}\n";
        }

        const std::string inputVar =
            aspace == "uniform" ? "@binding(0) @group(0) var<uniform> input : T;"
                                : "@binding(0) @group(0) var<storage, read> input : T;";

        const std::string wgsl =
            "\nalias T3 = vec4i;\nalias T2 = array<T3, 2>;\nalias T1 = array<T2, 3>;\nalias T = "
            "array<T1, 2>;\n\n" +
            inputVar +
            "\n@binding(1) @group(0) var<storage, read_write> output : T3;\n\nfn f2(p : ptr<" +
            aspace + ", T2>) -> T3 {\n  return (*p)[1];\n}\n\nfn f1(p : ptr<" + aspace +
            ", T1>) -> T3 {\n  return f2(&(*p)[0]) + f2(&(*p)[2]);\n}\n\nfn f0(p : ptr<" + aspace +
            ", T>) -> T3 {\n  return f1(&(*p)[0]);\n}\n\n" + main + "\n";

        const std::vector<uint32_t> input = {
            /* [0][0][0] */ 1, 2, 3, 4,        /* [0][0][1] */ 5, 6, 7, 8,
            /* [0][1][0] */ 9, 10, 11, 12,     /* [0][1][1] */ 13, 14, 15, 16,
            /* [0][2][0] */ 17, 18, 19, 20,    /* [0][2][1] */ 21, 22, 23, 24,
            /* [1][0][0] */ 25, 26, 27, 28,    /* [1][0][1] */ 29, 30, 31, 32,
            /* [1][1][0] */ 33, 34, 35, 36,    /* [1][1][1] */ 37, 38, 39, 40,
            /* [1][2][0] */ 41, 42, 43, 44,    /* [1][2][1] */ 45, 46, 47, 48};
        const std::vector<uint32_t> expected = {5 + 21, 6 + 22, 7 + 23, 8 + 24};
        runPtr(t, wgsl, aspace == "uniform", input, expected);
    });

// ---------------------------------------------------------------------------
// write_full_object
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "write_full_object")
    .desc("Test a pointer parameter can be written to by a callee function")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"function", "private", "workgroup", "storage"})
            .combine("call_indirection", {Value(static_cast<int64_t>(0)),
                                          Value(static_cast<int64_t>(1)),
                                          Value(static_cast<int64_t>(2))})
            .combine("type", {"vec4i", "array", "override_array1", "override_array2",
                              "override_array3", "struct"})
            .filter([](const ParamRecord& p) {
                const std::string type = valueAs<std::string>(*findParam(p, "type"));
                const std::string aspace = valueAs<std::string>(*findParam(p, "address_space"));
                if (type == "override_array1" || type == "override_array2" ||
                    type == "override_array3") {
                    return aspace == "workgroup";
                }
                return true;
            });
    })
    .fn([](GpuTest& t) {
        const std::string aspace = t.param<std::string>("address_space");
        const int64_t callIndirection = t.param<int64_t>("call_indirection");
        const std::string type = t.param<std::string>("type");

        if (aspace == "workgroup" || aspace == "storage") {
            skipIfUnrestrictedPointerParametersNotSupported(t);
        }

        std::string wgOutputAssign = "output = W;";
        std::string assignFromInput = "*p = input;";
        if (aspace == "workgroup" && isOverrideArrayType(type)) {
            wgOutputAssign = "\nfor (var i = 0u; i < 3; i++) {\n  output[i] = W[i];\n}";
            assignFromInput = "\nfor (var i = 0u; i < 3; i++) {\n  (*p)[i] = input[i];\n}";
        }

        const std::string ptr = aspace == "storage" ? "ptr<storage, T, read_write>"
                                                     : ("ptr<" + aspace + ", T>");

        std::string main;
        if (aspace == "function") {
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  var F : T;\n  f0(&F);\n  output = "
                   "F;\n}\n";
        } else if (aspace == "private") {
            main = "\nvar<private> P : T;\n@compute @workgroup_size(1)\nfn main() {\n  f0(&P);\n  "
                   "output = P;\n}\n";
        } else if (aspace == "workgroup") {
            main = "\nvar<workgroup> W : T;\n@compute @workgroup_size(1)\nfn main() {\n  f0(&W);\n  " +
                   wgOutputAssign + "\n}\n";
        } else {  // storage
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  f0(&output);\n}\n";
        }

        std::string callChain;
        for (int64_t i = 0; i < callIndirection; i++) {
            callChain += "\nfn f" + std::to_string(i) + "(p : " + ptr + ") {\n  f" +
                         std::to_string(i + 1) + "(p);\n}\n";
        }

        const std::string wgsl =
            "\noverride over_no_default : u32;\noverride over_default = 1u;\noverride over_expr = "
            "over_default + over_no_default - 3u;\n" +
            wgslTypeDecl(type) +
            "\n\n@binding(0) @group(0) var<uniform> input : RT;\n@binding(1) @group(0) var<storage, "
            "read_write> output : RT;\n\nfn f" +
            std::to_string(callIndirection) + "(p : " + ptr + ") {\n  " + assignFromInput +
            "\n}\n\n" + callChain + "\n" + main + "\n";

        const std::vector<uint32_t> values = valuesForType(type);
        runPtr(t, wgsl, /*uniformInput=*/true, values, values, kOverrideConstants);
    });

// ---------------------------------------------------------------------------
// write_ptr_to_member
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "write_ptr_to_member")
    .desc("Test a pointer parameter to a member of a structure can be written to by a callee "
          "function")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"function", "private", "workgroup", "storage"});
    })
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);
        const std::string aspace = t.param<std::string>("address_space");

        std::string main;
        if (aspace == "function") {
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  var v : S;\n  f0(&v);\n  output = "
                   "v;\n}\n";
        } else if (aspace == "private") {
            main = "\nvar<private> P : S;\n@compute @workgroup_size(1)\nfn main() {\n  f0(&P);\n  "
                   "output = P;\n}\n";
        } else if (aspace == "workgroup") {
            main = "\nvar<workgroup> W : S;\n@compute @workgroup_size(1)\nfn main() {\n  f0(&W);\n  "
                   "output = W;\n}\n";
        } else {  // storage
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  f1(&output);\n}\n";
        }

        auto ptr = [&](const std::string& ty) {
            return aspace == "storage" ? ("ptr<storage, " + ty + ", read_write>")
                                       : ("ptr<" + aspace + ", " + ty + ">");
        };

        const std::string wgsl =
            "\nstruct S {\n  a : vec4i,\n  b : T,\n  c : vec4i,\n}\n\nstruct T {\n  a : vec4i,\n  b "
            ": vec4i,\n}\n\n\n@binding(0) @group(0) var<storage> input : T;\n@binding(1) @group(0) "
            "var<storage, read_write> output : S;\n\nfn f2(p : " +
            ptr("T") + ") {\n  *p = input;\n}\n\nfn f1(p : " + ptr("S") +
            ") {\n  f2(&(*p).b);\n}\n\nfn f0(p : " + ptr("S") + ") {\n  f1(p);\n}\n\n" + main + "\n";

        const std::vector<uint32_t> input = {/* S.b.a */ 5, 6, 7, 8,
                                             /* S.b.b */ 9, 10, 11, 12};
        const std::vector<uint32_t> expected = {/* S.a   */ 0, 0, 0, 0,
                                                /* S.b.a */ 5, 6, 7, 8,
                                                /* S.b.b */ 9, 10, 11, 12,
                                                /* S.c   */ 0, 0, 0, 0};
        runPtr(t, wgsl, /*uniformInput=*/false, input, expected);
    });

// ---------------------------------------------------------------------------
// write_ptr_to_element
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "write_ptr_to_element")
    .desc("Test a pointer parameter to an element of an array can be written to by a callee "
          "function")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"function", "private", "workgroup", "storage"});
    })
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);
        const std::string aspace = t.param<std::string>("address_space");

        std::string main;
        if (aspace == "function") {
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  var v : T;\n  f0(&v);\n  output = "
                   "v;\n}\n";
        } else if (aspace == "private") {
            main = "\nvar<private> P : T;\n@compute @workgroup_size(1)\nfn main() {\n  f0(&P);\n  "
                   "output = P;\n}\n";
        } else if (aspace == "workgroup") {
            main = "\nvar<workgroup> W : T;\n@compute @workgroup_size(1)\nfn main() {\n  f0(&W);\n  "
                   "output = W;\n}\n";
        } else {  // storage
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  f0(&output);\n}\n";
        }

        auto ptr = [&](const std::string& ty) {
            return aspace == "storage" ? ("ptr<storage, " + ty + ", read_write>")
                                       : ("ptr<" + aspace + ", " + ty + ">");
        };

        const std::string wgsl =
            "\nalias T3 = vec4i;\nalias T2 = array<T3, 2>;\nalias T1 = array<T2, 3>;\nalias T = "
            "array<T1, 2>;\n\n@binding(0) @group(0) var<storage, read> input : T3;\n@binding(1) "
            "@group(0) var<storage, read_write> output : T;\n\nfn f2(p : " +
            ptr("T2") + ") {\n  (*p)[1] = input;\n}\n\nfn f1(p : " + ptr("T1") +
            ") {\n  f2(&(*p)[0]);\n  f2(&(*p)[2]);\n}\n\nfn f0(p : " + ptr("T") +
            ") {\n  f1(&(*p)[0]);\n}\n\n" + main + "\n";

        const std::vector<uint32_t> input = {1, 2, 3, 4};
        const std::vector<uint32_t> expected = {
            /* [0][0][0] */ 0, 0, 0, 0,        /* [0][0][1] */ 1, 2, 3, 4,
            /* [0][1][0] */ 0, 0, 0, 0,        /* [0][1][1] */ 0, 0, 0, 0,
            /* [0][2][0] */ 0, 0, 0, 0,        /* [0][2][1] */ 1, 2, 3, 4,
            /* [1][0][0] */ 0, 0, 0, 0,        /* [1][0][1] */ 0, 0, 0, 0,
            /* [1][1][0] */ 0, 0, 0, 0,        /* [1][1][1] */ 0, 0, 0, 0,
            /* [1][2][0] */ 0, 0, 0, 0,        /* [1][2][1] */ 0, 0, 0, 0};
        runPtr(t, wgsl, /*uniformInput=*/false, input, expected);
    });

// ---------------------------------------------------------------------------
// atomic_ptr_to_element
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "atomic_ptr_to_element")
    .desc("Test a pointer parameter to an atomic<i32> of an array can be read from and written to "
          "by a callee function")
    .params([](ParamsBuilder u) { return u.combine("address_space", {"workgroup", "storage"}); })
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);
        const std::string aspace = t.param<std::string>("address_space");

        std::string main;
        if (aspace == "workgroup") {
            main =
                "\nvar<workgroup> W_input : T;\nvar<workgroup> W_output : T;\n@compute "
                "@workgroup_size(1)\nfn main() {\n  // Copy input -> W_input\n  for (var i = 0; i < "
                "2; i++) {\n    for (var j = 0; j < 3; j++) {\n      for (var k = 0; k < 2; k++) {\n "
                "       atomicStore(&W_input[k][j][i], atomicLoad(&input[k][j][i]));\n      }\n    "
                "}\n  }\n\n  f0(&W_input, &W_output);\n\n  // Copy W_output -> output\n  for (var i "
                "= 0; i < 2; i++) {\n    for (var j = 0; j < 3; j++) {\n      for (var k = 0; k < 2; "
                "k++) {\n        atomicStore(&output[k][j][i], atomicLoad(&W_output[k][j][i]));\n    "
                "  }\n    }\n  }\n}\n";
        } else {  // storage
            main = "\n@compute @workgroup_size(1)\nfn main() {\n  f0(&input, &output);\n}\n";
        }

        auto ptr = [&](const std::string& ty) {
            return aspace == "storage" ? ("ptr<storage, " + ty + ", read_write>")
                                       : ("ptr<" + aspace + ", " + ty + ">");
        };

        const std::string wgsl =
            "\nalias T3 = atomic<i32>;\nalias T2 = array<T3, 2>;\nalias T1 = array<T2, 3>;\nalias T "
            "= array<T1, 2>;\n\n@binding(0) @group(0) var<storage, read_write> input : "
            "T;\n@binding(1) @group(0) var<storage, read_write> output : T;\n\nfn f2(in : " +
            ptr("T2") + ", out : " + ptr("T2") +
            ") {\n  let v = atomicLoad(&(*in)[0]);\n  atomicStore(&(*out)[1], v);\n}\n\nfn f1(in : " +
            ptr("T1") + ", out : " + ptr("T1") +
            ") {\n  f2(&(*in)[0], &(*out)[1]);\n  f2(&(*in)[2], &(*out)[0]);\n}\n\nfn f0(in : " +
            ptr("T") + ", out : " + ptr("T") +
            ") {\n  f1(&(*in)[1], &(*out)[0]);\n}\n\n" + main + "\n";

        const std::vector<uint32_t> input = {/* [0][0][0] */ 1,  /* [0][0][1] */ 2,
                                             /* [0][1][0] */ 3,  /* [0][1][1] */ 4,
                                             /* [0][2][0] */ 5,  /* [0][2][1] */ 6,
                                             /* [1][0][0] */ 7,  /* [1][0][1] */ 8,
                                             /* [1][1][0] */ 9,  /* [1][1][1] */ 10,
                                             /* [1][2][0] */ 11, /* [1][2][1] */ 12};
        const std::vector<uint32_t> expected = {/* [0][0][0] */ 0,  /* [0][0][1] */ 11,
                                                /* [0][1][0] */ 0,  /* [0][1][1] */ 7,
                                                /* [0][2][0] */ 0,  /* [0][2][1] */ 0,
                                                /* [1][0][0] */ 0,  /* [1][0][1] */ 0,
                                                /* [1][1][0] */ 0,  /* [1][1][1] */ 0,
                                                /* [1][2][0] */ 0,  /* [1][2][1] */ 0};
        runPtr(t, wgsl, /*uniformInput=*/false, input, expected);
    });

// ---------------------------------------------------------------------------
// array_length
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "array_length")
    .desc("Test a pointer parameter to a runtime sized array can be used by arrayLength() in a "
          "callee function")
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);

        const std::string wgsl =
            "\n@binding(0) @group(0) var<storage, read> arr : array<u32>;\n@binding(1) @group(0) "
            "var<storage, read_write> output : u32;\n\nfn f2(p : ptr<storage, array<u32>, read>) -> "
            "u32 {\n  return arrayLength(p);\n}\n\nfn f1(p : ptr<storage, array<u32>, read>) -> u32 "
            "{\n  return f2(p);\n}\n\nfn f0(p : ptr<storage, array<u32>, read>) -> u32 {\n  return "
            "f1(p);\n}\n\n@compute @workgroup_size(1)\nfn main() {\n  output = f0(&arr);\n}\n";

        const std::vector<uint32_t> input = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        const std::vector<uint32_t> expected = {12};
        runPtr(t, wgsl, /*uniformInput=*/false, input, expected);
    });

// ---------------------------------------------------------------------------
// mixed_ptr_parameters
// ---------------------------------------------------------------------------
CTS_TEST(testGroup, "mixed_ptr_parameters")
    .desc("Test that functions can accept multiple, mixed pointer parameters")
    .fn([](GpuTest& t) {
        skipIfUnrestrictedPointerParametersNotSupported(t);

        const std::string wgsl =
            "\n@binding(0) @group(0) var<uniform> input : array<vec4i, 4>;\n@binding(1) @group(0) "
            "var<storage, read_write> output : array<vec4i, 4>;\n\nfn sum(f : ptr<function, i32>,\n "
            "      w : ptr<workgroup, atomic<i32>>,\n       p : ptr<private, i32>,\n       u : "
            "ptr<uniform, vec4i>) -> vec4i {\n\n  return vec4(*f + atomicLoad(w) + *p) + "
            "*u;\n}\n\nstruct S {\n  i : i32,\n}\n\nvar<private> P0 = S(0);\nvar<private> P1 = "
            "S(10);\nvar<private> P2 = 20;\nvar<private> P3 = 30;\n\nstruct T {\n  i : "
            "atomic<i32>,\n}\n\nvar<workgroup> W0 : T;\nvar<workgroup> W1 : atomic<i32>;\n"
            "var<workgroup> W2 : T;\nvar<workgroup> W3 : atomic<i32>;\n\n@compute "
            "@workgroup_size(1)\nfn main() {\n  atomicStore(&W0.i, 0);\n  atomicStore(&W1,   100);\n "
            " atomicStore(&W2.i, 200);\n  atomicStore(&W3,   300);\n\n  var F = array(0, 1000, 2000, "
            "3000);\n\n  output[0] = sum(&F[2], &W3,   &P1.i, &input[0]);\n  output[1] = sum(&F[1], "
            "&W2.i, &P0.i, &input[1]);\n  output[2] = sum(&F[3], &W0.i, &P3,   &input[2]);\n  "
            "output[3] = sum(&F[2], &W1,   &P2,   &input[3]);\n}\n";

        const std::vector<uint32_t> input = {/* [0] */ 1, 2, 3, 4,
                                             /* [1] */ 4, 3, 2, 1,
                                             /* [2] */ 2, 4, 1, 3,
                                             /* [3] */ 4, 1, 2, 3};
        const std::vector<uint32_t> expected = {/* [0] */ 2311, 2312, 2313, 2314,
                                                /* [1] */ 1204, 1203, 1202, 1201,
                                                /* [2] */ 3032, 3034, 3031, 3033,
                                                /* [3] */ 2124, 2121, 2122, 2123};
        runPtr(t, wgsl, /*uniformInput=*/true, input, expected);
    });
