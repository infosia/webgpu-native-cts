// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/interpolate.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// All five g.test use the compile-only path (t.expectCompileResult); none
// validate at pipeline creation.

#include <set>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,interpolate",
    "Validation tests for the interpolate attribute");

// Mirrors upstream util.ts generateShader().
static std::string generateShader(const std::string& attribute,
                                  const std::string& type,
                                  const std::string& stage,
                                  const std::string& io,
                                  bool use_struct) {
    std::string code;

    if (use_struct) {
        code += "struct S {\n";
        code += "  " + attribute + " value : " + type + ",\n";
        if (stage == "vertex" && io == "out" &&
            attribute.find("builtin(position)") == std::string::npos) {
            code += "  @builtin(position) position : vec4<f32>,\n";
        }
        code += "};\n\n";
    }

    if (stage != "") {
        code += "@" + stage;
        if (stage == "compute") {
            code += " @workgroup_size(1)";
        }
    }

    std::string param;
    std::string retType;
    std::string retVal;
    if (io == "in") {
        if (use_struct) {
            param = "in : S";
        } else {
            param = attribute + " value : " + type;
        }
        if (stage == "vertex") {
            retType = "-> @builtin(position) vec4<f32>";
            retVal = "return vec4<f32>();";
        }
    } else if (io == "out") {
        if (use_struct) {
            retType = "-> S";
            retVal = "return S();";
        } else {
            retType = "-> " + attribute + " " + type;
            retVal = "return " + type + "();";
        }
    }

    code += "\n    fn main(" + param + ") " + retType + " {\n      " + retVal + "\n    }\n  ";
    return code;
}

// Mirrors upstream kValidInterpolationAttributes (a JS Set, so duplicate
// literals collapse; insertion order of the unique entries preserved).
static const std::vector<std::string>& kValidInterpolationAttributesOrdered() {
    static const std::vector<std::string> v = {
        "",
        "@interpolate(perspective)",
        "@interpolate(perspective, center)",
        "@interpolate(perspective, centroid)",
        "@interpolate(flat)",
        "@interpolate(flat, first)",
        "@interpolate(flat, either)",
        "@interpolate(perspective, sample)",
        "@interpolate(linear)",
        "@interpolate(linear, center)",
        "@interpolate(linear, centroid)",
        "@interpolate(linear, sample)",
    };
    return v;
}

static bool isValidInterpolationAttribute(const std::string& s) {
    for (const std::string& v : kValidInterpolationAttributesOrdered()) {
        if (v == s) {
            return true;
        }
    }
    return false;
}

CTS_TEST(g, "type_and_sampling")
    .desc("Test that all combinations of interpolation type and sampling are validated correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {std::string("vertex"), std::string("fragment")})
                .combine("io", {std::string("in"), std::string("out")})
                .combine("use_struct", {Value(true), Value(false)})
                .combine("type", {std::string(""), std::string("flat"), std::string("perspective"),
                                  std::string("linear"), std::string("center"),
                                  std::string("centroid"), std::string("sample"),
                                  std::string("first"), std::string("either")})
                .filter([](const ParamRecord& p) {
                    const std::string stage = valueAs<std::string>(*findParam(p, "stage"));
                    const bool useStruct = valueAs<bool>(*findParam(p, "use_struct"));
                    return !(stage == "vertex" && useStruct == false);
                })
                .combine("sampling", {std::string(""), std::string("center"),
                                      std::string("centroid"), std::string("sample"),
                                      std::string("first"), std::string("either"),
                                      std::string("flat"), std::string("perspective"),
                                      std::string("linear")})
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string io = t.param<std::string>("io");
        const bool useStruct = t.param<bool>("use_struct");
        const std::string type = t.param<std::string>("type");
        const std::string sampling = t.param<std::string>("sampling");

        std::string interpolate;
        if (type != "" || sampling != "") {
            interpolate = "@interpolate(";
            if (type != "") {
                interpolate += type;
            }
            if (sampling != "") {
                interpolate += ", " + sampling;
            }
            interpolate += ")";
        }
        const std::string code =
            generateShader("@location(0)" + interpolate, "f32", stage, io, useStruct);
        t.expectCompileResult(isValidInterpolationAttribute(interpolate), code);
    });

