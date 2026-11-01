// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/buffer.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdlib>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,buffer",
    "Validation tests for buffer types");

struct ParseCase {
    const char* name;
    const char* code;
    bool valid;
};

const std::vector<ParseCase> kParseCases = {
    {"unsized", "alias T = buffer;", true},
    {"literal", "alias T = buffer<16>;", true},
    {"literal_negative", "alias T = buffer<-1>;", false},
    {"literal_zero", "alias T = buffer<0>;", false},
    {"const", "const x = 16; alias T = buffer<x>;", true},
    {"const_negative", "const x = -1; alias T = buffer<x>;", false},
    {"const_zero", "const x = 0; alias T = buffer<x>;", false},
    {"const_function", "const x = 16; const y = 32; alias T = buffer<min(x, y)>;", true},
    {"override", "override x = 16; alias T = buffer<x>;", true},
    {"override_negative", "override x = -1; alias T = buffer<x>;", true},
    {"override_zero", "override x = 0; alias T = buffer<x>;", true},
    {"override_function", "override x = 16; override y = 32; alias T = buffer<max(x, y)>;", true},
    {"empty_template", "alias T = buffer<>;", false},
    {"missing_rparen", "alias T = buffer<16;", false},
    {"subtype", "alias T = buffer<u32>;", false},
    {"pointer_workgroup_unsized_buffer", "alias T = buffer; alias PT = ptr<workgroup, T>;", true},
    {"pointer_uniform_unsized_buffer", "alias T = buffer; alias PT = ptr<uniform, T>;", true},
    {"pointer_storage_unsized_buffer", "alias T = buffer; alias PT = ptr<storage, T>;", true},
    {"pointer_workgroup_sized_buffer", "alias T = buffer<32>; alias PT = ptr<workgroup, T>;", true},
    {"pointer_uniform_sized_buffer", "alias T = buffer<32>; alias PT = ptr<uniform, T>;", true},
    {"pointer_storage_sized_buffer", "alias T = buffer<32>; alias PT = ptr<storage, T>;", true},
    {"pointer_workgroup_override_sized_buffer", "override o : i32; alias T = buffer<o>; alias PT = ptr<workgroup, T>;", true},
    {"pointer_uniform_override_sized_buffer", "override o : i32; alias T = buffer<o>; alias PT = ptr<uniform, T>;", false},
    {"pointer_storage_override_sized_buffer", "override o : i32; alias T = buffer<o>; alias PT = ptr<storage, T>;", false},
    {"buffer_3bytes", "alias T = buffer<3>;", false},
    {"buffer_2bytes_f16", R"wgsl(enable f16;
alias T = buffer<2>;)wgsl", true},
    {"buffer_2bytes_no_f16", "alias T = buffer<2>;", false},
};

std::vector<Value> caseNames() {
    std::vector<Value> values;
    values.reserve(kParseCases.size());
    for (const ParseCase& testcase : kParseCases) {
        values.emplace_back(std::string(testcase.name));
    }
    return values;
}

const ParseCase& findCase(const std::string& name) {
    for (const ParseCase& testcase : kParseCases) {
        if (name == testcase.name) {
            return testcase;
        }
    }
    std::abort();
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

CTS_TEST(g, "parse")
    .desc("Test buffer type parsing")
    .params([](ParamsBuilder u) { return u.combine("case", caseNames()); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const ParseCase& testcase = findCase(t.param<std::string>("case"));
        t.expectCompileResult(testcase.valid, testcase.code);
    });

CTS_TEST(g, "address_space")
    .desc("Test buffer type validity for each address space")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames())
            .filter([](const ParamRecord& p) {
                const std::string name = valueAs<std::string>(*findParam(p, "case"));
                return findCase(name).valid && !contains(name, "pointer");
            })
            .beginSubcases()
            .combine("aspace", {Value("function"), Value("private"), Value("storage"), Value("uniform"),
                                Value("workgroup"), Value("immediate")});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("buffer_view");
        const std::string caseName = t.param<std::string>("case");
        const std::string aspace = t.param<std::string>("aspace");
        const ParseCase& testcase = findCase(caseName);

        std::string mvar;
        std::string fvar;
        if (aspace == "function") {
            fvar = "var v : T;";
        } else if (aspace == "private" || aspace == "workgroup" || aspace == "immediate") {
            mvar = "var<" + aspace + "> v : T;";
        } else {  // 'storage' and 'uniform'
            mvar = "@group(0) @binding(0) var<" + aspace + "> v : T;";
        }

        const std::string wgsl = std::string("\n") + testcase.code + "\n" + mvar +
                                 "\n@compute @workgroup_size(1)\nfn main() {\n  " + fvar + "\n}";

        bool expected = aspace != "function" && aspace != "private" && aspace != "immediate";
        if (caseName == "unsized") {
            expected = aspace == "storage";
        } else if (contains(caseName, "override")) {
            expected = aspace == "workgroup";
        }
        t.expectCompileResult(expected, wgsl);
    });

}  // namespace
