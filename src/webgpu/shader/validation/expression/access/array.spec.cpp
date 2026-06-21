// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/access/array.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// index_type uses `Type[t].create(0).wgsl()` upstream — the *value* form of the
// index type (i32 -> `i32(0)`, u32 -> `0u`, f32 -> `0.0f`, f16 -> `0.0h`,
// abstract-int -> `0`, abstract-float -> `0.0`, bool -> `false`, vec2i ->
// `vec2(i32(0), i32(0))`). Each is precomputed to match conversion.ts wgsl().
//
// result_type iterates kConcreteNumericScalarsAndVectors + kAllBoolScalarsAndVectors
// (in that order). The Record key is `type.toString()` (scalar -> kind e.g. `i32`,
// vector -> `vec2<i32>`). `requiresF16` and the `elementTypeOf(ty) === Type.bool`
// filter (skip elements==0 for bool-element types) are mirrored per row.
//
// early_eval_errors mirrors upstream kOutOfBoundsCases (a Record of struct cases).
// `Value` cannot hold these structs, so cases are keyed by `name` and looked up.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,access,array",
    "Validation tests for array access expressions");

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
                                 "\n    fn foo() {\n      var x = array(1,2,3);\n      let tmp = x[" +
                                 c.value + "];\n    }";
        const bool expect = type == "i32" || type == "u32" || type == "abstract-int";
        t.expectCompileResult(expect, code);
    });

// ---- result_type: kConcreteNumericScalarsAndVectors + kAllBoolScalarsAndVectors
struct ResultTypeRow {
    const char* key;     // type.toString() (Record key)
    bool requiresF16;
    bool boolElem;       // elementTypeOf(ty) === Type.bool
};

static const std::vector<ResultTypeRow>& kResultTypeRows() {
    static const std::vector<ResultTypeRow> v = {
        // kConcreteIntegerScalarsAndVectors (signed then unsigned)
        {"i32", false, false},
        {"vec2<i32>", false, false},
        {"vec3<i32>", false, false},
        {"vec4<i32>", false, false},
        {"u32", false, false},
        {"vec2<u32>", false, false},
        {"vec3<u32>", false, false},
        {"vec4<u32>", false, false},
        // kConcreteF16ScalarsAndVectors
        {"f16", true, false},
        {"vec2<f16>", true, false},
        {"vec3<f16>", true, false},
        {"vec4<f16>", true, false},
        // kConcreteF32ScalarsAndVectors
        {"f32", false, false},
        {"vec2<f32>", false, false},
        {"vec3<f32>", false, false},
        {"vec4<f32>", false, false},
        // kAllBoolScalarsAndVectors
        {"bool", false, true},
        {"vec2<bool>", false, true},
        {"vec3<bool>", false, true},
        {"vec4<bool>", false, true},
    };
    return v;
}

static std::vector<Value> resultTypeNames() {
    std::vector<Value> values;
    for (const ResultTypeRow& r : kResultTypeRows()) {
        values.emplace_back(std::string(r.key));
    }
    return values;
}

static const ResultTypeRow& findResultTypeRow(const std::string& key) {
    for (const ResultTypeRow& r : kResultTypeRows()) {
        if (key == r.key) {
            return r;
        }
    }
    static const ResultTypeRow dummy{"", false, false};
    return dummy;
}

