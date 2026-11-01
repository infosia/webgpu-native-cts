// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/immediate.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdlib>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/decl/util.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
using cts::shader_validation::skipIfImmediateDataNotSupported;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,decl,immediate",
    "Validation tests for the WGSL immediate address space.");

const std::string kImmediateFeature = "immediate_address_space";
const std::string kImmediateHeader = "requires " + kImmediateFeature + ";";

struct StoreType {
    const char* name;
    const char* enable;
    const char* prelude;
    const char* type;
};

const std::vector<StoreType> kValidStoreTypes = {
    {"u32", "", "", "u32"},
    {"i32", "", "", "i32"},
    {"f32", "", "", "f32"},
    {"f16", "enable f16;", "", "f16"},
    {"vec2u", "", "", "vec2u"},
    {"vec3i", "", "", "vec3i"},
    {"vec4f", "", "", "vec4f"},
    {"vec3h", "enable f16;", "", "vec3h"},
    {"mat2x2f", "", "", "mat2x2f"},
    {"struct_numeric", "", "struct S { a : u32, b : vec4f }", "S"},
};

const std::vector<StoreType> kInvalidStoreTypes = {
    {"bool", "", "", "bool"},
    {"vec2_bool", "", "", "vec2<bool>"},
    {"atomic_u32", "", "", "atomic<u32>"},
    {"ptr_function_u32", "", "", "ptr<function, u32>"},
    {"sampler", "", "", "sampler"},
    {"sampler_comparison", "", "", "sampler_comparison"},
    {"texture_2d", "", "", "texture_2d<f32>"},
    {"runtime_array", "", "", "array<u32>"},
    {"fixed_array", "", "", "array<u32, 4>"},
    {"struct_runtime_array", "", "struct S { data : array<u32> }", "S"},
    {"struct_fixed_array", "", "struct S { data : array<vec4u, 4> }", "S"},
};

struct EntryPointCase {
    const char* name;
    bool valid;
    const char* body;
};

const std::vector<EntryPointCase> kEntryPointCases = {
    {
        "one_used",
        true,
        R"wgsl(
var<immediate> a : u32;
@compute @workgroup_size(1)
fn main() {
  _ = a;
})wgsl",
    },
    {
        "two_declared_one_used",
        true,
        R"wgsl(
var<immediate> a : u32;
var<immediate> b : u32;
@compute @workgroup_size(1)
fn main() {
  _ = a;
})wgsl",
    },
    {
        "two_entry_points_one_each",
        true,
        R"wgsl(
var<immediate> a : u32;
var<immediate> b : u32;
@compute @workgroup_size(1)
fn main_a() {
  _ = a;
}
@compute @workgroup_size(1)
fn main_b() {
  _ = b;
})wgsl",
    },
    {
        "one_entry_point_uses_two_directly",
        false,
        R"wgsl(
var<immediate> a : u32;
var<immediate> b : u32;
@compute @workgroup_size(1)
fn main() {
  _ = a + b;
})wgsl",
    },
    {
        "one_entry_point_uses_two_through_helper",
        false,
        R"wgsl(
var<immediate> a : u32;
var<immediate> b : u32;
fn read_b() -> u32 {
  return b;
}
@compute @workgroup_size(1)
fn main() {
  _ = a + read_b();
})wgsl",
    },
};

struct PointerCase {
    const char* name;
    bool valid;
    bool needsUnrestrictedPointerParameters;
    const char* body;
};

const std::vector<PointerCase> kPointerCases = {
    {
        "alias_module_scope",
        true,
        false,
        R"wgsl(
alias P = ptr<immediate, u32>;
var<immediate> data : u32;
@compute @workgroup_size(1)
fn main() {
  let p : P = &data;
  _ = *p;
})wgsl",
    },
    {
        "let_inside_function",
        true,
        false,
        R"wgsl(
var<immediate> data : u32;
@compute @workgroup_size(1)
fn main() {
  let p : ptr<immediate, u32> = &data;
  _ = *p;
})wgsl",
    },
    {
        "write_through_pointer",
        false,
        false,
        R"wgsl(
var<immediate> data : u32;
@compute @workgroup_size(1)
fn main() {
  let p : ptr<immediate, u32> = &data;
  *p = 1u;
})wgsl",
    },
    {
        "pointer_parameter",
        true,
        true,
        R"wgsl(
var<immediate> data : u32;
fn read_data(p : ptr<immediate, u32>) -> u32 {
  return *p;
}
@compute @workgroup_size(1)
fn main() {
  _ = read_data(&data);
})wgsl",
    },
    {
        "explicit_read_access",
        false,
        false,
        R"wgsl(
alias P = ptr<immediate, u32, read>;)wgsl",
    },
    {
        "explicit_write_access",
        false,
        false,
        R"wgsl(
alias P = ptr<immediate, u32, write>;)wgsl",
    },
    {
        "explicit_read_write_access",
        false,
        false,
        R"wgsl(
alias P = ptr<immediate, u32, read_write>;)wgsl",
    },
    {
        "missing_store_type",
        false,
        false,
        R"wgsl(
alias P = ptr<immediate>;)wgsl",
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

