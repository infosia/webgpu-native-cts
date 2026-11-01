// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/bufferArrayView.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdlib>
#include <map>
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
    "shader,validation,expression,call,builtin,bufferArrayView",
    "Validation tests for bufferArrayView");

bool isConcreteNumeric(const std::string& typeName) {
    const bt::ScalarKind eleType = bt::typeByName(typeName).kind;
    return eleType != bt::ScalarKind::AbstractInt && eleType != bt::ScalarKind::AbstractFloat &&
           eleType != bt::ScalarKind::Bool;
}

bool requiresF16(const bt::Type& type) { return type.kind == bt::ScalarKind::F16; }

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
  )wgsl") + (mustUse ? "let p = " : "") + "bufferArrayView<array<u32>>(&v, 0, 64);\n}";

        t.expectCompileResult(mustUse, wgsl);
    });

CTS_TEST(g, "return_store_type")
    .desc("Validates return type")
    .params([](ParamsBuilder u) {
        return u.combine("template_type", bt::typeNames(bt::kAllScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                return isConcreteNumeric(valueAs<std::string>(*findParam(p, "template_type")));
            })
            .combine("return_type", bt::typeNames(bt::kAllScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                return isConcreteNumeric(valueAs<std::string>(*findParam(p, "return_type")));
            })
            .beginSubcases()
            .combine("ptr", {false, true})
            .combine("array", {false, true});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const bt::Type templateType = bt::typeByName(t.param<std::string>("template_type"));
        const bt::Type returnType = bt::typeByName(t.param<std::string>("return_type"));
        const bool ptr = t.param<bool>("ptr");
        const bool array = t.param<bool>("array");
        std::string retType = returnType.toString();
        std::string tempType = templateType.toString();
        if (array) {
            retType = "array<" + returnType.toString() + ">";
            tempType = "array<" + tempType + ">";
        }
        if (ptr) {
            retType = "ptr<storage, " + retType + ">";
        }
        std::string enables;
        if (requiresF16(templateType) || requiresF16(returnType)) {
            enables = "enable f16;";
        }
        const std::string wgsl = "\n" + enables + R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let res : )wgsl" + retType + " = bufferArrayView<" + tempType + ">(&v, 0, 128);\n}";

        t.expectCompileResult(ptr && array && templateType == returnType, wgsl);
    });

CTS_TEST(g, "buffer_type")
    .desc("Validates the buffer parameter type")
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
        const std::string type = t.param<std::string>("type");
        const bool ptr = t.param<bool>("ptr");
        const std::string aspace = type.substr(type.rfind('_') + 1);
        std::string access;
        if (type == "unsized_ro_storage" || type == "sized_ro_storage") {
            access = ", read";
        } else if (type == "unsized_storage" || type == "sized_storage") {
            access = ", read_write";
        }
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
  let p : ptr<)wgsl") + aspace + ", array<u32>" + access + "> = bufferArrayView<array<u32>>(" +
                                 (ptr ? "&" : "") + type + ", 0, 128);\n}";

        t.expectCompileResult(ptr, wgsl);
    });

bool isIntegerParamType(const bt::Type& type) {
    return type == bt::scalar(bt::ScalarKind::AbstractInt) || type == bt::scalar(bt::ScalarKind::U32) ||
           type == bt::scalar(bt::ScalarKind::I32);
}

CTS_TEST(g, "offset_type")
    .desc("Validates the offset parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const std::string enables = requiresF16(type) ? "enable f16;" : "";
        const std::string wgsl = "\n" + enables + R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<u32>>(&v, )wgsl" + bt::createWgsl(type, 0) + ", 128);\n}";

        t.expectCompileResult(isIntegerParamType(type), wgsl);
    });

CTS_TEST(g, "size_type")
    .desc("Validates the offset parameter type")
    .params([](ParamsBuilder u) {
        return u.combine("type", bt::typeNames(bt::kAllScalarsAndVectors()));
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const bt::Type type = bt::typeByName(t.param<std::string>("type"));
        const std::string enables = requiresF16(type) ? "enable f16;" : "";
        const std::string wgsl = "\n" + enables + R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<u32>>(&v, 0, )wgsl" + bt::createWgsl(type, 128) + ");\n}";

        t.expectCompileResult(isIntegerParamType(type), wgsl);
    });

enum class Validity {
    kValid,
    kInvalid,
    kPipeline,  // Compiles, but pipeline creation fails.
};

struct EarlyEvalCase {
    const char* name;
    const char* code;
    Validity valid;
    std::map<std::string, double> constants;
    bool ptrParam;
};

