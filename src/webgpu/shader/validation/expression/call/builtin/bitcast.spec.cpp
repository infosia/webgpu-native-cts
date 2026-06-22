// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/bitcast.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation negative tests for bitcast builtins. Ports the local helpers
// (kVectorCases, the f32/f16 Inf/NaN-in-integer tables via linearRange, and
// u16x2ToU32) faithfully. The bad-value literals are formatted with
// binary::numberToString (JS Number.toString() shortest round-trip), matching the
// upstream `${bitBadValue}` / `${badSrcElemBitsInU32}` template interpolation.
// f16 cases emit `enable f16;`. kBit constants needed here (f32/f16 +/-infinity,
// i32.max, u32.max) are defined locally (not present in const_override_builtin.h).

#include <cstdint>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/const_override.h"
#include "webgpu/shader/validation/expression/call/builtin/const_override_builtin.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,bitcast", "Validation negative tests for bitcast builtins.");

// kBit subset (util/constants.ts) needed for the Inf/NaN integer ranges.
struct LocalKBit {
    static constexpr double f32_pos_inf = 0x7f800000;       // 2139095040
    static constexpr double f32_neg_inf = 0xff800000u;      // 4286578688
    static constexpr double i32_pos_max = 0x7fffffff;       // 2147483647
    static constexpr double u32_max = 0xffffffffu;          // 4294967295
    static constexpr double f16_pos_inf = 0x7c00;           // 31744
    static constexpr double f16_neg_inf = 0xfc00;           // 64512
};

// A VectorCase specifies the number of components a vector type has, and which
// component will have a bad value. width = 1 indicates a scalar.
struct VectorCase {
    std::string name;
    int width;
    int badIndex;
};
static const std::vector<VectorCase>& kVectorCases() {
    static const std::vector<VectorCase> v = {
        {"v1_b0", 1, 0}, {"v2_b0", 2, 0}, {"v2_b1", 2, 1}, {"v3_b0", 3, 0}, {"v3_b1", 3, 1},
        {"v3_b2", 3, 2}, {"v4_b0", 4, 0}, {"v4_b1", 4, 1}, {"v4_b2", 4, 2}, {"v4_b3", 4, 3},
    };
    return v;
}
static std::vector<Value> vectorCaseNames() {
    std::vector<Value> out;
    for (const VectorCase& vc : kVectorCases()) {
        out.emplace_back(vc.name);
    }
    return out;
}
static const VectorCase& findVectorCase(const std::string& name) {
    for (const VectorCase& vc : kVectorCases()) {
        if (vc.name == name) {
            return vc;
        }
    }
    static const VectorCase dummy{"", 1, 0};
    return dummy;
}

static constexpr int kNumNaNs = 4;

// f32InfAndNaNInU32 (as doubles, matching JS number semantics).
static const std::vector<double>& f32InfAndNaNInU32() {
    static const std::vector<double> v = [] {
        std::vector<double> out;
        for (double d : b::linearRange(LocalKBit::f32_pos_inf + 1, LocalKBit::i32_pos_max, kNumNaNs)) {
            out.push_back(d);
        }
        for (double d : b::linearRange(LocalKBit::f32_neg_inf + 1, LocalKBit::u32_max, kNumNaNs)) {
            out.push_back(d);
        }
        out.push_back(LocalKBit::f32_pos_inf);
        out.push_back(LocalKBit::f32_neg_inf);
        return out;
    }();
    return v;
}

// f16InfAndNaNInU16.
static const std::vector<double>& f16InfAndNaNInU16() {
    static const std::vector<double> v = [] {
        std::vector<double> out;
        for (double d : b::linearRange(LocalKBit::f16_pos_inf + 1, 32767, kNumNaNs)) {
            out.push_back(d);
        }
        for (double d : b::linearRange(LocalKBit::f16_neg_inf + 1, 65535, kNumNaNs)) {
            out.push_back(d);
        }
        out.push_back(LocalKBit::f16_pos_inf);
        out.push_back(LocalKBit::f16_neg_inf);
        return out;
    }();
    return v;
}

// u16x2ToU32: little-endian combine of two u16 into a u32.
static double u16x2ToU32(double lo, double hi) {
    const uint32_t loU = static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int64_t>(lo)));
    const uint32_t hiU = static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int64_t>(hi)));
    return static_cast<double>(loU | (hiU << 16));
}

