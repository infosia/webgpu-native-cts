// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/must_use.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,parse,must_use",
    "Validation tests for @must_use");

struct Declaration {
    const char* name;
    const char* code;
    bool valid;
};

// Mirrors upstream kMustUseDeclarations (object key order preserved).
static const std::vector<Declaration>& kMustUseDeclarations() {
    static const std::vector<Declaration> v = {
        {"var",
         "@must_use @group(0) @binding(0)\n    var<storage> x : array<u32>;", false},
        {"function_no_return", "@must_use fn foo() { }", false},
        {"function_scalar_return", "@must_use fn foo() -> u32 { return 0; }", true},
        {"function_struct_return",
         "struct S { x : u32 }\n    @must_use fn foo() -> S { return S(); }", true},
        {"function_var", "fn foo() { @must_use var x = 0; }", false},
        {"function_call",
         "fn bar() -> u32 { return 0; }\n    fn foo() { @must_use bar(); }", false},
        {"function_parameter", "fn foo(@must_use param : u32) -> u32 { return param; }", false},
        {"empty_parameter", "@must_use() fn foo() -> u32 { return 0; }", false},
        {"parameter", "@must_use(0) fn foo() -> u32 { return 0; }", false},
        {"duplicate", "@must_use @must_use fn foo() -> u32 { return 0; }", false},
    };
    return v;
}

static std::vector<Value> declarationNames() {
    std::vector<Value> values;
    for (const Declaration& d : kMustUseDeclarations()) {
        values.emplace_back(std::string(d.name));
    }
    return values;
}

static const Declaration& findDeclaration(const std::string& name) {
    for (const Declaration& d : kMustUseDeclarations()) {
        if (name == d.name) {
            return d;
        }
    }
    static const Declaration dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "declaration")
    .desc("Validate attribute can only be applied to a function declaration with a return type")
    .params([](ParamsBuilder u) {
        return u.combine("test", declarationNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Declaration& d = findDeclaration(t.param<std::string>("test"));
        t.expectCompileResult(d.valid, d.code);
    });

struct CallCase {
    const char* name;
    const char* body;
};

// Mirrors upstream kMustUseCalls (object key order preserved).
static const std::vector<CallCase>& kMustUseCalls() {
    static const std::vector<CallCase> v = {
        {"no_call", ""},
        {"phony", "_ = bar();"},
        {"let", "let tmp = bar();"},
        {"local_var", "var tmp = bar();"},
        {"private_var", "private_var = bar();"},
        {"storage_var", "storage_var = bar();"},
        {"pointer", "\n    var a : f32;\n    let p = &a;\n    (*p) = bar();"},
        {"vector_elem", "\n    var a : vec3<f32>;\n    a.x = bar();"},
        {"matrix_elem", "\n    var a : mat3x2<f32>;\n    a[0][0] = bar();"},
        {"condition", "if bar() == 0 { }"},
        {"param", "baz(bar());"},
        {"return", "return bar();"},
        {"statement", "bar();"},
    };
    return v;
}

static std::vector<Value> callNames() {
    std::vector<Value> values;
    for (const CallCase& c : kMustUseCalls()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const CallCase& findCall(const std::string& name) {
    for (const CallCase& c : kMustUseCalls()) {
        if (name == c.name) {
            return c;
        }
    }
    static const CallCase dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "call")
    .desc("Validate that a call to must_use function cannot be the whole function call statement")
    .params([](ParamsBuilder u) {
        return u.combine("use", {"@must_use", ""}).combine("call", callNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string use = t.param<std::string>("use");
        const std::string callName = t.param<std::string>("call");
        const CallCase& c = findCall(callName);
        const std::string code =
            "\n    @group(0) @binding(0) var<storage, read_write> storage_var : f32;"
            "\n    var<private> private_var : f32;"
            "\n"
            "\n    fn baz(param : f32) { }"
            "\n"
            "\n    " + use + " fn bar() -> f32 { return 0; }"
            "\n"
            "\n    fn foo() " + (callName == "return" ? std::string("-> f32") : std::string("")) +
            " {"
            "\n      " + c.body +
            "\n    }";
        const bool shouldPass = callName != "statement" || use == "";
        t.expectCompileResult(shouldPass, code);
    });

CTS_TEST(g, "ignore_result_of_non_must_use_that_returns_call_of_must_use")
    .desc("Test that ignoring the result of a non-@must_use function that returns the result of a "
          "@must_use function succeeds")
    .fn([](ShaderValidationTest& t) {
        const std::string wgsl =
            "\n    @must_use"
            "\n    fn f() -> f32 {"
            "\n      return 0;"
            "\n    }"
            "\n"
            "\n    fn g() -> f32 {"
            "\n      return f();"
            "\n    }"
            "\n"
            "\n    fn main() {"
            "\n      g(); // Ignore result"
            "\n    }"
            "\n    ";
        t.expectCompileResult(true, wgsl);
    });

}  // namespace
