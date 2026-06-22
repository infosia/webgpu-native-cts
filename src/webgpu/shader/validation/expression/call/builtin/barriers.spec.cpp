// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/barriers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,barriers",
    "Validation tests for {storage,texture,workgroup}Barrier() builtins.");

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
            .combine("call", {Value(std::string("bar")), Value(std::string("storageBarrier")),
                              Value(std::string("textureBarrier")),
                              Value(std::string("workgroupBarrier"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string call = t.param<std::string>("call");
        if (call.rfind("textureBarrier", 0) == 0) {
            t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");
        }
        const EntryPoint& config = findEntryPoint(t.param<std::string>("entry_point"));
        const std::string code = std::string("\n") + config.code +
                                 "\nfn bar() {}\n\nfn foo() {\n  " + call + "();\n}";
        t.expectCompileResult(call == "bar" || config.supportsBarrier, code);
    });

CTS_TEST(g, "no_return_value")
    .desc("Barrier functions do not return a value.")
    .params([](ParamsBuilder u) {
        return u.combine("assign", {Value(false), Value(true)})
            .combine("rhs", {Value(std::string("bar")), Value(std::string("storageBarrier")),
                             Value(std::string("textureBarrier")),
                             Value(std::string("workgroupBarrier"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rhs = t.param<std::string>("rhs");
        if (rhs.rfind("textureBarrier", 0) == 0) {
            t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");
        }
        const bool assign = t.param<bool>("assign");
        const std::string assignIt = assign ? "_ = " : "";
        const std::string code = "\nfn bar() {}\n\nfn foo() {\n  " + assignIt + " " + rhs + "();\n}";
        t.expectCompileResult(!assign || rhs == "bar()", code);
    });

}  // namespace
