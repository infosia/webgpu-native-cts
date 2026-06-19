// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/entry_point.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,entry_point",
    "Validation tests for attributes and entry point requirements");

CTS_TEST(g, "missing_attribute_on_param")
    .desc("Test that an entry point without an IO attribute on one of its parameters is rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("target_stage",
                         {Value(std::string("")), Value(std::string("vertex")),
                          Value(std::string("fragment")), Value(std::string("compute"))})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string vertexAttr = targetStage == "vertex" ? "" : "@location(1)";
        const std::string fragmentAttr = targetStage == "fragment" ? "" : "@location(1)";
        const std::string computeAttr = targetStage == "compute" ? "" : "@builtin(workgroup_id)";
        const std::string code =
            "\n@vertex"
            "\nfn vert_main(@location(0) a : f32,"
            "\n             " + vertexAttr + "  b : f32,"
            "\n@             location(2) c : f32) -> @builtin(position) vec4<f32> {"
            "\n  return vec4<f32>();"
            "\n}"
            "\n"
            "\n@fragment"
            "\nfn frag_main(@location(0)  a : f32,"
            "\n             " + fragmentAttr + " b : f32,"
            "\n@             location(2)  c : f32) {"
            "\n}"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn comp_main(@builtin(global_invocation_id) a : vec3<u32>,"
            "\n             " + computeAttr + "                   b : vec3<u32>,"
            "\n             @builtin(local_invocation_id)  c : vec3<u32>) {"
            "\n}"
            "\n";
        t.expectCompileResult(targetStage == "", code);
    });

CTS_TEST(g, "missing_attribute_on_param_struct")
    .desc("Test that an entry point struct parameter without an IO attribute on one of its members is rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("target_stage",
                         {Value(std::string("")), Value(std::string("vertex")),
                          Value(std::string("fragment")), Value(std::string("compute"))})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string vertexAttr = targetStage == "vertex" ? "" : "@location(1)";
        const std::string fragmentAttr = targetStage == "fragment" ? "" : "@location(1)";
        const std::string computeAttr = targetStage == "compute" ? "" : "@builtin(workgroup_id)";
        const std::string code =
            "\nstruct VertexInputs {"
            "\n  @location(0) a : f32,"
            "\n  " + vertexAttr + "  b : f32,"
            "\n@  location(2) c : f32,"
            "\n};"
            "\nstruct FragmentInputs {"
            "\n  @location(0)  a : f32,"
            "\n  " + fragmentAttr + " b : f32,"
            "\n@  location(2)  c : f32,"
            "\n};"
            "\nstruct ComputeInputs {"
            "\n  @builtin(global_invocation_id) a : vec3<u32>,"
            "\n  " + computeAttr + "                   b : vec3<u32>,"
            "\n  @builtin(local_invocation_id)  c : vec3<u32>,"
            "\n};"
            "\n"
            "\n@vertex"
            "\nfn vert_main(inputs : VertexInputs) -> @builtin(position) vec4<f32> {"
            "\n  return vec4<f32>();"
            "\n}"
            "\n"
            "\n@fragment"
            "\nfn frag_main(inputs : FragmentInputs) {"
            "\n}"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn comp_main(inputs : ComputeInputs) {"
            "\n}"
            "\n";
        t.expectCompileResult(targetStage == "", code);
    });

CTS_TEST(g, "missing_attribute_on_return_type")
    .desc("Test that an entry point without an IO attribute on its return type is rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("target_stage",
                         {Value(std::string("")), Value(std::string("vertex")),
                          Value(std::string("fragment"))})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string vertexAttr = targetStage == "vertex" ? "" : "@builtin(position)";
        const std::string fragmentAttr = targetStage == "fragment" ? "" : "@location(0)";
        const std::string code =
            "\n@vertex"
            "\nfn vert_main() -> " + vertexAttr + " vec4<f32> {"
            "\n  return vec4<f32>();"
            "\n}"
            "\n"
            "\n@fragment"
            "\nfn frag_main() -> " + fragmentAttr + " vec4<f32> {"
            "\n  return vec4<f32>();"
            "\n}"
            "\n";
        t.expectCompileResult(targetStage == "", code);
    });

CTS_TEST(g, "missing_attribute_on_return_type_struct")
    .desc("Test that an entry point struct return type without an IO attribute on one of its members is rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("target_stage",
                         {Value(std::string("")), Value(std::string("vertex")),
                          Value(std::string("fragment"))})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string vertexAttr = targetStage == "vertex" ? "" : "@location(1)";
        const std::string fragmentAttr = targetStage == "fragment" ? "" : "@location(1)";
        const std::string code =
            "\nstruct VertexOutputs {"
            "\n  @location(0)       a : f32,"
            "\n  " + vertexAttr + "        b : f32,"
            "\n  @builtin(position) c : vec4<f32>,"
            "\n};"
            "\nstruct FragmentOutputs {"
            "\n  @location(0)  a : f32,"
            "\n  " + fragmentAttr + " b : f32,"
            "\n@  location(2)  c : f32,"
            "\n};"
            "\n"
            "\n@vertex"
            "\nfn vert_main() -> VertexOutputs {"
            "\n  return VertexOutputs();"
            "\n}"
            "\n"
            "\n@fragment"
            "\nfn frag_main() -> FragmentOutputs {"
            "\n  return FragmentOutputs();"
            "\n}"
            "\n";
        t.expectCompileResult(targetStage == "", code);
    });

CTS_TEST(g, "no_entry_point_provided")
    .desc("Tests that a shader without an entry point is accepted")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn main() {}");
    });

} // namespace
