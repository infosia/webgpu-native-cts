// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/semicolon.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,semicolon",
    "Validation tests for semicolon placements");

CTS_TEST(g, "module_scope_single")
    .desc("Test that a semicolon can be placed at module scope.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, ";"); });

CTS_TEST(g, "module_scope_multiple")
    .desc("Test that multiple semicolons can be placed at module scope.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, ";;;"); });

CTS_TEST(g, "after_enable")
    .desc("Test that a semicolon must be placed after an enable directive.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "enable f16;");
        t.expectCompileResult(false, "enable f16");
    });

CTS_TEST(g, "after_requires")
    .desc("Test that a semicolon must be placed after a requires directive.")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");
        t.expectCompileResult(true, "requires readonly_and_readwrite_storage_textures;");
        t.expectCompileResult(false, "requires readonly_and_readwrite_storage_textures");
    });

CTS_TEST(g, "after_diagnostic")
    .desc("Test that a semicolon must be placed after a requires directive.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "diagnostic(info, derivative_uniformity);");
        t.expectCompileResult(false, "diagnostic(info, derivative_uniformity)");
    });

CTS_TEST(g, "after_struct_decl")
    .desc("Test that a semicolon can be placed after an struct declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "struct S { x : i32 };");
        t.expectCompileResult(true, "struct S { x : i32 }");
    });

CTS_TEST(g, "after_member")
    .desc("Test that a semicolon must not be placed after an struct member declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "struct S { x : i32 }");
        t.expectCompileResult(false, "struct S { x : i32; }");
    });

CTS_TEST(g, "after_func_decl")
    .desc("Test that a semicolon can be placed after a function declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() {};");
        t.expectCompileResult(true, "fn f() {}");
    });

CTS_TEST(g, "after_type_alias_decl")
    .desc("Test that a semicolon must be placed after an type alias declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "alias T = i32;");
        t.expectCompileResult(false, "alias T = i32");
    });

CTS_TEST(g, "after_return")
    .desc("Test that a semicolon must be placed after a return statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { return; }");
        t.expectCompileResult(false, "fn f() { return }");
    });

CTS_TEST(g, "after_call")
    .desc("Test that a semicolon must be placed after a function call.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { workgroupBarrier(); }");
        t.expectCompileResult(false, "fn f() { workgroupBarrier() }");
    });

CTS_TEST(g, "after_module_const_decl")
    .desc("Test that a semicolon must be placed after a module-scope const declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "const v = 1;");
        t.expectCompileResult(false, "const v = 1");
    });

CTS_TEST(g, "after_fn_const_decl")
    .desc("Test that a semicolon must be placed after a function-scope const declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { const v = 1; }");
        t.expectCompileResult(false, "fn f() { const v = 1 }");
    });

CTS_TEST(g, "after_module_var_decl")
    .desc("Test that a semicolon must be placed after a module-scope var declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "var<private> v = 1;");
        t.expectCompileResult(false, "var<private> v = 1");
    });

CTS_TEST(g, "after_fn_var_decl")
    .desc("Test that a semicolon must be placed after a function-scope var declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { var v = 1; }");
        t.expectCompileResult(false, "fn f() { var v = 1 }");
    });

CTS_TEST(g, "after_let_decl")
    .desc("Test that a semicolon must be placed after a let declaration.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { let v = 1; }");
        t.expectCompileResult(false, "fn f() { let v = 1 }");
    });

CTS_TEST(g, "after_discard")
    .desc("Test that a semicolon must be placed after a discard statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { discard; }");
        t.expectCompileResult(false, "fn f() { discard }");
    });

CTS_TEST(g, "after_assignment")
    .desc("Test that a semicolon must be placed after an assignment statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { var v = 1; v = 2; }");
        t.expectCompileResult(false, "fn f() { var v = 1; v = 2 }");
    });

CTS_TEST(g, "after_fn_const_assert")
    .desc("Test that a semicolon must be placed after an function-scope static assert.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { const_assert(true); }");
        t.expectCompileResult(false, "fn f() { const_assert(true) }");
    });

