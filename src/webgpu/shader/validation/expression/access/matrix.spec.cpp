// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/access/matrix.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// index_type uses `Type[t].create(0).wgsl()` upstream — the *value* form of the
// index type (e.g. i32 -> `i32(0)`, u32 -> `0u`, f32 -> `0.0f`, f16 -> `0.0h`,
// abstract-int -> `0`, abstract-float -> `0.0`, bool -> `false`, vec2i ->
// `vec2(i32(0), i32(0))`). Each value spelling is precomputed here to match
// conversion.ts wgsl() exactly. requiresF16() is true only for the f16 case.
//
// early_eval_errors mirrors upstream kOutOfBoundsCases (a Record of struct cases
// keyed by name). `Value` cannot hold these structs, so cases are keyed by their
// scalar `name` and looked up in the body.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,access,matrix",
    "Validation tests for matrix access expressions");

// ---- index_type --------------------------------------------------------------
struct IndexTypeCase {
    const char* name;
    const char* value;  // Type[t].create(0).wgsl()
    bool requiresF16;
};

static const std::vector<IndexTypeCase>& kIndexTypeCases() {
    static const std::vector<IndexTypeCase> v = {
        {"bool", "false", false},
        {"u32", "0u", false},
        {"i32", "i32(0)", false},
        {"abstract-int", "0", false},
        {"f32", "0.0f", false},
        {"f16", "0.0h", true},
        {"abstract-float", "0.0", false},
        {"vec2i", "vec2(i32(0), i32(0))", false},
    };
    return v;
}

