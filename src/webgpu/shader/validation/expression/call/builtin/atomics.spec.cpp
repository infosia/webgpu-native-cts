// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/atomics.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,atomics", "Validation tests for atomic builtins.");

// kAtomicOps: key -> (a) => atomic builtin call string. Order preserved.
struct AtomicOp {
    const char* key;
};
static const std::vector<AtomicOp>& kAtomicOps() {
    static const std::vector<AtomicOp> v = {
        {"add"}, {"sub"},  {"max"},      {"min"},      {"and"},
        {"or"},  {"xor"},  {"load"},     {"store"},    {"exchange"},
        {"compareexchangeweak"},
    };
    return v;
}
static std::string atomicOpCall(const std::string& op, const std::string& a) {
    if (op == "add") return "atomicAdd(" + a + ",1)";
    if (op == "sub") return "atomicSub(" + a + ",1)";
    if (op == "max") return "atomicMax(" + a + ",1)";
    if (op == "min") return "atomicMin(" + a + ",1)";
    if (op == "and") return "atomicAnd(" + a + ",1)";
    if (op == "or") return "atomicOr(" + a + ",1)";
    if (op == "xor") return "atomicXor(" + a + ",1)";
    if (op == "load") return "atomicLoad(" + a + ")";
    if (op == "store") return "atomicStore(" + a + ",1)";
    if (op == "exchange") return "atomicExchange(" + a + ",1)";
    if (op == "compareexchangeweak") return "atomicCompareExchangeWeak(" + a + ",1,1)";
    return "";
}
static std::vector<Value> atomicOpKeys() {
    std::vector<Value> out;
    for (const AtomicOp& a : kAtomicOps()) {
        out.emplace_back(std::string(a.key));
    }
    return out;
}

CTS_TEST(g, "stage")
    .desc("Atomic built-in functions must not be used in a vertex shader stage.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {Value(std::string("fragment")), Value(std::string("vertex")),
                                   Value(std::string("compute"))})
            .combine("atomicOp", atomicOpKeys());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string atomicOp = atomicOpCall(t.param<std::string>("atomicOp"), "&a");
        std::string code = "\n@group(0) @binding(0) var<storage, read_write> a: atomic<i32>;\n";
        if (stage == "compute") {
            code += "\n@compute @workgroup_size(1,1,1) fn main() {\n  " + atomicOp + ";\n}";
        } else if (stage == "fragment") {
            code += "\n@fragment fn main() -> @location(0) vec4<f32> {\n  " + atomicOp +
                    ";\n  return vec4<f32>();\n}";
        } else if (stage == "vertex") {
            code += "\n@vertex fn vmain() -> @builtin(position) vec4<f32> {\n  " + atomicOp +
                    ";\n  return vec4<f32>();\n}";
        }
        const bool pass = stage != "vertex";
        t.expectCompileResult(pass, code);
    });

static std::string generateAtomicCode(const std::string& type, const std::string& access,
                                      const std::string& aspace, const std::string& style,
                                      const std::string& op) {
    std::string moduleVar;
    std::string functionVar;
    std::string param;
    std::string aParam;
    if (style == "var") {
        aParam = "&a";
        if (aspace == "storage") {
            moduleVar = "@group(0) @binding(0) var<storage, " + access + "> a : atomic<" + type +
                        ">;\n";
        } else if (aspace == "workgroup") {
            moduleVar = "var<workgroup> a : atomic<" + type + ">;\n";
        } else if (aspace == "uniform") {
            moduleVar = "@group(0) @binding(0) var<uniform> a : atomic<" + type + ">;\n";
        } else if (aspace == "private") {
            moduleVar = "var<private> a : atomic<" + type + ">;\n";
        } else if (aspace == "function") {
            functionVar = "var a : atomic<" + type + ">;\n";
        }
    } else {
        const std::string aspaceParam = aspace == "storage" ? (", " + access) : "";
        param = "p : ptr<" + aspace + ", atomic<" + type + ">" + aspaceParam + ">";
        aParam = "p";
    }

    return "\n" + moduleVar + "\nfn foo(" + param + ") {\n  " + functionVar + "\n  " +
           atomicOpCall(op, aParam) + ";\n}\n";
}

