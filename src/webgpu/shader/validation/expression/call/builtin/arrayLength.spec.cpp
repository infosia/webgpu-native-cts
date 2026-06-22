// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/arrayLength.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,arrayLength",
    "Validation tests for arrayLength builtins.");

CTS_TEST(g, "bool_type")
    .desc("arrayLength accepts only runtime-sized arrays")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@compute @workgroup_size(1)\nfn main() {\n  var b = true;\n  _ = "
            "arrayLength(&b);\n}";
        t.expectCompileResult(false, code);
    });

// type list: i32,u32,f32,f16, f32_matrix_types, f16_matrix_types, vec_types,
// atomic_types, 'T', 'array<i32, 2>', 'array<i32>'.
//   f32_matrix_types = mat{2,3,4}x{2,3,4}f
//   f16_matrix_types = mat{2,3,4}x{2,3,4}<f16>
//   vec_types        = vec{2,3,4}<{i32,u32,f32,f16}>
//   atomic_types     = atomic<u32>, atomic<i32>
static std::vector<Value> typeList() {
    std::vector<std::string> out = {"i32", "u32", "f32", "f16"};
    for (int i = 2; i <= 4; ++i) {
        for (int j = 2; j <= 4; ++j) {
            out.push_back("mat" + std::to_string(i) + "x" + std::to_string(j) + "f");
        }
    }
    for (int i = 2; i <= 4; ++i) {
        for (int j = 2; j <= 4; ++j) {
            out.push_back("mat" + std::to_string(i) + "x" + std::to_string(j) + "<f16>");
        }
    }
    const char* elem[] = {"i32", "u32", "f32", "f16"};
    for (int i = 2; i <= 4; ++i) {
        for (const char* e : elem) {
            out.push_back("vec" + std::to_string(i) + "<" + e + ">");
        }
    }
    out.push_back("atomic<u32>");
    out.push_back("atomic<i32>");
    out.push_back("T");
    out.push_back("array<i32, 2>");
    out.push_back("array<i32>");

    std::vector<Value> values;
    for (const std::string& s : out) {
        values.emplace_back(s);
    }
    return values;
}

CTS_TEST(g, "type")
    .desc("arrayLength accepts only runtime-sized arrays")
    .params([](ParamsBuilder u) { return u.combine("type", typeList()); })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string code =
            "\nstruct T {\n  b: i32,\n}\nstruct S {\n  ary: " + type +
            "\n}\n\n@group(0) @binding(0) var<storage, read_write> items: S;\n\n@compute "
            "@workgroup_size(1)\nfn main() {\n  _ = arrayLength(&items.ary);\n}";
        t.expectCompileResult(type == "array<i32>", code);
    });

// Note, the `write` case actually fails because you can't declare a storage
// buffer of access_mode `write`.
CTS_TEST(g, "access_mode")
    .desc("arrayLength runtime-sized array must have an access_mode of read or read_write")
    .params([](ParamsBuilder u) {
        return u.combine("mode", {Value(std::string("read")), Value(std::string("read_write")),
                                  Value(std::string("write"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string mode = t.param<std::string>("mode");
        const std::string code =
            "\nstruct S {\n  ary: array<i32>,\n}\n\n@group(0) @binding(0) var<storage, " + mode +
            "> items: S;\n\n@compute @workgroup_size(1)\nfn main() {\n  _ = "
            "arrayLength(&items.ary);\n}";
        t.expectCompileResult(mode != "write", code);
    });

CTS_TEST(g, "must_use")
    .desc("Test that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ =" : "";
        const std::string code =
            "\n    @group(0) @binding(0) var<storage> v : array<u32>;\n    fn foo() {\n      " +
            useIt + " arrayLength(&v);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
