// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/workgroup_size.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - The `workgroup_size` test mirrors upstream's kWorkgroupSizeTests table:
//     each entry has `pass` and `pipeline` flags. `pipeline:true` entries are
//     validated at pipeline creation (expectPipelineResult with
//     addWorkgroupSize=false, since src already supplies @workgroup_size); the
//     rest are compile-only and the src is suffixed with `@compute fn main() {}`.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,workgroup_size", "Validation tests for workgroup_size");

struct WgsTest {
    const char* name;
    const char* src;
    bool pass;
    bool pipeline;
};

static const std::vector<WgsTest>& kWorkgroupSizeTests() {
    static const std::vector<WgsTest> tests = {
        {"x_only_float", "@workgroup_size(8f)", false, false},
        {"xy_only_float", "@workgroup_size(8, 8f)", false, false},
        {"xyz_float", "@workgroup_size(8, 8, 8f)", false, false},
        {"x_only_float_literal", "@workgroup_size(8.0)", false, false},
        {"xy_only_float_literal", "@workgroup_size(8, 8.0)", false, false},
        {"xyz_float_literal", "@workgroup_size(8, 8, 8.0)", false, false},
        {"empty", "@workgroup_size()", false, false},
        {"empty_x", "@workgroup_size(, 8)", false, false},
        {"empty_y", "@workgroup_size(8, , 8)", false, false},
        {"invalid_entry", "@workgroup_size(let)", false, false},
        {"x_only_abstract", "@workgroup_size(8)", true, false},
        {"xy_only_abstract", "@workgroup_size(8, 8)", true, false},
        {"xyz_abstract", "@workgroup_size(8, 8, 8)", true, false},
        {"x_only_unsigned", "@workgroup_size(8u)", true, false},
        {"xy_only_unsigned", "@workgroup_size(8u, 8u)", true, false},
        {"xyz_unsigned", "@workgroup_size(8u, 8u, 8u)", true, false},
        {"x_only_signed", "@workgroup_size(8i)", true, false},
        {"xy_only_signed", "@workgroup_size(8i, 8i)", true, false},
        {"xyz_signed", "@workgroup_size(8i, 8i, 8i)", true, false},
        {"x_only_hex", "@workgroup_size(0x1)", true, false},
        {"xy_only_hex", "@workgroup_size(0x1, 0x1)", true, false},
        {"xyz_hex", "@workgroup_size(0x1, 0x1, 0x1)", true, false},
        {"const_expr", "const a = 4;\n    const b = 5;\n    @workgroup_size(a, b, a + b)", true, false},
        {"override", "@id(42) override block_width = 12u;\n@workgroup_size(block_width)", true, true},
        {"override_no_default", "override block_width: i32;\n@workgroup_size(block_width)", true, false},
        {"override_no_default_pipe_fail", "override block_width: i32;\n@workgroup_size(block_width)", false, true},
        {"trailing_comma_x", "@workgroup_size(8, )", true, false},
        {"trailing_comma_y", "@workgroup_size(8, 8,)", true, false},
        {"trailing_comma_z", "@workgroup_size(8, 8, 8,)", true, false},
        {"override_expr", "override a = 3;\n    override b = 6;\n    @workgroup_size(a, b, a + b)", true, true},
        {"mixed_abstract_signed", "@workgroup_size(8, 8i)", true, false},
        {"mixed_abstract_unsigned", "@workgroup_size(8u, 8)", true, false},
        {"mixed_signed_unsigned", "@workgroup_size(8i, 8i, 8u)", false, false},
        {"zero_x", "@workgroup_size(0)", false, false},
        {"zero_y", "@workgroup_size(8, 0)", false, false},
        {"zero_z", "@workgroup_size(8, 8, 0)", false, false},
        {"negative_x", "@workgroup_size(-8)", false, false},
        {"negative_y", "@workgroup_size(8, -8)", false, false},
        {"negative_z", "@workgroup_size(8, 8, -8)", false, false},
        {"max_values", "@workgroup_size(256, 256, 64)", true, false},
        {"missing_left_paren", "@workgroup_size 1, 2, 3)", false, false},
        {"missing_right_paren", "@workgroup_size(1, 2, 3", false, false},
        {"misspelling", "@aworkgroup_size(1)", false, false},
        {"no_params", "@workgroup_size", false, false},
        {"multi_line", "@\nworkgroup_size(1)", true, false},
        {"comment", "@/* comment */workgroup_size(1)", true, false},
        {"mix_ux", "@workgroup_size(1u, 1i, 1i)", false, false},
        {"mix_uy", "@workgroup_size(1i, 1u, 1i)", false, false},
        {"mix_uz", "@workgroup_size(1i, 1i, 1u)", false, false},
        {"duplicate1", "@workgroup_size(1) @workgroup_size(1)", false, false},
        {"duplicate2", "@workgroup_size(1)\n@workgroup_size(2, 2, 2)", false, false},
    };
    return tests;
}

static std::vector<Value> wgsTestNames() {
    std::vector<Value> values;
    for (const WgsTest& w : kWorkgroupSizeTests()) {
        values.emplace_back(std::string(w.name));
    }
    return values;
}

static const WgsTest& findWgsTest(const std::string& name) {
    for (const WgsTest& w : kWorkgroupSizeTests()) {
        if (name == w.name) {
            return w;
        }
    }
    static const WgsTest dummy{"", "", false, false};
    return dummy;
}

CTS_TEST(g, "workgroup_size")
    .desc("Test validation of workgroup_size")
    .params([](ParamsBuilder u) {
        return u.combine("attr", wgsTestNames());
    })
    .fn([](ShaderValidationTest& t) {
        const WgsTest& test = findWgsTest(t.param<std::string>("attr"));
        if (test.pipeline) {
            ShaderValidationTest::PipelineArgs args;
            args.addWorkgroupSize = false;
            args.expectedResult = test.pass;
            args.code = std::string(test.src);
            t.expectPipelineResult(args);
        } else {
            const std::string code = std::string(" ") + test.src + "\n      @compute fn main() {}";
            t.expectCompileResult(test.pass, code);
        }
    });

CTS_TEST(g, "workgroup_size_fragment_shader")
    .desc("Test validation of workgroup_size on a fragment shader")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1)"
            "\n@fragment fn main(@builtin(position) pos: vec4<f32>) {}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "workgroup_size_vertex_shader")
    .desc("Test validation of workgroup_size on a vertex shader")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1)"
            "\n@vertex fn main() -> @builtin(position) vec4<f32> {}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "workgroup_size_function")
    .desc("Test validation of workgroup_size on user function")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1)"
            "\nfn my_func() {}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "workgroup_size_const")
    .desc("Test validation of workgroup_size on a const")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1)"
            "\nconst a : i32 = 4;"
            "\n"
            "\nfn my_func() {}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "workgroup_size_var")
    .desc("Test validation of workgroup_size on a var")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1)"
            "\n@group(1) @binding(1)"
            "\nvar<storage> a: i32;"
            "\n"
            "\nfn my_func() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "workgroup_size_fp16")
    .desc("Test validation of workgroup_size with fp16")
    .params([](ParamsBuilder u) {
        return u.combine("ext", {std::string(""), std::string("h")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ext = t.param<std::string>("ext");
        const std::string code =
            "\n@workgroup_size(1" + ext + ")"
            "\n@compute fn main() {}";
        t.expectCompileResult(ext == "", code);
    });

} // namespace