CTS_TEST(g, "result_type")
    .desc("Tests that correct result type is produced for an access expression")
    .params([](ParamsBuilder u) {
        return u.combine("type", resultTypeNames())
            .combine("elements", {0, 4})
            .filter([](const ParamRecord& p) {
                const Value* elemVal = findParam(p, "elements");
                const Value* typeVal = findParam(p, "type");
                const int elements = elemVal != nullptr ? valueAs<int>(*elemVal) : 0;
                if (elements == 0 && typeVal != nullptr) {
                    if (findResultTypeRow(valueAs<std::string>(*typeVal)).boolElem) {
                        return false;
                    }
                }
                return true;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeKey = t.param<std::string>("type");
        const int elements = t.param<int>("elements");
        const ResultTypeRow& r = findResultTypeRow(typeKey);
        const std::string enable = r.requiresF16 ? "enable f16;" : "";
        const std::string arrayTy =
            elements == 0 ? "array<" + typeKey + ">"
                          : "array<" + typeKey + ", " + std::to_string(elements) + ">";
        const std::string module_decl =
            elements == 0 ? "@group(0) @binding(0) var<storage> x : " + arrayTy + ";" : "";
        const std::string function_decl = elements == 0 ? "" : "var x : " + arrayTy + ";";
        const std::string code = enable + "\n    " + module_decl + "\n    fn foo() {\n      " +
                                 function_decl + "\n      let tmp1 : " + typeKey +
                                 " = x[0];\n      let tmp2 : " + typeKey +
                                 " = x[1];\n      let tmp3 : " + typeKey + " = x[2];\n    }";
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
        {"const_module_in_bounds", "const x = array(1,2,3)[0];", true, false, 0},
        {"const_module_oob_neg", "const x = array(1,2,3)[-1];", false, false, 0},
        {"const_module_oob_pos", "const x = array(1,2,3)[3];", false, false, 0},
        {"const_func_in_bounds",
         "fn foo() {\n      const x = array(1,2,3)[0];\n    }", true, false, 0},
        {"const_func_oob_neg", "fn foo {\n      const x = array(1,2,3)[-1];\n    }", false, false, 0},
        {"const_func_oob_pos", "fn foo {\n      const x = array(1,2,3)[3];\n    }", false, false, 0},
        {"override_in_bounds",
         "override x : i32;\n    fn y() -> u32 {\n      let tmp = array(1,2,3)[x];\n      return "
         "0;\n    }",
         true, true, 0},
        {"override_oob_neg",
         "override x : i32;\n    fn y() -> u32 {\n      let tmp = array(1,2,3)[x];\n      return "
         "0;\n    }",
         false, true, -1},
        {"override_oob_pos",
         "override x : i32;\n    fn y() -> u32 {\n      let tmp = array(1,2,3)[x];\n      return "
         "0;\n    }",
         false, true, 3},
        {"runtime_in_bounds",
         "fn foo() {\n      let idx = 0;\n      let x = array(1,2,3)[idx];\n    }", true, false, 0},
        {"runtime_oob_neg",
         "fn foo() {\n      let idx = -1;\n      let x = array(1,2,3)[idx];\n    }", true, false, 0},
        {"runtime_oob_pos",
         "fn foo() {\n      let idx = 3;\n      let x = array(1,2,3)[idx];\n    }", true, false, 0},
        {"runtime_array_const_oob_neg",
         "@group(0) @binding(0) var<storage> x : array<u32>;\n    fn y() -> u32 {\n      let tmp = "
         "x[-1];\n      return 0;\n    }",
         false, false, 0},
        {"runtime_array_override_oob_neg",
         "@group(0) @binding(0) var<storage> v : array<u32>;\n    override x : i32;\n    fn y() -> "
         "u32 {\n      let tmp = v[x];\n      return 0;\n    }",
         false, true, -1},
        {"runtime_nested_array_override_oob_neg",
         "@group(0) @binding(0) var<storage> v : array<array<u32, 4>>;\n    override x : i32;\n    "
         "override w = 0u;\n    fn y() -> u32 {\n      let tmp = v[w][x];\n      return 0;\n    }",
         false, true, -1},
        {"runtime_nested_array_override_oob_pos",
         "@group(0) @binding(0) var<storage> v : array<array<u32,4>, 5>;\n    override x : i32;\n    "
         "override w = 0u;\n    fn y() -> u32 {\n      let tmp = v[w][x];\n      return 0;\n    }",
         false, true, 4},
        {"runtime_nested_array_override_pos",
         "@group(0) @binding(0) var<storage> v : array<array<u32,10>, 2>;\n    override x : i32;\n   "
         " override w = 0u;\n    fn y() -> u32 {\n      let tmp = v[w][x];\n      return 0;\n    }",
         true, true, 9},
        {"runtime_deep_nested_array_override_oob_pos",
         "@group(0) @binding(0) var<storage> v : array<array<array<u32, 3>, 4>, 5>;\n    override x "
         ": i32;\n    override w = 0u;\n    override u = 0u;\n    fn y() -> u32 {\n      let tmp = "
         "v[w][u][x];\n      return 0;\n    }",
         false, true, 3},
        {"runtime_deep_nested_array_override_pos",
         "@group(0) @binding(0) var<storage> v : array<array<array<u32, 3>, 4>, 5>;\n    override x "
         ": i32;\n    override w = 4u;\n    override u = 3u;\n    fn y() -> u32 {\n      let tmp = "
         "v[w][u][x];\n      return 0;\n    }",
         true, true, 2},
        {"runtime_structure_array_override_oob_neg",
         "\n      override x : i32;\n      struct S {\n        w : array<u32>\n      }\n      "
         "@group(0) @binding(0) var<storage> v : S;\n      fn y() -> u32 {\n        let tmp : u32 = "
         "v.w[x];\n        return 0;\n      }",
         false, true, -1},
        {"runtime_structure_array_override_pos",
         "\n      override x : i32;\n      struct S {\n        w : array<u32>\n      }\n      "
         "@group(0) @binding(0) var<storage> v : S;\n      fn y() -> u32 {\n        let tmp : u32 = "
         "v.w[x];\n        return 0;\n      }",
         true, true, 1},
        {"runtime_structure_array_override_oob_pos",
         "\n      override x : i32;\n      struct S {\n        w : array<u32, 5>\n      }\n      "
         "@group(0) @binding(0) var<storage> v : S;\n      fn y() -> u32 {\n        let tmp : u32 = "
         "v.w[x];\n        return 0;\n      }",
         false, true, 5},
        {"runtime_nested_structure_array_override_oob_pos",
         "\n      override x : i32;\n      struct S {\n        w : array<u32, 5>\n      }\n      "
         "struct S2 {\n        r : S\n      }\n      @group(0) @binding(0) var<storage> v : S2;\n     "
         " fn y() -> u32 {\n        let tmp : u32 = v.r.w[x];\n        return 0;\n      }",
         false, true, 5},
        {"runtime_nested_structure_array_override_pos",
         "\n      override x : i32;\n      struct S {\n        w : array<u32, 6>\n      }\n      "
         "struct S2 {\n        r : S\n      }\n      @group(0) @binding(0) var<storage> v : S2;\n     "
         " fn y() -> u32 {\n        let tmp : u32 = v.r.w[x];\n        return 0;\n      }",
         true, true, 5},
        {"override_array_cnt_size_zero_unsigned",
         "override x : u32;\n    var<workgroup> v : array<u32,x>;\n    fn y() -> u32 {\n      return "
         "v[0];\n    }",
         false, true, 0},
        {"override_array_cnt_size_zero_signed",
         "override x : i32;\n    var<workgroup> v : array<u32,x>;\n    fn y() -> u32 {\n      return "
         "v[0];\n    }",
         false, true, 0},
        {"override_array_cnt_size_neg",
         "override x : i32;\n    var<workgroup> v : array<u32,x>;\n    fn y() -> u32 {\n      return "
         "v[0];\n    }",
         false, true, -1},
        {"override_array_cnt_size_one",
         "override x : i32;\n    var<workgroup> v : array<u32,x>;\n    fn y() -> u32 {\n      return "
         "v[0];\n    }",
         true, true, 1},
        {"override_array_dynamic_type_checked_oob_pos",
         "@group(0) @binding(0) var<storage> v : array<array<array<u32, 3>, 4>, 5>;\n    override x "
         ": i32;\n    override w = 0u;\n    fn y() -> u32 {\n      var u = 0;\n      let tmp = "
         "v[w][u][x];\n      return 0;\n    }",
         false, true, 3},
        {"override_array_dynamic_type_checked_oob_neg",
         "@group(0) @binding(0) var<storage> v : array<array<array<u32, 3>, 4>, 5>;\n    override x "
         ": i32;\n    override w = 0u;\n    fn y() -> u32 {\n      var u = 0;\n      let tmp = "
         "v[w][u][x];\n      return 0;\n    }",
         false, true, -1},
        {"override_array_dynamic_type_checked_bounds",
         "@group(0) @binding(0) var<storage> v : array<array<array<u32, 3>, 4>, 5>;\n    override x "
         ": i32;\n    override w = 0u;\n    fn y() -> u32 {\n      var u = 0;\n      let tmp = "
         "v[w][u][x];\n      return 0;\n    }",
         true, true, 1},
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

CTS_TEST(g, "abstract_array_concrete_index")
    .desc("Tests that a concrete index type on an abstract array remains abstract")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n    const idx = 0i;\n    const_assert array(0xfffffffff,2,3)[idx] == 0xfffffffff;";
        t.expectCompileResult(true, code);
    });

}  // namespace
