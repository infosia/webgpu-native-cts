// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/eval_order.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
// - Upstream's eval_order tests have no case params (no preventValueOptimizations
//   combine), so none are added here.
// - Each test returns FlowControlWgsl{entrypoint, extra}; the entrypoint
//   template is expanded before the extra template, matching upstream's
//   JS template-literal evaluation order of the builder calls.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,eval_order",
    "\nFlow control tests for expression evaluation order.\n");

CTS_TEST(g, "binary_op")
    .desc("Test that a binary operator evaluates the LHS then the RHS")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = lhs() + rhs();
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn lhs() -> i32 {
  %%
  return 0;
}
fn rhs() -> i32 {
  %%
  return 0;
})",
                {f.expect_order(1), f.expect_order(2)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_rhs_const")
    .desc("Test that a binary operator evaluates the LHS, when the RHS is a constant expression")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = lhs() + rhs();
  %%
)",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn lhs() -> i32 {
  %%
  return 0;
}
fn rhs() -> i32 {
  return 0;
})",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_lhs_const")
    .desc("Test that a binary operator evaluates the RHS, when the LHS is a constant expression")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = lhs() + rhs();
  %%
)",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn lhs() -> i32 {
  return 0;
}
fn rhs() -> i32 {
  %%
  return 0;
})",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_chain")
    .desc("Test that a binary operator chain evaluates left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = a() + b() - c() * d();
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_chain_R_C_C_C")
    .desc("Test evaluation order of a binary operator chain with a runtime-expression for the left-most expression")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = f() + 1 + 2 + 3;
  %%
)",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn f() -> i32 {
  %%
  return 1;
}
)",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_chain_C_R_C_C")
    .desc("Test evaluation order of a binary operator chain with a runtime-expression for the second-left-most-const")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = 1 + f() + 2 + 3;
  %%
  )",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn f() -> i32 {
  %%
  return 1;
}
  )",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_chain_C_C_R_C")
    .desc("Test evaluation order of a binary operator chain with a runtime-expression for the second-right-most-const")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = 1 + 2 + f() + 3;
  %%
)",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn f() -> i32 {
  %%
  return 1;
}
  )",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_chain_C_C_C_R")
    .desc("Test evaluation order of a binary operator chain with a runtime-expression for the right-most expression")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
    %%
    let l = 1 + 2 + 3 + f();
    %%
  )",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn f() -> i32 {
  %%
  return 1;
}
  )",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "binary_op_parenthesized_expr")
    .desc("Test that a parenthesized binary operator expression evaluates left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let x = (a() + b()) - (c() * d());
  %%
  let y = a() + (b() - c()) * d();
  %%
)",
                {f.expect_order(0), f.expect_order(5), f.expect_order(10)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1, 6), f.expect_order(2, 7), f.expect_order(3, 8),
                 f.expect_order(4, 9)});
            return wgsl;
        });
    });

CTS_TEST(g, "array_index")
    .desc("Test that array indices are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<array<i32, 8>, 8>, 8>;
  %%
  let x = arr[a()][b()][c()];
  %%
)",
                {f.expect_order(0), f.expect_order(4)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

CTS_TEST(g, "array_index_lhs_assignment")
    .desc("Test that array indices are evaluated left-to-right, when indexing the LHS of an assignment")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<array<i32, 8>, 8>, 8>;
  %%
  arr[a()][b()][c()] = ~d();
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "array_index_lhs_member_assignment")
    .desc("Test that array indices are evaluated left-to-right, when indexing with member-accessors in the LHS of an assignment")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<S, 8>, 8>;
  %%
  arr[a()][b()].member[c()] = d();
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
struct S {
  member : array<i32, 8>,
}
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "array_index_via_ptrs")
    .desc("Test that array indices are evaluated in order, when used via pointers")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<array<i32, 8>, 8>, 8>;
  %%
  let p0 = &arr;
  %%
  let p1 = &(*p0)[a()];
  %%
  let p2 = &(*p1)[b()];
  %%
  let p3 = &(*p2)[c()];
  %%
  let p4 = *p3;
)",
                {f.expect_order(0), f.expect_order(1), f.expect_order(3), f.expect_order(5),
                 f.expect_order(7)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(2), f.expect_order(4), f.expect_order(6)});
            return wgsl;
        });
    });

