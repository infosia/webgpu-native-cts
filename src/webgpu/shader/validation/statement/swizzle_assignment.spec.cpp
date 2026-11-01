// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/swizzle_assignment.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,swizzle_assignment",
    "Validation tests for swizzle assignments.\n");

CTS_TEST(g, "valid")
    .desc("Valid swizzle assignments")
    .params([](ParamsBuilder u) {
        return u.combine("elemType", {"f32", "i32", "u32"}).combine("vecSize", {2, 3, 4});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string elemType = t.param<std::string>("elemType");
        const int vecSize = t.param<int>("vecSize");
        const std::string swizzle = std::string("xyzw").substr(0, static_cast<size_t>(vecSize));
        const std::string vecType =
            "vec" + std::to_string(swizzle.length()) + "<" + elemType + ">";
        const std::string code = "\n@fragment\nfn main() {\n  var v = vec4<" + elemType +
                                 ">(0);\n  v." + swizzle + " = " + vecType + "(1);\n}\n";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "invalid_lhs_not_reference")
    .desc("Invalid swizzle assignment where LHS is not a reference")
    .params([](ParamsBuilder u) {
        return u.combine("lhs", {"vec4f()", "const_vec", "foo()"});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string lhs = t.param<std::string>("lhs");
        const std::string code =
            "\nconst const_vec = vec4f();\n\nfn foo() -> vec4f {\n  return vec4f();\n}\n\n@fragment"
            "\nfn main() {\n  var v = vec4f();\n  " +
            lhs + ".xyz = vec3(0.0);\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_duplicate_components")
    .desc("Invalid swizzle assignment with duplicate LHS components")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xx = vec2f();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_component_mismatch")
    .desc("Invalid swizzle assignment with mismatched number of components")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xy = vec3f();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_component_oob")
    .desc("Invalid swizzle assignment with components out of bounds")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec3f();\n  v.xyzw = vec4f();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_type_mismatch")
    .desc("Invalid swizzle assignment with mismatched types")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xy = vec2i();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_mixed_letter_schemes")
    .desc("Invalid swizzle assignment with mixed letter schemes (xyzw vs. rgba)")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xr = vec2i();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_address_of_swizzle_view")
    .desc("Invalid to take the address of a swizzle view")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  let p = &v.xy;\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "index_into_swizzle_view")
    .desc("Validate constant indexing into a swizzle view on the lhs")
    .params([](ParamsBuilder u) {
        return u.combine("swizzle", {"x", "xy", "xx", "xyz", "xyzw", "zxy"})
            .combine("index", {0, 1, 2, 3, 4});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string swizzle = t.param<std::string>("swizzle");
        const int index = t.param<int>("index");

        const std::string code = "\n@fragment\nfn main() {\n  var v = vec4u();\n  v." + swizzle +
                                 "[" + std::to_string(index) + "] = 1u;\n}\n";

        const bool isVector = swizzle.length() > 1;
        const bool inBounds = static_cast<size_t>(index) < swizzle.length();
        const bool expected = isVector && inBounds;

        t.expectCompileResult(expected, code);
    });

CTS_TEST(g, "dynamic_index_into_swizzle_view")
    .desc("Validate dynamic indexing into a swizzle view on the lhs")
    .params([](ParamsBuilder u) {
        return u.combine("swizzle", {"x", "xy", "xx", "xyz", "xyzw", "zxy"});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string swizzle = t.param<std::string>("swizzle");

        const std::string code = "\n@fragment\nfn main() {\n  var v = vec4u();\n  var i = 1u;\n  v." +
                                 swizzle + "[i] = 1u;\n}\n";

        const bool isVector = swizzle.length() > 1;

        t.expectCompileResult(isVector, code);
    });

CTS_TEST(g, "pointer_swizzle_assignment")
    .desc("Validate swizzle assignments on pointers")
    .params([](ParamsBuilder u) {
        return u.combine("use_pointer", {true, false})
            .combine("requires_pointer_composite_access", {true, false})
            .combine("use_compound", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const bool use_pointer = t.param<bool>("use_pointer");
        const bool requires_pointer_composite_access =
            t.param<bool>("requires_pointer_composite_access");
        const bool use_compound = t.param<bool>("use_compound");

        const std::string requires_directive =
            requires_pointer_composite_access ? "requires pointer_composite_access;" : "";

        const std::string lhs = use_pointer ? "p.xy" : "(*p).xy";
        const std::string op = use_compound ? "+=" : "=";

        const std::string code = "\n" + requires_directive +
                                 "\n@fragment\nfn main() {\n  var v = vec4f();\n  let p = &v;\n  " +
                                 lhs + " " + op + " vec2f(1.0);\n}\n";

        const bool has_feature = t.hasLanguageFeature("pointer_composite_access");
        const bool expected = !use_pointer || has_feature;

        t.expectCompileResult(expected, code);
    });

// kChainedSwizzleCases (object key order preserved).
struct ChainedSwizzleCase {
    const char* name;
    const char* chain;
    int size;
    bool pass;
};

static const std::vector<ChainedSwizzleCase>& kChainedSwizzleCases() {
    static const std::vector<ChainedSwizzleCase> cases = {
        // Valid cases:
        {"xy_y", "xy.y", 1, true},                // y
        {"xx_x", "xx.x", 1, true},                // x
        {"xyy_xy", "xyy.xy", 2, true},            // xy
        {"xxyy_zxy_xy", "xxyy.zxy.xy", 2, true},  // yx
        // Invalid cases:
        {"xxyy_xy", "xxyy.xy", 2, false},         // xx
        {"xyy_yy", "xyy.yy", 2, false},           // yy
        {"xxyy_zxy_yz", "xxyy.zxy.yz", 2, false}, // xx
        {"zy_xyx", "zy.xyx", 3, false},           // zyz
    };
    return cases;
}

static std::vector<Value> chainedSwizzleCaseNames() {
    std::vector<Value> values;
    for (const ChainedSwizzleCase& c : kChainedSwizzleCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ChainedSwizzleCase& findChainedSwizzleCase(const std::string& name) {
    for (const ChainedSwizzleCase& c : kChainedSwizzleCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ChainedSwizzleCase dummy{"", "", 1, false};
    return dummy;
}

CTS_TEST(g, "chained_swizzle")
    .desc("Validate chained swizzles on the LHS of an assignment")
    .params([](ParamsBuilder u) {
        return u.combine("case", chainedSwizzleCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const ChainedSwizzleCase& c = findChainedSwizzleCase(t.param<std::string>("case"));
        const std::string rhs =
            c.size == 1 ? std::string("1.0") : "vec" + std::to_string(c.size) + "f(1.0)";
        const std::string code = "\n@fragment\nfn main() {\n  var v = vec4f();\n  v." +
                                 std::string(c.chain) + " = " + rhs + ";\n}\n";
        const bool expected = c.pass && t.hasLanguageFeature("swizzle_assignment");
        t.expectCompileResult(expected, code);
    });

}  // namespace
