// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/pipeline_stage.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// All eight g.test use the compile-only path (t.expectCompileResult); none
// validate at pipeline creation.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,pipeline_stage",
    "Validation tests for pipeline stage");

// Decode the test-string escapes before emitting WGSL:
//   - '^'  -> '*'  (mirrors upstream `.replace(/\^/g, '*')`, keeps comment
//                   markers out of JS regex/test strings),
//   - "\t" (literal backslash-t two-char sequence) -> a real TAB. Upstream uses a
//     literal TAB in the value, but a raw control character cannot round-trip
//     through our query serializer, so the case tables store the escaped form
//     (query identity uses `@\tvertex`) and we decode it here for the WGSL.
static std::string replaceCaret(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '^') {
            out += '*';
        } else if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == 't') {
            out += '\t';
            ++i;
        } else {
            out += in[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// vertex_parsing / fragment_parsing / compute_parsing share a value-set shape.
// ---------------------------------------------------------------------------
struct StageCase {
    const char* val;  // the attribute text (may contain '^' placeholders)
    bool valid;       // whether this belongs to the valid set
};

static std::vector<Value> caseVals(const std::vector<StageCase>& cases) {
    std::vector<Value> values;
    for (const StageCase& c : cases) {
        values.emplace_back(std::string(c.val));
    }
    return values;
}

static bool lookupValid(const std::vector<StageCase>& cases, const std::string& val) {
    for (const StageCase& c : cases) {
        if (val == c.val) {
            return c.valid;
        }
    }
    return false;
}

// kValidVertex + kInvalidVertex (insertion order preserved for query identity).
static const std::vector<StageCase>& kVertexCases() {
    static const std::vector<StageCase> v = {
        {"", true},
        {"@vertex", true},
        {"@\\tvertex", true},
        {"@/^comment^/vertex", true},
        {"@mvertex", false},
        {"@vertex()", false},
        {"@vertex )", false},
        {"@vertex(", false},
    };
    return v;
}

static const std::vector<StageCase>& kFragmentCases() {
    static const std::vector<StageCase> v = {
        {"", true},
        {"@fragment", true},
        {"@\\tfragment", true},
        {"@/^comment^/fragment", true},
        {"@mfragment", false},
        {"@fragment()", false},
        {"@fragment )", false},
        {"@fragment(", false},
    };
    return v;
}

static const std::vector<StageCase>& kComputeCases() {
    static const std::vector<StageCase> v = {
        {"", true},
        {"@compute", true},
        {"@\\tcompute", true},
        {"@/^comment^/compute", true},
        {"@mcompute", false},
        {"@compute()", false},
        {"@compute )", false},
        {"@compute(", false},
    };
    return v;
}

CTS_TEST(g, "vertex_parsing")
    .desc("Test that @vertex is parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("val", caseVals(kVertexCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rawVal = t.param<std::string>("val");
        const std::string v = replaceCaret(rawVal);
        const std::string r = rawVal != "" ? "@builtin(position)" : "";
        const std::string code =
            "\n" + v +
            "\nfn main() -> " + r + " vec4<f32> {"
            "\n  return vec4<f32>(.4, .2, .3, .1);"
            "\n}";
        t.expectCompileResult(lookupValid(kVertexCases(), rawVal), code);
    });

CTS_TEST(g, "fragment_parsing")
    .desc("Test that @fragment is parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("val", caseVals(kFragmentCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rawVal = t.param<std::string>("val");
        const std::string v = replaceCaret(rawVal);
        const std::string r = rawVal != "" ? "@location(0)" : "";
        const std::string code =
            "\n" + v +
            "\nfn main() -> " + r + " vec4<f32> {"
            "\n  return vec4<f32>(.4, .2, .3, .1);"
            "\n}";
        t.expectCompileResult(lookupValid(kFragmentCases(), rawVal), code);
    });

CTS_TEST(g, "compute_parsing")
    .desc("Test that @compute is parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("val", caseVals(kComputeCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rawVal = t.param<std::string>("val");
        std::string v = replaceCaret(rawVal);
        // Always add a workgroup size unless there is no parameter.
        if (v != "") {
            v += "\n@workgroup_size(1)";
        }
        const std::string code =
            "\n" + v +
            "\nfn main() {}"
            "\n";
        t.expectCompileResult(lookupValid(kComputeCases(), rawVal), code);
    });

