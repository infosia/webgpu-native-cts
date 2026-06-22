// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/quadBroadcast.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

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
    "shader,validation,expression,call,builtin,quadBroadcast",
    "Validation tests for quadBroadcast");

bool requiresF16(const bt::Type& type) { return type.kind == bt::ScalarKind::F16; }

CTS_TEST(g, "requires_subgroups")
    .desc("Validates that the subgroups feature is required")
    .params([](ParamsBuilder u) { return u.combine("enable", {Value(false), Value(true)}); })
    .fn([](ShaderValidationTest& t) {
        const bool enable = t.param<bool>("enable");
        const std::string wgsl = std::string("\n") + (enable ? "enable subgroups;" : "") +
                                 "\nfn foo() {\n  _ = quadBroadcast(0, 0);\n}";
        t.expectCompileResult(enable, wgsl);
    });

std::string stageCode(const std::string& stage) {
    if (stage == "constant") {
        return "\nenable subgroups;\n@compute @workgroup_size(16)\nfn main() {\n  const x = "
               "quadBroadcast(0, 0);\n}";
    }
    if (stage == "override") {
        return "\nenable subgroups;\noverride o = quadBroadcast(0, 0);";
    }
    return "\nenable subgroups;\n@compute @workgroup_size(16)\nfn main() {\n  let x = "
           "quadBroadcast(0, 0);\n}";
}

CTS_TEST(g, "early_eval")
    .desc("Ensures the builtin is not able to be compile time evaluated")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {Value(std::string("constant")), Value(std::string("override")),
                                   Value(std::string("runtime"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        t.expectCompileResult(stage == "runtime", stageCode(stage));
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the builtin has the @must_use attribute")
    .params([](ParamsBuilder u) { return u.combine("must_use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool mustUse = t.param<bool>("must_use");
        const std::string wgsl = std::string("\nenable subgroups;\n@compute @workgroup_size(16)\n"
                                             "fn main() {\n  ") +
                                 (mustUse ? "_ = " : "") + "quadBroadcast(0, 0);\n}";
        t.expectCompileResult(mustUse, wgsl);
    });

CTS_TEST(g, "data_type")
    .desc("Validates data parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;";
        }
        const std::string wgsl = std::string("\n") + enables +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  _ = quadBroadcast(" +
                                 bt::createWgsl(type, 0) + ", 0);\n}";
        t.expectCompileResult(bt::scalarTypeOf(type).kind != bt::ScalarKind::Bool, wgsl);
    });

CTS_TEST(g, "return_type")
    .desc("Validates data parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("dataType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("retType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                const bt::Type retType =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "retType")));
                const bt::Type dataType =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "dataType")));
                const bt::ScalarKind rk = bt::scalarTypeOf(retType).kind;
                const bt::ScalarKind dk = bt::scalarTypeOf(dataType).kind;
                return rk != bt::ScalarKind::AbstractInt && rk != bt::ScalarKind::AbstractFloat &&
                       dk != bt::ScalarKind::AbstractInt && dk != bt::ScalarKind::AbstractFloat;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type dataType = bt::typeByName(t.param<std::string>("dataType"));
        const bt::Type retType = bt::typeByName(t.param<std::string>("retType"));
        std::string enables = "enable subgroups;\n";
        if (requiresF16(dataType) || requiresF16(retType)) {
            enables += "enable f16;";
        }
        const std::string wgsl = std::string("\n") + enables +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  let res : " +
                                 retType.toString() + " = quadBroadcast(" +
                                 bt::createWgsl(dataType, 0) + ", 0);\n}";
        const bool expect =
            bt::scalarTypeOf(dataType).kind != bt::ScalarKind::Bool && dataType == retType;
        t.expectCompileResult(expect, wgsl);
    });

CTS_TEST(g, "id_type")
    .desc("Validates id parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const std::string wgsl =
            std::string("\nenable subgroups;\n@compute @workgroup_size(1)\nfn main() {\n  _ = "
                        "quadBroadcast(0, ") +
            bt::createWgsl(type, 0) + ");\n}";
        const bool expect = bt::isConvertible(type, bt::scalar(bt::ScalarKind::U32)) ||
                            bt::isConvertible(type, bt::scalar(bt::ScalarKind::I32));
        t.expectCompileResult(expect, wgsl);
    });

// kIdCases
struct IdCase {
    const char* name;
    const char* code;
    bool valid;
};
const std::vector<IdCase>& kIdCases() {
    static const std::vector<IdCase> v = {
        {"const_decl", "const_decl", true},   {"const_literal", "0", true},
        {"const_expr", "const_decl + 2", true}, {"let_decl", "let_decl", false},
        {"override_decl", "override_decl", false}, {"var_func_decl", "var_func_decl", false},
        {"var_priv_decl", "var_priv_decl", false},
    };
    return v;
}
std::vector<Value> idCaseNames() {
    std::vector<Value> out;
    for (const IdCase& c : kIdCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
const IdCase& findIdCase(const std::string& name) {
    for (const IdCase& c : kIdCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const IdCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "id_constness")
    .desc("Validates that id must be a const-expression")
    .params([](ParamsBuilder u) { return u.combine("value", idCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const IdCase& c = findIdCase(t.param<std::string>("value"));
        const std::string wgsl =
            std::string("\nenable subgroups;\noverride override_decl : u32;\n"
                        "var<private> var_priv_decl : u32;\nfn foo() {\n"
                        "  var var_func_decl : u32;\n  let let_decl = var_func_decl;\n"
                        "  const const_decl = 0u;\n  _ = quadBroadcast(0, ") +
            c.code + ");\n}";
        t.expectCompileResult(c.valid, wgsl);
    });

CTS_TEST(g, "id_values")
    .desc("Validates that id must be in the range [0, 4)")
    .params([](ParamsBuilder u) {
        return u
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(3)),
                               Value(int64_t(4))})
            .beginSubcases()
            .combine("type", {Value(std::string("literal")), Value(std::string("const")),
                              Value(std::string("expr"))});
    })
    .fn([](ShaderValidationTest& t) {
        const int64_t value = t.param<int64_t>("value");
        const std::string type = t.param<std::string>("type");
        std::string arg = std::to_string(value);
        if (type == "const") {
            arg = "c";
        } else if (type == "expr") {
            arg = "c + 0";
        }
        const std::string wgsl = std::string("\nenable subgroups;\n\nconst c = ") +
                                 std::to_string(value) + ";\n\nfn foo() {\n  _ = quadBroadcast(0, " +
                                 arg + ");\n}";
        const bool expect = value >= 0 && value < 4;
        t.expectCompileResult(expect, wgsl);
    });

CTS_TEST(g, "stage")
    .desc("Validates it is only usable in correct stage")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {Value(std::string("compute")), Value(std::string("fragment")),
                                   Value(std::string("vertex"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string compute = "\n@compute @workgroup_size(1)\nfn main() {\n  foo();\n}";
        const std::string fragment = "\n@fragment\nfn main() {\n  foo();\n}";
        const std::string vertex =
            "\n@vertex\nfn main() -> @builtin(position) vec4f {\n  foo();\n  return vec4f();\n}";
        const std::string entry =
            stage == "compute" ? compute : (stage == "fragment" ? fragment : vertex);
        const std::string wgsl = std::string("\nenable subgroups;\nfn foo() {\n  _ = "
                                             "quadBroadcast(0, 0);\n}\n\n") +
                                 entry + "\n";
        t.expectCompileResult(stage != "vertex", wgsl);
    });

}  // namespace