CTS_TEST(g, "array_index_via_struct_members")
    .desc("Test that array indices are evaluated in order, when accessed via structure members")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var x : X;
  %%
  let r = x.y[a()].z[b()].a[c()];
  %%
)",
                {f.expect_order(0), f.expect_order(4)});
            wgsl.extra = wgslTemplate(R"(
struct X {
  y : array<Y, 3>,
};
struct Y {
  z : array<Z, 3>,
};
struct Z {
  a : array<i32, 3>,
};
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
fn c() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

CTS_TEST(g, "matrix_index")
    .desc("Test that matrix indices are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var mat : mat4x4<f32>;
  %%
  let x = mat[a()][b()];
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(1), f.expect_order(2)});
            return wgsl;
        });
    });

CTS_TEST(g, "matrix_index_via_ptr")
    .desc("Test that matrix indices are evaluated in order, when used via pointers")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var mat : mat4x4<f32>;
  %%
  let p0 = &mat;
  %%
  let p1 = &(*p0)[a()];
  %%
  let v = (*p1)[b()];
  %%
)",
                {f.expect_order(0), f.expect_order(1), f.expect_order(3), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
})",
                {f.expect_order(2), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "logical_and")
    .desc("Test that a chain of logical-AND expressions are evaluated left-to-right, stopping at the first false")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = a() && b() && c();
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> bool {
  %%
  return true;
}
fn b() -> bool {
  %%
  return false;
}
fn c() -> bool {
  %%
  return true;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_not_reached()});
            return wgsl;
        });
    });

CTS_TEST(g, "logical_or")
    .desc("Test that a chain of logical-OR expressions are evaluated left-to-right, stopping at the first true")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = a() || b() || c();
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> bool {
  %%
  return false;
}
fn b() -> bool {
  %%
  return true;
}
fn c() -> bool {
  %%
  return true;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_not_reached()});
            return wgsl;
        });
    });

CTS_TEST(g, "bitwise_and")
    .desc("Test that a chain of bitwise-AND expressions are evaluated left-to-right, with no short-circuiting")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = a() & b() & c();
  %%
)",
                {f.expect_order(0), f.expect_order(4)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> bool {
  %%
  return true;
}
fn b() -> bool {
  %%
  return false;
}
fn c() -> bool {
  %%
  return true;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

CTS_TEST(g, "bitwise_or")
    .desc("Test that a chain of bitwise-OR expressions are evaluated left-to-right, with no short-circuiting")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = a() | b() | c();
  %%
)",
                {f.expect_order(0), f.expect_order(4)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> bool {
  %%
  return false;
}
fn b() -> bool {
  %%
  return true;
}
fn c() -> bool {
  %%
  return true;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

CTS_TEST(g, "user_fn_args")
    .desc("Test user function call arguments are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = f(a(), b(), c());
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 3;
}
fn f(x : i32, y : i32, z : i32) -> i32 {
  %%
  return x + y + z;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "nested_fn_args")
    .desc("Test user nested call arguments are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = g(c(a(), b()), f(d(), e()));
  %%
)",
                {f.expect_order(0), f.expect_order(8)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 0;
}
fn b() -> i32 {
  %%
  return 0;
}
fn c(x : i32, y : i32) -> i32 {
  %%
  return x + y;
}
fn d() -> i32 {
  %%
  return 0;
}
fn e() -> i32 {
  %%
  return 0;
}
fn f(x : i32, y : i32) -> i32 {
  %%
  return x + y;
}
fn g(x : i32, y : i32) -> i32 {
  %%
  return x + y;
})",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4),
                 f.expect_order(5), f.expect_order(6), f.expect_order(7)});
            return wgsl;
        });
    });

