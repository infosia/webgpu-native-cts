// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/requires.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
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
    "shader,validation,parse,requires",
    "Parser validation tests for requires");

struct RequiresCase {
    const char* name;
    const char* code;
    bool pass;
};

// Mirrors upstream kCases (object key order preserved).
static const std::vector<RequiresCase>& kCases() {
    static const std::vector<RequiresCase> v = {
        {"valid", "requires readonly_and_readwrite_storage_textures;", true},
        {"decl_before",
         "alias i = i32;\nrequires readonly_and_readwrite_storage_textures;", false},
        {"decl_after",
         "requires readonly_and_readwrite_storage_textures;\nalias i = i32;", true},
        {"enable_before",
         "enable f16;\nrequires readonly_and_readwrite_storage_textures;", true},
        {"diagnostic_before",
         "diagnostic(info, derivative_uniformity);\nrequires "
         "readonly_and_readwrite_storage_textures;", true},
        {"const_assert_before",
         "const_assert 1 == 1;\nrequires readonly_and_readwrite_storage_textures;", false},
        {"const_assert_after",
         "requires readonly_and_readwrite_storage_textures;\nconst_assert 1 == 1;", true},
        {"embedded_comment",
         "/* comment\n\n*/requires readonly_and_readwrite_storage_textures;", true},
        {"parens", "requires(readonly_and_readwrite_storage_textures);", false},
        {"multi_line", "requires\nreadonly_and_readwrite_storage_textures;", true},
        {"multiple_requires_duplicate",
         "requires readonly_and_readwrite_storage_textures;\nrequires "
         "readonly_and_readwrite_storage_textures;", true},
        {"multiple_requires_different",
         "requires readonly_and_readwrite_storage_textures;\nrequires "
         "packed_4x8_integer_dot_product;", true},
        {"multiple_entries_duplicate",
         "requires readonly_and_readwrite_storage_textures, "
         "readonly_and_readwrite_storage_textures, "
         "readonly_and_readwrite_storage_textures;", true},
        {"multiple_entries_different",
         "requires readonly_and_readwrite_storage_textures, "
         "packed_4x8_integer_dot_product;", true},
        {"unknown", "requires unknown;", false},
    };
    return v;
}

static std::vector<Value> caseNames() {
    std::vector<Value> values;
    for (const RequiresCase& c : kCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const RequiresCase& findCase(const std::string& name) {
    for (const RequiresCase& c : kCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const RequiresCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "requires")
    .desc("Tests that requires are validated correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames());
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");
        t.skipIfLanguageFeatureNotSupported("packed_4x8_integer_dot_product");

        const RequiresCase& c = findCase(t.param<std::string>("case"));
        t.expectCompileResult(c.pass, c.code);
    });

// Mirrors upstream kKnownWGSLLanguageFeatures (order preserved).
static const std::vector<std::string>& kKnownWGSLLanguageFeatures() {
    static const std::vector<std::string> v = {
        "readonly_and_readwrite_storage_textures",
        "packed_4x8_integer_dot_product",
        "unrestricted_pointer_parameters",
        "pointer_composite_access",
        "uniform_buffer_standard_layout",
        "texture_and_sampler_let",
        "subgroup_id",
        "subgroup_uniformity",
        "swizzle_assignment",
        "linear_indexing",
        "texture_formats_tier1",
        "immediate_address_space",
        "fragment_depth",
        "buffer_view",
        "unrestricted_aliasing",
    };
    return v;
}

static std::vector<Value> knownFeatureValues() {
    std::vector<Value> values;
    for (const std::string& f : kKnownWGSLLanguageFeatures()) {
        values.emplace_back(f);
    }
    return values;
}

CTS_TEST(g, "wgsl_matches_api")
    .desc("Tests that language features are accepted iff the API reports support for them.")
    .params([](ParamsBuilder u) {
        return u.combine("feature", knownFeatureValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string feature = t.param<std::string>("feature");
        const std::string code = "requires " + feature + ";";
        t.expectCompileResult(t.hasLanguageFeature(feature), code);
    });

CTS_TEST(g, "have_all_language_features")
    .desc("The spec technically requires support for all WGSL language features listed in the "
          "spec.\n    This test enforces that. It should be the ONLY test that fails when a new "
          "feature is\n    unavailable - others should skip themselves.")
    .params([](ParamsBuilder u) {
        return u.combine("feature", knownFeatureValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string feature = t.param<std::string>("feature");
        // First check that the API exposes the feature.
        if (!t.hasLanguageFeature(feature)) {
            t.fail("the API does not report WGSL language feature: " + feature);
        }
        // If it does, then also check that WGSL allows it.
        t.expectCompileResult(true, "requires " + feature + ";");
    });

}  // namespace