const std::vector<EarlyEvalCase> kEarlyEvalCases = {
    {
        "ro_storage_buffer",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer<16>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, 16);
})wgsl",
        Validity::kValid,
        {},
        false,
    },
    {
        "storage_buffer",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer<16>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, 16);
})wgsl",
        Validity::kValid,
        {},
        false,
    },
    {
        "uniform_buffer",
        R"wgsl(
@group(0) @binding(0) var<uniform> v : buffer<16>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, 16);
})wgsl",
        Validity::kValid,
        {},
        false,
    },
    {
        "workgroup_buffer",
        R"wgsl(
var<workgroup> v : buffer<16>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, 16);
})wgsl",
        Validity::kValid,
        {},
        false,
    },
    {
        "const_size_too_small_for_type",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, 12);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "buffer_too_small_for_const_size",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer<12>;
fn foo() {
  let p = bufferArrayView<array<u32>>(&v, 0, 16);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "buffer_too_small_for_type",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer<12>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, 16);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "const_size_too_small_for_const_offset_and_type",
        R"wgsl(
var<workgroup> v : buffer<128>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 16, 30);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "buffer_size_too_small_for_const_offset_and_type",
        R"wgsl(
var<workgroup> v : buffer<30>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 16, 128);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "override_size_too_small_for_type",
        R"wgsl(
override size : u32;
@group(0) @binding(0) var<storage> v : buffer;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, size);
})wgsl",
        Validity::kPipeline,
        {{"size", 12.0}},
        false,
    },
    {
        "buffer_too_small_for_override_size",
        R"wgsl(
override buffer_size : u32;
override size : u32;
var<workgroup> v : buffer<buffer_size>;
fn foo() {
  let p = bufferArrayView<array<u32>>(&v, 0, size);
})wgsl",
        Validity::kPipeline,
        {{"buffer_size", 12.0}, {"size", 16.0}},
        false,
    },
    {
        "override_buffer_too_small_for_type",
        R"wgsl(
override buffer_size : u32;
override size : u32;
var<workgroup> v : buffer<buffer_size>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, 0, size);
})wgsl",
        Validity::kPipeline,
        {{"buffer_size", 12.0}, {"size", 16.0}},
        false,
    },
    {
        "override_size_too_small_for_override_offset_and_type",
        R"wgsl(
override offset : u32;
override size : u32;
@group(0) @binding(0) var<uniform> v : buffer<128>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, offset, size);
})wgsl",
        Validity::kPipeline,
        {{"offset", 16.0}, {"size", 30.0}},
        false,
    },
    {
        "buffer_size_too_small_for_override_offset_and_type",
        R"wgsl(
override buffer_size : u32;
override offset : u32;
var<workgroup> v : buffer<buffer_size>;
fn foo() {
  let p = bufferArrayView<array<vec4u>>(&v, offset, 128);
})wgsl",
        Validity::kPipeline,
        {{"buffer_size", 30.0}, {"offset", 16.0}},
        false,
    },
    {
        "const_offset_not_aligned",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
fn foo() {
  let p = bufferarrayview<array<vec4u>>(&v, 12, 128);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "override_offset_not_aligned",
        R"wgsl(
override offset : u32;
@group(0) @binding(0) var<storage, read_write> v : buffer;
fn foo() {
  let p = bufferarrayview<array<vec4u>>(&v, offset, 128);
})wgsl",
        Validity::kInvalid,
        {{"offset", 12.0}},
        false,
    },
    {
        "const_offset_plus_type_size_out_of_range",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
struct S {
  a: vec4u,
  b: vec4f,
}
fn foo() {
  let p = buffer<array<S>>(&v, 4294967279, 128);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "override_offset_plus_type_size_out_of_range",
        R"wgsl(
override offset : u32;
@group(0) @binding(0) var<storage, read_write> v : buffer;
struct S {
  a: vec4u,
  b: vec4f,
}
fn foo() {
  let p = buffer<array<S>>(&v, offset, 128);
})wgsl",
        Validity::kInvalid,
        {{"offset", 4294967279.0}},
        false,
    },
    {
        "const_multiple_of_stride",
        R"wgsl(
struct S {
  a : vec4u,
  b : array<vec2u>,
}
var<workgroup> v : buffer<256>;
fn foo() {
  let p = bufferArrayView<S>(&v, 0, 24);
})wgsl",
        Validity::kValid,
        {},
        false,
    },
    {
        "const_not_multiple_of_stride",
        R"wgsl(
struct S {
  a : vec4u,
  b : array<vec2u>,
}
var<workgroup> v : buffer<256>;
fn foo() {
  let p = bufferArrayView<S>(&v, 0, 26);
})wgsl",
        Validity::kInvalid,
        {},
        false,
    },
    {
        "override_not_multiple_of_stride",
        R"wgsl(
override size : u32;
struct S {
  a : vec4u,
  b : array<vec2u>,
}
var<workgroup> v : buffer<256>;
fn foo() {
  let p = bufferArrayView<S>(&v, 0, size);
})wgsl",
        Validity::kPipeline,
        {{"size", 26.0}},
        false,
    },
    {
        "const_buffer_too_small_through_unsized_function",
        R"wgsl(
@group(0) @binding(0) var<storage> buffer<16>;
fn bar(p : ptr<storage, buffer>) {
  let q = bufferArrayView<array<u32>>(p, 0, 32);
}
fn foo() {
  bar(&v);
})wgsl",
        Validity::kInvalid,
        {},
        true,
    },
    {
        "override_buffer_too_small_through_unsized_function",
        R"wgsl(
override size : u32;
@group(0) @binding(0) var<storage> v : buffer<16>;
fn bar(p : ptr<storage, buffer>) {
  let q = bufferArrayView<array<u32>>(p, 0, size);
}
fn foo() {
  bar(&v);
})wgsl",
        Validity::kPipeline,
        {{"size", 32.0}},
        true,
    },
    {
        "sized_buffer_to_smaller_buffer_too_small",
        R"wgsl(
var<workgroup> v : buffer<256>;
fn bar(p : ptr<workgroup, buffer<16>>) {
  let q = bufferArrayView<array<u32>>(q, 16, 4);
}
fn foo() {
  bar(&v);
})wgsl",
        Validity::kInvalid,
        {},
        true,
    },
};

