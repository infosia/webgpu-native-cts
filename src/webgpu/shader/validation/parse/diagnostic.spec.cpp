// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/diagnostic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,parse,diagnostic",
    "Validation tests for diagnostic directive and attribute");

static const std::vector<const char*> kSpecDiagnosticRules = {
    "derivative_uniformity", "subgroup_uniformity"};
static const std::vector<const char*> kSpecDiagnosticSeverities = {"off", "info", "warning",
                                                                   "error"};
static const std::vector<const char*> kDiagnosticTypes = {"attribute", "directive"};

static const std::vector<const char*> kBadSeverities = {"none", "warn", "goose", "fatal", "severe"};
static const std::vector<const char*> kBadSingleTokenRules = {"unknown", "blahblahblah",
                                                              "derivative_uniform"};

static std::vector<Value> toValues(const std::vector<const char*>& src) {
    std::vector<Value> values;
    for (const char* s : src) {
        values.emplace_back(std::string(s));
    }
    return values;
}

static std::string generateDiagnostic(const std::string& type, const std::string& severity,
                                       const std::string& rule) {
    const std::string diagnostic = "diagnostic(" + severity + ", " + rule + ")";
    if (type == "directive") {
        return diagnostic;
    }
    return "@" + diagnostic;
}

// ---------------------------------------------------------------------------
// Location builders (kValidLocations / kInvalidLocations / kNestedLocations).
// Each is keyed by name and produces code from a diagnostic string (or two).
// ---------------------------------------------------------------------------
static std::string validLocation(const std::string& loc, const std::string& diag) {
    if (loc == "module") return diag + ";";
    if (loc == "function") return diag + " fn foo() { }";
    if (loc == "compound") return "fn foo() { " + diag + " { } }";
    if (loc == "if_stmt") return "fn foo() { " + diag + " if true { } }";
    if (loc == "if_then") return "fn foo() { if true " + diag + " { } }";
    if (loc == "if_else") return "fn foo() { if true { } else " + diag + " { } }";
    if (loc == "switch_stmt") return "fn foo() { " + diag + " switch 0 { default { } } }";
    if (loc == "switch_body") return "fn foo() { switch 0 " + diag + " { default { } } }";
    if (loc == "switch_default") return "fn foo() { switch 0 { default " + diag + " { } } }";
    if (loc == "switch_case")
        return "fn foo() { switch 0 { case 0 " + diag + " { } default { } } }";
    if (loc == "loop_stmt") return "fn foo() { " + diag + " loop { break; } }";
    if (loc == "loop_body") return "fn foo() { loop " + diag + " { break; } }";
    if (loc == "loop_continuing")
        return "fn foo() { loop { continuing " + diag + " { break if true; } } }";
    if (loc == "while_stmt") return "fn foo() { " + diag + " while true { break; } }";
    if (loc == "while_body") return "fn foo() { while true " + diag + " { break; } }";
    if (loc == "for_stmt") return "fn foo() { " + diag + " for (var i = 0; i < 10; i++) { } }";
    if (loc == "for_body") return "fn foo() { for (var i = 0; i < 10; i++) " + diag + " { } }";
    return "";
}

static const std::vector<const char*>& kValidLocationNames() {
    static const std::vector<const char*> v = {
        "module",        "function",       "compound",     "if_stmt",   "if_then",
        "if_else",       "switch_stmt",    "switch_body",  "switch_default", "switch_case",
        "loop_stmt",     "loop_body",      "loop_continuing", "while_stmt", "while_body",
        "for_stmt",      "for_body"};
    return v;
}