std::string storeTypeShader(const StoreType& testcase) {
    return "\n" + kImmediateHeader + "\n" + testcase.enable + "\n" + testcase.prelude +
           "\nvar<immediate> data : " + testcase.type +
           ";\n@compute @workgroup_size(1)\nfn main() {\n  _ = data;\n}";
}

CTS_TEST(g, "store_type,valid")
    .desc("Validates immediate store types supported by the current WGSL immediate implementation.")
    .params([](ParamsBuilder u) { return u.combine("type", caseNames(kValidStoreTypes)); })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const StoreType& testcase = findCase(kValidStoreTypes, t.param<std::string>("type"));
        if (std::string(testcase.enable).find("f16") != std::string::npos) {
            t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_ShaderF16, "shader-f16");
        }
        t.expectCompileResult(true, storeTypeShader(testcase));
    });

CTS_TEST(g, "store_type,invalid")
    .desc("Validates types that cannot be used for immediate variables.")
    .params([](ParamsBuilder u) { return u.combine("type", caseNames(kInvalidStoreTypes)); })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const StoreType& testcase = findCase(kInvalidStoreTypes, t.param<std::string>("type"));
        t.expectCompileResult(false, storeTypeShader(testcase));
    });

CTS_TEST(g, "scope")
    .desc("Validates that immediate variables cannot be declared at function scope.")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", {Value("function"), Value("immediate")});
    })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const std::string addressSpace = t.param<std::string>("addressSpace");
        const std::string wgsl = "\n" + kImmediateHeader +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  var<" + addressSpace +
                                 "> data : u32;\n  _ = data;\n}";
        t.expectCompileResult(addressSpace == "function", wgsl);
    });

CTS_TEST(g, "binding_attributes")
    .desc("Validates that @group and @binding are not allowed on immediate variables.")
    .params([](ParamsBuilder u) {
        return u.combine("group", {Value(""), Value("@group(0)")})
            .combine("binding", {Value(""), Value("@binding(0)")});
    })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const std::string group = t.param<std::string>("group");
        const std::string binding = t.param<std::string>("binding");
        const std::string wgsl = "\n" + kImmediateHeader + "\n" + group + " " + binding +
                                 " var<immediate> data : u32;\n@compute @workgroup_size(1)\n"
                                 "fn main() {\n  _ = data;\n}";
        t.expectCompileResult(group.empty() && binding.empty(), wgsl);
    });

CTS_TEST(g, "access_mode")
    .desc("Validates that immediate variables cannot spell an access mode.")
    .params([](ParamsBuilder u) {
        return u.combine("accessMode", {Value(""), Value("read"), Value("write"), Value("read_write")});
    })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const std::string accessMode = t.param<std::string>("accessMode");
        const std::string suffix = accessMode.empty() ? "" : ", " + accessMode;
        const std::string wgsl = "\n" + kImmediateHeader + "\nvar<immediate" + suffix +
                                 "> data : u32;\n@compute @workgroup_size(1)\n"
                                 "fn main() {\n  _ = data;\n}";
        t.expectCompileResult(accessMode.empty(), wgsl);
    });

CTS_TEST(g, "entry_point_interface")
    .desc("Validates one statically used immediate variable per entry point.")
    .params([](ParamsBuilder u) { return u.combine("case", caseNames(kEntryPointCases)); })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const EntryPointCase& testcase = findCase(kEntryPointCases, t.param<std::string>("case"));
        t.expectCompileResult(testcase.valid, kImmediateHeader + "\n" + testcase.body);
    });

CTS_TEST(g, "pointers")
    .desc("Validates ptr<immediate> type creation, use, access modes, and function parameters.")
    .params([](ParamsBuilder u) { return u.combine("case", caseNames(kPointerCases)); })
    .fn([](ShaderValidationTest& t) {
        skipIfImmediateDataNotSupported(t);
        const PointerCase& testcase = findCase(kPointerCases, t.param<std::string>("case"));
        const bool hasUnrestricted = t.hasLanguageFeature("unrestricted_pointer_parameters");
        const std::string unrestrictedHeader = testcase.needsUnrestrictedPointerParameters && hasUnrestricted
                                                   ? "requires unrestricted_pointer_parameters;\n"
                                                   : "";
        const bool expected =
            testcase.valid && (!testcase.needsUnrestrictedPointerParameters || hasUnrestricted);
        const std::string wgsl = kImmediateHeader + "\n" + unrestrictedHeader + testcase.body;
        t.expectCompileResult(expected, wgsl);
    });

}  // namespace
