// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/atomicStoreMinMax.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,atomicStoreMinMax",
    "Validation tests for atomicStoreMin and atomicStoreMax builtins.");

// keysOf(kAtomicOps)
std::vector<Value> kAtomicOps() {
    return {Value("atomicStoreMin"), Value("atomicStoreMax")};
}

std::string atomicOp(const std::string& op, const std::string& a, const std::string& v) {
    return op + "(" + a + ", " + v + ")";
}

// expectCompileResult skips a shader that enables a device feature the device lacks, but
// 'atomic-vec2u-min-max' has no enumerator in wgpu-native's webgpu.h, so the skip cannot see it there.
void expectAtomicCompileResult(ShaderValidationTest& t, bool expected, const std::string& code) {
#if !defined(CTS_BACKEND_DAWN) && !defined(CTS_BACKEND_YAWGPU)
    if (cts::shader_validation::codeEnables(code, "atomic_vec2u_min_max")) {
        t.skip("atomic-vec2u-min-max is not available on this backend");
    }
#endif
    t.expectCompileResult(expected, code);
}

CTS_TEST(g, "requires_extension")
    .desc("Tests that atomicStoreMin and atomicStoreMax require the atomic_vec2u_min_max extension")
    .params([](ParamsBuilder u) {
        return u.combine("op", kAtomicOps()).combine("enable", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const bool enable = t.param<bool>("enable");
        const std::string code = std::string("\n") + (enable ? "enable atomic_vec2u_min_max;" : "") +
                                 "\n@group(0) @binding(0) var<storage, read_write> a : atomic<vec2u>;\n"
                                 "fn foo() {\n  " +
                                 atomicOp(t.param<std::string>("op"), "&a", "vec2u(1, 1)") + ";\n}\n";
        expectAtomicCompileResult(t, enable, code);
    });

CTS_TEST(g, "stage")
    .desc("Tests that atomicStoreMin and atomicStoreMax must not be used in vertex shader stage")
    .params([](ParamsBuilder u) {
        return u.combine("op", kAtomicOps())
            .combine("stage", {Value("fragment"), Value("vertex"), Value("compute")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        std::string code =
            "\nenable atomic_vec2u_min_max;\n"
            "@group(0) @binding(0) var<storage, read_write> a : atomic<vec2u>;\n";

        const std::string op = atomicOp(t.param<std::string>("op"), "&a", "vec2u(1, 1)");

        if (stage == "compute") {
            code += "\n@compute @workgroup_size(1,1,1) fn main() {\n  " + op + ";\n}";
        } else if (stage == "fragment") {
            code += "\n@fragment fn main() -> @location(0) vec4<f32> {\n  " + op +
                    ";\n  return vec4<f32>();\n}";
        } else {
            code += "\n@vertex fn vmain() -> @builtin(position) vec4<f32> {\n  " + op +
                    ";\n  return vec4<f32>();\n}";
        }

        const bool pass = stage != "vertex";
        expectAtomicCompileResult(t, pass, code);
    });

CTS_TEST(g, "atomic_ptr_parameterization")
    .desc("Tests the valid atomic_ptr parameters")
    .params([](ParamsBuilder u) {
        return u.combine("op", kAtomicOps())
            .beginSubcases()
            .combine("aspace", {Value("storage"), Value("workgroup"), Value("private"), Value("uniform"),
                                Value("function")})
            .combine("access", {Value("read"), Value("read_write")})
            .combine("style", {Value("param"), Value("var")})
            .filter([](const ParamRecord& p) {
                const std::string aspace = valueAs<std::string>(*findParam(p, "aspace"));
                const std::string access = valueAs<std::string>(*findParam(p, "access"));
                const std::string style = valueAs<std::string>(*findParam(p, "style"));
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
        const std::string aspace = t.param<std::string>("aspace");
        const std::string access = t.param<std::string>("access");
        std::string moduleVar;
        std::string functionVar;
        std::string param;
        std::string aParam;
        if (t.param<std::string>("style") == "var") {
            aParam = "&a";
            if (aspace == "storage") {
                moduleVar = "@group(0) @binding(0) var<storage, " + access + "> a : atomic<vec2u>;";
            } else if (aspace == "workgroup") {
                moduleVar = "var<workgroup> a : atomic<vec2u>;";
            } else if (aspace == "uniform") {
                moduleVar = "@group(0) @binding(0) var<uniform> a : atomic<vec2u>;";
            } else if (aspace == "private") {
                moduleVar = "var<private> a : atomic<vec2u>;";
            } else if (aspace == "function") {
                functionVar = "var a : atomic<vec2u>;";
            }
        } else {
            const std::string aspaceParam = aspace == "storage" ? ", " + access : "";
            param = "p : ptr<" + aspace + ", atomic<vec2u>" + aspaceParam + ">";
            aParam = "p";
        }

        const std::string code = "\nenable atomic_vec2u_min_max;\n" + moduleVar + "\nfn foo(" + param +
                                 ") {\n  " + functionVar + "\n  " +
                                 atomicOp(t.param<std::string>("op"), aParam, "vec2u(1, 1)") + ";\n}\n";

        const bool aspaceOK = aspace == "storage";
        const bool accessOK = access == "read_write";
        expectAtomicCompileResult(t, aspaceOK && accessOK, code);
    });

CTS_TEST(g, "value_parameter")
    .desc("Validates that the value parameter must be vec2u")
    .params([](ParamsBuilder u) {
        return u.combine("op", kAtomicOps())
            .beginSubcases()
            .combine("valueType", {Value("vec2u"), Value("vec2<u32>"), Value("vec2i"), Value("vec2<i32>"),
                                   Value("vec2f"), Value("vec2<f32>"), Value("u32"), Value("i32"),
                                   Value("vec2<AbstractInt>"), Value("vec2<AbstractFloat>")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string valueType = t.param<std::string>("valueType");
        std::string value;
        if (valueType == "vec2u" || valueType == "vec2<u32>") {
            value = "vec2u(1u, 1u)";
        } else if (valueType == "vec2i" || valueType == "vec2<i32>") {
            value = "vec2i(1i, 1i)";
        } else if (valueType == "vec2f" || valueType == "vec2<f32>") {
            value = "vec2f(1f, 1f)";
        } else if (valueType == "u32") {
            value = "1u";
        } else if (valueType == "i32") {
            value = "1i";
        } else if (valueType == "vec2<AbstractInt>") {
            value = "vec2(1, 1)";
        } else if (valueType == "vec2<AbstractFloat>") {
            value = "vec2(1.0, 1.0)";
        }

        const std::string code =
            "\nenable atomic_vec2u_min_max;\n"
            "@group(0) @binding(0) var<storage, read_write> a : atomic<vec2u>;\n"
            "fn foo() {\n  " +
            atomicOp(t.param<std::string>("op"), "&a", value) + ";\n}\n";

        const bool expect =
            valueType == "vec2u" || valueType == "vec2<u32>" || valueType == "vec2<AbstractInt>";
        expectAtomicCompileResult(t, expect, code);
    });

CTS_TEST(g, "return_type")
    .desc("Validates that atomicStoreMin and atomicStoreMax return void")
    .params([](ParamsBuilder u) { return u.combine("op", kAtomicOps()); })
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\nenable atomic_vec2u_min_max;\n"
            "@group(0) @binding(0) var<storage, read_write> a: atomic<vec2u>;\n"
            "fn foo() {\n  let x = " +
            atomicOp(t.param<std::string>("op"), "&a", "vec2u(1, 1)") + ";\n}\n";

        expectAtomicCompileResult(t, false, code);
    });

CTS_TEST(g, "atomic_ptr_type")
    .desc("Validates that the atomic_ptr parameter must be atomic<vec2u>")
    .params([](ParamsBuilder u) {
        return u.combine("op", kAtomicOps())
            .beginSubcases()
            .combine("atomicType", {Value("vec4u"), Value("vec2u"), Value("u32"), Value("i32")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string atomicType = t.param<std::string>("atomicType");
        const std::string code =
            "\nenable atomic_vec2u_min_max;\n"
            "@group(0) @binding(0) var<storage, read_write> a : atomic<" +
            atomicType + ">;\nfn foo() {\n  " +
            atomicOp(t.param<std::string>("op"), "&a", atomicType + " ( 1) ") + ";\n}\n";

        const bool expect = atomicType == "vec2u";
        expectAtomicCompileResult(t, expect, code);
    });

CTS_TEST(g, "non_atomic")
    .desc("Test that non-atomic vec2u are rejected")
    .params([](ParamsBuilder u) {
        return u.combine("op", kAtomicOps())
            .combine("addrspace", {Value("storage"), Value("workgroup")})
            .combine("atomic", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addrspace = t.param<std::string>("addrspace");
        const bool atomic = t.param<bool>("atomic");
        const std::string type = atomic ? "atomic<vec2u>" : "vec2u";
        std::string decl;
        if (addrspace == "storage") {
            decl = "@group(0) @binding(0) var<storage, read_write> a : " + type;
        } else if (addrspace == "workgroup") {
            decl = "var<workgroup> a : " + type;
        }

        const std::string code = "\nenable atomic_vec2u_min_max;\n" + decl + ";\nfn foo() {\n  " +
                                 atomicOp(t.param<std::string>("op"), "&a", "vec2u(1, 1)") + ";\n}\n";

        expectAtomicCompileResult(t, atomic && addrspace == "storage", code);
    });

}  // namespace
