// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/derivatives.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/call/builtin/const_override_builtin.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,derivatives",
    "Validation tests for derivative builtins.");

static const std::vector<std::string>& kDerivativeBuiltins() {
    static const std::vector<std::string> v = {"dpdx",  "dpdxCoarse", "dpdxFine",
                                               "dpdy",  "dpdyCoarse", "dpdyFine",
                                               "fwidth", "fwidthCoarse", "fwidthFine"};
    return v;
}

// ---- kEntryPoints (fragment-only-builtin gating) -----------------------------
struct EntryPoint {
    const char* name;
    bool supportsDerivative;
    const char* code;
};
static const std::vector<EntryPoint>& kEntryPoints() {
    static const std::vector<EntryPoint> v = {
        {"none", true, ""},
        {"fragment", true, "@fragment\nfn main() {\n  foo();\n}"},
        {"vertex", false,
         "@vertex\nfn main() -> @builtin(position) vec4f {\n  foo();\n  return vec4f();\n}"},
        {"compute", false, "@compute @workgroup_size(1)\nfn main() {\n  foo();\n}"},
        {"fragment_and_compute", false,
         "@fragment\nfn main1() {\n  foo();\n}\n\n@compute @workgroup_size(1)\nfn main2() {\n  "
         "foo();\n}\n"},
        {"compute_without_call", true, "@compute @workgroup_size(1)\nfn main() {\n}\n"},
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

CTS_TEST(g, "only_in_fragment")
    .desc("Derivative functions must only be used in the fragment shader stage.")
    .params([](ParamsBuilder u) {
        std::vector<Value> calls = {Value(std::string("bar"))};
        for (const std::string& d : kDerivativeBuiltins()) {
            calls.emplace_back(d);
        }
        return u.combine("entry_point", entryPointNames()).combine("call", calls);
    })
    .fn([](ShaderValidationTest& t) {
        const EntryPoint& config = findEntryPoint(t.param<std::string>("entry_point"));
        const std::string call = t.param<std::string>("call");
        const std::string code = std::string("\n") + config.code +
                                 "\nfn bar(f : f32) -> f32 { return f; }\n\nfn foo() {\n  _ = " +
                                 call + "(1.0);\n}";
        t.expectCompileResult(call == "bar" || config.supportsDerivative, code);
    });

// ---- kArgumentTypes = [f32, kConcreteIntegerScalarsAndVectors,
//                        kConcreteF16ScalarsAndVectors, mat2x2f] -----------------
struct ArgType {
    std::string key;       // toString()
    std::string createOne; // create(1).wgsl()
    bool isF16;
    bool isF32;            // true only for the f32 control case
};
static const std::vector<ArgType>& kArgumentTypes() {
    static const std::vector<ArgType> v = [] {
        std::vector<ArgType> out;
        out.push_back({"f32", bt::createWgsl(bt::scalar(bt::ScalarKind::F32), 1), false, true});
        for (const bt::Type& ty : b::kConcreteIntegerScalarsAndVectors()) {
            out.push_back({ty.toString(), bt::createWgsl(ty, 1), false, false});
        }
        for (const bt::Type& ty : b::kConcreteF16ScalarsAndVectors()) {
            out.push_back({ty.toString(), bt::createWgsl(ty, 1), true, false});
        }
        // mat2x2f
        b::BuiltinValue mv = b::createMatrixValue(b::MatType{2, 2, bt::ScalarKind::F32}, 1.0);
        out.push_back({"mat2x2<f32>", b::builtinValueWgsl(mv), false, false});
        return out;
    }();
    return v;
}
static const ArgType& findArgType(const std::string& key) {
    for (const ArgType& a : kArgumentTypes()) {
        if (a.key == key) {
            return a;
        }
    }
    static const ArgType dummy{"", "", false, false};
    return dummy;
}
static std::vector<Value> argTypeNames() {
    std::vector<Value> out;
    for (const ArgType& a : kArgumentTypes()) {
        out.emplace_back(a.key);
    }
    return out;
}

CTS_TEST(g, "invalid_argument_types")
    .desc("Derivative builtins only accept f32 scalar and vector types.")
    .params([](ParamsBuilder u) {
        std::vector<Value> calls = {Value(std::string(""))};
        for (const std::string& d : kDerivativeBuiltins()) {
            calls.emplace_back(d);
        }
        return u.combine("type", argTypeNames()).combine("call", calls);
    })
    .fn([](ShaderValidationTest& t) {
        const ArgType& at = findArgType(t.param<std::string>("type"));
        const std::string call = t.param<std::string>("call");
        const std::string enables = at.isF16 ? "enable f16;" : "";
        const std::string code = std::string("\n") + enables + "\n\nfn foo() {\n  let x: " +
                                 at.key + " = " + call + "(" + at.createOne + ");\n}";
        t.expectCompileResult(at.isF32 || call.empty(), code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) {
        std::vector<Value> funcs;
        for (const std::string& d : kDerivativeBuiltins()) {
            funcs.emplace_back(d);
        }
        return u.combine("use", {Value(true), Value(false)}).combine("func", funcs);
    })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string func = t.param<std::string>("func");
        const std::string useIt = use ? "_ =" : "";
        const std::string code =
            "\n    fn foo() {\n      " + useIt + " " + func + "(1.0);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