CTS_TEST(g, "require_location")
    .desc("Test that the interpolate attribute is only accepted with user-defined IO.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {std::string("vertex"), std::string("fragment")})
                .combine("attribute",
                         {std::string("@location(0)"), std::string("@builtin(position)")})
                .combine("use_struct", {Value(true), Value(false)})
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string attribute = t.param<std::string>("attribute");
        const bool useStruct = t.param<bool>("use_struct");

        if (stage == "vertex" && useStruct == false &&
            attribute.find("position") == std::string::npos) {
            t.skip("vertex output must include a position builtin, so must use a struct");
        }

        const std::string io = stage == "fragment" ? "in" : "out";
        const std::string code =
            generateShader(attribute + "@interpolate(flat, either)", "vec4<f32>", stage, io,
                           useStruct);
        t.expectCompileResult(attribute == "@location(0)", code);
    });

CTS_TEST(g, "integral_types")
    .desc("Test that the implementation requires @interpolate(flat) for integral user-defined IO.")
    .params([](ParamsBuilder u) {
        std::vector<Value> attrs;
        for (const std::string& a : kValidInterpolationAttributesOrdered()) {
            attrs.emplace_back(a);
        }
        return u.combine("stage", {std::string("vertex"), std::string("fragment")})
                .combine("type", {std::string("i32"), std::string("u32"),
                                  std::string("vec2<i32>"), std::string("vec4<u32>")})
                .combine("use_struct", {Value(true), Value(false)})
                .combine("attribute", attrs)
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string type = t.param<std::string>("type");
        const bool useStruct = t.param<bool>("use_struct");
        const std::string attribute = t.param<std::string>("attribute");

        if (stage == "vertex" && useStruct == false) {
            t.skip("vertex output must include a position builtin, so must use a struct");
        }

        const std::string io = stage == "vertex" ? "out" : "in";
        const std::string code =
            generateShader("@location(0)" + attribute, type, stage, io, useStruct);

        const bool expectSuccess = attribute.rfind("@interpolate(flat", 0) == 0;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "duplicate")
    .desc("Test that the interpolate attribute can only be applied once.")
    .params([](ParamsBuilder u) {
        return u.combine("attr", {std::string(""), std::string("@interpolate(flat)")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string attr = t.param<std::string>("attr");
        const std::string code = generateShader(
            "@location(0) @interpolate(flat, either) " + attr, "vec4<f32>", "fragment", "in",
            /*use_struct=*/false);
        t.expectCompileResult(attr == "", code);
    });

// Mirrors upstream kValidationTests (object key order preserved).
struct ValidationCase {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<ValidationCase>& kValidationTests() {
    static const std::vector<ValidationCase> v = {
        {"valid", "@interpolate(perspective)", true},
        {"no_space", "@interpolate(perspective,center)", true},
        {"trailing_comma_one_arg", "@interpolate(flat,)", true},
        {"trailing_comma_two_arg", "@interpolate(perspective, center,)", true},
        {"newline", "@\ninterpolate(perspective)", true},
        {"comment", "@/* comment */interpolate(perspective)", true},
        {"no_params", "@interpolate()", false},
        {"missing_left_paren", "@interpolate perspective)", false},
        {"missing_value_and_left_paren", "@interpolate)", false},
        {"missing_right_paren", "@interpolate(perspective", false},
        {"missing_parens", "@interpolate", false},
        {"missing_comma", "@interpolate(perspective center)", false},
        {"numeric", "@interpolate(1)", false},
        {"numeric_second_param", "@interpolate(perspective, 1)", false},
    };
    return v;
}

CTS_TEST(g, "interpolation_validation")
    .desc("Test validation of interpolation")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const ValidationCase& c : kValidationTests()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("attr", names);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("attr");
        const ValidationCase* found = nullptr;
        for (const ValidationCase& c : kValidationTests()) {
            if (name == c.name) {
                found = &c;
                break;
            }
        }
        const std::string src = found != nullptr ? found->src : "";
        const std::string code =
            "\n@vertex fn main(" + src + " @location(0) b: f32) ->"
            "\n    @builtin(position) vec4<f32> {"
            "\n  return vec4f(0);"
            "\n}";
        t.expectCompileResult(found != nullptr && found->pass, code);
    });

} // namespace