struct LHSCallCase {
    const char* name;
    const char* code;
    std::vector<const char*> features;
};

const std::vector<LHSCallCase> kLHSCallCases = {
    {
        "u32_array",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  (*bufferArrayView<array<u32>>(&v, 0, 128))[0] = 123u;
})wgsl",
        {},
    },
    {
        "f32_vector_letter",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  (*bufferArrayView<array<vec4f>>(&v, 0, 128))[0].x = 13.3f;
})wgsl",
        {},
    },
    {
        "f32_struct_access",
        R"wgsl(
struct S {
  member_a: f32,
}
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  (*bufferArrayView<array<S>>(&v, 0, 128))[0].member_a = 13.3f;
})wgsl",
        {},
    },
    {
        "u32_array_ptr_composite",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  bufferArrayView<array<u32>>(&v, 0, 128)[0] = 123u;
})wgsl",
        {"pointer_composite_access"},
    },
    {
        "vec4u_swizzle_assign",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  (*bufferArrayView<array<vec4u>>(&v, 0, 128))[0].zxy = vec3u(42);
})wgsl",
        {"swizzle_assignment"},
    },
    {
        "vec4u_ptr_composite_swizzle_assign",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  bufferArrayView<array<vec4u>>(&v, 0, 128)[0].zxy = vec3u(42);
})wgsl",
        {"pointer_composite_access", "swizzle_assignment"},
    },
    {
        "compound_assign",
        R"wgsl(
var<workgroup> v : buffer<256>;
@compute @workgroup_size(1)
fn main() {
  (*bufferArrayView<array<u32>>(&v, 0, 128))[0] += 123u;
})wgsl",
        {},
    },
};

template <class T>
std::vector<Value> caseNames(const std::vector<T>& cases) {
    std::vector<Value> values;
    values.reserve(cases.size());
    for (const T& testcase : cases) {
        values.emplace_back(std::string(testcase.name));
    }
    return values;
}

template <class T>
const T& findCase(const std::vector<T>& cases, const std::string& name) {
    for (const T& testcase : cases) {
        if (name == testcase.name) {
            return testcase;
        }
    }
    std::abort();
}

CTS_TEST(g, "early_eval_errors")
    .desc("Test shader-creation and pipeline-creation errors")
    .params([](ParamsBuilder u) { return u.combine("case", caseNames(kEarlyEvalCases)); })
    .fn([](ShaderValidationTest& t) {
        const EarlyEvalCase& testcase = findCase(kEarlyEvalCases, t.param<std::string>("case"));
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        if (testcase.ptrParam) {
            t.skipIfLanguageFeatureNotSupported("unrestricted_pointer_parameters");
        }

        t.expectCompileResult(testcase.valid != Validity::kInvalid, testcase.code);
        if (testcase.valid != Validity::kInvalid) {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = testcase.valid == Validity::kValid;
            args.code = testcase.code;
            args.constants = testcase.constants;
            args.statements = {"foo();"};
            t.expectPipelineResult(args);
        }
    });

CTS_TEST(g, "lhs_call")
    .desc("Validate that bufferView can be on the LHS of an assignment")
    .params([](ParamsBuilder u) { return u.combine("case", caseNames(kLHSCallCases)); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");

        const LHSCallCase& testcase = findCase(kLHSCallCases, t.param<std::string>("case"));
        bool valid = true;
        for (const char* feature : testcase.features) {
            valid = valid && t.hasLanguageFeature(feature);
        }
        t.expectCompileResult(valid, testcase.code);
    });

}  // namespace
