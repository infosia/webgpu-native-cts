// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/subgroupBitwise.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,subgroupBitwise",
    "Validation tests for subgroupAnd, subgroupOr, and subgroupXor.");

// kOps = ['subgroupAnd', 'subgroupOr', 'subgroupXor']
std::vector<Value> kOps() {
    return {Value(std::string("subgroupAnd")), Value(std::string("subgroupOr")),
            Value(std::string("subgroupXor"))};
}

bool requiresF16(const bt::Type& type) { return type.kind == bt::ScalarKind::F16; }

CTS_TEST(g, "requires_subgroups")
    .desc("Validates that the subgroups feature is required")
    .params([](ParamsBuilder u) {
        return u.combine("enable", {Value(false), Value(true)}).combine("op", kOps());
    })
    .fn([](ShaderValidationTest& t) {
        const bool enable = t.param<bool>("enable");
        const std::string op = t.param<std::string>("op");
        const std::string wgsl = std::string("\n") + (enable ? "enable subgroups;" : "") +
                                 "\nfn foo() {\n  _ = " + op + "(0);\n}";
        t.expectCompileResult(enable, wgsl);
    });

std::string stageCode(const std::string& stage, const std::string& op) {
    if (stage == "constant") {
        return "\nenable subgroups;\n@compute @workgroup_size(16)\nfn main() {\n  const x = " + op +
               "(0);\n}";
    }
    if (stage == "override") {
        return "\nenable subgroups\noverride o = " + op + "(0);";
    }
    return "\nenable subgroups;\n@compute @workgroup_size(16)\nfn main() {\n  let x = " + op +
           "(0);\n}";
}

CTS_TEST(g, "early_eval")
    .desc("Ensures the builtin is not able to be compile time evaluated")
    .params([](ParamsBuilder u) {
        return u
            .combine("stage", {Value(std::string("constant")), Value(std::string("override")),
                               Value(std::string("runtime"))})
            .combine("op", kOps());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string op = t.param<std::string>("op");
        t.expectCompileResult(stage == "runtime", stageCode(stage, op));
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the builtin has the @must_use attribute")
    .params([](ParamsBuilder u) {
        return u.combine("must_use", {Value(true), Value(false)}).combine("op", kOps());
    })
    .fn([](ShaderValidationTest& t) {
        const bool mustUse = t.param<bool>("must_use");
        const std::string op = t.param<std::string>("op");
        const std::string wgsl = std::string("\nenable subgroups;\n@compute @workgroup_size(16)\n"
                                             "fn main() {\n  ") +
                                 (mustUse ? "_ = " : "") + op + "(0);\n}";
        t.expectCompileResult(mustUse, wgsl);
    });

CTS_TEST(g, "data_type")
    .desc("Validates data parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors())).combine("op", kOps());
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const std::string op = t.param<std::string>("op");
        std::string enables = "enable subgroups;\n";
        if (requiresF16(type)) {
            enables += "enable f16;";
        }
        const std::string wgsl = std::string("\n") + enables +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  _ = " + op + "(" +
                                 bt::createWgsl(type, 0) + ");\n}";
        const bt::Type eleType = bt::scalarTypeOf(type);
        const bool expect = bt::isConvertible(eleType, bt::scalar(bt::ScalarKind::U32)) ||
                            bt::isConvertible(eleType, bt::scalar(bt::ScalarKind::I32));
        t.expectCompileResult(expect, wgsl);
    });

CTS_TEST(g, "return_type")
    .desc("Validates return type")
    .params([](ParamsBuilder u) {
        return u.combine("retType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                const bt::Type type =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "retType")));
                const bt::ScalarKind k = type.kind;
                return k != bt::ScalarKind::AbstractInt && k != bt::ScalarKind::AbstractFloat;
            })
            .combine("op", kOps())
            .combine("paramType", bt::typeNames(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type retType = bt::typeByName(t.param<std::string>("retType"));
        const bt::Type paramType = bt::typeByName(t.param<std::string>("paramType"));
        const std::string op = t.param<std::string>("op");
        std::string enables = "enable subgroups;\n";
        if (requiresF16(retType) || requiresF16(paramType)) {
            enables += "enable f16;";
        }
        const std::string wgsl = std::string("\n") + enables +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  let res : " +
                                 retType.toString() + " = " + op + "(" +
                                 bt::createWgsl(paramType, 0) + ");\n}";
        const bt::ScalarKind eleParamType = bt::scalarTypeOf(paramType).kind;
        const bt::ScalarKind eleRetType = bt::scalarTypeOf(retType).kind;
        bool expect = paramType == retType &&
                      (eleRetType == bt::ScalarKind::I32 || eleRetType == bt::ScalarKind::U32);
        if (eleParamType == bt::ScalarKind::AbstractInt) {
            expect = eleRetType == bt::ScalarKind::I32 && bt::isConvertible(paramType, retType);
        }
        t.expectCompileResult(expect, wgsl);
    });

CTS_TEST(g, "stage")
    .desc("validates builtin is only usable in the correct stages")
    .params([](ParamsBuilder u) {
        return u
            .combine("stage", {Value(std::string("compute")), Value(std::string("fragment")),
                               Value(std::string("vertex"))})
            .combine("op", kOps());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string op = t.param<std::string>("op");
        const std::string compute = "\n@compute @workgroup_size(1)\nfn main() {\n  foo();\n}";
        const std::string fragment = "\n@fragment\nfn main() {\n  foo();\n}";
        const std::string vertex =
            "\n@vertex\nfn main() -> @builtin(position) vec4f {\n  foo();\n  return vec4f();\n}";
        const std::string entry =
            stage == "compute" ? compute : (stage == "fragment" ? fragment : vertex);
        const std::string wgsl = std::string("\nenable subgroups;\nfn foo() {\n  _ = ") + op +
                                 "(0);\n}\n\n" + entry + "\n";
        t.expectCompileResult(stage != "vertex", wgsl);
    });

}  // namespace
