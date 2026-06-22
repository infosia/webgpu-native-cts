// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/insertBits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kValuesTypes = kConcreteIntegerScalarsAndVectors. insertBits(e, newbits, offset,
// count) with offset/count u32 literals. `values` sweeps fullRangeForType(.,5) for
// both e and newbits; `mismatched` checks arg0/arg1 types must match;
// `count_offset` builds custom WGSL across constant/override/runtime arg stages
// (offset+count > 32 => error); `typed_arguments` mirrors clamp's `arguments`
// (kInputArgTypes; the call uses input.arg for BOTH e and newbits). `must_use`
// checks the result is consumed.

#include <map>
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
    "shader,validation,expression,call,builtin,insertBits",
    "Validation tests for the insertBits() builtin.");

// kValuesTypes = kConcreteIntegerScalarsAndVectors.
static const std::vector<bt::Type>& kValuesTypes() {
    return b::kConcreteIntegerScalarsAndVectors();
}

static std::string u32Wgsl(int64_t v) {
    return std::to_string(static_cast<uint32_t>(v)) + "u";
}

struct OffsetCount {
    int offset;
    int count;
};
static const std::vector<OffsetCount>& kValuesOffsetCount() {
    static const std::vector<OffsetCount> v = {
        {0, 0}, {0, 31}, {0, 32}, {4, 0}, {4, 27}, {4, 28}, {16, 0}, {16, 15}, {16, 16}, {32, 0},
    };
    return v;
}
static std::vector<ParamRecord> kValuesOffsetCountParams() {
    std::vector<ParamRecord> out;
    for (const OffsetCount& oc : kValuesOffsetCount()) {
        out.push_back(ParamRecord{{"offset", Value(int64_t(oc.offset))},
                                  {"count", Value(int64_t(oc.count))}});
    }
    return out;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of insertBits() never errors "
          "on valid inputs")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(kValuesTypes()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("value",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .expand("newbits",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .combineWithParams(kValuesOffsetCountParams());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue value = b::rangeValueFromParam(*findParam(t.params(), "value"));
        const b::RangeValue newbits = b::rangeValueFromParam(*findParam(t.params(), "newbits"));
        const int64_t offset = t.param<int64_t>("offset");
        const int64_t count = t.param<int64_t>("count");
        const bt::Type u32 = bt::scalar(bt::ScalarKind::U32);
        b::validateConstOrOverrideBuiltinEval(
            t, "insertBits", /*expectedResult=*/true,
            {b::createBuiltinValue(ty, value), b::createBuiltinValue(ty, newbits),
             b::createBuiltinValue(u32, b::RangeValue::makeI(offset)),
             b::createBuiltinValue(u32, b::RangeValue::makeI(count))},
            stage);
    });

CTS_TEST(g, "mismatched")
    .desc("Validates that even with valid types, if arg0 and arg1 do not match types insertBits() "
          "errors")
    .params([](ParamsBuilder u) {
        return u.combine("arg0", b::typeKeys(kValuesTypes()))
            .combine("arg1", b::typeKeys(kValuesTypes()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string arg0Name = t.param<std::string>("arg0");
        const std::string arg1Name = t.param<std::string>("arg1");
        const bt::Type arg0 = bt::typeByName(arg0Name);
        const bt::Type arg1 = bt::typeByName(arg1Name);
        const bt::Type u32 = bt::scalar(bt::ScalarKind::U32);
        b::validateConstOrOverrideBuiltinEval(
            t, "insertBits", /*expectedResult=*/arg0Name == arg1Name,
            {b::createBuiltinValue(arg0, b::RangeValue::makeI(0)),
             b::createBuiltinValue(arg1, b::RangeValue::makeI(1)),
             b::createBuiltinValue(u32, b::RangeValue::makeI(0)),
             b::createBuiltinValue(u32, b::RangeValue::makeI(32))},
            "constant");
    });

// count_offset table.
static const std::vector<OffsetCount>& kCountOffsetTable() {
    static const std::vector<OffsetCount> v = {
        // offset + count < 32
        {0, 31}, {1, 30}, {31, 0}, {30, 1},
        // offset + count == 32
        {0, 32}, {1, 31}, {16, 16}, {31, 1}, {32, 0},
        // offset + count > 32
        {2, 31}, {31, 2},
        // offset > 32
        {33, 0}, {33, 1},
        // count > 32
        {0, 33}, {1, 33},
    };
    return v;
}
static std::vector<ParamRecord> kCountOffsetParams() {
    std::vector<ParamRecord> out;
    for (const OffsetCount& oc : kCountOffsetTable()) {
        out.push_back(ParamRecord{{"offset", Value(int64_t(oc.offset))},
                                  {"count", Value(int64_t(oc.count))}});
    }
    return out;
}

CTS_TEST(g, "count_offset")
    .desc("Validates that count and offset must be smaller than the size of the primitive.")
    .params([](ParamsBuilder u) {
        const std::vector<Value> stages = {Value(std::string("constant")),
                                           Value(std::string("override")),
                                           Value(std::string("runtime"))};
        return u.combine("offsetStage", stages)
            .combine("countStage", stages)
            .beginSubcases()
            .combineWithParams(kCountOffsetParams())
            .combine("in_shader", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string offsetStage = t.param<std::string>("offsetStage");
        const std::string countStage = t.param<std::string>("countStage");
        const int64_t offset = t.param<int64_t>("offset");
        const int64_t count = t.param<int64_t>("count");
        const bool inShader = t.param<bool>("in_shader");

        std::string offsetArg;
        if (offsetStage == "constant") {
            offsetArg = u32Wgsl(offset);
        } else if (offsetStage == "override") {
            offsetArg = "o_offset";
        } else {
            offsetArg = "v_offset";
        }
        std::string countArg;
        if (countStage == "constant") {
            countArg = u32Wgsl(count);
        } else if (countStage == "override") {
            countArg = "o_count";
        } else {
            countArg = "v_count";
        }
        const std::string wgsl =
            "\noverride o_offset : u32;\noverride o_count : u32;\nfn foo() {\n  var v_offset : "
            "u32;\n  var v_count : u32;\n  var e : u32;\n  var newbits : u32;\n  let tmp = "
            "insertBits(e, newbits, " +
            offsetArg + ", " + countArg + ");\n}";

        const bool error = offset + count > 32;
        const bool shaderError = error && offsetStage == "constant" && countStage == "constant";
        const bool pipelineError =
            inShader && error && offsetStage != "runtime" && countStage != "runtime";
        t.expectCompileResult(!shaderError, wgsl);
        if (!shaderError) {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = !pipelineError;
            args.code = wgsl;
            args.constants = {{"o_offset", static_cast<double>(offset)},
                              {"o_count", static_cast<double>(count)}};
            args.reference = {"o_offset", "o_count"};
            if (inShader) {
                args.statements = {"foo();"};
            }
            t.expectPipelineResult(args);
        }
    });

// ---- kInputArgTypes -------------------------------------------------------
struct InputArg {
    std::string name;
    std::string preamble;
    std::string arg;
    bool pass;
};

static std::vector<InputArg> buildTypedArgs() {
    std::vector<InputArg> out;
    {
        const bt::Type u32 = bt::scalar(bt::ScalarKind::U32);
        out.push_back({u32.toString(), "",
                       b::builtinValueWgsl(b::createBuiltinValue(u32, b::RangeValue::makeI(0))),
                       true});
    }
    for (const bt::Type& ty : b::kFloatScalarsAndVectors()) {
        out.push_back({ty.toString(), "",
                       b::builtinValueWgsl(b::createBuiltinValue(ty, b::RangeValue::makeI(0))),
                       false});
    }
    {
        const bt::Type boolTy = bt::scalar(bt::ScalarKind::Bool);
        out.push_back({boolTy.toString(), "",
                       b::builtinValueWgsl(b::createBuiltinValue(boolTy, b::RangeValue::makeI(0))),
                       false});
    }
    {
        b::MatType mat{2, 2, bt::ScalarKind::F32};
        out.push_back(
            {mat.toString(), "", b::builtinValueWgsl(b::createMatrixValue(mat, 0.0)), false});
    }
    return out;
}

static const std::vector<InputArg>& kInputArgTypes() {
    static const std::vector<InputArg> v = [] {
        std::vector<InputArg> out = buildTypedArgs();
        out.push_back({"alias", "", "u32_alias(1)", true});
        out.push_back({"vec_bool", "", "vec2<bool>(false,true)", false});
        out.push_back({"atomic", "", "a", false});
        out.push_back({"array", "var arry: array<u32, 5>;", "arry", false});
        out.push_back({"array_runtime", "", "k.arry", false});
        out.push_back({"struct", "var x: A;", "x", false});
        out.push_back({"enumerant", "", "read_write", false});
        out.push_back({"ptr",
                       "var<function> f = 1u;\n               let p: ptr<function, u32> = &f;", "p",
                       false});
        out.push_back({"ptr_deref",
                       "var<function> f = 1u;\n               let p: ptr<function, u32> = &f;", "*p",
                       true});
        out.push_back({"sampler", "", "s", false});
        out.push_back({"texture", "", "t", false});
        return out;
    }();
    return v;
}
static std::vector<Value> inputArgNames() {
    std::vector<Value> out;
    for (const InputArg& a : kInputArgTypes()) {
        out.emplace_back(a.name);
    }
    return out;
}
static const InputArg& findInputArg(const std::string& name) {
    for (const InputArg& a : kInputArgTypes()) {
        if (name == a.name) {
            return a;
        }
    }
    static const InputArg dummy{"", "", "", false};
    return dummy;
}

CTS_TEST(g, "typed_arguments")
    .desc("Test compilation validation of insertBits with variously typed arguments")
    .params([](ParamsBuilder u) {
        return u.combine("input", inputArgNames())
            .beginSubcases()
            .combine("offset", inputArgNames())
            .expand("count", [](const ParamRecord& p) {
                const InputArg& input = findInputArg(valueAs<std::string>(*findParam(p, "input")));
                if (input.pass) {
                    return inputArgNames();
                }
                std::vector<Value> out;
                out.emplace_back(valueAs<std::string>(*findParam(p, "offset")));
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const InputArg& input = findInputArg(t.param<std::string>("input"));
        const InputArg& offset = findInputArg(t.param<std::string>("offset"));
        const InputArg& count = findInputArg(t.param<std::string>("count"));
        const bool expectedResult = input.pass && offset.pass && count.pass;

        const bool offsetDistinct = offset.name != input.name;
        const bool countDistinct = count.name != input.name && count.name != offset.name;
        const std::string code =
            std::string(
                "alias u32_alias = u32;\n\n"
                "      @group(0) @binding(0) var s: sampler;\n"
                "      @group(0) @binding(1) var t: texture_2d<f32>;\n\n"
                "      var<workgroup> a: atomic<u32>;\n\n"
                "      struct A {\n        i: u32,\n      }\n"
                "      struct B {\n        arry: array<u32>,\n      }\n"
                "      @group(0) @binding(3) var<storage> k: B;\n\n\n"
                "      @vertex\n      fn main() -> @builtin(position) vec4<f32> {\n        ") +
            (input.preamble.empty() ? "" : input.preamble) + "\n        " +
            (offsetDistinct && !offset.preamble.empty() ? offset.preamble : "") + "\n        " +
            (countDistinct && !count.preamble.empty() ? count.preamble : "") +
            "\n        _ = insertBits(" + input.arg + "," + input.arg + "," + offset.arg + "," +
            count.arg + ");\n        return vec4<f32>(.4, .2, .3, .1);\n      }";
        t.expectCompileResult(expectedResult, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of insertBits must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use,
                              std::string("fn f() { ") + useIt + "insertBits(0u,1u,0u,1u); }");
    });

}  // namespace