// bad-value tables -> Value lists (doubles).
static std::vector<Value> doubleValues(const std::vector<double>& vs) {
    std::vector<Value> out;
    for (double d : vs) {
        out.emplace_back(d);
    }
    return out;
}

CTS_TEST(g, "bad_const_to_f32")
    .desc("It is a shader-creation error if any const-expression of floating-point type evaluates "
          "to NaN or infinity.")
    .params([](ParamsBuilder u) {
        return u.combine("fromScalarType",
                         {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("vectorize", vectorCaseNames())
            .beginSubcases()
            .combine("useBadValue", {Value(true), Value(false)})
            .expand("bitBadValue", [](const ParamRecord& p) {
                if (valueAs<bool>(*findParam(p, "useBadValue"))) {
                    return doubleValues(f32InfAndNaNInU32());
                }
                std::vector<Value> out;
                out.emplace_back(double(0));
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string fromScalarType = t.param<std::string>("fromScalarType");
        const VectorCase& vectorize = findVectorCase(t.param<std::string>("vectorize"));
        const bool useBadValue = t.param<bool>("useBadValue");
        const double bitBadValue = t.param<double>("bitBadValue");
        const int width = vectorize.width;
        const int badIndex = vectorize.badIndex;
        const std::string badScalar =
            fromScalarType + "(u32(" + bt::numberToString(bitBadValue) + "))";
        const std::string destType = width == 1 ? "f32" : ("vec" + std::to_string(width) + "f");
        const std::string srcType =
            width == 1 ? fromScalarType : ("vec" + std::to_string(width) + "<" + fromScalarType + ">");
        std::string components;
        for (int i = 0; i < width; ++i) {
            if (i) {
                components += ",";
            }
            components += (i == badIndex) ? badScalar : "0";
        }
        const std::string code =
            "const f = bitcast<" + destType + ">(" + srcType + "(" + components + "));";
        t.expectCompileResult(!useBadValue, code);
    });

CTS_TEST(g, "bad_const_to_f16")
    .desc("It is a shader-creation error if any const-expression of floating-point type evaluates "
          "to NaN or infinity.")
    .params([](ParamsBuilder u) {
        return u.combine("fromScalarType",
                         {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("vectorize", vectorCaseNames())
            .filter([](const ParamRecord& p) {
                return findVectorCase(valueAs<std::string>(*findParam(p, "vectorize"))).width % 2 ==
                       0;
            })
            .beginSubcases()
            .combine("useBadValue", {Value(true), Value(false)})
            .expand("bitBadValue", [](const ParamRecord& p) {
                if (valueAs<bool>(*findParam(p, "useBadValue"))) {
                    return doubleValues(f16InfAndNaNInU16());
                }
                std::vector<Value> out;
                out.emplace_back(double(0));
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string fromScalarType = t.param<std::string>("fromScalarType");
        const VectorCase& vectorize = findVectorCase(t.param<std::string>("vectorize"));
        const bool useBadValue = t.param<bool>("useBadValue");
        const double bitBadValue = t.param<double>("bitBadValue");
        const int width = vectorize.width;
        const int badIndex = vectorize.badIndex;

        // Put the bad f16 bits into the lower 16 bits of the source element if bad
        // index is 0 or 2, else the higher 16 bits.
        const double badSrcElemBitsInU32 = (badIndex % 2 == 0)
                                               ? u16x2ToU32(bitBadValue, 0)
                                               : u16x2ToU32(0, bitBadValue);
        const std::string badScalar =
            fromScalarType + "(u32(" + bt::numberToString(badSrcElemBitsInU32) + "))";

        const std::string destType = "vec" + std::to_string(width) + "<f16>";
        const std::string srcType =
            width == 2 ? fromScalarType : ("vec2<" + fromScalarType + ">");
        std::string components;
        for (int i = 0; i < width / 2; ++i) {
            if (i) {
                components += ",";
            }
            components += (i == (badIndex >> 1)) ? badScalar : "0";
        }
        const std::string code = "\n    enable f16;\n    const f = bitcast<" + destType + ">(" +
                                 srcType + "(" + components + "));";
        t.expectCompileResult(!useBadValue, code);
    });

// f32_matrix_types / f16_matrix_types / bool_types + 'array<i32,2>', 'S'.
static std::vector<Value> badTypeConstructibleTypes() {
    std::vector<Value> out;
    for (int i = 2; i <= 4; ++i) {
        for (int j = 2; j <= 4; ++j) {
            out.emplace_back("mat" + std::to_string(i) + "x" + std::to_string(j) + "f");
        }
    }
    for (int i = 2; i <= 4; ++i) {
        for (int j = 2; j <= 4; ++j) {
            out.emplace_back("mat" + std::to_string(i) + "x" + std::to_string(j) + "<f16>");
        }
    }
    out.emplace_back("bool");
    for (int i = 2; i <= 4; ++i) {
        out.emplace_back("vec" + std::to_string(i) + "<bool>");
    }
    out.emplace_back("array<i32,2>");
    out.emplace_back("S");
    return out;
}

CTS_TEST(g, "bad_type_constructible")
    .desc("Bitcast only applies to concrete numeric scalar or concrete numeric vector. Test "
          "constructible types.")
    .params([](ParamsBuilder u) {
        return u.combine("type", badTypeConstructibleTypes())
            .combine("direction", {Value(std::string("to")), Value(std::string("from"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string T = t.param<std::string>("type");
        const std::string direction = t.param<std::string>("direction");
        const std::string enableDirectives =
            T.find("f16") != std::string::npos ? "enable f16;\n" : "";
        const std::string preamble = T == "S" ? "struct S { a:i32 } " : "";
        const std::string srcVal = direction == "to" ? "0" : (T + "()");
        const std::string destType = direction == "to" ? T : "i32";
        const std::string code =
            enableDirectives + preamble + "const x = bitcast<" + destType + ">(" + srcVal + ");";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "bad_type_nonconstructible")
    .desc("Bitcast only applies to concrete numeric scalar or concrete numeric vector. Test "
          "non-constructible types.")
    .params([](ParamsBuilder u) {
        return u.combine("var", {Value(std::string("s")), Value(std::string("t")),
                                 Value(std::string("b")), Value(std::string("p"))})
            .combine("direction", {Value(std::string("to")), Value(std::string("from"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string varName = t.param<std::string>("var");
        const std::string direction = t.param<std::string>("direction");
        std::string typeOf;
        if (varName == "s") {
            typeOf = "sampler";
        } else if (varName == "t") {
            typeOf = "texture_depth_2d";
        } else if (varName == "b") {
            typeOf = "array<i32>";
        } else {
            typeOf = "ptr<private,i32>";
        }
        const std::string srcVal = direction == "to" ? "0" : varName;
        const std::string destType = direction == "to" ? typeOf : "i32";
        const std::string code =
            "\n    @group(0) @binding(0) var s: sampler;\n"
            "    @group(0) @binding(1) var t: texture_depth_2d;\n"
            "    @group(0) @binding(2) var<storage> b: array<i32>;\n"
            "    var<private> v: i32;\n"
            "    @compute @workgroup_size(1)\n"
            "    fn main() {\n"
            "      let p = &v;\n"
            "      let x = bitcast<" +
            destType + ">(" + srcVal +
            ");\n"
            "    }\n    ";
        t.expectCompileResult(false, code);
    });

// other_type list shared by bad_to_vec3h (with vec2h,vec4h) and bad_to_f16
// (with vec2h,vec3h,vec4h). Built per-test to preserve exact ordering.
CTS_TEST(g, "bad_to_vec3h")
    .desc("Can't cast numeric type to vec3<f16> because it is 48 bits wide and no other type is "
          "that size.")
    .params([](ParamsBuilder u) {
        return u.combine("other_type",
                         {Value(std::string("bool")), Value(std::string("u32")),
                          Value(std::string("i32")), Value(std::string("f32")),
                          Value(std::string("vec2<bool>")), Value(std::string("vec3<bool>")),
                          Value(std::string("vec4<bool>")), Value(std::string("vec2u")),
                          Value(std::string("vec3u")), Value(std::string("vec4u")),
                          Value(std::string("vec2i")), Value(std::string("vec3i")),
                          Value(std::string("vec4i")), Value(std::string("vec2f")),
                          Value(std::string("vec3f")), Value(std::string("vec4f")),
                          Value(std::string("vec2h")), Value(std::string("vec4h"))})
            .combine("direction", {Value(std::string("to")), Value(std::string("from"))})
            .combine("type", {Value(std::string("vec3<f16>")), Value(std::string("vec3h"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string otherType = t.param<std::string>("other_type");
        const std::string direction = t.param<std::string>("direction");
        const std::string type = t.param<std::string>("type");
        const std::string srcType = direction == "to" ? type : otherType;
        const std::string dstType = direction == "from" ? type : otherType;
        const std::string code = "\nenable f16;\n@fragment\nfn main() {\n  var src : " + srcType +
                                 ";\n  let dst = bitcast<" + dstType + ">(src);\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "bad_to_f16")
    .desc("Can't cast non-16-bit types to f16 because it is 16 bits wide and no other type is that "
          "size.")
    .params([](ParamsBuilder u) {
        return u.combine("other_type",
                         {Value(std::string("bool")), Value(std::string("u32")),
                          Value(std::string("i32")), Value(std::string("f32")),
                          Value(std::string("vec2<bool>")), Value(std::string("vec3<bool>")),
                          Value(std::string("vec4<bool>")), Value(std::string("vec2u")),
                          Value(std::string("vec3u")), Value(std::string("vec4u")),
                          Value(std::string("vec2i")), Value(std::string("vec3i")),
                          Value(std::string("vec4i")), Value(std::string("vec2f")),
                          Value(std::string("vec3f")), Value(std::string("vec4f")),
                          Value(std::string("vec2h")), Value(std::string("vec3h")),
                          Value(std::string("vec4h"))})
            .combine("direction", {Value(std::string("to")), Value(std::string("from"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string otherType = t.param<std::string>("other_type");
        const std::string direction = t.param<std::string>("direction");
        const std::string srcType = direction == "to" ? "f16" : otherType;
        const std::string dstType = direction == "from" ? "f16" : otherType;
        const std::string code = "\nenable f16;\n@fragment\nfn main() {\n  var src : " + srcType +
                                 ";\n  let dst = bitcast<" + dstType + ">(src);\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "valid_vec2h")
    .desc("Check valid vec2<f16> bitcasts")
    .params([](ParamsBuilder u) {
        return u.combine("other_type", {Value(std::string("u32")), Value(std::string("i32")),
                                        Value(std::string("f32"))})
            .combine("type", {Value(std::string("vec2<f16>")), Value(std::string("vec2h"))})
            .combine("direction", {Value(std::string("to")), Value(std::string("from"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string otherType = t.param<std::string>("other_type");
        const std::string type = t.param<std::string>("type");
        const std::string direction = t.param<std::string>("direction");
        const std::string srcType = direction == "to" ? type : otherType;
        const std::string dstType = direction == "from" ? type : otherType;
        const std::string code = "\nenable f16;\n@fragment\nfn main() {\n  var src : " + srcType +
                                 ";\n  let dst = bitcast<" + dstType + ">(src);\n}";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "valid_vec4h")
    .desc("Check valid vec2<f16> bitcasts")
    .params([](ParamsBuilder u) {
        return u.combine("other_type",
                         {Value(std::string("vec2<u32>")), Value(std::string("vec2u")),
                          Value(std::string("vec2<i32>")), Value(std::string("vec2i")),
                          Value(std::string("vec2<f32>")), Value(std::string("vec2f"))})
            .combine("type", {Value(std::string("vec4<f16>")), Value(std::string("vec4h"))})
            .combine("direction", {Value(std::string("to")), Value(std::string("from"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string otherType = t.param<std::string>("other_type");
        const std::string type = t.param<std::string>("type");
        const std::string direction = t.param<std::string>("direction");
        const std::string srcType = direction == "to" ? type : otherType;
        const std::string dstType = direction == "from" ? type : otherType;
        const std::string code = "\nenable f16;\n@fragment\nfn main() {\n  var src : " + srcType +
                                 ";\n  let dst = bitcast<" + dstType + ">(src);\n}";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "must_use")
    .desc("Test that bitcast result must be used")
    .params([](ParamsBuilder u) {
        return u.combine("case", {Value(std::string("bitcast<u32>(1i)")),
                                  Value(std::string("bitcast<f32>(1u)")),
                                  Value(std::string("bitcast<vec2f>(vec2i())")),
                                  Value(std::string("bitcast<vec3u>(vec3u())")),
                                  Value(std::string("bitcast<vec4i>(vec4f())"))})
            .combine("use", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseStr = t.param<std::string>("case");
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    fn foo() {\n      " + std::string(use ? "_ =" : "") + " " + caseStr + ";\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