CTS_TEST(g, "atomic_parameterization")
    .desc("Tests the valid atomic parameters")
    .params([](ParamsBuilder u) {
        return u.combine("op", atomicOpKeys())
            .beginSubcases()
            .combine("aspace", {Value(std::string("storage")), Value(std::string("workgroup")),
                                Value(std::string("private")), Value(std::string("uniform")),
                                Value(std::string("function"))})
            .combine("access", {Value(std::string("read")), Value(std::string("read_write"))})
            .combine("type", {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("style", {Value(std::string("param")), Value(std::string("var"))})
            .filter([](const ParamRecord& p) {
                const std::string aspace = valueAs<std::string>(*findParam(p, "aspace"));
                const std::string style = valueAs<std::string>(*findParam(p, "style"));
                const std::string access = valueAs<std::string>(*findParam(p, "access"));
                if (aspace == "uniform") {
                    return style == "param" && access == "read";
                }
                if (aspace == "workgroup") {
                    return access == "read_write";
                }
                if (aspace == "function" || aspace == "private") {
                    return style == "param" && access == "read_write";
                }
                return true;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const std::string aspace = t.param<std::string>("aspace");
        const std::string access = t.param<std::string>("access");
        const std::string type = t.param<std::string>("type");
        const std::string style = t.param<std::string>("style");
        if (style == "param" && !(aspace == "function" || aspace == "private")) {
            t.skipIfLanguageFeatureNotSupported("unrestricted_pointer_parameters");
        }
        const bool aspaceOK = aspace == "storage" || aspace == "workgroup";
        const bool accessOK = access == "read_write";
        t.expectCompileResult(aspaceOK && accessOK,
                              generateAtomicCode(type, access, aspace, style, op));
    });

CTS_TEST(g, "data_parameters")
    .desc("Validates that data parameters must match atomic type (or be implicitly convertible)")
    .params([](ParamsBuilder u) {
        return u
            .combine("op",
                     {Value(std::string("atomicStore")), Value(std::string("atomicAdd")),
                      Value(std::string("atomicSub")), Value(std::string("atomicMax")),
                      Value(std::string("atomicMin")), Value(std::string("atomicAnd")),
                      Value(std::string("atomicOr")), Value(std::string("atomicXor")),
                      Value(std::string("atomicExchange")),
                      Value(std::string("atomicCompareExchangeWeak1")),
                      Value(std::string("atomicCompareExchangeWeak2"))})
            .beginSubcases()
            .combine("atomicType", {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("dataType", {Value(std::string("i32")), Value(std::string("u32")),
                                  Value(std::string("f32")), Value(std::string("AbstractInt"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string opParam = t.param<std::string>("op");
        const std::string atomicType = t.param<std::string>("atomicType");
        const std::string dataType = t.param<std::string>("dataType");
        std::string dataValue;
        if (dataType == "i32") {
            dataValue = "1i";
        } else if (dataType == "u32") {
            dataValue = "1u";
        } else if (dataType == "f32") {
            dataValue = "1f";
        } else if (dataType == "AbstractInt") {
            dataValue = "1";
        }
        std::string op;
        if (opParam == "atomicCompareExchangeWeak1") {
            op = "atomicCompareExchangeWeak(&a, " + dataValue + ", 1)";
        } else if (opParam == "atomicCompareExchangeWeak2") {
            op = "atomicCompareExchangeWeak(&a, 1, " + dataValue + ")";
        } else {
            op = opParam + "(&a, " + dataValue + ")";
        }
        const std::string code = "\nvar<workgroup> a : atomic<" + atomicType + ">;\nfn foo() {\n  " +
                                 op + ";\n}\n";
        const bool expect = atomicType == dataType || dataType == "AbstractInt";
        t.expectCompileResult(expect, code);
    });

CTS_TEST(g, "return_types")
    .desc("Validates return types of atomics")
    .params([](ParamsBuilder u) {
        return u.combine("op", atomicOpKeys())
            .beginSubcases()
            .combine("atomicType", {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("returnType", {Value(std::string("i32")), Value(std::string("u32")),
                                    Value(std::string("f32"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string opKey = t.param<std::string>("op");
        const std::string atomicType = t.param<std::string>("atomicType");
        const std::string returnType = t.param<std::string>("returnType");
        std::string op = atomicOpCall(opKey, "&a");
        if (opKey == "compareexchangeweak") {
            op = "let tmp : " + returnType + " = " + op + ".old_value";
        } else {
            op = "let tmp : " + returnType + " = " + op;
        }
        const std::string code = "\nvar<workgroup> a : atomic<" + atomicType + ">;\nfn foo() {\n  " +
                                 op + ";\n}\n";
        const bool expect = atomicType == returnType && opKey != "store";
        t.expectCompileResult(expect, code);
    });

CTS_TEST(g, "non_atomic")
    .desc("Test that non-atomic integers are rejected by all atomic functions.")
    .params([](ParamsBuilder u) {
        return u.combine("op", atomicOpKeys())
            .combine("addrspace",
                     {Value(std::string("storage")), Value(std::string("workgroup"))})
            .combine("type", {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("atomic", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string opKey = t.param<std::string>("op");
        const std::string addrspace = t.param<std::string>("addrspace");
        std::string type = t.param<std::string>("type");
        const bool atomic = t.param<bool>("atomic");
        if (atomic) {
            type = "atomic<" + type + ">";
        }
        std::string decl;
        if (addrspace == "storage") {
            decl = "@group(0) @binding(0) var<storage, read_write> a : " + type;
        } else if (addrspace == "workgroup") {
            decl = "var<workgroup> a : " + type;
        }
        const std::string op = atomicOpCall(opKey, "&a");
        const std::string code = "\n" + decl + ";\nfn foo() {\n  " + op + ";\n}\n";
        t.expectCompileResult(atomic, code);
    });

}  // namespace
