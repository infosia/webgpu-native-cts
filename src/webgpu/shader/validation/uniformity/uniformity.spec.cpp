// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/uniformity/uniformity.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for uniformity analysis. This file also ports the helper
// `uniformity/snippet.ts` (the loop-spec DSL and the compileShouldSucceed /
// Verdict logic) inline as free functions in the anonymous namespace.
//
// Porting notes:
//   - skipIfLanguageFeatureNotSupported(name) is replicated as a local helper
//     that skips the case when t.hasLanguageFeature(name) is false (a skip is
//     not a pass). 'readonly_and_readwrite_storage_textures',
//     'pointer_composite_access', 'subgroup_id' and 'subgroup_uniformity' are
//     all routed through the enabler's hasLanguageFeature.
//   - isCompatibility is false in this harness (Dawn runs non-compat), so the
//     upstream skipIf(isCompatibility && ...) in fragment_builtin_values never
//     fires; it is kept as a comment.
//   - `enable subgroups;` / `enable chromium_experimental_primitive_id;` are
//     auto-skipped by the enabler regex when the device lacks the feature.

#include <map>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,uniformity,uniformity",
    "Validation tests for uniformity analysis");

// ---------------------------------------------------------------------------
// snippet.ts port: Verdict + compileShouldSucceed
// ---------------------------------------------------------------------------
// 'sensitive': fail iff (condition non-uniform AND op requires uniformity).
// 'forbid':    fail iff op requires uniformity.
// 'permit':    always passes.
enum class Verdict { Sensitive, Forbid, Permit };

