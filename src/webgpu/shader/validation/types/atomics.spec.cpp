// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/atomics.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,types,atomics",
    "Validation tests for atomic types\n\nTests covered:\n* Base type\n* Address spaces\n* Invalid "
    "operations (non-exhaustive)\n\nNote: valid operations (e.g. atomic built-in functions) are "
    "tested in the builtin tests.");

CTS_TEST(g, "type")
    .desc("Test of the underlying atomic data type")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"u32", "i32", "f32", "f16", "bool", "vec2u", "vec3i", "vec4f",
                                  "mat2x2f", "R", "S", "array<u32, 1>", "array<i32, 4>",
                                  "array<u32>", "array<i32>", "atomic<u32>", "atomic<i32>",
                                  "sampler"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string code =
            "\nstruct S {\n  x : u32\n}\nstruct T {\n  x : i32\n}\nstruct R {\n  x : f32\n}\n\nstruct "
            "Test {\n  x : atomic<" +
            type + ">\n}\n";
        const bool expectValid = type == "u32" || type == "i32";
        t.expectCompileResult(expectValid, code);
    });

struct SpecifierCase {
    const char* name;
    const char* code;
    bool valid;
};

// Mirrors upstream kSpecifierCases (object key order preserved).
static const std::vector<SpecifierCase>& kSpecifierCases() {
    static const std::vector<SpecifierCase> cases = {
        {"no_type", "alias T = atomic;", false},
        {"missing_l_template", "alias T = atomici32>;", false},
        {"missing_r_template", "alias T = atomic<i32;", false},
        {"template_comma", "alias T = atomic<i32,>;", true},
        {"missing_template_param", "alias T = atomic<>;", false},
        {"space_in_specifier", "alias T = atomic <i32>;", true},
        {"space_as_l_template", "alias T = atomic i32>;", false},
        {"comment", "alias T = atomic\n    /* comment */\n    <i32>;", true},
    };
    return cases;
}

static const SpecifierCase& findSpecifierCase(const std::string& name) {
    for (const SpecifierCase& c : kSpecifierCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const SpecifierCase dummy{"", "", false};
    return dummy;
}

static std::vector<Value> specifierCaseNames() {
    std::vector<Value> values;
    for (const SpecifierCase& c : kSpecifierCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "parse")
    .desc("Test atomic parsing")
    .params([](ParamsBuilder u) {
        return u.combine("case", specifierCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const SpecifierCase& c = findSpecifierCase(t.param<std::string>("case"));
        t.expectCompileResult(c.valid, c.code);
    });

CTS_TEST(g, "address_space")
    .desc("Test allowed address spaces for atomics")
    .params([](ParamsBuilder u) {
        return u.combine("aspace", {"storage", "workgroup", "storage-ro", "uniform", "private",
                                    "function", "function-let"})
            .beginSubcases()
            .combine("type", {"i32", "u32"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string aspace = t.param<std::string>("aspace");
        const std::string type = t.param<std::string>("type");
        std::string moduleVar;
        std::string functionVar;
        if (aspace == "storage-ro") {
            moduleVar = "@group(0) @binding(0) var<storage> x : atomic<" + type + ">;\n";
        } else if (aspace == "storage") {
            moduleVar =
                "@group(0) @binding(0) var<storage, read_write> x : atomic<" + type + ">;\n";
        } else if (aspace == "uniform") {
            moduleVar = "@group(0) @binding(0) var<uniform> x : atomic<" + type + ">;\n";
        } else if (aspace == "workgroup" || aspace == "private") {
            moduleVar = "var<" + aspace + "> x : atomic<" + type + ">;\n";
        } else if (aspace == "function") {
            functionVar = "var x : atomic<" + type + ">;\n";
        } else if (aspace == "function-let") {
            functionVar = "let x : atomic<" + type + ">;\n";
        }
        const std::string code = "\n" + moduleVar + "\n\nfn foo() {\n  " + functionVar + "}\n";
        const bool expectValid = aspace == "storage" || aspace == "workgroup";
        t.expectCompileResult(expectValid, code);
    });

struct OpCase {
    const char* name;
    const char* op;
};

// Mirrors upstream kInvalidOperations (object key order preserved).
static const std::vector<OpCase>& kInvalidOperations() {
    static const std::vector<OpCase> cases = {
        {"add", "a1 + a2"},     {"load", "a1"},        {"store", "a1 = 1u"},
        {"deref", "*a1 = 1u"},  {"equality", "a1 == a2"}, {"abs", "abs(a1)"},
        {"address_abs", "abs(&a1)"},
    };
    return cases;
}

static const OpCase& findOpCase(const std::string& name) {
    for (const OpCase& c : kInvalidOperations()) {
        if (name == c.name) {
            return c;
        }
    }
    static const OpCase dummy{"", ""};
    return dummy;
}

static std::vector<Value> opCaseNames() {
    std::vector<Value> values;
    for (const OpCase& c : kInvalidOperations()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "invalid_operations")
    .desc("Tests that a selection of invalid operations are invalid")
    .params([](ParamsBuilder u) {
        return u.combine("op", opCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const OpCase& c = findOpCase(t.param<std::string>("op"));
        const std::string code =
            "\nvar<workgroup> a1 : atomic<u32>;\nvar<workgroup> a2 : atomic<u32>;\n\nfn foo() {\n  "
            "let x : u32 = " +
            std::string(c.op) + ";\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "trailing_comma")
    .desc("Test that trailing commas are accepted")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"u32", "i32"}).combine("comma", {"", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string comma = t.param<std::string>("comma");
        const std::string code = "alias T = atomic<" + type + comma + ">;";
        t.expectCompileResult(true, code);
    });

}  // namespace
