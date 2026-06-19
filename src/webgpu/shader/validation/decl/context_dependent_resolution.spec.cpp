// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/context_dependent_resolution.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Tests that context dependent names do not participate in name resolution.
//
// NOTE: isCompatibility is false in this harness (Dawn runs non-compat), so all
// upstream beforeAllSubcases compat skips (sample_mask/sample_index/linear/
// sample/first) are replicated as conditions that never fire, and the non-compat
// `@interpolate(flat)` / `@interpolate(linear)` / `@interpolate(perspective,...)`
// spellings are used.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,decl,context_dependent_resolution",
    "Tests that context dependent names do not participate in name resolution.");

const std::vector<Value>& declValues() {
    static const std::vector<Value> v = {Value("override"), Value("const"), Value("var<private>")};
    return v;
}

struct NameCase {
    const char* name;
    const char* src;
};

static std::vector<Value> caseNames(const std::vector<NameCase>& cases) {
    std::vector<Value> values;
    for (const NameCase& c : cases) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const NameCase& findCase(const std::vector<NameCase>& cases, const std::string& name) {
    for (const NameCase& c : cases) {
        if (name == c.name) {
            return c;
        }
    }
    static const NameCase dummy{"", ""};
    return dummy;
}

// Mirrors upstream kAttributeCases (object key order preserved).
static const std::vector<NameCase>& kAttributeCases() {
    static const std::vector<NameCase> cases = {
        {"align", "struct S { @align(16) x : u32 }"},
        {"binding", "@group(0) @binding(0) var s : sampler;"},
        {"builtin", "@vertex fn main() -> @builtin(position) vec4f { return vec4f(); }"},
        {"group", "@group(0) @binding(0) var s : sampler;"},
        {"id", "@id(1) override x : i32;"},
        {"interpolate",
         "@fragment fn main(@location(0) @interpolate(flat, either) x : i32) { }"},
        {"invariant", "@fragment fn main(@builtin(position) @invariant pos : vec4f) { }"},
        {"location", "@fragment fn main(@location(0) x : f32) { }"},
        {"must_use", "@must_use fn foo() -> u32 { return 0; }"},
        {"size", "struct S { @size(4) x : u32 }"},
        {"workgroup_size", "@compute @workgroup_size(1) fn main() { }"},
        {"compute", "@compute @workgroup_size(1) fn main() { }"},
        {"fragment", "@fragment fn main() { }"},
        {"vertex", "@vertex fn main() -> @builtin(position) vec4f { return vec4f(); }"},
    };
    return cases;
}

CTS_TEST(g, "attribute_names")
    .desc("Tests attribute names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kAttributeCases()))
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseName = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const NameCase& c = findCase(kAttributeCases(), caseName);
        const std::string code =
            "\n    " + decl + " " + caseName + " : u32 = 0;\n    " + c.src +
            "\n    fn use_var() -> u32 {\n      return " + caseName + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kBuiltinCases (object key order preserved).
static const std::vector<NameCase>& kBuiltinCases() {
    static const std::vector<NameCase> cases = {
        {"vertex_index",
         "\n  @vertex\n  fn main(@builtin(vertex_index) idx : u32) -> @builtin(position) vec4f\n "
         " { return vec4f(); }"},
        {"instance_index",
         "\n  @vertex\n  fn main(@builtin(instance_index) idx : u32) -> @builtin(position) "
         "vec4f\n  { return vec4f(); }"},
        {"position_vertex",
         "\n  @vertex fn main() -> @builtin(position) vec4f\n  { return vec4f(); }"},
        {"position_fragment", "@fragment fn main(@builtin(position) pos : vec4f) { }"},
        {"front_facing", "@fragment fn main(@builtin(front_facing) x : bool) { }"},
        {"frag_depth", "@fragment fn main() -> @builtin(frag_depth) f32 { return 0; }"},
        {"sample_index", "@fragment fn main(@builtin(sample_index) x : u32) { }"},
        {"sample_mask_input", "@fragment fn main(@builtin(sample_mask) x : u32) { }"},
        {"sample_mask_output", "@fragment fn main() -> @builtin(sample_mask) u32 { return 0; }"},
        {"local_invocation_id",
         "\n  @compute @workgroup_size(1)\n  fn main(@builtin(local_invocation_id) id : vec3u) { "
         "}"},
        {"local_invocation_index",
         "\n  @compute @workgroup_size(1)\n  fn main(@builtin(local_invocation_index) id : u32) "
         "{ }"},
        {"global_invocation_id",
         "\n  @compute @workgroup_size(1)\n  fn main(@builtin(global_invocation_id) id : vec3u) "
         "{ }"},
        {"workgroup_id",
         "\n  @compute @workgroup_size(1)\n  fn main(@builtin(workgroup_id) id : vec3u) { }"},
        {"num_workgroups",
         "\n  @compute @workgroup_size(1)\n  fn main(@builtin(num_workgroups) id : vec3u) { }"},
    };
    return cases;
}

CTS_TEST(g, "builtin_value_names")
    .desc("Tests builtin value names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kBuiltinCases()))
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        // beforeAllSubcases: isCompatibility == false in this harness, so the
        // sample_mask / sample_index compat skips never fire.
        const std::string caseName = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const NameCase& c = findCase(kBuiltinCases(), caseName);
        const std::string code =
            "\n    " + decl + " " + caseName + " : u32 = 0;\n    " + c.src +
            "\n    fn use_var() -> u32 {\n      return " + caseName + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kDiagnosticSeverityCases (object key order preserved).
static const std::vector<NameCase>& kDiagnosticSeverityCases() {
    static const std::vector<NameCase> cases = {
        {"error",
         "\n  diagnostic(error, derivative_uniformity);\n  @diagnostic(error, "
         "derivative_uniformity) fn foo() { }\n  "},
        {"warning",
         "\n  diagnostic(warning, derivative_uniformity);\n  @diagnostic(warning, "
         "derivative_uniformity) fn foo() { }\n  "},
        {"off",
         "\n  diagnostic(off, derivative_uniformity);\n  @diagnostic(off, "
         "derivative_uniformity) fn foo() { }\n  "},
        {"info",
         "\n  diagnostic(info, derivative_uniformity);\n  @diagnostic(info, "
         "derivative_uniformity) fn foo() { }\n  "},
    };
    return cases;
}

CTS_TEST(g, "diagnostic_severity_names")
    .desc("Tests diagnostic severity names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kDiagnosticSeverityCases()))
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseName = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const NameCase& c = findCase(kDiagnosticSeverityCases(), caseName);
        const std::string code =
            std::string("\n    ") + c.src + "\n    " + decl + " " + caseName +
            " : u32 = 0;\n    fn use_var() -> u32 {\n      return " + caseName + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kDiagnosticRuleCases (object key order preserved).
static const std::vector<NameCase>& kDiagnosticRuleCases() {
    static const std::vector<NameCase> cases = {
        {"derivative_uniformity",
         "\n  diagnostic(off, derivative_uniformity);\n  @diagnostic(warning, "
         "derivative_uniformity) fn foo() { }"},
        {"unknown_rule",
         "\n  diagnostic(off, unknown_rule);\n  @diagnostic(warning, unknown_rule) fn foo() { }"},
        {"unknown",
         "\n  diagnostic(off, unknown.rule);\n  @diagnostic(warning, unknown.rule) fn foo() { }"},
        {"rule",
         "\n  diagnostic(off, unknown.rule);\n  @diagnostic(warning, unknown.rule) fn foo() { }"},
    };
    return cases;
}

CTS_TEST(g, "diagnostic_rule_names")
    .desc("Tests diagnostic rule names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kDiagnosticRuleCases()))
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseName = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const NameCase& c = findCase(kDiagnosticRuleCases(), caseName);
        const std::string code =
            std::string("\n    ") + c.src + "\n    " + decl + " " + caseName +
            " : u32 = 0;\n    fn use_var() -> u32 {\n      return " + caseName + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kEnableCases (object key order preserved).
static const std::vector<NameCase>& kEnableCases() {
    static const std::vector<NameCase> cases = {
        {"f16", "enable f16;"},
    };
    return cases;
}

CTS_TEST(g, "enable_names")
    .desc("Tests enable extension names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kEnableCases()))
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_ShaderF16, "shader-f16");
        const std::string caseName = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const NameCase& c = findCase(kEnableCases(), caseName);
        const std::string code =
            std::string("\n    ") + c.src + "\n    " + decl + " " + caseName +
            " : u32 = 0;\n    fn use_var() -> u32 {\n      return " + caseName + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kLanguageCases (object key order preserved).
static const std::vector<NameCase>& kLanguageCases() {
    static const std::vector<NameCase> cases = {
        {"readonly_and_readwrite_storage_textures",
         "requires readonly_and_readwrite_storage_textures;"},
        {"packed_4x8_integer_dot_product", "requires packed_4x8_integer_dot_product;"},
        {"unrestricted_pointer_parameters", "requires unrestricted_pointer_parameters;"},
        {"pointer_composite_access", "requires pointer_composite_access;"},
    };
    return cases;
}

CTS_TEST(g, "language_names")
    .desc("Tests language extension names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kLanguageCases()))
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseName = t.param<std::string>("case");
        if (!t.hasLanguageFeature(caseName)) {
            t.skip("Missing language feature");
        }
        const std::string decl = t.param<std::string>("decl");
        const NameCase& c = findCase(kLanguageCases(), caseName);
        const std::string code =
            std::string("\n    ") + c.src + "\n    " + decl + " " + caseName +
            " : u32 = 0;\n    fn use_var() -> u32 {\n      return " + caseName + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

// Mirrors upstream kSwizzleCases (order preserved).
static const std::vector<std::string>& kSwizzleCases() {
    static const std::vector<std::string> cases = {
        "x",    "y",    "z",    "w",    "xy",   "yxz",  "wxyz", "xyxy",
        "r",    "g",    "b",    "a",    "rgb",  "arr",  "bgra", "agra",
    };
    return cases;
}

static std::vector<Value> swizzleCaseValues() {
    std::vector<Value> values;
    for (const std::string& s : kSwizzleCases()) {
        values.emplace_back(s);
    }
    return values;
}

// padEnd(i, fillChar) per JS String.prototype.padEnd.
static std::string padEnd(const std::string& s, size_t len, char fill) {
    std::string out = s;
    while (out.size() < len) {
        out.push_back(fill);
    }
    return out;
}

CTS_TEST(g, "swizzle_names")
    .desc("Tests swizzle names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", swizzleCaseValues())
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string c = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        std::string code = decl + " " + c + " : u32 = 0;\n";
        if (c.size() == 1) {
            for (size_t i = 2; i <= 4; ++i) {
                code += decl + " " + padEnd(c, i, c[0]) + " : u32 = 0;\n";
            }
        }
        code += "fn foo() {\n      var x : vec4f;\n      _ = x." + c + ";\n    ";
        if (c.size() == 1) {
            for (size_t i = 2; i <= 4; ++i) {
                code += "_ = x." + padEnd(c, i, c[0]) + ";\n";
            }
        }
        code += "}\n    fn use_var() -> u32 {\n      return " + c + ";\n    }";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "interpolation_type_names")
    .desc("Tests interpolation type names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", {"perspective", "linear", "flat"})
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        // beforeAllSubcases: isCompatibility == false, so the linear-skip never fires.
        const std::string c = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        // isCompatibility == false => attr is `@interpolate(<case>)` (no `flat, either`).
        const std::string attr = "@interpolate(" + c + ")";
        const std::string code =
            "\n    " + decl + " " + c + " : u32 = 0;\n    @fragment fn main(@location(0) " + attr +
            " x : f32) { }\n    fn use_var() -> u32 {\n      return " + c + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "interpolation_sampling_names")
    .desc("Tests interpolation type names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", {"center", "centroid", "sample"})
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        // beforeAllSubcases: isCompatibility == false, so the sample-skip never fires.
        const std::string c = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const std::string code =
            "\n    " + decl + " " + c +
            " : u32 = 0;\n    @fragment fn main(@location(0) @interpolate(perspective, " + c +
            ") x : f32) { }\n    fn use_var() -> u32 {\n      return " + c + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "interpolation_flat_names")
    .desc("Tests interpolation type names do not use name resolution")
    .params([](ParamsBuilder u) {
        return u.combine("case", {"first", "either"})
            .beginSubcases()
            .combine("decl", declValues());
    })
    .fn([](ShaderValidationTest& t) {
        // beforeAllSubcases: isCompatibility == false, so the first-skip never fires.
        const std::string c = t.param<std::string>("case");
        const std::string decl = t.param<std::string>("decl");
        const std::string code =
            "\n    " + decl + " " + c +
            " : u32 = 0;\n    @fragment fn main(@location(0) @interpolate(flat, " + c +
            ") x : u32) { }\n    fn use_var() -> u32 {\n      return " + c + ";\n    }\n    ";
        t.expectCompileResult(true, code);
    });

} // namespace