bool compileShouldSucceed(bool requiresUniformity, bool conditionIsUniform, Verdict verdict) {
    switch (verdict) {
        case Verdict::Sensitive:
            return !requiresUniformity || conditionIsUniform;
        case Verdict::Forbid:
            return !requiresUniformity;
        case Verdict::Permit:
            return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// snippet.ts port: specToCode (loop-spec DSL) and LoopCase
// ---------------------------------------------------------------------------
struct Snippet {
    std::string name;
    std::string code;
    Verdict verdict = Verdict::Permit;
};

// Replace all occurrences of `from` with `to` in `s`.
std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return s;
    }
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// Replace only the first occurrence of `from` with `to` in `s`.
std::string replaceFirst(std::string s, const std::string& from, const std::string& to) {
    const size_t pos = s.find(from);
    if (pos != std::string::npos) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Expand a loop case spec to its shader code (mirrors upstream specToCode).
std::string specToCode(const std::string& spec) {
    // Match leading loop-kind token.
    static const char* kKinds[] = {"loop",       "for-unif",     "for-nonunif",
                                    "for",        "while-unif",   "while-nonunif"};
    std::string kind;
    std::string rest;
    for (const char* k : kKinds) {
        const std::string token = std::string(k) + "-";
        if (startsWith(spec, token)) {
            kind = k;
            rest = spec.substr(token.size());
            break;
        }
    }

    std::string prefix = "  ";
    std::string out;
    // end_parts is a stack of closing braces accumulated as we open blocks.
    std::vector<std::string> endParts = {prefix, "}\n"};

    out += prefix;
    if (kind == "loop") {
        out += "loop {";
    } else if (kind == "for") {
        out += "for (;;) {";
    } else if (kind == "for-unif") {
        out += "for (;<uniform_cond>;) {";
    } else if (kind == "for-nonunif") {
        out += "for (;<nonuniform_cond>;) {";
    } else if (kind == "while-unif") {
        out += "while (<uniform_cond>) {";
    } else if (kind == "while-nonunif") {
        out += "while (<nonuniform_cond>) {";
    }
    out += "\n";

    bool inContinuing = false;
    prefix = "    ";

    static const char* kElems[] = {
        "op",        "continuing", "end",         "unif-break",     "always-break",
        "cond-break", "always-return", "cond-return", "always-continue", "cond-continue"};

    while (!rest.empty()) {
        // Find the longest matching element token at the start of `rest`,
        // followed by '-' or end-of-string.
        std::string elem;
        std::string next;
        for (const char* e : kElems) {
            const std::string tok(e);
            if (rest.compare(0, tok.size(), tok) == 0) {
                const size_t after = tok.size();
                const bool boundary = (after == rest.size()) || (rest[after] == '-');
                if (boundary && tok.size() > elem.size()) {
                    elem = tok;
                    next = (after == rest.size()) ? std::string() : rest.substr(after + 1);
                }
            }
        }
        rest = next;

        if (elem == "op") {
            out += prefix + "<op>\n";
        } else if (elem == "end") {
            if (inContinuing) {
                prefix = "    ";
            }
            prefix = "  ";
            for (const std::string& p : endParts) {
                out += p;
            }
            endParts.clear();
            inContinuing = false;
        } else if (elem == "continuing") {
            out += prefix + "continuing {\n";
            endParts.insert(endParts.begin(), {prefix, "}\n"});
            inContinuing = true;
            prefix = "      ";
        } else if (elem == "unif-break") {
            out += prefix + "if <uniform_cond> {break;}\n";
        } else if (elem == "always-break") {
            out += prefix + "break;\n";
        } else if (elem == "cond-break") {
            if (inContinuing) {
                out += prefix + "break if <cond>;\n";
            } else {
                out += prefix + "if <cond> {break;}\n";
            }
        } else if (elem == "always-return") {
            out += prefix + "return;\n";
        } else if (elem == "cond-return") {
            out += prefix + "if <cond> {return;}\n";
        } else if (elem == "always-continue") {
            out += prefix + "continue;\n";
        } else if (elem == "cond-continue") {
            out += prefix + "if <cond> {continue;}\n";
        }
    }
    for (const std::string& p : endParts) {
        out += p;
    }
    return out;
}

[[maybe_unused]] Snippet loopCase(const std::string& spec, Verdict verdict) {
    return Snippet{spec, specToCode(spec), verdict};
}

// ---------------------------------------------------------------------------
// generateCondition / generateOp (mirror upstream)
// ---------------------------------------------------------------------------
std::string generateCondition(const std::string& condition) {
    if (condition == "uniform_storage_ro") {
        return "ro_buffer[0] == 0";
    } else if (condition == "nonuniform_storage_ro") {
        return "ro_buffer[priv_var[0]] == 0";
    } else if (condition == "nonuniform_storage_rw") {
        return "rw_buffer[0] == 0";
    } else if (condition == "nonuniform_builtin") {
        return "p.x == 0";
    } else if (condition == "uniform_literal") {
        return "false";
    } else if (condition == "uniform_const") {
        return "c";
    } else if (condition == "uniform_override") {
        return "o == 0";
    } else if (condition == "uniform_let") {
        return "u_let == 0";
    } else if (condition == "nonuniform_let") {
        return "n_let == 0";
    } else if (condition == "uniform_or") {
        return "u_let == 0 || uniform_buffer.y > 1";
    } else if (condition == "nonuniform_or1") {
        return "u_let == 0 || n_let == 0";
    } else if (condition == "nonuniform_or2") {
        return "n_let == 0 || u_let == 0";
    } else if (condition == "uniform_and") {
        return "u_let == 0 && uniform_buffer.y > 1";
    } else if (condition == "nonuniform_and1") {
        return "u_let == 0 && n_let == 0";
    } else if (condition == "nonuniform_and2") {
        return "n_let == 0 && u_let == 0";
    } else if (condition == "uniform_func_var") {
        return "u_f == 0";
    } else if (condition == "nonuniform_func_var") {
        return "n_f == 0";
    } else if (condition == "storage_texture_ro") {
        return "textureLoad(ro_storage_texture, vec2()).x == 0";
    } else if (condition == "storage_texture_rw") {
        return "textureLoad(rw_storage_texture, vec2()).x == 0";
    } else if (condition == "control_case") {
        return "true";
    } else if (condition == "subgroupAdd" || condition == "subgroupInclusiveAdd" ||
               condition == "subgroupExclusiveAdd" || condition == "subgroupMul" ||
               condition == "subgroupInclusiveMul" || condition == "subgroupExclusiveMul" ||
               condition == "subgroupMax" || condition == "subgroupMin" ||
               condition == "subgroupAnd" || condition == "subgroupOr" ||
               condition == "subgroupXor" || condition == "subgroupBroadcastFirst" ||
               condition == "quadSwapX" || condition == "quadSwapY" ||
               condition == "quadSwapDiagonal") {
        return condition + "(0) == 0";
    } else if (condition == "subgroupAll" || condition == "subgroupAny") {
        return condition + "(false)";
    } else if (condition == "subgroupBallot") {
        return condition + "(false).x == 0";
    } else if (condition == "subgroupElect") {
        return condition + "()";
    } else if (condition == "subgroupBroadcast" || condition == "subgroupShuffle" ||
               condition == "subgroupShuffleUp" || condition == "subgroupShuffleDown" ||
               condition == "subgroupShuffleXor" || condition == "quadBroadcast") {
        return condition + "(0, 0) == 0";
    }
    return "true";  // Unhandled (unreachable upstream).
}

std::string generateOp(const std::string& op) {
    if (op == "control_case" || op == "control_case_compute" || op == "control_case_fragment") {
        return "";
    } else if (op == "textureSample") {
        return "let x = " + op + "(tex, s, vec2(0,0));\n";
    } else if (op == "textureSampleBias") {
        return "let x = " + op + "(tex, s, vec2(0,0), 0);\n";
    } else if (op == "textureSampleCompare") {
        return "let x = " + op + "(tex_depth, s_comp, vec2(0,0), 0);\n";
    } else if (op == "storageBarrier" || op == "textureBarrier" || op == "workgroupBarrier") {
        return op + "();\n";
    } else if (op == "workgroupUniformLoad") {
        return "let x = " + op + "(&wg);";
    } else if (op == "dpdx" || op == "dpdxCoarse" || op == "dpdxFine" || op == "dpdy" ||
               op == "dpdyCoarse" || op == "dpdyFine" || op == "fwidth" || op == "fwidthCoarse" ||
               op == "fwidthFine") {
        return "let x = " + op + "(0);\n";
    } else if (op == "subgroupAdd" || op == "subgroupInclusiveAdd" ||
               op == "subgroupExclusiveAdd" || op == "subgroupMul" ||
               op == "subgroupInclusiveMul" || op == "subgroupExclusiveMul" ||
               op == "subgroupMax" || op == "subgroupMin" || op == "subgroupAnd" ||
               op == "subgroupOr" || op == "subgroupXor" || op == "subgroupBroadcastFirst" ||
               op == "quadSwapX" || op == "quadSwapY" || op == "quadSwapDiagonal") {
        return "let x = " + op + "(0);\n";
    } else if (op == "subgroupAll" || op == "subgroupAny" || op == "subgroupBallot") {
        return "let x = " + op + "(false);\n";
    } else if (op == "subgroupElect") {
        return "let x = " + op + "();\n";
    } else if (op == "subgroupBroadcast" || op == "subgroupShuffle" || op == "subgroupShuffleUp" ||
               op == "subgroupShuffleDown" || op == "subgroupShuffleXor" || op == "quadBroadcast") {
        return "let x = " + op + "(0, 0);\n";
    }
    return "";  // Unhandled (unreachable upstream).
}

// ---------------------------------------------------------------------------
// kStatementCases (ordered) — name/code/verdict
// ---------------------------------------------------------------------------
struct StatementCase {
    std::string name;
    std::string code;
    Verdict verdict;
};

const std::vector<StatementCase>& kStatementCases() {
    static const std::vector<StatementCase> cases = [] {
        std::vector<StatementCase> v;
        // Basic non-loop cases.
        v.push_back({"if", "if <cond> { <op> }", Verdict::Sensitive});
        v.push_back({"switch",
                     "\n          switch u32(<cond>) {\n            case 0: {\n              <op>\n"
                     "            }\n            default: { }\n          }",
                     Verdict::Sensitive});

        struct LC {
            const char* spec;
            Verdict verdict;
        };
        static const LC loops[] = {
            // loop without continuing — op before the interruption
            {"loop-op-always-break", Verdict::Permit},
            {"loop-op-cond-break", Verdict::Sensitive},
            {"loop-op-always-return", Verdict::Permit},
            {"loop-op-cond-return", Verdict::Sensitive},
            {"loop-unif-break-op-always-continue", Verdict::Permit},
            {"loop-unif-break-op-cond-continue", Verdict::Sensitive},
            // op after the interruption
            {"loop-always-break-op", Verdict::Permit},
            {"loop-cond-break-op", Verdict::Sensitive},
            {"loop-always-return-op", Verdict::Permit},
            {"loop-cond-return-op", Verdict::Sensitive},
            {"loop-unif-break-always-continue-op", Verdict::Permit},
            {"loop-unif-break-cond-continue-op", Verdict::Sensitive},
            // op after the end of the loop
            {"loop-always-break-end-op", Verdict::Permit},
            {"loop-unif-break-end-op", Verdict::Permit},
            {"loop-cond-break-end-op", Verdict::Permit},
            {"loop-always-return-end-op", Verdict::Permit},
            {"loop-cond-return-end-op", Verdict::Permit},
            {"loop-unif-break-always-continue-end-op", Verdict::Permit},
            {"loop-unif-break-cond-continue-end-op", Verdict::Permit},
            // loop with continuing block — op before interruption before continuing
            {"loop-op-always-break-continuing", Verdict::Permit},
            {"loop-op-unif-break-continuing", Verdict::Permit},
            {"loop-op-cond-break-continuing", Verdict::Sensitive},
            {"loop-op-always-return-continuing", Verdict::Permit},
            {"loop-op-cond-return-continuing", Verdict::Sensitive},
            {"loop-unif-break-op-always-continue-continuing", Verdict::Permit},
            {"loop-unif-break-op-cond-continue-continuing", Verdict::Sensitive},
            // op in body, interruption in continuing
            {"loop-op-continuing-cond-break", Verdict::Sensitive},
            // interruption in body, op in continuing
            {"loop-always-break-continuing-op", Verdict::Permit},
            {"loop-cond-break-continuing-op", Verdict::Sensitive},
            {"loop-always-return-continuing-op", Verdict::Permit},
            {"loop-cond-return-continuing-op", Verdict::Sensitive},
            // op and interruption in continuing
            {"loop-continuing-op-cond-break", Verdict::Sensitive},
            // interruption in body, op after end
            {"loop-always-break-continuing-end-op", Verdict::Permit},
            {"loop-cond-break-continuing-end-op", Verdict::Permit},
            {"loop-always-return-continuing-end-op", Verdict::Permit},
            {"loop-cond-return-continuing-end-op", Verdict::Permit},
            {"loop-unif-break-always-continue-continuing-end-op", Verdict::Permit},
            {"loop-unif-break-cond-continue-continuing-end-op", Verdict::Permit},
            // interruption in continuing, op after end
            {"loop-continuing-cond-break-end-op", Verdict::Permit},
            {"loop-always-break-continuing-cond-break-end-op", Verdict::Permit},
            {"loop-always-return-continuing-cond-break-end-op", Verdict::Permit},
            // Unconditional for — interruption then op
            {"for-always-break-op", Verdict::Permit},
            {"for-cond-break-op", Verdict::Sensitive},
            {"for-always-return-op", Verdict::Permit},
            {"for-cond-return-op", Verdict::Sensitive},
            {"for-unif-unif-break-always-continue-op", Verdict::Permit},
            {"for-unif-unif-break-cond-continue-op", Verdict::Sensitive},
            // op then interruption
            {"for-op-always-break", Verdict::Permit},
            {"for-op-cond-break", Verdict::Sensitive},
            {"for-op-always-return", Verdict::Permit},
            {"for-op-cond-return", Verdict::Sensitive},
            {"for-op-unif-break-always-continue", Verdict::Permit},
            {"for-op-unif-break-cond-continue", Verdict::Sensitive},
            // For with uniform condition
            {"for-unif-op", Verdict::Permit},
            {"for-unif-always-break-op", Verdict::Permit},
            {"for-unif-cond-break-op", Verdict::Sensitive},
            {"for-unif-always-return-op", Verdict::Permit},
            {"for-unif-cond-return-op", Verdict::Sensitive},
            {"for-unif-always-continue-op", Verdict::Permit},
            {"for-unif-cond-continue-op", Verdict::Sensitive},
            {"for-unif-op-always-break", Verdict::Permit},
            {"for-unif-op-cond-break", Verdict::Sensitive},
            {"for-unif-op-always-return", Verdict::Permit},
            {"for-unif-op-cond-return", Verdict::Sensitive},
            {"for-unif-op-always-continue", Verdict::Permit},
            {"for-unif-op-cond-continue", Verdict::Sensitive},
            {"for-unif-end-op", Verdict::Permit},
            {"for-unif-always-break-end-op", Verdict::Permit},
            {"for-unif-cond-break-end-op", Verdict::Permit},
            {"for-unif-always-return-end-op", Verdict::Permit},
            {"for-unif-cond-return-end-op", Verdict::Sensitive},
            {"for-unif-always-continue-end-op", Verdict::Permit},
            {"for-unif-cond-continue-end-op", Verdict::Permit},
            // For with non-uniform condition
            {"for-nonunif-op", Verdict::Forbid},
            {"for-nonunif-always-break-op", Verdict::Permit},
            {"for-nonunif-cond-break-op", Verdict::Forbid},
            {"for-nonunif-always-return-op", Verdict::Permit},
            {"for-nonunif-cond-return-op", Verdict::Forbid},
            {"for-nonunif-always-continue-op", Verdict::Permit},
            {"for-nonunif-cond-continue-op", Verdict::Forbid},
            {"for-nonunif-op-always-break", Verdict::Forbid},
            {"for-nonunif-op-cond-break", Verdict::Forbid},
            {"for-nonunif-op-always-return", Verdict::Forbid},
            {"for-nonunif-op-cond-return", Verdict::Forbid},
            {"for-nonunif-op-always-continue", Verdict::Forbid},
            {"for-nonunif-op-cond-continue", Verdict::Forbid},
            {"for-nonunif-end-op", Verdict::Permit},
            {"for-nonunif-always-break-end-op", Verdict::Permit},
            {"for-nonunif-cond-break-end-op", Verdict::Permit},
            {"for-nonunif-always-return-end-op", Verdict::Forbid},
            {"for-nonunif-cond-return-end-op", Verdict::Forbid},
            {"for-nonunif-always-continue-end-op", Verdict::Permit},
            {"for-nonunif-cond-continue-end-op", Verdict::Permit},
            // While with uniform condition
            {"while-unif-op", Verdict::Permit},
            {"while-unif-always-break-op", Verdict::Permit},
            {"while-unif-cond-break-op", Verdict::Sensitive},
            {"while-unif-always-return-op", Verdict::Permit},
            {"while-unif-cond-return-op", Verdict::Sensitive},
            {"while-unif-always-continue-op", Verdict::Permit},
            {"while-unif-cond-continue-op", Verdict::Sensitive},
            {"while-unif-op-always-break", Verdict::Permit},
            {"while-unif-op-cond-break", Verdict::Sensitive},
            {"while-unif-op-always-return", Verdict::Permit},
            {"while-unif-op-cond-return", Verdict::Sensitive},
            {"while-unif-op-always-continue", Verdict::Permit},
            {"while-unif-op-cond-continue", Verdict::Sensitive},
            {"while-unif-end-op", Verdict::Permit},
            {"while-unif-always-break-end-op", Verdict::Permit},
            {"while-unif-cond-break-end-op", Verdict::Permit},
            {"while-unif-always-return-end-op", Verdict::Permit},
            {"while-unif-cond-return-end-op", Verdict::Sensitive},
            {"while-unif-always-continue-end-op", Verdict::Permit},
            {"while-unif-cond-continue-end-op", Verdict::Permit},
            // While with non-uniform condition
            {"while-nonunif-op", Verdict::Forbid},
            {"while-nonunif-always-break-op", Verdict::Permit},
            {"while-nonunif-cond-break-op", Verdict::Forbid},
            {"while-nonunif-always-return-op", Verdict::Permit},
            {"while-nonunif-cond-return-op", Verdict::Forbid},
            {"while-nonunif-always-continue-op", Verdict::Permit},
            {"while-nonunif-cond-continue-op", Verdict::Forbid},
            {"while-nonunif-op-always-break", Verdict::Forbid},
            {"while-nonunif-op-cond-break", Verdict::Forbid},
            {"while-nonunif-op-always-return", Verdict::Forbid},
            {"while-nonunif-op-cond-return", Verdict::Forbid},
            {"while-nonunif-op-always-continue", Verdict::Forbid},
            {"while-nonunif-op-cond-continue", Verdict::Forbid},
            {"while-nonunif-end-op", Verdict::Permit},
            {"while-nonunif-always-break-end-op", Verdict::Permit},
            {"while-nonunif-cond-break-end-op", Verdict::Permit},
            {"while-nonunif-always-return-end-op", Verdict::Forbid},
            {"while-nonunif-cond-return-end-op", Verdict::Forbid},
            {"while-nonunif-always-continue-end-op", Verdict::Permit},
            {"while-nonunif-cond-continue-end-op", Verdict::Permit},
        };
        for (const LC& lc : loops) {
            v.push_back({lc.spec, specToCode(lc.spec), lc.verdict});
        }
        return v;
    }();
    return cases;
}

std::vector<Value> statementNames() {
    std::vector<Value> names;
    for (const StatementCase& sc : kStatementCases()) {
        names.emplace_back(sc.name);
    }
    return names;
}

const StatementCase& findStatement(const std::string& name) {
    for (const StatementCase& sc : kStatementCases()) {
        if (sc.name == name) {
            return sc;
        }
    }
    static const StatementCase dummy{"", "", Verdict::Permit};
    return dummy;
}

Snippet generateConditionalStatement(const std::string& name, const std::string& conditionName,
                                      const std::string& opName) {
    const std::string cond = generateCondition(conditionName);
    const std::string op = generateOp(opName);
    const StatementCase& sc = findStatement(name);
    std::string code = sc.code;
    code = replaceFirst(code, "<op>", op);
    code = replaceFirst(code, "<cond>", cond);
    code = replaceAll(code, "<uniform_cond>", generateCondition("uniform_storage_ro"));
    code = replaceAll(code, "<nonuniform_cond>", generateCondition("nonuniform_storage_ro"));
    return Snippet{name, code, sc.verdict};
}

// ---------------------------------------------------------------------------
// kConditions / kCollectiveOps (ordered)
// ---------------------------------------------------------------------------
struct ConditionEntry {
    const char* cond;
    bool expectation;
};
const ConditionEntry kConditions[] = {
    {"uniform_storage_ro", true},   {"nonuniform_storage_ro", false},
    {"nonuniform_storage_rw", false}, {"nonuniform_builtin", false},
    {"uniform_literal", true},      {"uniform_const", true},
    {"uniform_override", true},     {"uniform_let", true},
    {"nonuniform_let", false},      {"uniform_or", true},
    {"nonuniform_or1", false},      {"nonuniform_or2", false},
    {"uniform_and", true},          {"nonuniform_and1", false},
    {"nonuniform_and2", false},     {"uniform_func_var", true},
    {"nonuniform_func_var", false}, {"storage_texture_ro", true},
    {"storage_texture_rw", false},
};

struct CollectiveOp {
    const char* op;
    const char* stage;
};
const CollectiveOp kCollectiveOps[] = {
    {"control_case_compute", "compute"},  {"control_case_fragment", "fragment"},
    {"textureSample", "fragment"},        {"textureSampleBias", "fragment"},
    {"textureSampleCompare", "fragment"}, {"dpdx", "fragment"},
    {"dpdxCoarse", "fragment"},           {"dpdxFine", "fragment"},
    {"dpdy", "fragment"},                 {"dpdyCoarse", "fragment"},
    {"dpdyFine", "fragment"},             {"fwidth", "fragment"},
    {"fwidthCoarse", "fragment"},         {"fwidthFine", "fragment"},
    {"storageBarrier", "compute"},        {"textureBarrier", "compute"},
    {"workgroupBarrier", "compute"},      {"workgroupUniformLoad", "compute"},
};

// Helper: skip the case if the WGSL language feature is not supported.
void skipIfLanguageFeatureNotSupported(ShaderValidationTest& t, const std::string& feature) {
    if (!t.hasLanguageFeature(feature)) {
        t.skip("WGSL language feature not supported: " + feature);
    }
}

// Shared bindings preamble used by basics / basics,subgroups.
std::string basicsPreamble() {
    return std::string(
        "\n @group(0) @binding(0) var s : sampler;"
        "\n @group(0) @binding(1) var s_comp : sampler_comparison;"
        "\n @group(0) @binding(2) var tex : texture_2d<f32>;"
        "\n @group(0) @binding(3) var tex_depth : texture_depth_2d;"
        "\n"
        "\n @group(1) @binding(0) var<storage, read> ro_buffer : array<f32, 4>;"
        "\n @group(1) @binding(1) var<storage, read_write> rw_buffer : array<f32, 4>;"
        "\n @group(1) @binding(2) var<uniform> uniform_buffer : vec4<f32>;"
        "\n"
        "\n @group(2) @binding(0) var ro_storage_texture : texture_storage_2d<rgba8unorm, read>;"
        "\n @group(2) @binding(1) var rw_storage_texture : texture_storage_2d<rgba8unorm, "
        "read_write>;"
        "\n"
        "\n var<private> priv_var : array<u32, 4> = array(0,0,0,0);"
        "\n"
        "\n const c = false;"
        "\n override o : f32;\n");
}

// ===========================================================================
// basics
// ===========================================================================
CTS_TEST(g, "basics")
    .desc("Test collective operations in simple uniform or non-uniform control flow.")
    .params([](ParamsBuilder u) {
        std::vector<ParamRecord> conditions;
        for (const ConditionEntry& c : kConditions) {
            conditions.push_back(
                {{"cond", Value(std::string(c.cond))}, {"expectation", Value(c.expectation)}});
        }
        std::vector<ParamRecord> ops;
        for (const CollectiveOp& o : kCollectiveOps) {
            ops.push_back({{"op", Value(std::string(o.op))}, {"stage", Value(std::string(o.stage))}});
        }
        return u.combine("statement", statementNames())
            .beginSubcases()
            .combineWithParams(conditions)
            .combineWithParams(ops);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string statement = t.param<std::string>("statement");
        const std::string cond = t.param<std::string>("cond");
        const bool expectation = t.param<bool>("expectation");
        const std::string op = t.param<std::string>("op");
        const std::string stage = t.param<std::string>("stage");

        if (op == "textureBarrier" || startsWith(cond, "storage_texture")) {
            skipIfLanguageFeatureNotSupported(t, "readonly_and_readwrite_storage_textures");
        }

        std::string code = basicsPreamble();
        if (stage == "compute") {
            code += "var<workgroup> wg : f32;\n";
            code += " @workgroup_size(16, 1, 1)";
        }
        code += "@" + stage;
        code += "\nfn main(";
        if (stage == "compute") {
            code += "@builtin(global_invocation_id) p : vec3<u32>";
        } else {
            code += "@builtin(position) p : vec4<f32>";
        }
        code +=
            ") {\n  let u_let = uniform_buffer.x;\n  let n_let = rw_buffer[0];\n  var u_f = "
            "uniform_buffer.z;\n  var n_f = rw_buffer[1];\n";

        const Snippet snippet = generateConditionalStatement(statement, cond, op);
        code += snippet.code;
        code += "\n}\n";

        t.expectCompileResult(
            compileShouldSucceed(!startsWith(op, "control_case"), expectation, snippet.verdict),
            code);
    });

// ===========================================================================
// basics,subgroups
// ===========================================================================
const char* kUniformSubgroupOps[] = {
    "subgroupAdd",      "subgroupMul",   "subgroupMax",
    "subgroupMin",      "subgroupAll",   "subgroupAny",
    "subgroupAnd",      "subgroupOr",    "subgroupXor",
    "subgroupBallot",   "subgroupBroadcast", "subgroupBroadcastFirst",
};

bool isUniformSubgroupOp(const std::string& op) {
    for (const char* o : kUniformSubgroupOps) {
        if (op == o) {
            return true;
        }
    }
    return false;
}

std::vector<Value> subgroupOps() {
    std::vector<Value> ops = {
        Value(std::string("control_case")),       Value(std::string("subgroupInclusiveAdd")),
        Value(std::string("subgroupExclusiveAdd")), Value(std::string("subgroupInclusiveMul")),
        Value(std::string("subgroupExclusiveMul")), Value(std::string("subgroupElect")),
        Value(std::string("subgroupShuffle")),     Value(std::string("subgroupShuffleUp")),
        Value(std::string("subgroupShuffleDown")), Value(std::string("subgroupShuffleXor")),
        Value(std::string("quadBroadcast")),       Value(std::string("quadSwapX")),
        Value(std::string("quadSwapY")),           Value(std::string("quadSwapDiagonal")),
    };
    for (const char* o : kUniformSubgroupOps) {
        ops.emplace_back(std::string(o));
    }
    return ops;
}

CTS_TEST(g, "basics,subgroups")
    .desc("Test subgroup operations in simple uniform or non-uniform control flow.")
    .params([](ParamsBuilder u) {
        std::vector<ParamRecord> conditions;
        for (const ConditionEntry& c : kConditions) {
            conditions.push_back(
                {{"cond", Value(std::string(c.cond))}, {"expectation", Value(c.expectation)}});
        }
        return u.combine("statement", statementNames())
            .beginSubcases()
            .combineWithParams(conditions)
            .combine("op", subgroupOps())
            .combine("stage", {"compute", "fragment"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string statement = t.param<std::string>("statement");
        const std::string cond = t.param<std::string>("cond");
        const bool expectation = t.param<bool>("expectation");
        const std::string op = t.param<std::string>("op");
        const std::string stage = t.param<std::string>("stage");

        std::string code = "\n enable subgroups;\n" + basicsPreamble();
        if (stage == "compute") {
            code += "var<workgroup> wg : f32;\n";
            code += " @workgroup_size(16, 1, 1)";
        }
        code += "@" + stage;
        code += "\nfn main(";
        if (stage == "compute") {
            code += "@builtin(global_invocation_id) p : vec3<u32>";
        } else {
            code += "@builtin(position) p : vec4<f32>";
        }
        code +=
            ") {\n      let u_let = uniform_buffer.x;\n      let n_let = rw_buffer[0];\n      var "
            "u_f = uniform_buffer.z;\n      var n_f = rw_buffer[1];\n    ";

        const Snippet snippet = generateConditionalStatement(statement, cond, op);
        code += snippet.code;
        code += "\n}\n";

        t.expectCompileResult(
            compileShouldSucceed(!startsWith(op, "control_case"), expectation, snippet.verdict),
            code);
    });

// ===========================================================================
// uniform_subgroup_ops
// ===========================================================================
CTS_TEST(g, "uniform_subgroup_ops")
    .desc("Test subgroup operations that are uniform with subgroup uniformity.")
    .params([](ParamsBuilder u) {
        return u.combine("op", subgroupOps()).combine("scope", {"workgroup", "subgroup"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const std::string scope = t.param<std::string>("scope");

        const std::string testCode =
            scope == "workgroup" ? "workgroupBarrier();" : "_ = subgroupAny(true);";
        const std::string code = "\nenable subgroups;\nfn foo() {\n  if " + generateCondition(op) +
                                 " {\n    " + testCode + "\n  }\n}";

        const bool isUniform = isUniformSubgroupOp(op) &&
                               t.hasLanguageFeature("subgroup_uniformity") && scope == "subgroup";
        t.expectCompileResult(
            compileShouldSucceed(!startsWith(op, "control_case"), isUniform, Verdict::Sensitive),
            code);
    });

// ===========================================================================
// fragment_builtin_values
// ===========================================================================
struct FragmentBuiltin {
    const char* builtin;
    const char* type;
};
const FragmentBuiltin kFragmentBuiltinValues[] = {
    {"position", "vec4<f32>"},
    {"front_facing", "bool"},
    {"sample_index", "u32"},
    {"sample_mask", "u32"},
    {"subgroup_invocation_id", "u32"},
    {"subgroup_size", "u32"},
    {"primitive_id", "u32"},
};

CTS_TEST(g, "fragment_builtin_values")
    .desc("Test uniformity of fragment built-in values")
    .params([](ParamsBuilder u) {
        std::vector<ParamRecord> recs;
        for (const FragmentBuiltin& b : kFragmentBuiltinValues) {
            recs.push_back(
                {{"builtin", Value(std::string(b.builtin))}, {"type", Value(std::string(b.type))}});
        }
        return u.combineWithParams(recs).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        // beforeAllSubcases: skipIf(isCompatibility && builtin in {sample_index, sample_mask}).
        // isCompatibility == false in this harness, so the skip never fires.
        const std::string builtin = t.param<std::string>("builtin");
        const std::string type = t.param<std::string>("type");

        std::string cond;
        if (type == "u32" || type == "i32" || type == "f32") {
            cond = "p > 0";
        } else if (type == "vec4<u32>" || type == "vec4<i32>" || type == "vec4<f32>") {
            cond = "p.x > 0";
        } else if (type == "bool") {
            cond = "p";
        }

        std::string enable;
        if (builtin.find("subgroup") != std::string::npos) {
            enable = "enable subgroups;\n";
        } else if (builtin == "primitive_id") {
            enable = "enable chromium_experimental_primitive_id;\n";
            // The `chromium_experimental_primitive_id` WGSL extension token is gated at the
            // token level (Dawn renamed it / accepts a different spelling), independent of the
            // device feature, so the enabler's device-feature auto-skip does not catch it. Skip
            // when the token itself is not usable (mirrors shader_io/builtins). A skip, not a pass.
            if (!t.wgslExtensionUsable("enable chromium_experimental_primitive_id;")) {
                t.skip("WGSL extension token `chromium_experimental_primitive_id` not usable on this backend");
                return;
            }
        }

        const std::string code = "\n" + enable +
                                 "\n@group(0) @binding(0) var s : sampler;"
                                 "\n@group(0) @binding(1) var tex : texture_2d<f32>;"
                                 "\n"
                                 "\n@fragment"
                                 "\nfn main(@builtin(" +
                                 builtin + ") p : " + type + ") {\n  if " + cond +
                                 " {\n    let texel = textureSample(tex, s, vec2<f32>(0,0));\n  "
                                 "}\n}\n";

        t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        t.expectCompileResult(false, code);
    });

// ===========================================================================
// compute_builtin_values
// ===========================================================================
struct ComputeBuiltin {
    const char* builtin;
    const char* type;
    bool uniform;
};
const ComputeBuiltin kComputeBuiltinValues[] = {
    {"local_invocation_id", "vec3<f32>", false},
    {"local_invocation_index", "u32", false},
    {"global_invocation_id", "vec3<u32>", false},
    {"workgroup_id", "vec3<u32>", true},
    {"num_workgroups", "vec3<u32>", true},
    {"subgroup_invocation_id", "u32", false},
    {"subgroup_size", "u32", true},
    {"subgroup_id", "u32", false},
    {"num_subgroups", "u32", true},
};

CTS_TEST(g, "compute_builtin_values")
    .desc("Test uniformity of compute built-in values")
    .params([](ParamsBuilder u) {
        std::vector<ParamRecord> recs;
        for (const ComputeBuiltin& b : kComputeBuiltinValues) {
            recs.push_back({{"builtin", Value(std::string(b.builtin))},
                            {"type", Value(std::string(b.type))},
                            {"uniform", Value(b.uniform)}});
        }
        return u.combineWithParams(recs).beginSubcases().combine("scope",
                                                                  {"workgroup", "subgroup"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string builtin = t.param<std::string>("builtin");
        const std::string type = t.param<std::string>("type");
        const bool uniform = t.param<bool>("uniform");
        const std::string scope = t.param<std::string>("scope");

        // beforeAllSubcases: subgroup_id/num_subgroups require 'subgroup_id'; subgroup
        // scope requires 'subgroup_uniformity'.
        if (builtin == "subgroup_id" || builtin == "num_subgroups") {
            skipIfLanguageFeatureNotSupported(t, "subgroup_id");
        }
        if (scope == "subgroup") {
            skipIfLanguageFeatureNotSupported(t, "subgroup_uniformity");
        }

        std::string cond;
        if (type == "u32" || type == "i32" || type == "f32") {
            cond = "p > 0";
        } else if (type == "vec3<u32>" || type == "vec3<i32>" || type == "vec3<f32>") {
            cond = "p.x > 0";
        } else if (type == "bool") {
            cond = "p";
        }

        const std::string enable =
            (builtin.find("subgroup") != std::string::npos || scope == "subgroup")
                ? "enable subgroups;"
                : "";
        const std::string op =
            scope == "workgroup" ? "workgroupBarrier()" : "_ = subgroupAny(true)";
        const std::string code = "\n" + enable +
                                 "\n@compute @workgroup_size(16,1,1)\nfn main(@builtin(" + builtin +
                                 ") p : " + type + ") {\n  if " + cond + " {\n    " + op +
                                 ";\n  }\n}\n";

        const bool expect = uniform || (builtin == "subgroup_id" && scope == "subgroup");
        t.expectCompileResult(expect, code);
    });

// ===========================================================================
// pointers
// ===========================================================================
// uniform: 0 == false, 1 == true, 2 == 'never'.
struct PointerCase {
    const char* name;
    const char* code;
    const char* check;  // "address" or "contents"
    int uniform;        // 0=false, 1=true, 2=never
    bool needsDerefSugar;
};

std::string generatePointerCheck(const std::string& check) {
    if (check == "address") {
        return "let tmp = workgroupUniformLoad(ptr);";
    }
    return "if test_val > 0 {\n      workgroupBarrier();\n    }";
}

const std::vector<PointerCase>& kPointerCases() {
    static const std::vector<PointerCase> cases = {
        {"address_uniform_literal", "let ptr = &wg_array[0];", "address", 1, false},
        {"address_uniform_value", "let ptr = &wg_array[uniform_value];", "address", 1, false},
        {"address_nonuniform_value", "let ptr = &wg_array[nonuniform_value];", "address", 0, false},
        {"address_uniform_chain",
         "let p1 = &wg_struct.x;\n    let p2 = &(*p1)[uniform_value];\n    let p3 = &(*p2).x;\n    "
         "let ptr = &(*p3)[uniform_value];",
         "address", 1, false},
        {"address_nonuniform_chain1",
         "let p1 = &wg_struct.x;\n    let p2 = &(*p1)[nonuniform_value];\n    let p3 = &(*p2).x;\n  "
         "  let ptr = &(*p3)[uniform_value];",
         "address", 0, false},
        {"address_nonuniform_chain2",
         "let p1 = &wg_struct.x;\n    let p2 = &(*p1)[uniform_value];\n    let p3 = &(*p2).x;\n    "
         "let ptr = &(*p3)[nonuniform_value];",
         "address", 0, false},
        {"wg_uniform_load_is_uniform", "let test_val = workgroupUniformLoad(&wg_scalar);",
         "contents", 1, false},
        {"wg_uniform_load_atomic_is_uniform", "let ptr = &wg_atomic;", "address", 1, false},
        {"contents_scalar_uniform1", "let ptr = &func_scalar;\n    let test_val = *ptr;", "contents",
         1, false},
        {"contents_scalar_uniform2",
         "func_scalar = nonuniform_value;\n    let ptr = &func_scalar;\n    func_scalar = 0;\n    "
         "let test_val = *ptr;",
         "contents", 1, false},
        {"contents_scalar_uniform3",
         "let ptr = &func_scalar;\n    func_scalar = nonuniform_value;\n    func_scalar = "
         "uniform_value;\n    let test_val = *ptr;",
         "contents", 1, false},
        {"contents_scalar_nonuniform1",
         "func_scalar = nonuniform_value;\n    let ptr = &func_scalar;\n    let test_val = *ptr;",
         "contents", 0, false},
        {"contents_scalar_nonuniform2",
         "let ptr = &func_scalar;\n    *ptr = nonuniform_value;\n    let test_val = *ptr;",
         "contents", 0, false},
        {"contents_scalar_alias_uniform",
         "let p = &func_scalar;\n    let ptr = p;\n    let test_val = *ptr;", "contents", 1, false},
        {"contents_scalar_alias_nonuniform1",
         "func_scalar = nonuniform_value;\n    let p = &func_scalar;\n    let ptr = p;\n    let "
         "test_val = *ptr;",
         "contents", 0, false},
        {"contents_scalar_alias_nonuniform2",
         "let p = &func_scalar;\n    *p = nonuniform_value;\n    let ptr = p;\n    let test_val = "
         "*ptr;",
         "contents", 0, false},
        {"contents_scalar_alias_nonuniform3",
         "let p = &func_scalar;\n    let ptr = p;\n    *p = nonuniform_value;\n    let test_val = "
         "*ptr;",
         "contents", 0, false},
        {"contents_scalar_alias_nonuniform4",
         "let p = &func_scalar;\n    func_scalar = nonuniform_value;\n    let test_val = *p;",
         "contents", 0, false},
        {"contents_scalar_alias_nonuniform5",
         "let p = &func_scalar;\n    *p = nonuniform_value;\n    let test_val = func_scalar;",
         "contents", 0, false},
        {"contents_array_uniform_index",
         "let ptr = &func_array[uniform_value];\n    let test_val = *ptr;", "contents", 1, false},
        {"contents_array_nonuniform_index1",
         "let ptr = &func_array[nonuniform_value];\n    let test_val = *ptr;", "contents", 0,
         false},
        {"contents_array_nonuniform_index2",
         "let ptr = &func_array[lid.x];\n    let test_val = *ptr;", "contents", 0, false},
        {"contents_array_nonuniform_index3",
         "let ptr = &func_array[gid.x];\n    let test_val = *ptr;", "contents", 0, false},
        {"contents_struct_uniform",
         "let p1 = &func_struct.x[uniform_value].x[uniform_value].x[uniform_value];\n    let "
         "test_val = *p1;",
         "contents", 1, false},
        {"contents_struct_nonuniform1",
         "let p1 = &func_struct.x[nonuniform_value].x[uniform_value].x[uniform_value];\n    let "
         "test_val = *p1;",
         "contents", 0, false},
        {"contents_struct_nonuniform2",
         "let p1 = &func_struct.x[uniform_value].x[gid.x].x[uniform_value];\n    let test_val = "
         "*p1;",
         "contents", 0, false},
        {"contents_struct_nonuniform3",
         "let p1 = &func_struct.x[uniform_value].x[uniform_value].x[lid.y];\n    let test_val = "
         "*p1;",
         "contents", 0, false},
        {"contents_struct_chain_uniform",
         "let p1 = &func_struct.x;\n    let p2 = &(*p1)[uniform_value];\n    let p3 = &(*p2).x;\n   "
         " let p4 = &(*p3)[uniform_value];\n    let p5 = &(*p4).x;\n    let p6 = "
         "&(*p5)[uniform_value];\n    let test_val = *p6;",
         "contents", 1, false},
        {"contents_struct_chain_nonuniform1",
         "let p1 = &func_struct.x;\n    let p2 = &(*p1)[nonuniform_value];\n    let p3 = "
         "&(*p2).x;\n    let p4 = &(*p3)[uniform_value];\n    let p5 = &(*p4).x;\n    let p6 = "
         "&(*p5)[uniform_value];\n    let test_val = *p6;",
         "contents", 0, false},
        {"contents_struct_chain_nonuniform2",
         "let p1 = &func_struct.x;\n    let p2 = &(*p1)[uniform_value];\n    let p3 = &(*p2).x;\n   "
         " let p4 = &(*p3)[gid.x];\n    let p5 = &(*p4).x;\n    let p6 = &(*p5)[uniform_value];\n   "
         " let test_val = *p6;",
         "contents", 0, false},
        {"contents_struct_chain_nonuniform3",
         "let p1 = &func_struct.x;\n    let p2 = &(*p1)[uniform_value];\n    let p3 = &(*p2).x;\n   "
         " let p4 = &(*p3)[uniform_value];\n    let p5 = &(*p4).x;\n    let p6 = &(*p5)[lid.y];\n   "
         " let test_val = *p6;",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref1",
         "*&func_scalar = uniform_value;\n    let test_val = func_scalar;", "contents", 1, false},
        {"contents_lhs_ref_pointer_deref1a",
         "*&func_scalar = nonuniform_value;\n    let test_val = func_scalar;", "contents", 0,
         false},
        {"contents_lhs_ref_pointer_deref2",
         "*&(func_array[nonuniform_value]) = uniform_value;\n    let test_val = func_array[0];",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref2a",
         "(func_array[nonuniform_value]) = uniform_value;\n    let test_val = func_array[0];",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref3",
         "*&(func_array[needs_uniform(uniform_value)]) = uniform_value;\n    let test_val = "
         "func_array[0];",
         "contents", 1, false},
        {"contents_lhs_ref_pointer_deref3a",
         "*&(func_array[needs_uniform(nonuniform_value)]) = uniform_value;\n    let test_val = "
         "func_array[0];",
         "contents", 2, false},
        {"contents_lhs_ref_pointer_deref4",
         "*&((*&(func_struct.x[uniform_value])).x[uniform_value].x[uniform_value]) = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 1, false},
        {"contents_lhs_ref_pointer_deref4a",
         "*&((*&(func_struct.x[uniform_value])).x[uniform_value].x[uniform_value]) = "
         "nonuniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref4b",
         "*&((*&(func_struct.x[uniform_value])).x[uniform_value].x[nonuniform_value]) = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref4c",
         "*&((*&(func_struct.x[uniform_value])).x[nonuniform_value]).x[uniform_value] = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref4d",
         "*&((*&(func_struct.x[nonuniform_value])).x[uniform_value].x)[uniform_value] = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, false},
        {"contents_lhs_ref_pointer_deref4e",
         "*&((*&(func_struct.x[uniform_value])).x[needs_uniform(nonuniform_value)].x[uniform_value]"
         ") = uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 2, false},
        // The following require 'pointer_composite_access'.
        {"contents_lhs_pointer_deref2",
         "(&func_array)[uniform_value] = uniform_value;\n    let test_val = func_array[0];",
         "contents", 1, true},
        {"contents_lhs_pointer_deref2a",
         "(&func_array)[nonuniform_value] = uniform_value;\n    let test_val = func_array[0];",
         "contents", 0, true},
        {"contents_lhs_pointer_deref3",
         "(&func_array)[needs_uniform(uniform_value)] = uniform_value;\n    let test_val = "
         "func_array[0];",
         "contents", 1, true},
        {"contents_lhs_pointer_deref3a",
         "(&func_array)[needs_uniform(nonuniform_value)] = uniform_value;\n    let test_val = "
         "func_array[0];",
         "contents", 2, true},
        {"contents_lhs_pointer_deref4",
         "(&((&(func_struct.x[uniform_value])).x[uniform_value]).x)[uniform_value] = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 1, true},
        {"contents_lhs_pointer_deref4a",
         "(&((&(func_struct.x[uniform_value])).x[uniform_value]).x)[uniform_value] = "
         "nonuniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, true},
        {"contents_lhs_pointer_deref4b",
         "(&((&(func_struct.x[uniform_value])).x)[uniform_value]).x[nonuniform_value] = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, true},
        {"contents_lhs_pointer_deref4c",
         "(&((&(func_struct.x[uniform_value])).x[nonuniform_value]).x)[uniform_value] = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, true},
        {"contents_lhs_pointer_deref4d",
         "(&((&(func_struct.x[nonuniform_value])).x[uniform_value]).x)[uniform_value] = "
         "uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 0, true},
        {"contents_lhs_pointer_deref4e",
         "(&((&(func_struct.x[uniform_value])).x)[needs_uniform(nonuniform_value)].x[uniform_value]"
         ") = uniform_value;\n    let test_val = func_struct.x[0].x[0].x[0];",
         "contents", 2, true},
        {"contents_rhs_pointer_deref1", "let test_val = (&func_array)[uniform_value];", "contents",
         1, true},
        {"contents_rhs_pointer_deref1a", "let test_val = (&func_array)[nonuniform_value];",
         "contents", 0, true},
        {"contents_rhs_pointer_deref2",
         "let test_val = (&func_array)[needs_uniform(nonuniform_value)];", "contents", 2, true},
        {"contents_rhs_pointer_swizzle_uniform",
         "func_vector = vec4(uniform_value);\n    let test_val = dot((&func_vector).yw, vec2());",
         "contents", 1, true},
        {"contents_rhs_pointer_swizzle_non_uniform",
         "func_vector = vec4(nonuniform_value);\n    let test_val = dot((&func_vector).yw, "
         "vec2());",
         "contents", 0, true},
    };
    return cases;
}

const PointerCase& findPointerCase(const std::string& name) {
    for (const PointerCase& c : kPointerCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const PointerCase dummy{"", "", "contents", 0, false};
    return dummy;
}

CTS_TEST(g, "pointers")
    .desc("Test pointer uniformity (contents and addresses)")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const PointerCase& c : kPointerCases()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const PointerCase& testcase = findPointerCase(t.param<std::string>("case"));
        const std::string code =
            std::string(
                "\nvar<workgroup> wg_scalar : u32;\nvar<workgroup> wg_array : array<u32, 16>;\n"
                "var<workgroup> wg_atomic : atomic<u32>;\n"
                "\nstruct Inner {\n  x : array<u32, 4>\n}\nstruct Middle {\n  x : array<Inner, "
                "4>\n}\nstruct Outer {\n  x : array<Middle, 4>\n}\nvar<workgroup> wg_struct : "
                "Outer;\n"
                "\n@group(0) @binding(0)\nvar<storage> uniform_value : u32;\n@group(0) "
                "@binding(1)\nvar<storage, read_write> nonuniform_value : u32;\n"
                "\nfn needs_uniform(val : u32) -> u32{\n  if val == 0 {\n    workgroupBarrier();\n "
                " }\n  return val;\n}\n"
                "\n@compute @workgroup_size(16, 1, 1)\nfn main(@builtin(local_invocation_id) lid : "
                "vec3<u32>,\n        @builtin(global_invocation_id) gid : vec3<u32>) {\n  var "
                "func_scalar : u32;\n  var func_vector : vec4u;\n  var func_array : array<u32, "
                "16>;\n  var func_struct : Outer;\n\n  ") +
            testcase.code + "\n";

        const std::string withCheck =
            code + "\n" + generatePointerCheck(testcase.check) + "\n}";

        if (testcase.needsDerefSugar) {
            skipIfLanguageFeatureNotSupported(t, "pointer_composite_access");
        }
        // Explicitly check false to distinguish from never.
        if (testcase.uniform == 0) {
            const std::string withoutCheck = code + "}\n";
            t.expectCompileResult(true, withoutCheck);
        }
        t.expectCompileResult(testcase.uniform == 1, withCheck);
    });

// ===========================================================================
// function_variables
// ===========================================================================
// uniform: "always" | "init" | "never". expectedUniformity per upstream.
bool expectedUniformity(const std::string& uniform, const std::string& init) {
    if (uniform == "always") {
        return true;
    } else if (uniform == "init") {
        return init == "no_init" || init == "uniform";
    }
    return false;  // "never" or unknown.
}

struct FuncVarCase {
    const char* name;
    const char* typeName;
    const char* typeDecl;
    const char* assignment;
    const char* cond;
    const char* uniform;
    const char* requiresFeature;  // nullptr if none.
};

const std::vector<FuncVarCase>& kFuncVarCases() {
    static const std::vector<FuncVarCase> cases = {
        {"no_assign", "u32", "", "", "x > 0", "init", nullptr},
        {"simple_uniform", "u32", "", "x = uniform_value[0];", "x > 0", "always", nullptr},
        {"simple_nonuniform", "u32", "", "x = nonuniform_value[0];", "x > 0", "never", nullptr},
        {"compound_assign_uniform", "u32", "", "x += uniform_value[0];", "x > 0", "init", nullptr},
        {"compound_assign_nonuniform", "u32", "", "x -= nonuniform_value[0];", "x > 0", "never",
         nullptr},
        {"unreachable_uniform", "u32", "", "loop {\n      break;\n      x = uniform_value[0];\n    }",
         "x > 0", "init", nullptr},
        {"unreachable_nonuniform", "u32", "",
         "loop {\n      break;\n      x = nonuniform_value[0];\n    }", "x > 0", "init", nullptr},
        {"if_no_else_uniform", "u32", "", "if uniform_cond {\n      x = uniform_value[0];\n    }",
         "x > 0", "init", nullptr},
        {"if_no_else_nonuniform", "u32", "",
         "if uniform_cond {\n      x = nonuniform_value[0];\n    }", "x > 0", "never", nullptr},
        {"if_no_then_uniform", "u32", "",
         "if uniform_cond {\n    } else {\n      x = uniform_value[0];\n    }", "x > 0", "init",
         nullptr},
        {"if_no_then_nonuniform", "u32", "",
         "if uniform_cond {\n    } else {\n      x = nonuniform_value[0];\n    }", "x > 0", "never",
         nullptr},
        {"if_else_uniform", "u32", "",
         "if uniform_cond {\n      x = uniform_value[0];\n    } else {\n      x = "
         "uniform_value[1];\n    }",
         "x > 0", "always", nullptr},
        {"if_else_nonuniform", "u32", "",
         "if uniform_cond {\n      x = nonuniform_value[0];\n    } else {\n      x = "
         "nonuniform_value[1];\n    }",
         "x > 0", "never", nullptr},
        {"if_else_split", "u32", "",
         "if uniform_cond {\n      x = uniform_value[0];\n    } else {\n      x = "
         "nonuniform_value[0];\n    }",
         "x > 0", "never", nullptr},
        {"if_unreachable_else_none", "u32", "", "if uniform_cond {\n    } else {\n      return;\n    }",
         "x > 0", "init", nullptr},
        {"if_unreachable_else_uniform", "u32", "",
         "if uniform_cond {\n      x = uniform_value[0];\n    } else {\n      return;\n    }",
         "x > 0", "always", nullptr},
        {"if_unreachable_else_nonuniform", "u32", "",
         "if uniform_cond {\n      x = nonuniform_value[0];\n    } else {\n      return;\n    }",
         "x > 0", "never", nullptr},
        {"if_unreachable_then_none", "u32", "", "if uniform_cond {\n      return;\n    }", "x > 0",
         "init", nullptr},
        {"if_unreachable_then_uniform", "u32", "",
         "if uniform_cond {\n      return;\n    } else {\n      x = uniform_value[0];\n    }",
         "x > 0", "always", nullptr},
        {"if_unreachable_then_nonuniform", "u32", "",
         "if uniform_cond {\n      return;\n    } else {\n      x = nonuniform_value[0];\n    }",
         "x > 0", "never", nullptr},
        {"if_nonescaping_nonuniform", "u32", "",
         "if uniform_cond {\n      x = nonuniform_value[0];\n      return;\n    }", "x > 0", "init",
         nullptr},
        {"loop_body_depends_on_continuing_uniform", "u32", "",
         "loop {\n      if x > 0 {\n        let tmp = textureSample(t, s, vec2f(0,0));\n      }\n   "
         "   continuing {\n        x = uniform_value[0];\n        break if uniform_cond;\n      "
         "}\n    }",
         "true", "init", nullptr},
        {"loop_body_depends_on_continuing_nonuniform", "u32", "",
         "loop {\n      if x > 0 {\n        let tmp = textureSample(t, s, vec2f(0,0));\n      }\n   "
         "   continuing {\n        x = nonuniform_value[0];\n        break if uniform_cond;\n      "
         "}\n    }",
         "true", "never", nullptr},
        {"loop_body_uniform", "u32", "",
         "loop {\n      x = uniform_value[0];\n      continuing {\n        break if "
         "uniform_cond;\n      }\n    }",
         "x > 0", "always", nullptr},
        {"loop_body_nonuniform", "u32", "",
         "loop {\n      x = nonuniform_value[0];\n      continuing {\n        break if "
         "uniform_cond;\n      }\n    }",
         "x > 0", "never", nullptr},
        {"loop_body_nonuniform_cond", "u32", "",
         "loop {\n      // The analysis doesn't recognize the content of the value.\n      x = "
         "uniform_value[0];\n      continuing {\n        break if nonuniform_cond;\n      }\n    }",
         "x > 0", "never", nullptr},
        {"loop_unreachable_continuing", "u32", "",
         "loop {\n      break;\n      continuing {\n        break if uniform_cond;\n      }\n    }",
         "x > 0", "init", nullptr},
        {"loop_continuing_from_body_uniform", "u32", "",
         "loop {\n      x = uniform_value[0];\n      continuing  {\n        if x > 0 {\n          "
         "let tmp = textureSample(t, s, vec2f(0,0));\n        }\n        break if uniform_cond;\n  "
         "    }\n    }",
         "true", "always", nullptr},
        {"loop_continuing_from_body_nonuniform", "u32", "",
         "loop {\n      x = nonuniform_value[0];\n      continuing  {\n        if x > 0 {\n        "
         "  let tmp = textureSample(t, s, vec2f(0,0));\n        }\n        break if "
         "uniform_cond;\n      }\n    }",
         "true", "never", nullptr},
        {"loop_continuing_from_body_split1", "u32", "",
         "loop {\n      if uniform_cond {\n        x = uniform_value[0];\n      }\n      continuing "
         "{\n        if x > 0 {\n          let tmp = textureSample(t, s, vec2f(0,0));\n        }\n  "
         "      break if uniform_cond;\n      }\n    }",
         "true", "init", nullptr},
        {"loop_continuing_from_body_split2", "u32", "",
         "loop {\n      if uniform_cond {\n        x = nonuniform_value[0];\n      }\n      "
         "continuing {\n        if x > 0 {\n          let tmp = textureSample(t, s, "
         "vec2f(0,0));\n        }\n        break if uniform_cond;\n      }\n    }",
         "true", "never", nullptr},
        {"loop_continuing_from_body_split3", "u32", "",
         "loop {\n      if uniform_cond {\n        x = uniform_value[0];\n      } else {\n        x "
         "= uniform_value[1];\n      }\n      continuing {\n        if x > 0 {\n          let tmp = "
         "textureSample(t, s, vec2f(0,0));\n        }\n        break if uniform_cond;\n      }\n    "
         "}",
         "true", "always", nullptr},
        {"loop_continuing_from_body_split4", "u32", "",
         "loop {\n      if nonuniform_cond {\n        x = uniform_value[0];\n      } else {\n      "
         "  x = uniform_value[1];\n      }\n      continuing {\n        if x > 0 {\n          let "
         "tmp = textureSample(t, s, vec2f(0,0));\n        }\n        break if uniform_cond;\n      "
         "}\n    }",
         "true", "never", nullptr},
        {"loop_continuing_from_body_split5", "u32", "",
         "loop {\n      if nonuniform_cond {\n        x = uniform_value[0];\n      } else {\n      "
         "  x = uniform_value[0];\n      }\n      continuing {\n        if x > 0 {\n          let "
         "tmp = textureSample(t, s, vec2f(0,0));\n        }\n        break if uniform_cond;\n      "
         "}\n    }",
         "true", "never", nullptr},
        {"loop_in_loop_with_continue_uniform", "u32", "",
         "loop {\n      loop {\n        x = nonuniform_value[0];\n        if nonuniform_cond {\n    "
         "      break;\n        }\n        continue;\n      }\n      x = uniform_value[0];\n      "
         "continuing {\n        if x > 0 {\n          let tmp = textureSample(t, s, "
         "vec2f(0,0));\n        }\n        break if uniform_cond;\n      }\n    }",
         "true", "always", nullptr},
        {"loop_in_loop_with_continue_nonuniform", "u32", "",
         "loop {\n      loop {\n        x = uniform_value[0];\n        if uniform_cond {\n          "
         "break;\n        }\n        continue;\n      }\n      x = nonuniform_value[0];\n      "
         "continuing {\n        if x > 0 {\n          let tmp = textureSample(t, s, "
         "vec2f(0,0));\n        }\n        break if uniform_cond;\n      }\n    }",
         "true", "never", nullptr},
        {"after_loop_with_uniform_break_uniform", "u32", "",
         "loop {\n      if uniform_cond {\n        x = uniform_value[0];\n        break;\n      "
         "}\n    }",
         "x > 0", "always", nullptr},
        {"after_loop_with_uniform_break_nonuniform", "u32", "",
         "loop {\n      if uniform_cond {\n        x = nonuniform_value[0];\n        break;\n      "
         "}\n    }",
         "x > 0", "never", nullptr},
        {"after_loop_with_nonuniform_break", "u32", "",
         "loop {\n      if nonuniform_cond {\n        x = uniform_value[0];\n        break;\n      "
         "}\n    }",
         "x > 0", "never", nullptr},
        {"after_loop_with_uniform_breaks", "u32", "",
         "loop {\n      if uniform_cond {\n        x = uniform_value[0];\n        break;\n      } "
         "else {\n        break;\n      }\n    }",
         "x > 0", "init", nullptr},
        {"switch_uniform_case", "u32", "",
         "switch uniform_val {\n      case 0 {\n        if x > 0 {\n          let tmp = "
         "textureSample(t, s, vec2f(0,0));\n        }\n      }\n      default {\n      }\n    }",
         "true", "init", nullptr},
        {"switch_nonuniform_case", "u32", "",
         "switch nonuniform_val {\n      case 0 {\n        if x > 0 {\n          let tmp = "
         "textureSample(t, s, vec2f(0,0));\n        }\n      }\n      default {\n      }\n    }",
         "true", "never", nullptr},
        {"after_switch_all_uniform", "u32", "",
         "switch uniform_val {\n      case 0 {\n        x = uniform_value[0];\n      }\n      case "
         "1,2 {\n        x = uniform_value[1];\n      }\n      default {\n        x = "
         "uniform_value[2];\n      }\n    }",
         "x > 0", "always", nullptr},
        {"after_switch_some_assign", "u32", "",
         "switch uniform_val {\n      case 0 {\n        x = uniform_value[0];\n      }\n      case "
         "1,2 {\n        x = uniform_value[1];\n      }\n      default {\n      }\n    }",
         "x > 0", "init", nullptr},
        {"after_switch_nonuniform", "u32", "",
         "switch uniform_val {\n      case 0 {\n        x = uniform_value[0];\n      }\n      case "
         "1,2 {\n        x = uniform_value[1];\n      }\n      default {\n        x = "
         "nonuniform_value[0];\n      }\n    }",
         "x > 0", "never", nullptr},
        {"after_switch_with_break_nonuniform1", "u32", "",
         "switch uniform_val {\n      default {\n        if uniform_cond {\n          x = "
         "uniform_value[0];\n          break;\n        }\n        x = nonuniform_value[0];\n      "
         "}\n    }",
         "x > 0", "never", nullptr},
        {"after_switch_with_break_nonuniform2", "u32", "",
         "switch uniform_val {\n      default {\n        x = uniform_value[0];\n        if "
         "uniform_cond {\n          x = nonuniform_value[0];\n          break;\n        }\n      "
         "}\n    }",
         "x > 0", "never", nullptr},
        {"for_loop_uniform_body", "u32", "",
         "for (var i = 0; i < 10; i += 1) {\n      x = uniform_value[0];\n    }", "x > 0", "init",
         nullptr},
        {"for_loop_nonuniform_body", "u32", "",
         "for (var i = 0; i < 10; i += 1) {\n      x = nonuniform_value[0];\n    }", "x > 0",
         "never", nullptr},
        {"for_loop_uniform_body_no_condition", "u32", "",
         "for (var i = 0; ; i += 1) {\n      x = uniform_value[0];\n      if uniform_cond {\n      "
         "  break;\n      }\n    }",
         "x > 0", "always", nullptr},
        {"for_loop_nonuniform_body_no_condition", "u32", "",
         "for (var i = 0; ; i += 1) {\n      x = nonuniform_value[0];\n      if uniform_cond {\n   "
         "     break;\n      }\n    }",
         "x > 0", "never", nullptr},
        {"for_loop_uniform_increment", "u32", "",
         "for (; uniform_cond; x += uniform_value[0]) {\n    }", "x > 0", "init", nullptr},
        {"for_loop_nonuniform_increment", "u32", "",
         "for (; uniform_cond; x += nonuniform_value[0]) {\n    }", "x > 0", "never", nullptr},
        {"for_loop_uniform_init", "u32", "", "for (x = uniform_value[0]; uniform_cond; ) {\n    }",
         "x > 0", "always", nullptr},
        {"for_loop_nonuniform_init", "u32", "",
         "for (x = nonuniform_value[0]; uniform_cond;) {\n    }", "x > 0", "never", nullptr},
        {"while_loop_uniform_body", "u32", "", "while uniform_cond {\n      x = uniform_value[0];\n    }",
         "x > 0", "init", nullptr},
        {"while_loop_nonuniform_body", "u32", "",
         "while uniform_cond {\n      x = nonuniform_value[0];\n    }", "x > 0", "never", nullptr},
        {"partial_assignment_uniform", "block", "struct block {\n      x : u32,\n      y : u32\n    }",
         "x.x = uniform_value[0].x;", "x.x > 0", "init", nullptr},
        {"partial_assignment_nonuniform", "block",
         "struct block {\n      x : u32,\n      y : u32\n    }", "x.x = nonuniform_value[0].x;",
         "x.x > 0", "never", nullptr},
        {"partial_assignment_all_members_uniform", "block",
         "struct block {\n      x : u32,\n      y : u32\n    }",
         "x.x = uniform_value[0].x;\n    x.y = uniform_value[1].y;", "x.x > 0", "init", nullptr},
        {"partial_assignment_all_members_nonuniform", "block",
         "struct block {\n      x : u32,\n      y : u32\n    }",
         "x.x = nonuniform_value[0].x;\n    x.y = uniform_value[0].x;", "x.x > 0", "never",
         nullptr},
        {"partial_assignment_single_element_struct_uniform", "block",
         "struct block {\n      x : u32\n    }", "x.x = uniform_value[0].x;", "x.x > 0", "init",
         nullptr},
        {"partial_assignment_single_element_struct_nonuniform", "block",
         "struct block {\n      x : u32\n    }", "x.x = nonuniform_value[0].x;", "x.x > 0", "never",
         nullptr},
        {"partial_assignment_single_element_array_uniform", "array<u32, 1>", "",
         "x[0] = uniform_value[0][0];", "x[0] > 0", "init", nullptr},
        {"partial_assignment_single_element_array_nonuniform", "array<u32, 1>", "",
         "x[0] = nonuniform_value[0][0];", "x[0] > 0", "never", nullptr},
        {"nested1", "block", "struct block {\n      x : u32,\n      y : u32\n    }",
         "for (; uniform_cond; ) {\n      if uniform_cond {\n        x = uniform_value[0];\n       "
         " break;\n        x.y = nonuniform_value[0].y;\n      } else {\n        if uniform_cond "
         "{\n          continue;\n        }\n        x = uniform_value[1];\n      }\n    }",
         "x.x > 0", "init", nullptr},
        {"nested2", "block", "struct block {\n      x : u32,\n      y : u32\n    }",
         "for (; uniform_cond; ) {\n      if uniform_cond {\n        x = uniform_value[0];\n       "
         " break;\n        x.y = nonuniform_value[0].y;\n      } else {\n        if nonuniform_cond "
         "{\n          continue;\n        }\n        x = uniform_value[1];\n      }\n    }",
         "x.x > 0", "never", nullptr},
        {"full_assignment_vec_uniform", "vec3u", "", "x = uniform_value[0];", "x.x > 0", "always",
         nullptr},
        {"full_assignment_vec_nonuniform", "vec3u", "", "x = nonuniform_value[0];", "x.x > 0",
         "never", nullptr},
        {"partial_assignment_vec_uniform", "vec3u", "", "x.x = uniform_value[0].x;", "x.x > 0",
         "init", nullptr},
        {"partial_assignment_vec_nonuniform", "vec3u", "", "x.x = nonuniform_value[0].x;",
         "x.x > 0", "never", nullptr},
        {"partial_assignment_vec_all_components_uniform", "vec3u", "",
         "x.x = uniform_value[0].x;\n    x.y = uniform_value[0].y;\n    x.z = uniform_value[0].z;",
         "x.x > 0", "init", nullptr},
        {"partial_assignment_vec_all_components_nonuniform", "vec3u", "",
         "x.x = nonuniform_value[0].x;\n    x.y = uniform_value[0].y;\n    x.z = "
         "uniform_value[0].z;",
         "x.x > 0", "never", nullptr},
        {"full_swizzle_assignment_uniform", "vec3u", "", "x.xyz = uniform_value[0].xyz;", "x.x > 0",
         "always", "swizzle_assignment"},
        {"full_swizzle_assignment_nonuniform", "vec3u", "", "x.xyz = nonuniform_value[0].xyz;",
         "x.x > 0", "never", "swizzle_assignment"},
        {"full_swizzle_assignment_permutation_uniform", "vec3u", "", "x.zyx = uniform_value[0].xyz;",
         "x.x > 0", "always", "swizzle_assignment"},
        {"full_swizzle_assignment_permutation_nonuniform", "vec3u", "",
         "x.zyx = nonuniform_value[0].xyz;", "x.x > 0", "never", "swizzle_assignment"},
        {"partial_swizzle_assignment_uniform", "vec3u", "", "x.xy = uniform_value[0].xy;", "x.x > 0",
         "init", "swizzle_assignment"},
        {"partial_swizzle_assignment_nonuniform", "vec3u", "", "x.xy = nonuniform_value[0].xy;",
         "x.x > 0", "never", "swizzle_assignment"},
        {"chained_full_swizzle_assignment_uniform", "vec3u", "", "x.xyz.zyx = uniform_value[0].xyz;",
         "x.x > 0", "always", "swizzle_assignment"},
        {"chained_partial_swizzle_assignment_uniform", "vec3u", "",
         "x.xyz.xy = uniform_value[0].xy;", "x.x > 0", "init", "swizzle_assignment"},
        {"chained_full_swizzle_assignment_nonuniform", "vec3u", "",
         "x.xyz.zyx = nonuniform_value[0].xyz;", "x.x > 0", "never", "swizzle_assignment"},
    };
    return cases;
}

const FuncVarCase& findFuncVarCase(const std::string& name) {
    for (const FuncVarCase& c : kFuncVarCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const FuncVarCase dummy{"", "u32", "", "", "x > 0", "never", nullptr};
    return dummy;
}

// kVarInit (ordered: no_init, uniform, nonuniform)
std::string varInitCode(const std::string& init) {
    if (init == "no_init") {
        return "";
    } else if (init == "uniform") {
        return "= uniform_value[3];";
    } else if (init == "nonuniform") {
        return "= nonuniform_value[3];";
    }
    return "";
}

CTS_TEST(g, "function_variables")
    .desc("Test uniformity of function variables")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const FuncVarCase& c : kFuncVarCases()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names).combine("init", {"no_init", "uniform", "nonuniform"});
    })
    .fn([](ShaderValidationTest& t) {
        const FuncVarCase& funcCase = findFuncVarCase(t.param<std::string>("case"));
        const std::string init = t.param<std::string>("init");
        if (funcCase.requiresFeature != nullptr) {
            skipIfLanguageFeatureNotSupported(t, funcCase.requiresFeature);
        }
        const std::string code =
            std::string("\n") + funcCase.typeDecl +
            "\n\n@group(0) @binding(0)\nvar<storage> uniform_value : array<" + funcCase.typeName +
            ", 4>;\n@group(0) @binding(1)\nvar<storage, read_write> nonuniform_value : array<" +
            funcCase.typeName +
            ", 4>;\n\n@group(1) @binding(0)\nvar t : texture_2d<f32>;\n@group(1) "
            "@binding(1)\nvar s : sampler;\n\nvar<private> nonuniform_cond : bool = "
            "true;\nconst uniform_cond : bool = true;\nvar<private> nonuniform_val : u32 = "
            "0;\nconst uniform_val : u32 = 0;\n\n@fragment\nfn main() {\n  var x : " +
            funcCase.typeName + " " + varInitCode(init) + ";\n\n  " + funcCase.assignment +
            "\n\n  if " + funcCase.cond +
            " {\n    let tmp = textureSample(t, s, vec2f(0,0));\n  }\n}\n";

        const bool result = expectedUniformity(funcCase.uniform, init);
        if (!result) {
            t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        }
        t.expectCompileResult(result, code);
    });

// ===========================================================================
// short_circuit_expressions
// ===========================================================================
struct ShortCircuitCase {
    const char* name;
    const char* code;
    bool uniform;
};
const std::vector<ShortCircuitCase>& kShortCircuitExpressionCases() {
    static const std::vector<ShortCircuitCase> cases = {
        {"or_uniform_uniform",
         "\n      let x = uniform_cond || uniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         true},
        {"or_uniform_nonuniform",
         "\n      let x = uniform_cond || nonuniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         false},
        {"or_nonuniform_uniform",
         "\n      let x = nonuniform_cond || uniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         false},
        {"or_nonuniform_nonuniform",
         "\n      let x = nonuniform_cond || nonuniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         false},
        {"or_uniform_first_nonuniform",
         "\n      let x = textureSample(t, s, vec2f(0,0)).x == 0 || nonuniform_cond;\n    ", true},
        {"or_uniform_second_nonuniform",
         "\n      let x = nonuniform_cond || textureSample(t, s, vec2f(0,0)).x == 0;\n    ", false},
        {"and_uniform_uniform",
         "\n      let x = uniform_cond && uniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         true},
        {"and_uniform_nonuniform",
         "\n      let x = uniform_cond && nonuniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         false},
        {"and_nonuniform_uniform",
         "\n      let x = nonuniform_cond && uniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         false},
        {"and_nonuniform_nonuniform",
         "\n      let x = nonuniform_cond && nonuniform_cond;\n      if x {\n        let tmp = "
         "textureSample(t, s, vec2f(0,0));\n      }\n    ",
         false},
        {"and_uniform_first_nonuniform",
         "\n      let x = textureSample(t, s, vec2f(0,0)).x == 0 && nonuniform_cond;\n    ", true},
        {"and_uniform_second_nonuniform",
         "\n      let x = nonuniform_cond && textureSample(t, s, vec2f(0,0)).x == 0;\n    ", false},
    };
    return cases;
}

const ShortCircuitCase& findShortCircuitCase(const std::string& name) {
    for (const ShortCircuitCase& c : kShortCircuitExpressionCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ShortCircuitCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "short_circuit_expressions")
    .desc("Test uniformity of expressions")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const ShortCircuitCase& c : kShortCircuitExpressionCases()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names);
    })
    .fn([](ShaderValidationTest& t) {
        const ShortCircuitCase& testcase = findShortCircuitCase(t.param<std::string>("case"));
        const std::string code =
            std::string(
                "\n@group(1) @binding(0)\nvar t : texture_2d<f32>;\n@group(1) @binding(1)\nvar s : "
                "sampler;\n\nconst uniform_cond = true;\nvar<private> nonuniform_cond = "
                "false;\n\n@fragment\nfn main() {\n  ") +
            testcase.code + "\n}\n";

        const bool res = testcase.uniform;
        if (!res) {
            t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        }
        t.expectCompileResult(res, code);
    });

// ===========================================================================
// binary_expressions / unary_expressions (shared kExpressionCases)
// ===========================================================================
struct ExpressionCase {
    const char* name;
    const char* code;
    bool uniform;
};
const std::vector<ExpressionCase>& kExpressionCases() {
    static const std::vector<ExpressionCase> cases = {
        {"literal", "1u", true},
        {"uniform", "uniform_val", true},
        {"nonuniform", "nonuniform_val", false},
        {"uniform_index", "uniform_value[uniform_val]", true},
        {"nonuniform_index1", "uniform_value[nonuniform_val]", false},
        {"nonuniform_index2", "nonuniform_value[uniform_val]", false},
        {"uniform_struct", "uniform_struct.x", true},
        {"nonuniform_struct", "nonuniform_struct.x", false},
    };
    return cases;
}

const ExpressionCase& findExpressionCase(const std::string& name) {
    for (const ExpressionCase& c : kExpressionCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ExpressionCase dummy{"", "1u", true};
    return dummy;
}

std::vector<Value> expressionCaseNames() {
    std::vector<Value> names;
    for (const ExpressionCase& c : kExpressionCases()) {
        names.emplace_back(std::string(c.name));
    }
    return names;
}

struct BinOp {
    const char* name;
    const char* code;
    const char* test;
};
const std::vector<BinOp>& kBinOps() {
    static const std::vector<BinOp> ops = {
        {"plus", "+", "> 0"},  {"minus", "-", "> 0"},     {"times", "*", "> 0"},
        {"div", "/", "> 0"},   {"rem", "%", "> 0"},       {"and", "&", "> 0"},
        {"or", "|", "> 0"},    {"xor", "^", "> 0"},       {"shl", "<<", "> 0"},
        {"shr", ">>", "> 0"},  {"less", "<", ""},         {"lessequal", "<=", ""},
        {"greater", ">", ""},  {"greaterequal", ">=", ""}, {"equal", "==", ""},
        {"notequal", "!=", ""},
    };
    return ops;
}

const BinOp& findBinOp(const std::string& name) {
    for (const BinOp& o : kBinOps()) {
        if (name == o.name) {
            return o;
        }
    }
    static const BinOp dummy{"", "+", "> 0"};
    return dummy;
}

CTS_TEST(g, "binary_expressions")
    .desc("Test uniformity of binary expressions")
    .params([](ParamsBuilder u) {
        std::vector<Value> binOpNames;
        for (const BinOp& o : kBinOps()) {
            binOpNames.emplace_back(std::string(o.name));
        }
        return u.combine("e1", expressionCaseNames())
            .combine("e2", expressionCaseNames())
            .beginSubcases()
            .combine("op", binOpNames);
    })
    .fn([](ShaderValidationTest& t) {
        const ExpressionCase& e1 = findExpressionCase(t.param<std::string>("e1"));
        const ExpressionCase& e2 = findExpressionCase(t.param<std::string>("e2"));
        const BinOp& op = findBinOp(t.param<std::string>("op"));
        const std::string code =
            std::string(
                "\n@group(0) @binding(0)\nvar t : texture_2d<f32>;\n@group(0) @binding(1)\nvar s : "
                "sampler;\n\nstruct S {\n  x : u32\n}\n\nconst uniform_struct = "
                "S(1);\nvar<private> nonuniform_struct = S(1);\n\nconst uniform_value : array<u32, "
                "2> = array(1,1);\nvar<private> nonuniform_value : array<u32, 2> = "
                "array(1,1);\n\nconst uniform_val : u32 = 1;\nvar<private> nonuniform_val : u32 = "
                "1;\n\n@fragment\nfn main() {\n  let tmp = ") +
            e1.code + " " + op.code + " " + e2.code + ";\n  if tmp " + op.test +
            " {\n    let res = textureSample(t, s, vec2f(0,0));\n  }\n}\n";

        const bool res = e1.uniform && e2.uniform;
        if (!res) {
            t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        }
        t.expectCompileResult(res, code);
    });

CTS_TEST(g, "unary_expressions")
    .desc("Test uniformity of uniary expressions")
    .params([](ParamsBuilder u) {
        return u.combine("e", expressionCaseNames())
            .combine("op", {"!b_tmp", "~i_tmp > 0", "-i32(i_tmp) > 0"});
    })
    .fn([](ShaderValidationTest& t) {
        const ExpressionCase& e = findExpressionCase(t.param<std::string>("e"));
        const std::string op = t.param<std::string>("op");
        const std::string code =
            std::string(
                "\n@group(0) @binding(0)\nvar t : texture_2d<f32>;\n@group(0) @binding(1)\nvar s : "
                "sampler;\n\nstruct S {\n  x : i32\n}\n\nconst uniform_struct = "
                "S(1);\nvar<private> nonuniform_struct = S(1);\n\nconst uniform_value : array<i32, "
                "2> = array(1,1);\nvar<private> nonuniform_value : array<i32, 2> = "
                "array(1,1);\n\nconst uniform_val : i32 = 1;\nvar<private> nonuniform_val : i32 = "
                "1;\n\n@fragment\nfn main() {\n  let i_tmp = ") +
            e.code + ";\n  let b_tmp = bool(i_tmp);\n  let tmp = " + op +
            ";\n  if tmp {\n    let res = textureSample(t, s, vec2f(0,0));\n  }\n}\n";

        const bool res = e.uniform;
        if (!res) {
            t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        }
        t.expectCompileResult(res, code);
    });

// ===========================================================================
// function_pointer_parameters
// ===========================================================================
struct PointerParamCase {
    const char* name;
    const char* function;
    const char* call;
    const char* cond;
    bool uniform;
};
const std::vector<PointerParamCase>& kPointerParamCases() {
    static const std::vector<PointerParamCase> cases = {
        {"pointer_uniform_passthrough_value",
         "fn foo(p : ptr<function, u32>) -> u32 {\n      return *p;\n    }",
         "var x = uniform_values[0];\n    let call = foo(&x);", "x > 0", true},
        {"pointer_nonuniform_passthrough_value",
         "fn foo(p : ptr<function, u32>) -> u32 {\n      return *p;\n    }",
         "var x = uniform_values[0];\n    let call = foo(&x);", "x > 0", true},
        {"pointer_store_uniform_value",
         "fn foo(p : ptr<function, u32>) {\n      *p = uniform_values[0];\n    }",
         "var x = nonuniform_values[0];\n    foo(&x);", "x > 0", true},
        {"pointer_store_nonuniform_value",
         "fn foo(p : ptr<function, u32>) {\n      *p = nonuniform_values[0];\n    }",
         "var x = uniform_values[0];\n    foo(&x);", "x > 0", false},
        {"pointer_depends_on_nonpointer_param_uniform",
         "fn foo(p : ptr<function, u32>, x : u32) {\n      *p = x;\n    }",
         "var x = nonuniform_values[0];\n    foo(&x, uniform_values[0]);", "x > 0", true},
        {"pointer_depends_on_nonpointer_param_nonuniform",
         "fn foo(p : ptr<function, u32>, x : u32) {\n      *p = x;\n    }",
         "var x = uniform_values[0];\n    foo(&x, nonuniform_values[0]);", "x > 0", false},
        {"pointer_depends_on_pointer_param_uniform",
         "fn foo(p : ptr<function, u32>, q : ptr<function, u32>) {\n      *p = *q;\n    }",
         "var x = nonuniform_values[0];\n    var y = uniform_values[0];\n    foo(&x, &y);", "x > 0",
         true},
        {"pointer_depends_on_pointer_param_nonuniform",
         "fn foo(p : ptr<function, u32>, q : ptr<function, u32>) {\n      *p = *q;\n    }",
         "var x = uniform_values[0];\n    var y = nonuniform_values[0];\n    foo(&x, &y);", "x > 0",
         false},
        {"pointer_codependent1",
         "fn foo(p : ptr<function, u32>, q : ptr<function, u32>) {\n      if *p > 0 {\n        *p = "
         "*q;\n      } else {\n        *q++;\n      }\n    }",
         "var x = uniform_values[0];\n    var y = uniform_values[1];\n    foo(&x, &y);\n    let a = "
         "x + y;",
         "a > 0", true},
        {"pointer_codependent2",
         "fn foo(p : ptr<function, u32>, q : ptr<function, u32>) {\n      if *p > 0 {\n        *p = "
         "*q;\n      } else {\n        *q++;\n      }\n    }",
         "var x = uniform_values[0];\n    var y = nonuniform_values[1];\n    foo(&x, &y);\n    let "
         "a = x + y;",
         "a > 0", false},
        {"pointer_codependent3",
         "fn foo(p : ptr<function, u32>, q : ptr<function, u32>) {\n      if *p > 0 {\n        *p = "
         "*q;\n      } else {\n        *q++;\n      }\n    }",
         "var x = nonuniform_values[0];\n    var y = uniform_values[1];\n    foo(&x, &y);\n    let "
         "a = x + y;",
         "a > 0", false},
        {"pointer_codependent4",
         "fn foo(p : ptr<function, u32>, q : ptr<function, u32>) {\n      if *p > 0 {\n        *p = "
         "*q;\n      } else {\n        *q++;\n      }\n    }",
         "var x = nonuniform_values[0];\n    var y = nonuniform_values[1];\n    foo(&x, &y);\n    "
         "let a = x + y;",
         "a > 0", false},
        {"uniform_param_uniform_assignment",
         "fn foo(p : ptr<function, array<u32, 2>>, idx : u32) {\n      (*p)[idx] = "
         "uniform_values[0];\n    }",
         "var x = array(uniform_values[0], uniform_values[1]);\n    foo(&x, uniform_values[3]);",
         "x[0] > 0", true},
        {"uniform_param_nonuniform_assignment",
         "fn foo(p : ptr<function, array<u32, 2>>, idx : u32) {\n      (*p)[idx] = "
         "nonuniform_values[0];\n    }",
         "var x = array(uniform_values[0], uniform_values[1]);\n    foo(&x, uniform_values[3]);",
         "x[0] > 0", false},
        {"nonuniform_param_uniform_assignment",
         "fn foo(p : ptr<function, array<u32, 2>>, idx : u32) {\n      (*p)[idx] = "
         "uniform_values[0];\n    }",
         "var x = array(uniform_values[0], uniform_values[1]);\n    foo(&x, u32(clamp(pos.x, 0, "
         "1)));",
         "x[0] > 0", false},
        {"nonuniform_param_nonuniform_assignment",
         "fn foo(p : ptr<function, array<u32, 2>>, idx : u32) {\n      (*p)[idx] = "
         "nonuniform_values[0];\n    }",
         "var x = array(uniform_values[0], uniform_values[1]);\n    foo(&x, u32(clamp(pos.x, 0, "
         "1)));",
         "x[0] > 0", false},
        {"required_uniform_success",
         "fn foo(p : ptr<function, u32>) {\n      if *p > 0 {\n        let tmp = "
         "textureSample(t,s,vec2f(0,0));\n      }\n    }",
         "var x = uniform_values[0];\n    foo(&x);", "uniform_cond", true},
        {"required_uniform_failure",
         "fn foo(p : ptr<function, u32>) {\n      if *p > 0 {\n        let tmp = "
         "textureSample(t,s,vec2f(0,0));\n      }\n    }",
         "var x = nonuniform_values[0];\n    foo(&x);", "uniform_cond", false},
        {"uniform_conditional_call_assign_uniform",
         "fn foo(p : ptr<function, u32>) {\n      *p = uniform_values[0];\n    }",
         "var x = uniform_values[1];\n    if uniform_cond {\n      foo(&x);\n    }", "x > 0", true},
        {"uniform_conditional_call_assign_nonuniform1",
         "fn foo(p : ptr<function, u32>) {\n      *p = nonuniform_values[0];\n    }",
         "var x = uniform_values[1];\n    if uniform_cond {\n      foo(&x);\n    }", "x > 0", false},
        {"uniform_conditional_call_assign_nonuniform2",
         "fn foo(p : ptr<function, u32>) {\n      *p = uniform_values[0];\n    }",
         "var x = nonuniform_values[1];\n    if uniform_cond {\n      foo(&x);\n    }", "x > 0",
         false},
        {"nonuniform_conditional_call_assign_uniform",
         "fn foo(p : ptr<function, u32>) {\n      *p = uniform_values[0];\n    }",
         "var x = uniform_values[1];\n    if nonuniform_cond {\n      foo(&x);\n    }", "x > 0",
         false},
    };
    return cases;
}

const PointerParamCase& findPointerParamCase(const std::string& name) {
    for (const PointerParamCase& c : kPointerParamCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const PointerParamCase dummy{"", "", "", "x > 0", false};
    return dummy;
}

CTS_TEST(g, "function_pointer_parameters")
    .desc("Test functions and calls with pointer parameters")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const PointerParamCase& c : kPointerParamCases()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names);
    })
    .fn([](ShaderValidationTest& t) {
        const PointerParamCase& pc = findPointerParamCase(t.param<std::string>("case"));
        const std::string code =
            std::string(
                "\n@group(0) @binding(0)\nvar t : texture_2d<f32>;\n@group(0) @binding(1)\nvar s : "
                "sampler;\n\nconst uniform_cond = true;\nvar<private> nonuniform_cond = "
                "true;\n\n@group(1) @binding(0)\nvar<storage> uniform_values : array<u32, "
                "4>;\n@group(1) @binding(1)\nvar<storage, read_write> nonuniform_values : "
                "array<u32, 4>;\n\n") +
            pc.function +
            "\n\n@fragment\nfn main(@builtin(position) pos : vec4f) {\n  " + pc.call + "\n\n  if " +
            pc.cond + " {\n    let tmp = textureSample(t,s,vec2f(0,0));\n  }\n}\n";

        const bool res = pc.uniform;
        if (!res) {
            t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        }
        t.expectCompileResult(res, code);
    });

// ===========================================================================
// functions
// ===========================================================================
struct FunctionCase {
    const char* name;
    const char* function;
    const char* call;
    const char* cond;
    bool uniform;
};
const std::vector<FunctionCase>& kFunctionCases() {
    static const std::vector<FunctionCase> cases = {
        {"uniform_result", "fn foo() -> u32 {\n      return uniform_values[0];\n    }",
         "let call = foo();", "call > 0", true},
        {"nonuniform_result", "fn foo() -> u32 {\n      return nonuniform_values[0];\n    }",
         "let call = foo();", "call > 0", false},
        {"nonuniform_return_is_uniform_after_call",
         "fn foo() {\n      if nonuniform_values[0] > 0 {\n        return;\n      } else {\n        "
         "return;\n      }\n    }",
         "foo();", "uniform_cond", true},
        {"uniform_passthrough_parameter", "fn foo(x : u32) -> u32 {\n      return x;\n    }",
         "let call = foo(uniform_values[0]);", "call > 0", true},
        {"nonuniform_passthrough_parameter", "fn foo(x : u32) -> u32 {\n      return x;\n    }",
         "let call = foo(nonuniform_values[0]);", "call > 0", false},
        {"combined_parameters1", "fn foo(x : u32, y : u32) -> u32 {\n      return x + y;\n    }",
         "let call = foo(uniform_values[0], uniform_values[1]);", "call > 0", true},
        {"combined_parameters2", "fn foo(x : u32, y : u32) -> u32 {\n      return x + y;\n    }",
         "let call = foo(nonuniform_values[0], uniform_values[1]);", "call > 0", false},
        {"combined_parameters3", "fn foo(x : u32, y : u32) -> u32 {\n      return x + y;\n    }",
         "let call = foo(uniform_values[0], nonuniform_values[1]);", "call > 0", false},
        {"combined_parameters4", "fn foo(x : u32, y : u32) -> u32 {\n      return x + y;\n    }",
         "let call = foo(nonuniform_values[0], nonuniform_values[1]);", "call > 0", false},
        {"uniform_parameter_cf_after_nonuniform_expr",
         "fn foo(x : bool, y : vec4f) -> f32 {\n      return select(0, y.x, x);\n    }",
         "let call = foo(nonuniform_cond || uniform_cond, textureSample(t,s,vec2f(0,0)));",
         "uniform_cond", true},
        {"required_uniform_function_call_in_uniform_cf",
         "fn foo() -> vec4f {\n      return textureSample(t,s,vec2f(0,0));\n    }",
         "if uniform_cond {\n      let call = foo();\n    }", "uniform_cond", true},
        {"required_uniform_function_call_in_nonuniform_cf",
         "fn foo() -> vec4f {\n      return textureSample(t,s,vec2f(0,0));\n    }",
         "if nonuniform_cond {\n      let call = foo();\n    }", "uniform_cond", false},
        {"required_uniform_function_call_in_nonuniform_cf2",
         "@diagnostic(warning, derivative_uniformity)\n    fn foo() -> vec4f {\n      return "
         "textureSample(t,s,vec2f(0,0));\n    }",
         "if nonuniform_cond {\n      let call = foo();\n      let sample = "
         "textureSample(t,s,vec2f(0,0));\n    }",
         "uniform_cond", false},
        {"required_uniform_function_call_depends_on_uniform_param",
         "fn foo(x : bool) -> vec4f {\n      if x {\n        return "
         "textureSample(t,s,vec2f(0,0));\n      }\n      return vec4f(0);\n    }",
         "let call = foo(uniform_cond);", "uniform_cond", true},
        {"required_uniform_function_call_depends_on_nonuniform_param",
         "fn foo(x : bool) -> vec4f {\n      if x {\n        return "
         "textureSample(t,s,vec2f(0,0));\n      }\n      return vec4f(0);\n    }",
         "let call = foo(nonuniform_cond);", "uniform_cond", false},
        {"dpdx_nonuniform_result", "", "let call = dpdx(1);", "call > 0", false},
        {"dpdy_nonuniform_result", "", "let call = dpdy(1);", "call > 0", false},
        {"dpdxCoarse_nonuniform_result", "", "let call = dpdxCoarse(1);", "call > 0", false},
        {"dpdyCoarse_nonuniform_result", "", "let call = dpdyCoarse(1);", "call > 0", false},
        {"dpdxFine_nonuniform_result", "", "let call = dpdxFine(1);", "call > 0", false},
        {"dpdyFine_nonuniform_result", "", "let call = dpdyFine(1);", "call > 0", false},
        {"fwidth_nonuniform_result", "", "let call = fwidth(1);", "call > 0", false},
        {"fwidthCoarse_nonuniform_result", "", "let call = fwidthCoarse(1);", "call > 0", false},
        {"fwidthFine_nonuniform_result", "", "let call = fwidthFine(1);", "call > 0", false},
        {"textureSample_nonuniform_result", "", "let call = textureSample(t,s,vec2f(0,0));",
         "call.x > 0", false},
        {"textureSampleBias_nonuniform_result", "",
         "let call = textureSampleBias(t,s,vec2f(0,0), 0);", "call.x > 0", false},
        {"textureSampleCompare_nonuniform_result", "",
         "let call = textureSampleCompare(td,sd,vec2f(0,0), 0);", "call > 0", false},
        {"textureDimensions_uniform_input_uniform_result", "", "let call = textureDimensions(t);",
         "call.x > 0", true},
        {"textureGather_uniform_input_uniform_result", "",
         "let call = textureGather(0,t,s,vec2f(0,0));", "call.x > 0", true},
        {"textureGatherCompare_uniform_input_uniform_result", "",
         "let call = textureGatherCompare(td,sd,vec2f(0,0), 0);", "call.x > 0", true},
        {"textureLoad_uniform_input_uniform_result", "", "let call = textureLoad(t,vec2u(0,0),0);",
         "call.x > 0", true},
        {"textureNumLayers_uniform_input_uniform_result", "", "let call = textureNumLayers(ta);",
         "call > 0", true},
        {"textureNumLevels_uniform_input_uniform_result", "", "let call = textureNumLevels(t);",
         "call > 0", true},
        {"textureNumSamples_uniform_input_uniform_result", "", "let call = textureNumSamples(ts);",
         "call > 0", true},
        {"textureSampleLevel_uniform_input_uniform_result", "",
         "let call = textureSampleLevel(t,s,vec2f(0,0),0);", "call.x > 0", true},
        {"textureSampleGrad_uniform_input_uniform_result", "",
         "let call = textureSampleGrad(t,s,vec2f(0,0),vec2f(0,0),vec2f(0,0));", "call.x > 0", true},
        {"textureSampleCompareLevel_uniform_input_uniform_result", "",
         "let call = textureSampleCompareLevel(td,sd,vec2f(0,0), 0);", "call > 0", true},
        {"textureSampleBaseClampToEdge_uniform_input_uniform_result", "",
         "let call = textureSampleBaseClampToEdge(t,s,vec2f(0,0));", "call.x > 0", true},
        {"min_uniform_input_uniform_result", "", "let call = min(0,0);", "call > 0", true},
        {"value_constructor_uniform_input_uniform_result", "", "let call = vec2u(0,0);",
         "call.x > 0", true},
    };
    return cases;
}

const FunctionCase& findFunctionCase(const std::string& name) {
    for (const FunctionCase& c : kFunctionCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const FunctionCase dummy{"", "", "", "call > 0", false};
    return dummy;
}

CTS_TEST(g, "functions")
    .desc("Test uniformity of function calls (non-pointer parameters)")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const FunctionCase& c : kFunctionCases()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names);
    })
    .fn([](ShaderValidationTest& t) {
        const FunctionCase& fc = findFunctionCase(t.param<std::string>("case"));
        const std::string code =
            std::string(
                "\n@group(0) @binding(0)\nvar t : texture_2d<f32>;\n@group(0) @binding(1)\nvar s : "
                "sampler;\n@group(0) @binding(2)\nvar td : texture_depth_2d;\n@group(0) "
                "@binding(3)\nvar sd : sampler_comparison;\n@group(0) @binding(4)\nvar ta : "
                "texture_2d_array<f32>;\n@group(0) @binding(5)\nvar ts : "
                "texture_multisampled_2d<f32>;\n\nconst uniform_cond = true;\nvar<private> "
                "nonuniform_cond = true;\n\n@group(1) @binding(0)\nvar<storage> uniform_values : "
                "array<u32, 4>;\n@group(1) @binding(1)\nvar<storage, read_write> nonuniform_values "
                ": array<u32, 4>;\n\n") +
            fc.function + "\n\n@fragment\nfn main() {\n  " + fc.call + "\n\n  if " + fc.cond +
            " {\n    let tmp = textureSample(t,s,vec2f(0,0));\n  }\n}\n";

        const bool res = fc.uniform;
        if (!res) {
            t.expectCompileResult(true, "diagnostic(off, derivative_uniformity);\n" + code);
        }
        t.expectCompileResult(res, code);
    });

// ===========================================================================
// subgroups,parameters
// ===========================================================================
CTS_TEST(g, "subgroups,parameters")
    .desc("Test subgroup operations that require a uniform parameter")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"subgroupShuffleUp", "subgroupShuffleDown", "subgroupShuffleXor"})
            .combine("uniform", {false, true});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const bool uniform = t.param<bool>("uniform");
        const std::string wgsl =
            "\nenable subgroups;\n\nvar<private> non_uniform : u32 = 0;\n\n@group(0) "
            "@binding(0)\nvar<storage> uniform : u32;\n\n@compute @workgroup_size(16,1,1)\nfn "
            "main() {\n  let x = " +
            op + "(non_uniform, " + (uniform ? "uniform" : "non_uniform") + ");\n}";

        t.expectCompileResult(uniform, wgsl);
    });

}  // namespace
