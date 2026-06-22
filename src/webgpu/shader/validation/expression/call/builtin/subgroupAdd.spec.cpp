// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/subgroupAdd.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,subgroupAdd",
    "Validation tests for subgroupAdd and subgroupExclusiveAdd");

// kBuiltins = ['subgroupAdd', 'subgroupExclusiveAdd', 'subgroupInclusiveAdd']
std::vector<Value> kBuiltins() {
    return {Value(std::string("subgroupAdd")), Value(std::string("subgroupExclusiveAdd")),
            Value(std::string("subgroupInclusiveAdd"))};
}

bool requiresF16(const bt::Type& type) { return type.kind == bt::ScalarKind::F16; }

std::string stageCode(const std::string& stage, const std::string& builtin) {
    if (stage == "constant") {
        return "\nenable subgroups;\n@compute @workgroup_size(16)\nfn main() {\n  const x = " +
               builtin + "(0);\n}";
    }
    if (stage == "override") {
        return "\nenable subgroups;\noverride o = " + builtin + "(0);";
    }
    return "\nenable subgroups;\n@compute @workgroup_size(16)\nfn main() {\n  let x = " + builtin +
           "(0);\n}";
}

CTS_TEST(g, "early_eval")
    .desc("Ensures the builtin is not able to be compile time evaluated")
    .params([](ParamsBuilder u) {
        return u
            .combine("stage", {Value(std::string("constant")), Value(std::string("override")),
                               Value(std::string("runtime"))})
            .beginSubcases()
            .combine("builtin", kBuiltins());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string builtin = t.param<std::string>("builtin");
        t.expectCompileResult(stage == "runtime", stageCode(stage, builtin));
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the builtin has the @must_use attribute")
    .params([](ParamsBuilder u) {
        return u.combine("must_use", {Value(true), Value(false)})
            .beginSubcases()
            .combine("builtin", kBuiltins());
    })
    .fn([](ShaderValidationTest& t) {
        const bool mustUse = t.param<bool>("must_use");
        const std::string builtin = t.param<std::string>("builtin");
        const std::string wgsl = std::string("\nenable subgroups;\n@compute @workgroup_size(16)\n"
                                             "fn main() {\n  ") +
                                 (mustUse ? "_ = " : "") + builtin + "(0);\n}";
        t.expectCompileResult(mustUse, wgsl);
    });

CTS_TEST(g, "data_type")
    .desc("Validates data parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("builtin", kBuiltins());
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const std::string builtin = t.param<std::string>("builtin");
        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;";
        }
        const std::string wgsl = std::string("\n") + enables +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  _ = " + builtin +
                                 "(" + bt::createWgsl(type, 0) + ");\n}";
        t.expectCompileResult(type.kind != bt::ScalarKind::Bool, wgsl);
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
                const bt::ScalarKind rk = retType.kind;
                const bt::ScalarKind dk = dataType.kind;
                return rk != bt::ScalarKind::AbstractInt && rk != bt::ScalarKind::AbstractFloat &&
                       dk != bt::ScalarKind::AbstractInt && dk != bt::ScalarKind::AbstractFloat;
            })
            .beginSubcases()
            .combine("builtin", kBuiltins());
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type dataType = bt::typeByName(t.param<std::string>("dataType"));
        const bt::Type retType = bt::typeByName(t.param<std::string>("retType"));
        const std::string builtin = t.param<std::string>("builtin");
        std::string enables = "enable subgroups;\n";
        if (requiresF16(dataType) || requiresF16(retType)) {
            enables += "enable f16;";
        }
        const std::string wgsl = std::string("\n") + enables +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  let res : " +
                                 retType.toString() + " = " + builtin + "(" +
                                 bt::createWgsl(dataType, 0) + ");\n}";
        const bool expect = dataType.kind != bt::ScalarKind::Bool && dataType == retType;
        t.expectCompileResult(expect, wgsl);
    });

CTS_TEST(g, "stage")
    .desc("Validates it is only usable in correct stage")
    .params([](ParamsBuilder u) {
        return u
            .combine("stage", {Value(std::string("compute")), Value(std::string("fragment")),
                               Value(std::string("vertex"))})
            .beginSubcases()
            .combine("builtin", kBuiltins());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string builtin = t.param<std::string>("builtin");
        const std::string compute = "\n@compute @workgroup_size(1)\nfn main() {\n  foo();\n}";
        const std::string fragment = "\n@fragment\nfn main() {\n  foo();\n}";
        const std::string vertex =
            "\n@vertex\nfn main() -> @builtin(position) vec4f {\n  foo();\n  return vec4f();\n}";
        const std::string entry =
            stage == "compute" ? compute : (stage == "fragment" ? fragment : vertex);
        const std::string wgsl = std::string("\nenable subgroups;\nfn foo() {\n  _ = ") + builtin +
                                 "(0);\n}\n\n" + entry + "\n";
        t.expectCompileResult(stage != "vertex", wgsl);
    });

// kInvalidTypeCases
struct InvalidCase {
    const char* name;
    const char* val;
};
std::vector<InvalidCase> kInvalidTypeCases() {
    return {
        {"array_u32", "array(1u,2u,3u)"}, {"array_f32", "array<f32, 4>()"},
        {"struct_s", "S()"},              {"struct_t", "T(1, 1)"},
        {"ptr_func", "&func_var"},        {"ptr_priv", "&priv_var"},
        {"frexp_ret", "frexp(0)"},
    };
}
std::vector<Value> invalidCaseNames() {
    std::vector<Value> out;
    for (const InvalidCase& c : kInvalidTypeCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
std::string invalidCaseVal(const std::string& name) {
    for (const InvalidCase& c : kInvalidTypeCases()) {
        if (name == c.name) {
            return c.val;
        }
    }
    return "";
}

CTS_TEST(g, "invalid_types")
    .desc("Tests that invalid non-plain types are rejected")
    .params([](ParamsBuilder u) {
        return u.combine("case", invalidCaseNames()).beginSubcases().combine("builtin", kBuiltins());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string val = invalidCaseVal(t.param<std::string>("case"));
        const std::string builtin = t.param<std::string>("builtin");
        const std::string wgsl =
            std::string("\nenable subgroups;\n\nstruct S {\n  x : u32\n}\n\n"
                        "struct T {\n  a : f32,\n  b : u32,\n}\n\n"
                        "var<private> priv_var : f32;\nfn foo() {\n  var func_var : vec4u;\n  _ = ") +
            builtin + "(" + val + ");\n}";
        t.expectCompileResult(false, wgsl);
    });

}  // namespace
