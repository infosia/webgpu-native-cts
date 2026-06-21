// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/statement_behavior.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,statement_behavior",
    "Test statement behavior analysis.\n\nFunctions must have a behavior of {Return}, {Next}, or "
    "{Return, Next}.\nFunctions with a return type must have a behavior of {Return}.\n\nEach "
    "statement in the function must be valid according to the table.\n");

struct Entry {
    const char* name;
    const char* body;
};

// Mirrors upstream kInvalidStatements (object key order preserved).
static const std::vector<Entry>& kInvalidStatements() {
    static const std::vector<Entry> v = {
        {"break", "break"},
        {"break_if", "break if true"},
        {"continue", "continue"},
        {"loop1", "loop { }"},
        {"loop2", "loop { continuing { } }"},
        {"loop3", "loop { continue; continuing { } }"},
        {"loop4", "loop { continuing { break; } }"},
        {"loop5", "loop { continuing { continue; } }"},
        {"loop6", "loop { continuing { return; } }"},
        {"loop7", "loop { continue; break; }"},
        {"loop8", "loop { continuing { break if true; return; } }"},
        {"for1", "for (;;) { }"},
        {"for2", "for (var i = 0; ; i++) { }"},
        {"for3", "for (;; break) { }"},
        {"for4", "for (;; continue ) { }"},
        {"for5", "for (;; return ) { }"},
        {"for6", "for (;;) { continue; break; }"},
        // while loops always have break in their behaviors.
        {"switch1", "switch (1) { case 1 { } }"},
        {"sequence1", "return; loop { }"},
        {"compound1", "{ loop { } }"},
    };
    return v;
}

// Mirrors upstream kValidStatements (object key order preserved).
static const std::vector<Entry>& kValidStatements() {
    static const std::vector<Entry> v = {
        {"empty", ""},
        {"const_assert", "const_assert true"},
        {"let", "let x = 1"},
        {"var1", "var x = 1"},
        {"var2", "var x : i32"},
        {"assign", "v = 1"},
        {"phony_assign", "_ = 1"},
        {"compound_assign", "v += 1"},
        {"return", "return"},
        {"discard", "discard"},
        {"function_call1", "bar()"},
        {"function_call2", "workgroupBarrier()"},
        {"if1", "if true { } else { }"},
        {"if2", "if true { }"},
        {"break1", "loop { break; }"},
        {"break2", "loop { if false { break; } }"},
        {"break_if", "loop { continuing { break if false; } }"},
        {"continue1", "loop { continue; continuing { break if true; } }"},
        {"loop1", "loop { break; }"},
        {"loop2", "loop { break; continuing { } }"},
        {"loop3", "loop { continue; continuing { break if true; } }"},
        {"loop4", "loop { break; continue; }"},
        {"for1", "for (; true; ) { }"},
        {"for2", "for (;;) { break; }"},
        {"for3", "for (;true;) { continue; }"},
        {"while1", "while true { }"},
        {"while2", "while true { continue; }"},
        {"while3", "while true { continue; break; }"},
        {"switch1", "switch 1 { default { } }"},
        {"switch2", "switch 1 { case 1 { } default { } }"},
        {"switch3", "switch 1 { default { break; } }"},
        {"switch4", "switch 1 { default { } case 1 { break; } }"},
        {"sequence1", "return; let x = 1"},
        {"sequence2", "if true { } let x = 1"},
        {"sequence3", "switch 1 { default { break; return; } }"},
        {"compound1", "{ }"},
        {"compound2", "{ loop { break; } if true { return; } }"},
    };
    return v;
}

// Mirrors upstream kInvalidFunctions (object key order preserved).
static const std::vector<Entry>& kInvalidFunctions() {
    static const std::vector<Entry> v = {
        {"next_for_type", "fn foo() -> bool { }"},
        {"next_return_for_type", "fn foo() -> bool { if true { return true; } }"},
    };
    return v;
}

// Mirrors upstream kValidFunctions (object key order preserved).
static const std::vector<Entry>& kValidFunctions() {
    static const std::vector<Entry> v = {
        {"empty", "fn foo() { }"},
        {"next_return", "fn foo() { if true { return; } }"},
        {"unreachable_code_after_return_with_value", "fn foo() -> bool { return false; _ = 0; }"},
        {"no_final_return", "fn foo() -> bool { if true { return true; } else { return false; } }"},
        {"no_final_return_unreachable_code",
         "fn foo() -> bool { if true { return true; } else { return false; } _ = 0; _ = 1; }"},
    };
    return v;
}

static std::vector<Value> namesOf(const std::vector<Entry>& entries) {
    std::vector<Value> values;
    for (const Entry& e : entries) {
        values.emplace_back(std::string(e.name));
    }
    return values;
}

static const char* bodyOf(const std::vector<Entry>& entries, const std::string& name) {
    for (const Entry& e : entries) {
        if (name == e.name) {
            return e.body;
        }
    }
    return "";
}

CTS_TEST(g, "invalid_statements")
    .desc("Test statements with invalid behaviors")
    .params([](ParamsBuilder u) { return u.combine("body", namesOf(kInvalidStatements())); })
    .fn([](ShaderValidationTest& t) {
        const std::string body = bodyOf(kInvalidStatements(), t.param<std::string>("body"));
        const std::string code = "fn foo() {\n      " + body + ";\n    }";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "valid_statements")
    .desc("Test statements with valid behaviors")
    .params([](ParamsBuilder u) { return u.combine("body", namesOf(kValidStatements())); })
    .fn([](ShaderValidationTest& t) {
        const std::string body = bodyOf(kValidStatements(), t.param<std::string>("body"));
        const std::string code =
            "\n    var<private> v : i32;\n    fn bar() { }\n    fn foo() {\n      " + body +
            ";\n    }";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "invalid_functions")
    .desc("Test functions with invalid behaviors")
    .params([](ParamsBuilder u) { return u.combine("function", namesOf(kInvalidFunctions())); })
    .fn([](ShaderValidationTest& t) {
        const std::string func = bodyOf(kInvalidFunctions(), t.param<std::string>("function"));
        t.expectCompileResult(false, func);
    });

CTS_TEST(g, "valid_functions")
    .desc("Test functions with valid behaviors")
    .params([](ParamsBuilder u) { return u.combine("function", namesOf(kValidFunctions())); })
    .fn([](ShaderValidationTest& t) {
        const std::string func = bodyOf(kValidFunctions(), t.param<std::string>("function"));
        t.expectCompileResult(true, func);
    });

}  // namespace
