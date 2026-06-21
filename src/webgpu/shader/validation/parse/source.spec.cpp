// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/source.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,source",
    "Validation tests for source parsing");

CTS_TEST(g, "valid_source")
    .desc("Tests that a valid source is consumed successfully.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n    @fragment"
            "\n    fn main() -> @location(0) vec4<f32> {"
            "\n      return vec4<f32>(.4, .2, .3, .1);"
            "\n    }";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "empty")
    .desc("Test that an empty source is consumed successfully.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "");
    });

CTS_TEST(g, "invalid_source")
    .desc("Tests that a source which does not match the grammar fails.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "invalid_source");
    });

}  // namespace
