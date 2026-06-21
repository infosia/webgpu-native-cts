// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/discard.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <map>
#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,discard",
    "Validation tests for discard");

CTS_TEST(g, "placement")
    .desc("Test that discard usage is validated")
    .params([](ParamsBuilder u) {
        return u.combine("place", {"compute", "vertex", "fragment", "module", "subfrag", "subvert",
                                   "subcomp"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string place = t.param<std::string>("place");

        std::map<std::string, std::string> pos = {
            {"module", ""}, {"subvert", ""},  {"subfrag", ""}, {"subcomp", ""},
            {"vertex", ""}, {"fragment", ""}, {"compute", ""},
        };
        pos[place] = "discard;";

        const std::string code =
            "\n" + pos["module"] +
            "\n\nfn subvert() {\n  " + pos["subvert"] +
            "\n}\n\n@vertex\nfn vtx() -> @builtin(position) vec4f {\n  " + pos["vertex"] +
            "\n  subvert();\n  return vec4f(1);\n}\n\nfn subfrag() {\n  " + pos["subfrag"] +
            "\n}\n\n@fragment\nfn frag() -> @location(0) vec4f {\n  " + pos["fragment"] +
            "\n  subfrag();\n  return vec4f(1);\n}\n\nfn subcomp() {\n  " + pos["subcomp"] +
            "\n}\n\n@compute\n@workgroup_size(1)\nfn comp() {\n  " + pos["compute"] +
            "\n  subcomp();\n}\n";

        const bool pass = place == "fragment" || place == "subfrag";
        t.expectCompileResult(pass, code);
    });

}  // namespace