CTS_TEST(g, "builtin_fn_args")
    .desc("Test builtin function call arguments are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = mix(a(), b(), c());
  %%
)",
                {f.expect_order(0), f.expect_order(4)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> f32 {
  %%
  return 1;
}
fn b() -> f32 {
  %%
  return 2;
}
fn c() -> f32 {
  %%
  return 3;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

CTS_TEST(g, "nested_builtin_fn_args")
    .desc("Test nested builtin function call arguments are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let l = mix(a(), mix(b(), c(), d()), e());
  %%
)",
                {f.expect_order(0), f.expect_order(6)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> f32 {
  %%
  return 1;
}
fn b() -> f32 {
  %%
  return 2;
}
fn c() -> f32 {
  %%
  return 3;
}
fn d() -> f32 {
  %%
  return 3;
}
fn e() -> f32 {
  %%
  return 3;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4),
                 f.expect_order(5)});
            return wgsl;
        });
    });

CTS_TEST(g, "1d_array_constructor")
    .desc("Test arguments of an array constructor are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let v = array(a(), b(), c(), d());
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "2d_array_constructor")
    .desc("Test arguments of a 2D array constructor are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let v = array(array(a(), b()), array(c(), d()));
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "vec4_constructor")
    .desc("Test arguments of a vector constructor are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let v = vec4(a(), b(), c(), d());
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "nested_vec4_constructor")
    .desc("Test arguments of a nested vector constructor are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let v = vec4(a(), vec2(b(), c()), d());
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "struct_constructor")
    .desc("Test arguments of a structure constructor are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let v = S(a(), b(), c(), d());
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
struct S {
  a : i32,
  b : i32,
  c : i32,
  d : i32,
}
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "nested_struct_constructor")
    .desc("Test arguments of a nested structure constructor are evaluated left-to-right")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  %%
  let v = Y(a(), X(b(), c()), d());
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
struct Y {
  a : i32,
  x : X,
  c : i32,
}
struct X {
  b : i32,
  c : i32,
}
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "1d_array_assignment")
    .desc("Test LHS of an array element assignment is evaluated before RHS")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<i32, 8>;
  %%
  arr[a()] = arr[b()];
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2)});
            return wgsl;
        });
    });

CTS_TEST(g, "2d_array_assignment")
    .desc("Test LHS of 2D-array element assignment is evaluated before RHS")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<i32, 8>, 8>;
  %%
  arr[a()][b()] = arr[c()][d()];
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "1d_array_compound_assignment")
    .desc("Test LHS of an array element compound assignment is evaluated before RHS")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<i32, 8>;
  %%
  arr[a()] += arr[b()];
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2)});
            return wgsl;
        });
    });

CTS_TEST(g, "2d_array_compound_assignment")
    .desc("Test LHS of a 2D-array element compound assignment is evaluated before RHS")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<i32, 8>, 8>;
  %%
  arr[a()][b()] += arr[c()][d()];
  %%
)",
                {f.expect_order(0), f.expect_order(5)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 2;
}
fn c() -> i32 {
  %%
  return 1;
}
fn d() -> i32 {
  %%
  return 2;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3), f.expect_order(4)});
            return wgsl;
        });
    });

CTS_TEST(g, "1d_array_increment")
    .desc("Test index of an array element increment is evaluated only once")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<i32, 8>;
  %%
  arr[a()]++;
  %%
)",
                {f.expect_order(0), f.expect_order(2)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
)",
                {f.expect_order(1)});
            return wgsl;
        });
    });

CTS_TEST(g, "2d_array_increment")
    .desc("Test index of a 2D-array element increment is evaluated only once")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  var arr : array<array<i32, 8>, 8>;
  %%
  arr[a()][b()]++;
  %%
)",
                {f.expect_order(0), f.expect_order(3)});
            wgsl.extra = wgslTemplate(R"(
fn a() -> i32 {
  %%
  return 1;
}
fn b() -> i32 {
  %%
  return 1;
}
)",
                {f.expect_order(1), f.expect_order(2)});
            return wgsl;
        });
    });

} // namespace