CTS_TEST(g, "function_body_single")
    .desc("Test that a semicolon can be placed in a function body.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, "fn f() { ; }"); });

CTS_TEST(g, "function_body_multiple")
    .desc("Test that multiple semicolons can be placed in a function body.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, "fn f() { ;;; }"); });

CTS_TEST(g, "compound_statement_single")
    .desc("Test that a semicolon can be placed in a compound statement.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, "fn f() { { ; } }"); });

CTS_TEST(g, "compound_statement_multiple")
    .desc("Test that multiple semicolons can be placed in a compound statement.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, "fn f() { { ;;; } }"); });

CTS_TEST(g, "after_compound_statement")
    .desc("Test that a semicolon can be placed after a compound statement.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, "fn f() { {} ; }"); });

CTS_TEST(g, "after_if")
    .desc("Test that a semicolon can be placed after an if-statement.")
    .fn([](ShaderValidationTest& t) { t.expectCompileResult(true, "fn f() { if true {} ; }"); });

CTS_TEST(g, "after_if_else")
    .desc("Test that a semicolon can be placed after an if-else-statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { if true {} else {} ; }");
    });

CTS_TEST(g, "after_switch")
    .desc("Test that a semicolon can be placed after an switch-statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { switch 1 { default {} } ; }");
    });

CTS_TEST(g, "after_case")
    .desc("Test that a semicolon cannot be placed after a non-default switch case.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "fn f() { switch 1 { case 1 {}; default {} } }");
        t.expectCompileResult(true, "fn f() { switch 1 { case 1 {} default {} } }");
    });

CTS_TEST(g, "after_case_break")
    .desc("Test that a semicolon must be placed after a case break statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "fn f() { switch 1 { case 1 { break } default {} } }");
        t.expectCompileResult(true, "fn f() { switch 1 { case 1 { break; } default {} } }");
    });

CTS_TEST(g, "after_default_case")
    .desc("Test that a semicolon cannot be placed after a default switch case.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "fn f() { switch 1 { default {}; } }");
        t.expectCompileResult(true, "fn f() { switch 1 { default {} } }");
    });

CTS_TEST(g, "after_default_case_break")
    .desc("Test that a semicolon cannot be placed after a default switch case.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "fn f() { switch 1 { default { break } } }");
        t.expectCompileResult(true, "fn f() { switch 1 { default { break; } } }");
    });

CTS_TEST(g, "after_for")
    .desc("Test that a semicolon can be placed after a for-loop.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { for (; false;) {}; }");
    });

CTS_TEST(g, "after_for_break")
    .desc("Test that a semicolon must be placed after a for-loop break statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { for (; false;) { break; } }");
        t.expectCompileResult(false, "fn f() { for (; false;) { break } }");
    });

CTS_TEST(g, "after_loop")
    .desc("Test that a semicolon can be placed after a loop.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { loop { break; }; }");
    });

CTS_TEST(g, "after_loop_break")
    .desc("Test that a semicolon must be placed after a loop break statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { loop { break; }; }");
        t.expectCompileResult(false, "fn f() { loop { break }; }");
    });

CTS_TEST(g, "after_loop_break_if")
    .desc("Test that a semicolon must be placed after a loop break-if statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { loop { continuing { break if true; } }; }");
        t.expectCompileResult(false, "fn f() { loop { continuing { break if true } }; }");
    });

CTS_TEST(g, "after_loop_continue")
    .desc("Test that a semicolon must be placed after a loop continue statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { loop { if true { continue; } { break; } } }");
        t.expectCompileResult(false, "fn f() { loop { if true { continue } { break; } } }");
    });

CTS_TEST(g, "after_continuing")
    .desc("Test that a semicolon cannot be placed after a continuing.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "fn f() { loop { break; continuing{}; } }");
        t.expectCompileResult(true, "fn f() { loop { break; continuing{} } }");
    });

CTS_TEST(g, "after_while")
    .desc("Test that a semicolon cannot be placed after a while-loop.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { while false {}; }");
    });

CTS_TEST(g, "after_while_break")
    .desc("Test that a semicolon must be placed after a while break statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { while false { break; } }");
        t.expectCompileResult(false, "fn f() { while false { break } }");
    });

CTS_TEST(g, "after_while_continue")
    .desc("Test that a semicolon must be placed after a while continue statement.")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn f() { while false { continue; } }");
        t.expectCompileResult(false, "fn f() { while false { continue } }");
    });

}  // namespace