static std::string invalidLocation(const std::string& loc, const std::string& diag) {
    if (loc == "module_var") return diag + " var<private> x : u32;";
    if (loc == "module_const") return diag + " const x = 0;";
    if (loc == "module_override") return diag + " override x : u32;";
    if (loc == "struct") return diag + " struct S { x : u32 }";
    if (loc == "struct_member") return " struct S { " + diag + " x : u32 }";
    if (loc == "function_params") return "fn foo" + diag + "() { }";
    if (loc == "function_var") return "fn foo() { " + diag + " var x = 0; }";
    if (loc == "function_let") return "fn foo() { " + diag + " let x = 0; }";
    if (loc == "function_const") return "fn foo() { " + diag + " const x = 0; }";
    if (loc == "pre_else") return "fn foo() { if true { } " + diag + " else { } }";
    if (loc == "pre_default") return "fn foo() { switch 0 { " + diag + " default { } } }";
    if (loc == "pre_case") return "fn foo() { switch 0 { " + diag + " case 0 { } default { } } }";
    if (loc == "pre_continuing")
        return "fn foo() { loop { " + diag + " continuing { break if true; } } }";
    if (loc == "pre_for_params")
        return "fn foo() { for " + diag + " (var i = 0; i < 10; i++) { } }";
    return "";
}

static const std::vector<const char*>& kInvalidLocationNames() {
    static const std::vector<const char*> v = {
        "module_var",   "module_const",  "module_override", "struct",         "struct_member",
        "function_params", "function_var", "function_let",  "function_const", "pre_else",
        "pre_default",  "pre_case",      "pre_continuing", "pre_for_params"};
    return v;
}

static std::string nestedLocation(const std::string& loc, const std::string& d1,
                                  const std::string& d2) {
    if (loc == "compound") return d1 + " fn foo() { " + d2 + " { } }";
    if (loc == "if_stmt") return "fn foo() { " + d1 + " if true " + d2 + " { } }";
    if (loc == "switch_stmt")
        return "fn foo() { " + d1 + " switch 0 " + d2 + " { default { } } }";
    if (loc == "switch_body")
        return "fn foo() { switch 0 " + d1 + " { default " + d2 + " { } } }";
    if (loc == "switch_case")
        return "fn foo() { switch 0 { case 0 " + d1 + " { } default " + d2 + " { } } }";
    if (loc == "loop_stmt") return "fn foo() { " + d1 + " loop " + d2 + " { break; } }";
    if (loc == "while_stmt") return "fn foo() { " + d1 + " while true " + d2 + " { break; } }";
    if (loc == "for_stmt")
        return "fn foo() { " + d1 + " for (var i = 0; i < 10; i++) " + d2 + " { } }";
    return "";
}

static const std::vector<const char*>& kNestedLocationNames() {
    static const std::vector<const char*> v = {"compound",    "if_stmt",    "switch_stmt",
                                               "switch_body", "switch_case", "loop_stmt",
                                               "while_stmt",  "for_stmt"};
    return v;
}

static std::vector<Value> names(const std::vector<const char*>& src) {
    return toValues(src);
}

// ---------------------------------------------------------------------------

