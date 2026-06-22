// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/workgroupUniformLoad.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,workgroupUniformLoad",
    "Validation tests for the workgroupUniformLoad() builtin.");

struct EntryPoint {
    const char* name;
    bool supportsBarrier;
    const char* code;
};
static const std::vector<EntryPoint>& kEntryPoints() {
    static const std::vector<EntryPoint> v = {
        {"none", true, ""},
        {"compute", true, "@compute @workgroup_size(1)\nfn main() {\n  foo();\n}"},
        {"vertex", false,
         "@vertex\nfn main() -> @builtin(position) vec4f {\n  foo();\n  return vec4f();\n}"},
        {"fragment", false, "@fragment\nfn main() {\n  foo();\n}"},
        {"compute_and_fragment", false,
         "@compute @workgroup_size(1)\nfn main1() {\n  foo();\n}\n\n@fragment\nfn main2() {\n  "
         "foo();\n}\n"},
        {"fragment_without_call", true, "@fragment\nfn main() {\n}\n"},
    };
    return v;
}
static const EntryPoint& findEntryPoint(const std::string& name) {
    for (const EntryPoint& e : kEntryPoints()) {
        if (name == e.name) {
            return e;
        }
    }
    static const EntryPoint dummy{"", false, ""};
    return dummy;
}
static std::vector<Value> entryPointNames() {
    std::vector<Value> out;
    for (const EntryPoint& e : kEntryPoints()) {
        out.emplace_back(std::string(e.name));
    }
    return out;
}

CTS_TEST(g, "only_in_compute")
    .desc("Synchronization functions must only be used in the compute shader stage.")
    .params([](ParamsBuilder u) {
        return u.combine("entry_point", entryPointNames())
            .combine("call", {Value(std::string("bar()")),
                              Value(std::string("workgroupUniformLoad(&wgvar)"))});
    })
    .fn([](ShaderValidationTest& t) {
        const EntryPoint& config = findEntryPoint(t.param<std::string>("entry_point"));
        const std::string call = t.param<std::string>("call");
        const std::string code =
            std::string("\n") + config.code +
            "\n\nvar<workgroup> wgvar : u32;\n\nfn bar() -> u32 {\n  return 0;\n}\n\nfn foo() {\n  _ "
            "= " +
            call + ";\n}";
        t.expectCompileResult(call == "bar()" || config.supportsBarrier, code);
    });

// A list of types that contains atomics, with a single control case.
static std::vector<Value> atomicTypes() {
    return {Value(std::string("bool")), Value(std::string("array<atomic<i32>, 4>")),
            Value(std::string("AtomicStruct"))};
}

CTS_TEST(g, "no_atomics")
    .desc(
        "The argument passed to workgroupUniformLoad cannot contain any atomic types.\n\nNOTE: "
        "Various other valid types are tested via execution tests, so we only check for invalid "
        "types here.")
    .params([](ParamsBuilder u) {
        return u.combine("type", atomicTypes())
            .combine("call", {Value(std::string("bar()")),
                              Value(std::string("workgroupUniformLoad(&wgvar)"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string call = t.param<std::string>("call");
        const std::string code =
            "\nstruct AtomicStruct {\n  a : atomic<u32>\n}\n\nvar<workgroup> wgvar : " + type +
            ";\n\nfn bar() -> bool {\n  return true;\n}\n\nfn foo() {\n  _ = " + call + ";\n}";
        t.expectCompileResult(type == "bool" || call == "bar()", code);
    });

CTS_TEST(g, "param_constructible_only")
    .desc("The type of the argument passed to workgroupUniformLoad must be constructible.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {Value(std::string("const")), Value(std::string("override"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string code =
            "\n" + stage +
            " array_size = 10u;\nvar<workgroup> wgvar : array<u32, array_size>;\n\nfn foo() {\n  _ "
            "= workgroupUniformLoad(&wgvar)[0];\n}";
        t.expectCompileResult(stage == "const", code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ =" : "";
        const std::string code =
            "\n    var<workgroup> v : u32;\n    fn foo() {\n      " + useIt +
            " workgroupUniformLoad(&v);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
