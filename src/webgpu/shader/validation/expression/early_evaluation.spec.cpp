// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/early_evaluation.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Tests specific validation for early evaluation expressions.
//
// Port note: upstream kCompositeCases is a Record<string, {code, stage, valid}>.
// Value cannot carry the struct, so cases are keyed by their scalar name and the
// {code, stage, valid} triple is reconstructed in the body via a local lookup.

#include <map>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,early_evaluation",
    "Tests specific validation for early evaluation expressions");

struct CompositeCase {
    const char* name;
    const char* code;
    const char* stage;  // "constant" or "override"
    bool valid;
};

// Mirrors upstream kCompositeCases (object key order preserved).
static const std::vector<CompositeCase>& kCompositeCases() {
    static const std::vector<CompositeCase> v = {
        {"const_scalar", "let tmp = const_1e30 * const_1e30;", "constant", false},
        {"const_vector", "let tmp = vec4(const_1e30) * vec4(const_1e30);", "constant", false},
        {"const_let_vector",
         "let tmp = vec4(const_1e30) * vec4(vec3(const_1e30), let_1e30);", "constant", true},
        {"const_let_vector_comp",
         "let tmp = vec2(const_1e30)[0] * vec2(const_1e30, let_1e30)[0];", "constant", true},
        {"const_let_array_comp",
         "let tmp = array(const_1e30, const_1e30)[0] * array(const_1e30, let_1e30)[0];",
         "constant", true},
        {"const_let_struct_comp",
         "let tmp = S(const_1e30, const_1e30).x * S(const_1e30, let_1e30).x;", "constant", true},
        {"const_let_matrix",
         "let tmp = mat2x2(vec2(const_1e30), vec2(const_1e30)) * mat2x2(vec2(const_1e30), "
         "vec2(let_1e30));",
         "constant", true},
        {"const_let_matrix_vec",
         "let tmp = mat2x2(vec2(const_1e30), vec2(const_1e30))[0] * mat2x2(vec2(const_1e30), "
         "vec2(let_1e30))[0];",
         "constant", true},
        {"const_let_matrix_comp",
         "let tmp = mat2x2(vec2(const_1e30), vec2(const_1e30))[0].x * mat2x2(vec2(const_1e30), "
         "vec2(let_1e30))[0].x;",
         "constant", true},
        {"override_scalar", "let tmp = override_1e30 * override_1e30;", "override", false},
        {"override_vector", "let tmp = vec4(override_1e30) * vec4(override_1e30);", "override",
         false},
        {"override_let_vector",
         "let tmp = vec4(override_1e30) * vec4(vec3(override_1e30), let_1e30);", "override", true},
        {"override_let_vector_comp",
         "let tmp = vec2(override_1e30)[0] * vec2(override_1e30, let_1e30)[0];", "override", true},
        {"override_let_array_comp",
         "let tmp = array(override_1e30, override_1e30)[0] * array(override_1e30, let_1e30)[0];",
         "override", true},
        {"override_let_struct_comp",
         "let tmp = S(override_1e30, override_1e30).x * S(override_1e30, let_1e30).x;", "override",
         true},
        {"override_let_matrix",
         "let tmp = mat2x2(vec2(override_1e30), vec2(override_1e30)) * "
         "mat2x2(vec2(override_1e30), vec2(let_1e30));",
         "override", true},
        {"override_let_matrix_vec",
         "let tmp = mat2x2(vec2(override_1e30), vec2(override_1e30))[0] * "
         "mat2x2(vec2(override_1e30), vec2(let_1e30))[0];",
         "override", true},
        {"override_let_matrix_comp",
         "let tmp = mat2x2(vec2(override_1e30), vec2(override_1e30))[0].x * "
         "mat2x2(vec2(override_1e30), vec2(let_1e30))[0].x;",
         "override", true},
    };
    return v;
}

static std::vector<Value> compositeNames() {
    std::vector<Value> values;
    for (const CompositeCase& c : kCompositeCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const CompositeCase& findComposite(const std::string& name) {
    for (const CompositeCase& c : kCompositeCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const CompositeCase dummy{"", "", "", false};
    return dummy;
}

CTS_TEST(g, "composites")
    .desc("Validates that composites are either wholly evaluated or not at all")
    .params([](ParamsBuilder u) { return u.combine("case", compositeNames()); })
    .fn([](ShaderValidationTest& t) {
        const CompositeCase& c = findComposite(t.param<std::string>("case"));
        const std::string wgsl =
            std::string("\nstruct S {\n  x : f32,\n  y : f32,\n}\n") +
            "const const_1e30 = f32(1e30);\n" +
            "override override_1e30 : f32;\n" +
            "fn foo() -> u32 {\n  let let_1e30 = f32(1e30);\n  " + c.code +
            "\n  return 0;\n}";

        if (std::string(c.stage) == "constant") {
            t.expectCompileResult(c.valid, wgsl);
        } else {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = c.valid;
            args.code = wgsl;
            args.constants["override_1e30"] = 1e30;
            args.reference = {"override_1e30", "foo()"};
            t.expectPipelineResult(args);
        }
    });

}  // namespace
