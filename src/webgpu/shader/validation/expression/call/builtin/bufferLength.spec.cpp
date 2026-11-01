// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/bufferLength.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdlib>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,bufferLength",
    "Validation tests for bufferLength");

CTS_TEST(g, "must_use")
    .desc("Tests that the builtin has the @must_use attribute")
    .params([](ParamsBuilder u) { return u.combine("must_use", {true, false}); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const bool mustUse = t.param<bool>("must_use");
        const std::string wgsl = std::string(R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  )wgsl") + (mustUse ? "_ = " : "") + "bufferLength(&v);\n}";

        t.expectCompileResult(mustUse, wgsl);
    });

CTS_TEST(g, "return_type")
    .desc("Validates return type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                const bt::ScalarKind eleType =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type"))).kind;
                return eleType != bt::ScalarKind::AbstractInt && eleType != bt::ScalarKind::AbstractFloat;
            });
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const std::string enables = type.kind == bt::ScalarKind::F16 ? "enable f16;" : "";
        const std::string wgsl = "\n" + enables + R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let res : )wgsl" + type.toString() + " = bufferLength(&v);\n}";

        t.expectCompileResult(type == bt::scalar(bt::ScalarKind::U32), wgsl);
    });

CTS_TEST(g, "data_type")
    .desc("Validates the input parameter type")
    .params([](ParamsBuilder u) {
        return u
            .combine("type", {Value("unsized_ro_storage"), Value("unsized_storage"), Value("sized_ro_storage"),
                              Value("sized_storage"), Value("sized_uniform"), Value("sized_workgroup"),
                              Value("override_workgroup")})
            .beginSubcases()
            .combine("ptr", {false, true});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const bool ptr = t.param<bool>("ptr");
        const std::string wgsl = std::string(R"wgsl(
@group(0) @binding(0) var<storage> unsized_ro_storage : buffer;
@group(0) @binding(1) var<storage, read_write> unsized_storage : buffer;
@group(0) @binding(2) var<storage> sized_ro_storage : buffer<128>;
@group(0) @binding(3) var<storage, read_write> sized_storage : buffer<128>;
@group(0) @binding(4) var<uniform> sized_uniform : buffer<128>;
var<workgroup> sized_workgroup : buffer<128>;
override o : u32;
var<workgroup> override_workgroup : buffer<o>;

@compute @workgroup_size(1)
fn main() {
  _ = bufferLength()wgsl") + (ptr ? "&" : "") + t.param<std::string>("type") + ");\n}";

        t.expectCompileResult(ptr, wgsl);
    });

struct InvalidCase {
    const char* name;
    const char* expression;
};

const std::vector<InvalidCase> kInvalidCases = {
    {"baseline", "&b"},
    {"no_args", "bufferLength()"},
    {"two_args", "bufferLength(&b, 0)"},
    {"scalar", "bufferLength(0u)"},
    {"ptr_to_scalar", "bufferLength(&v[0])"},
    {"ptr_to_array", "bufferLength(&v)"},
};

std::vector<Value> invalidCaseNames() {
    std::vector<Value> values;
    values.reserve(kInvalidCases.size());
    for (const InvalidCase& testcase : kInvalidCases) {
        values.emplace_back(std::string(testcase.name));
    }
    return values;
}

const InvalidCase& findInvalidCase(const std::string& name) {
    for (const InvalidCase& testcase : kInvalidCases) {
        if (name == testcase.name) {
            return testcase;
        }
    }
    std::abort();
}

CTS_TEST(g, "invalid_cases")
    .desc("Test a smattering of invalid bufferLength calls")
    .params([](ParamsBuilder u) { return u.combine("test", invalidCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const std::string test = t.param<std::string>("test");
        const std::string code = std::string(R"wgsl(
var<workgroup> v : array<u32, 4>;
var<workgroup> b : buffer<128>;
@compute @workgroup_size(1)
fn main() {
  _ = )wgsl") + findInvalidCase(test).expression + ";\n}";

        t.expectCompileResult(test == "baseline", code);
    });

}  // namespace