CTS_TEST(g, "valid_params")
    .desc("Tests required accepted diagnostic parameters")
    .params([](ParamsBuilder u) {
        return u.combine("severity", toValues(kSpecDiagnosticSeverities))
            .combine("rule", toValues(kSpecDiagnosticRules))
            .combine("type", toValues(kDiagnosticTypes));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string diag =
            generateDiagnostic(type, t.param<std::string>("severity"), t.param<std::string>("rule"));
        std::string code;
        if (type == "directive") {
            code = validLocation("module", diag);
        } else {
            code = validLocation("function", diag);
        }
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "invalid_severity")
    .desc("Tests invalid severities are rejected")
    .params([](ParamsBuilder u) {
        return u.combine("severity", toValues(kBadSeverities))
            .combine("type", toValues(kDiagnosticTypes));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string diag =
            generateDiagnostic(type, t.param<std::string>("severity"), "derivative_uniformity");
        std::string code;
        if (type == "directive") {
            code = validLocation("module", diag);
        } else {
            code = validLocation("function", diag);
        }
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "warning_unknown_rule")
    .desc("Tests unknown single token rules issue a warning")
    .params([](ParamsBuilder u) {
        return u.combine("type", toValues(kDiagnosticTypes))
            .combine("rule", toValues(kBadSingleTokenRules));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string diag = generateDiagnostic(type, "info", t.param<std::string>("rule"));
        std::string code;
        if (type == "directive") {
            code = validLocation("module", diag);
        } else {
            code = validLocation("function", diag);
        }
        t.expectCompileWarning(true, code);
    });

CTS_TEST(g, "valid_locations")
    .desc("Tests valid locations")
    .params([](ParamsBuilder u) {
        return u.combine("type", toValues(kDiagnosticTypes))
            .combine("location", names(kValidLocationNames()))
            .combine("rule", toValues(kSpecDiagnosticRules));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string location = t.param<std::string>("location");
        const std::string diag = generateDiagnostic(type, "info", t.param<std::string>("rule"));
        const std::string code = validLocation(location, diag);
        bool res = true;
        if (type == "directive") {
            res = location == "module";
        } else {
            res = location != "module";
        }
        if (res == false) {
            t.expectCompileResult(true, validLocation(location, ""));
        }
        t.expectCompileResult(res, code);
    });

CTS_TEST(g, "invalid_locations")
    .desc("Tests invalid locations")
    .params([](ParamsBuilder u) {
        return u.combine("type", toValues(kDiagnosticTypes))
            .combine("location", names(kInvalidLocationNames()))
            .combine("rule", toValues(kSpecDiagnosticRules));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string location = t.param<std::string>("location");
        const std::string diag = generateDiagnostic(type, "info", t.param<std::string>("rule"));
        t.expectCompileResult(true, invalidLocation(location, ""));
        t.expectCompileResult(false, invalidLocation(location, diag));
    });

CTS_TEST(g, "conflicting_directive")
    .desc("Tests conflicts between directives")
    .params([](ParamsBuilder u) {
        return u.combine("s1", toValues(kSpecDiagnosticSeverities))
            .combine("s2", toValues(kSpecDiagnosticSeverities));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string s1 = t.param<std::string>("s1");
        const std::string s2 = t.param<std::string>("s2");
        const std::string d1 = generateDiagnostic("directive", s1, "derivative_uniformity");
        const std::string d2 = generateDiagnostic("directive", s2, "derivative_uniformity");
        const std::string code = validLocation("module", d1) + "\n" + validLocation("module", d2);
        t.expectCompileResult(s1 == s2, code);
    });

CTS_TEST(g, "duplicate_attribute_same_location")
    .desc("Tests duplicate diagnostics at the same location must be on different rules")
    .params([](ParamsBuilder u) {
        return u.combine("loc", names(kValidLocationNames()))
            .combine("same_rule", {Value(true), Value(false)})
            .beginSubcases()
            .combine("s1", toValues(kSpecDiagnosticSeverities))
            .combine("s2", toValues(kSpecDiagnosticSeverities))
            .filter([](const ParamRecord& p) {
                const Value* loc = findParam(p, "loc");
                return loc != nullptr && valueAs<std::string>(*loc) != "module";
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string loc = t.param<std::string>("loc");
        const bool sameRule = t.param<bool>("same_rule");
        const std::string rule1 = "derivative_uniformity";
        const std::string rule2 = "another_diagnostic_rule";
        const std::string d1 = generateDiagnostic("attribute", t.param<std::string>("s1"), rule1);
        const std::string d2 =
            generateDiagnostic("attribute", t.param<std::string>("s2"), sameRule ? rule1 : rule2);
        const std::string code = validLocation(loc, d1 + " " + d2);
        t.expectCompileResult(!sameRule, code);
    });

CTS_TEST(g, "conflicting_attribute_different_location")
    .desc("Tests conflicts between attributes")
    .params([](ParamsBuilder u) {
        return u.combine("loc", names(kNestedLocationNames()))
            .combine("s1", toValues(kSpecDiagnosticSeverities))
            .combine("s2", toValues(kSpecDiagnosticSeverities))
            .filter([](const ParamRecord& p) {
                const Value* s1 = findParam(p, "s1");
                const Value* s2 = findParam(p, "s2");
                return s1 != nullptr && s2 != nullptr &&
                       valueAs<std::string>(*s1) != valueAs<std::string>(*s2);
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string loc = t.param<std::string>("loc");
        const std::string d1 =
            generateDiagnostic("attribute", t.param<std::string>("s1"), "derivative_uniformity");
        const std::string d2 =
            generateDiagnostic("attribute", t.param<std::string>("s2"), "derivative_uniformity");
        const std::string code = nestedLocation(loc, d1, d2);
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "after_other_directives")
    .desc("Tests other global directives before a diagnostic directive.")
    .params([](ParamsBuilder u) {
        return u.combine("directive",
                         {"enable f16", "requires readonly_and_readwrite_storage_textures"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string directive = t.param<std::string>("directive");
        if (directive.rfind("requires", 0) == 0) {
            t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");
        }
        std::string code = directive + ";";
        code += generateDiagnostic("directive", "info", "derivative_uniformity") + ";";
        t.expectCompileResult(true, code);
    });

// ---------------------------------------------------------------------------
// diagnostic_scoping
// ---------------------------------------------------------------------------
static std::string scopeCode(const std::string& body) {
    return "\n@group(0) @binding(0) var t : texture_1d<f32>;"
           "\n@group(0) @binding(1) var s : sampler;"
           "\nvar<private> non_uniform_cond : bool;"
           "\nvar<private> non_uniform_coord : f32;"
           "\nvar<private> non_uniform_val : u32;"
           "\n@fragment fn main() {"
           "\n  " + body +
           "\n}"
           "\n";
}

// result encoding: 0 = false, 1 = true, 2 = warn.
struct ScopeCase {
    const char* name;
    std::string code;
    int result;
};

static const std::vector<ScopeCase>& kScopeCases() {
    auto dir = [](const char* sev) {
        return generateDiagnostic("directive", sev, "derivative_uniformity");
    };
    auto attr = [](const char* sev) {
        return generateDiagnostic("", sev, "derivative_uniformity");
    };
    static const std::vector<ScopeCase> v = {
        {"override_global_off",
         "\n    " + dir("error") + ";\n    " +
             scopeCode("\n      " + attr("off") +
                       "\n      if non_uniform_cond {"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }") +
             ";\n    ",
         1},
        {"override_global_on",
         "\n    " + dir("off") + ";\n    " +
             scopeCode("\n      " + attr("error") +
                       "\n      if non_uniform_cond {"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }") +
             "\n    ",
         0},
        {"override_global_warn",
         "\n    " + dir("error") + ";\n    " +
             scopeCode("\n      " + attr("warning") +
                       "\n      if non_uniform_cond {"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }") +
             "\n    ",
         2},
        {"global_if_nothing_else_warn",
         "\n    " + dir("warning") + ";\n    " +
             scopeCode("\n      if non_uniform_cond {"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }") +
             "\n    ",
         2},
        {"deepest_nesting_warn",
         scopeCode("\n      " + attr("error") +
                   "\n      if non_uniform_cond {"
                   "\n        " + attr("warning") +
                   "\n        if non_uniform_cond {"
                   "\n          _ = textureSample(t,s,0.0);"
                   "\n        }"
                   "\n      }"),
         2},
        {"deepest_nesting_off",
         scopeCode("\n      " + attr("error") +
                   "\n      if non_uniform_cond {"
                   "\n        " + attr("off") +
                   "\n        if non_uniform_cond {"
                   "\n          _ = textureSample(t,s,0.0);"
                   "\n        }"
                   "\n      }"),
         1},
        {"deepest_nesting_error",
         scopeCode("\n      " + attr("off") +
                   "\n      if non_uniform_cond {"
                   "\n        " + attr("error") +
                   "\n        if non_uniform_cond {"
                   "\n          _ = textureSample(t,s,0.0);"
                   "\n        }"
                   "\n      }"),
         0},
        {"other_nest_unaffected",
         "\n    " + dir("warning") + ";\n    " +
             scopeCode("\n      " + attr("off") +
                       "\n      if non_uniform_cond {"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }"
                       "\n      if non_uniform_cond {"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }") +
             "\n    ",
         2},
        {"deeper_nest_no_effect",
         "\n    " + dir("error") + ";\n    " +
             scopeCode("\n      if non_uniform_cond {"
                       "\n        " + attr("off") +
                       "\n        if non_uniform_cond {"
                       "\n        }"
                       "\n        _ = textureSample(t,s,0.0);"
                       "\n      }") +
             "\n    ",
         0},
        {"call_unaffected_error",
         "\n    " + dir("error") +
             ";\n    fn foo() { _ = textureSample(t,s,0.0); }\n    " +
             scopeCode("\n      " + attr("off") +
                       "\n      if non_uniform_cond {"
                       "\n        foo();"
                       "\n      }") +
             "\n    ",
         0},
        {"call_unaffected_warn",
         "\n    " + dir("warning") +
             ";\n    fn foo() { _ = textureSample(t,s,0.0); }\n    " +
             scopeCode("\n      " + attr("off") +
                       "\n      if non_uniform_cond {"
                       "\n        foo();"
                       "\n      }") +
             "\n    ",
         2},
        {"call_unaffected_off",
         "\n    " + dir("off") +
             ";\n    fn foo() { _ = textureSample(t,s,0.0); }\n    " +
             scopeCode("\n      " + attr("error") +
                       "\n      if non_uniform_cond {"
                       "\n        foo();"
                       "\n      }") +
             "\n    ",
         1},
        {"if_condition_error",
         scopeCode("\n      if (non_uniform_cond) {"
                   "\n        " + attr("error") +
                   "\n        if textureSample(t,s,non_uniform_coord).x > 0.0"
                   "\n          " + attr("off") + " {"
                   "\n        }"
                   "\n      }"),
         0},
        {"if_condition_warn",
         scopeCode("\n      if non_uniform_cond {"
                   "\n        " + attr("warning") +
                   "\n        if textureSample(t,s,non_uniform_coord).x > 0.0"
                   "\n          " + attr("error") + " {"
                   "\n        }"
                   "\n      }"),
         2},
        {"if_condition_off",
         scopeCode("\n      if non_uniform_cond {"
                   "\n        " + attr("off") +
                   "\n        if textureSample(t,s,non_uniform_coord).x > 0.0"
                   "\n          " + attr("error") + " {"
                   "\n        }"
                   "\n      }"),
         1},
        {"switch_error",
         scopeCode("\n        " + attr("error") +
                   "\n        switch non_uniform_val {"
                   "\n          case 0 " + attr("off") + " {"
                   "\n          }"
                   "\n          default {"
                   "\n            _ = textureSample(t,s,0.0);"
                   "\n          }"
                   "\n        }"),
         0},
        {"switch_warn",
         scopeCode("\n        " + attr("warning") +
                   "\n        switch non_uniform_val {"
                   "\n          case 0 " + attr("off") + " {"
                   "\n          }"
                   "\n          default {"
                   "\n            _ = textureSample(t,s,0.0);"
                   "\n          }"
                   "\n        }"),
         2},
        {"switch_off",
         scopeCode("\n        " + attr("off") +
                   "\n        switch non_uniform_val {"
                   "\n          case 0 " + attr("error") + "{"
                   "\n          }"
                   "\n          default {"
                   "\n            _ = textureSample(t,s,0.0);"
                   "\n          }"
                   "\n        }"),
         1},
    };
    return v;
}

static std::vector<Value> scopeCaseNames() {
    std::vector<Value> values;
    for (const ScopeCase& c : kScopeCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ScopeCase& findScopeCase(const std::string& name) {
    for (const ScopeCase& c : kScopeCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ScopeCase dummy{"", "", 0};
    return dummy;
}

CTS_TEST(g, "diagnostic_scoping")
    .desc("Tests that innermost scope controls the diagnostic")
    .params([](ParamsBuilder u) {
        return u.combine("case", scopeCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const ScopeCase& c = findScopeCase(t.param<std::string>("case"));
        if (c.result == 2) {
            t.expectCompileWarning(true, c.code);
        } else {
            t.expectCompileResult(c.result == 1, c.code);
        }
    });

}  // namespace