static std::vector<Value> indexTypeNames() {
    std::vector<Value> values;
    for (const IndexTypeCase& c : kIndexTypeCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const IndexTypeCase& findIndexType(const std::string& name) {
    for (const IndexTypeCase& c : kIndexTypeCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const IndexTypeCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "index_type")
    .desc("Tests valid index types for array access expressions")
    .params([](ParamsBuilder u) { return u.combine("type", indexTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const IndexTypeCase& c = findIndexType(type);
        const std::string enable = c.requiresF16 ? "enable f16;" : "";
        const std::string code = enable +
                                 "\n    fn foo() {\n      var x = mat2x2(1,2,3,4);\n      let tmp = x[" +
                                 c.value + "];\n    }";
        const bool expect = type == "i32" || type == "u32" || type == "abstract-int";
        t.expectCompileResult(expect, code);
    });

// ---- result_type -------------------------------------------------------------
CTS_TEST(g, "result_type")
    .desc("Tests that correct result type is produced for an access expression")
    .params([](ParamsBuilder u) {
        return u.combine("element", {"f16", "f32"})
            .combine("columns", {2, 3, 4})
            .beginSubcases()
            .combine("rows", {2, 3, 4})
            .combine("decl", {"function", "module"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string element = t.param<std::string>("element");
        const int columns = t.param<int>("columns");
        const int rows = t.param<int>("rows");
        const std::string decl = t.param<std::string>("decl");

        const std::string enable = element == "f16" ? "enable f16;" : "";
        const std::string vectorTy = "vec" + std::to_string(rows) + "<" + element + ">";
        const std::string matrixTy =
            "mat" + std::to_string(columns) + "x" + std::to_string(rows) + "<" + element + ">";
        const std::string module_decl =
            decl == "module" ? "@group(0) @binding(0) var<storage> x : " + matrixTy + ";" : "";
        const std::string function_decl = decl == "module" ? "" : "var x : " + matrixTy + ";";
        const std::string code = enable + "\n    " + module_decl + "\n    fn foo() {\n      " +
                                 function_decl + "\n      let tmp1 : " + vectorTy +
                                 " = x[0];\n      let tmp2 : " + vectorTy + " = x[1];\n    }";
        t.expectCompileResult(true, code);
    });

// ---- early_eval_errors: kOutOfBoundsCases (object key order preserved) --------
struct OutOfBoundsCase {
    const char* name;
    const char* code;
    bool result;
    bool pipeline;
    int value;
};

static const std::vector<OutOfBoundsCase>& kOutOfBoundsCases() {
    static const std::vector<OutOfBoundsCase> v = {
        {"const_module_in_bounds", "const x = mat2x2(1,2,3,4)[0];", true, false, 0},
        {"const_module_oob_neg", "const x = mat2x2(1,2,3,4)[-1];", false, false, 0},
        {"const_module_oob_pos", "const x = mat2x2(1,2,3,4)[2];", false, false, 0},
        {"const_func_in_bounds",
         "fn foo() {\n      const x = mat2x2(1,2,3,4)[0];\n    }", true, false, 0},
        {"const_func_oob_neg",
         "fn foo {\n      const x = mat2x2(1,2,3,4)[-1];\n    }", false, false, 0},
        {"const_func_oob_pos",
         "fn foo {\n      const x = mat2x2(1,2,3,4)[2];\n    }", false, false, 0},
        {"override_in_bounds",
         "override x : i32;\n    fn y() -> u32 {\n      let tmp = mat2x2(1,2,3,4)[x];\n      return "
         "0;\n    }",
         true, true, 0},
        {"override_oob_neg",
         "override x : i32;\n    fn y() -> u32 {\n      let tmp = mat2x2(1,2,3,4)[x];\n      return "
         "0;\n    }",
         false, true, -1},
        {"override_oob_pos",
         "override x : i32;\n    fn y() -> u32 {\n      let tmp = mat2x2(1,2,3,4)[x];\n      return "
         "0;\n    }",
         false, true, 2},
        {"runtime_in_bounds",
         "fn foo() {\n      let idx = 0;\n      let x = mat2x2(1,2,3,4)[idx];\n    }", true, false, 0},
        {"runtime_oob_neg",
         "fn foo() {\n      let idx = -1;\n      let x = mat2x2(1,2,3,4)[idx];\n    }", true, false,
         0},
        {"runtime_oob_pos",
         "fn foo() {\n      let idx = 3;\n      let x = mat2x2(1,2,3,4)[idx];\n    }", true, false, 0},
        {"runtime_array_const_oob_neg",
         "@group(0) @binding(0) var<storage> x : mat2x2<f32>;\n    fn y() -> u32 {\n      let tmp = "
         "x[-1];\n      return 0;\n    }",
         false, false, 0},
        {"runtime_array_override_oob_neg",
         "@group(0) @binding(0) var<storage> v : mat2x2<f32>;\n    override x : i32;\n    fn y() -> "
         "u32 {\n      let tmp = v[x];\n      return 0;\n    }",
         false, true, -1},
    };
    return v;
}

static std::vector<Value> outOfBoundsNames() {
    std::vector<Value> values;
    for (const OutOfBoundsCase& c : kOutOfBoundsCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const OutOfBoundsCase& findOutOfBounds(const std::string& name) {
    for (const OutOfBoundsCase& c : kOutOfBoundsCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const OutOfBoundsCase dummy{"", "", false, false, 0};
    return dummy;
}

CTS_TEST(g, "early_eval_errors")
    .desc("Tests early evaluation errors for out-of-bounds indexing")
    .params([](ParamsBuilder u) { return u.combine("case", outOfBoundsNames()); })
    .fn([](ShaderValidationTest& t) {
        const OutOfBoundsCase& testcase = findOutOfBounds(t.param<std::string>("case"));
        if (testcase.pipeline) {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = testcase.result;
            args.code = testcase.code;
            args.constants = {{"x", static_cast<double>(testcase.value)}};
            args.reference = {"y()"};
            t.expectPipelineResult(args);
        } else {
            t.expectCompileResult(testcase.result, testcase.code);
        }
    });

CTS_TEST(g, "abstract_matrix_concrete_index")
    .desc("Tests that a concrete index type on an abstract array remains abstract")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n    const idx = 0i;\n    const_assert "
            "mat2x2(1.11001100110011008404,1,1,1)[0i][0i] == 1.11001100110011008404;";
        t.expectCompileResult(true, code);
    });

}  // namespace