CTS_TEST(g, "multiple_entry_points")
    .desc("Test that multiple entry points are allowed.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@compute @workgroup_size(1) fn compute_1() {}"
            "\n@compute @workgroup_size(1) fn compute_2() {}"
            "\n"
            "\n@fragment fn frag_1() -> @location(2) vec4f { return vec4f(1); }"
            "\n@fragment fn frag_2() -> @location(2) vec4f { return vec4f(1); }"
            "\n@fragment fn frag_3() -> @location(2) vec4f { return vec4f(1); }"
            "\n"
            "\n@vertex fn vtx_1() -> @builtin(position) vec4f { return vec4f(1); }"
            "\n@vertex fn vtx_2() -> @builtin(position) vec4f { return vec4f(1); }"
            "\n@vertex fn vtx_3() -> @builtin(position) vec4f { return vec4f(1); }"
            "\n";
        t.expectCompileResult(true, code);
    });

// ---------------------------------------------------------------------------
// extra_on_{compute,fragment,vertex}_function share the same param shape.
// ---------------------------------------------------------------------------
static std::vector<Value> extraVals() {
    return {std::string(""), std::string("@compute"), std::string("@fragment"),
            std::string("@vertex")};
}

CTS_TEST(g, "extra_on_compute_function")
    .desc("Test that an extra stage attribute on @compute functions are not allowed.")
    .params([](ParamsBuilder u) {
        return u.combine("extra", extraVals()).combine("before", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string extra = t.param<std::string>("extra");
        const bool beforeFlag = t.param<bool>("before");
        const std::string before = beforeFlag ? extra : "";
        const std::string after = beforeFlag ? "" : extra;
        const std::string code =
            "\n" + before + " @compute " + after + " @workgroup_size(1) fn main() {}"
            "\n";
        t.expectCompileResult(extra == "", code);
    });

CTS_TEST(g, "extra_on_fragment_function")
    .desc("Test that an extra stage attribute on @fragment functions are not allowed.")
    .params([](ParamsBuilder u) {
        return u.combine("extra", extraVals()).combine("before", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string extra = t.param<std::string>("extra");
        const bool beforeFlag = t.param<bool>("before");
        const std::string before = beforeFlag ? extra : "";
        const std::string after = beforeFlag ? "" : extra;
        const std::string code =
            "\n" + before + " @fragment " + after +
            " fn main() -> @location(0) vec4f { return vec4f(1); }"
            "\n";
        t.expectCompileResult(extra == "", code);
    });

CTS_TEST(g, "extra_on_vertex_function")
    .desc("Test that an extra stage attribute on @vertex functions are not allowed.")
    .params([](ParamsBuilder u) {
        return u.combine("extra", extraVals()).combine("before", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string extra = t.param<std::string>("extra");
        const bool beforeFlag = t.param<bool>("before");
        const std::string before = beforeFlag ? extra : "";
        const std::string after = beforeFlag ? "" : extra;
        const std::string code =
            "\n" + before + " @vertex " + after +
            " fn main() -> @builtin(position) vec4f { return vec4f(1); }"
            "\n";
        t.expectCompileResult(extra == "", code);
    });

CTS_TEST(g, "placement")
    .desc("Tests the locations @align is allowed to appear")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {std::string("private-var"), std::string("storage-var"),
                                   std::string("struct-member"), std::string("fn-param"),
                                   std::string("fn-var"), std::string("fn-return"),
                                   std::string("while-stmt"), Value::undef()})
                .combine("attr", {std::string("@compute"), std::string("@fragment"),
                                  std::string("@vertex")});
    })
    .fn([](ShaderValidationTest& t) {
        const bool scopeUndefined = t.paramIsUndefined("scope");
        const std::string scope = scopeUndefined ? "" : t.param<std::string>("scope");
        const std::string attr = t.param<std::string>("attr");

        auto at = [&](const char* s) { return scope == s ? attr : std::string(""); };
        const std::string code =
            "\n      " + at("private-var") +
            "\n      var<private> priv_var : i32;"
            "\n"
            "\n      " + at("storage-var") +
            "\n      @group(0) @binding(0)"
            "\n      var<storage> stor_var : i32;"
            "\n"
            "\n      struct A {"
            "\n        " + at("struct-member") +
            "\n        a : i32,"
            "\n      }"
            "\n"
            "\n      @vertex"
            "\n      fn f("
            "\n        " + at("fn-param") +
            "\n        @location(0) b : i32,"
            "\n      ) -> " + at("fn-return") + " @builtin(position) vec4f {"
            "\n        " + at("fn-var") +
            "\n        var<function> func_v : i32;"
            "\n"
            "\n        " + at("while-stmt") +
            "\n        while false {}"
            "\n"
            "\n        return vec4(1, 1, 1, 1);"
            "\n      }"
            "\n    ";
        t.expectCompileResult(scopeUndefined, code);
    });

} // namespace
