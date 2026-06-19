// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/assignment_statement.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,decl,assignment_statement",
    "Validation tests for assignment statements.");

CTS_TEST(g, "scalar_assignment")
    .desc("Test simple scalar assignments.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n      @fragment"
            "\n      fn main() {"
            "\n        var a: i32 = 0;"
            "\n        a = 1;"
            "\n        let b: f32 = 0.0;"
            "\n        var c: f32;"
            "\n        c = b;"
            "\n      }"
            "\n    ";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "vector_full_assignment")
    .desc("Test full vector assignments.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n      @fragment"
            "\n      fn main() {"
            "\n        var v1: vec3<f32> = vec3(0.0, 0.0, 0.0);"
            "\n        var v2: vec3<f32>;"
            "\n        v2 = v1;"
            "\n        v2 = vec3(1.0, 2.0, 3.0);"
            "\n      }"
            "\n    ";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "vector_indexed_assignment")
    .desc("Test vector indexed assignments.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n      @fragment"
            "\n      fn main() {"
            "\n        var v: vec3<i32> = vec3(0, 0, 0);"
            "\n        v[0] = 1;"
            "\n        v[2] = 5;"
            "\n      }"
            "\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kSwizzleTests. `pass` depends on the `swizzle_assignment`
// language feature for the multi/swizzleswizzle cases.
struct SwizzleCase {
    const char* name;
    const char* src;
    bool requiresSwizzleAssignment; // false => always pass; true => pass iff feature
};

static const std::vector<SwizzleCase>& kSwizzleTests() {
    static const std::vector<SwizzleCase> cases = {
        {"single", "v.x = 1.0", false},
        {"multi", "v.xy = vec2(1.0, 2.0)", true},
        {"swizzleswizzle", "v.xy.x = 1.0", true},
    };
    return cases;
}

static std::vector<Value> swizzleCaseNames() {
    std::vector<Value> values;
    for (const SwizzleCase& c : kSwizzleTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const SwizzleCase& findSwizzleCase(const std::string& name) {
    for (const SwizzleCase& c : kSwizzleTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const SwizzleCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "vector_swizzle_assignment")
    .desc("Test vector swizzle assignments.")
    .params([](ParamsBuilder u) {
        return u.combine("case", swizzleCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const SwizzleCase& c = findSwizzleCase(t.param<std::string>("case"));
        const std::string wgsl =
            std::string("\n      @fragment"
                        "\n      fn main() {"
                        "\n        var v: vec4<f32> = vec4(0.0, 0.0, 0.0, 0.0);"
                        "\n        ") +
            c.src + ";" +
            "\n      }";
        const bool pass =
            c.requiresSwizzleAssignment ? t.hasLanguageFeature("swizzle_assignment") : true;
        t.expectCompileResult(pass, wgsl);
    });

CTS_TEST(g, "compound_assignment_with_swizzle")
    .desc("Test compound assignment of a vector with a swizzle on the rhs.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n      @fragment"
            "\n      fn main() {"
            "\n        var v: vec3<i32> = vec3(1, 2, 3);"
            "\n        var w: vec4<i32> = vec4(10);"
            "\n        v *= w.xyz;"
            "\n      }"
            "\n    ";
        t.expectCompileResult(true, code);
    });

} // namespace
