// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/value_constructor.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,value_constructor",
    "Validation tests for constructor built-in functions.");

using bt::ScalarKind;

// ---------------------------------------------------------------------------
// Local Type model (mirrors conversion.ts ScalarType/VectorType/MatrixType for
// the names used in this spec). A "TypeRef" is a scalar, vector, or matrix.
// ---------------------------------------------------------------------------
enum class TypeClass { Scalar, Vector, Matrix };

struct TypeRef {
    TypeClass cls = TypeClass::Scalar;
    ScalarKind kind = ScalarKind::F32;
    int width = 0;  // vector width
    int cols = 0;   // matrix cols
    int rows = 0;   // matrix rows
};

ScalarKind kindByName(const std::string& name) {
    if (name == "bool") return ScalarKind::Bool;
    if (name == "i32") return ScalarKind::I32;
    if (name == "u32") return ScalarKind::U32;
    if (name == "f32") return ScalarKind::F32;
    if (name == "f16") return ScalarKind::F16;
    if (name == "abstract-int") return ScalarKind::AbstractInt;
    if (name == "abstract-float") return ScalarKind::AbstractFloat;
    return ScalarKind::F32;
}

TypeRef scalarT(ScalarKind k) { return TypeRef{TypeClass::Scalar, k, 0, 0, 0}; }
TypeRef vecT(int w, ScalarKind k) { return TypeRef{TypeClass::Vector, k, w, 0, 0}; }
TypeRef matT(int c, int r, ScalarKind k) { return TypeRef{TypeClass::Matrix, k, 0, c, r}; }

ScalarKind scalarKindOf(const TypeRef& t) { return t.kind; }

bool isAbstractType(const TypeRef& t) {
    return t.cls == TypeClass::Scalar &&
           (t.kind == ScalarKind::AbstractInt || t.kind == ScalarKind::AbstractFloat);
}

bool isFloatType(const TypeRef& t) {
    const ScalarKind k = t.kind;
    return k == ScalarKind::F32 || k == ScalarKind::F16 || k == ScalarKind::AbstractFloat;
}

bool requiresF16(const TypeRef& t) { return t.kind == ScalarKind::F16; }

// Map TypeRef (scalar/vector only) to a binary::Type for isConvertible reuse.
bt::Type toBinaryType(const TypeRef& t) {
    if (t.cls == TypeClass::Vector) {
        return bt::vec(t.width, t.kind);
    }
    return bt::scalar(t.kind);
}

bool isConvertible(const TypeRef& src, const TypeRef& dst) {
    return bt::isConvertible(toBinaryType(src), toBinaryType(dst));
}

// Type[name] for the element-type tables used by this spec.
TypeRef typeByName(const std::string& name) {
    if (name == "mat2x2f") return matT(2, 2, ScalarKind::F32);
    if (name == "mat3x3h") return matT(3, 3, ScalarKind::F16);
    if (name == "vec2i") return vecT(2, ScalarKind::I32);
    if (name == "vec3f") return vecT(3, ScalarKind::F32);
    return scalarT(kindByName(name));
}

// create(N).wgsl() for scalar/vector/matrix.
std::string createWgsl(const TypeRef& t, long long n) {
    if (t.cls == TypeClass::Matrix) {
        const std::string el = bt::scalarValueWgsl(t.kind, n);
        std::string els;
        for (int i = 0; i < t.cols * t.rows; ++i) {
            if (i) {
                els += ", ";
            }
            els += el;
        }
        return "mat" + std::to_string(t.cols) + "x" + std::to_string(t.rows) + "(" + els + ")";
    }
    if (t.cls == TypeClass::Vector) {
        return bt::createWgsl(bt::vec(t.width, t.kind), n);
    }
    return bt::scalarValueWgsl(t.kind, n);
}

const std::vector<std::string>& kScalarTypes() {
    static const std::vector<std::string> v = {"bool", "i32", "u32", "f32", "f16"};
    return v;
}
bool scalarTypesIncludes(const std::string& name) {
    for (const std::string& s : kScalarTypes()) {
        if (s == name) {
            return true;
        }
    }
    return false;
}

std::vector<Value> scalarTypeValues() {
    std::vector<Value> out;
    for (const std::string& s : kScalarTypes()) {
        out.emplace_back(s);
    }
    return out;
}

// ---------------------------------------------------------------------------
// scalar_zero_value
// ---------------------------------------------------------------------------
CTS_TEST(g, "scalar_zero_value")
    .desc("Tests zero value scalar constructors")
    .params([](ParamsBuilder u) { return u.combine("type", scalarTypeValues()); })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string enable = type == "f16" ? "enable f16;" : "";
        const std::string code = enable + "\n    const x : " + type + " = " + type +
                                 "();\n    const_assert x == " + type + "(0);";
        t.expectCompileResult(true, code);
    });

// ---------------------------------------------------------------------------
// scalar_value
// ---------------------------------------------------------------------------
CTS_TEST(g, "scalar_value")
    .desc("Tests scalar value constructors")
    .params([](ParamsBuilder u) {
        std::vector<Value> valueTypes = scalarTypeValues();
        valueTypes.emplace_back(std::string("vec2u"));
        valueTypes.emplace_back(std::string("S"));
        valueTypes.emplace_back(std::string("array<u32, 2>"));
        return u.combine("type", scalarTypeValues()).combine("value_type", valueTypes);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string valueType = t.param<std::string>("value_type");
        const std::string enable = (type == "f16" || valueType == "f16") ? "enable f16;" : "";
        const std::string code =
            enable + "\n    const x : " + type + " = " + type + "(" + valueType + "());";
        t.expectCompileResult(scalarTypesIncludes(valueType), code);
    });

// ---------------------------------------------------------------------------
// vector_zero_value
// ---------------------------------------------------------------------------
std::vector<Value> scalarPlusAbstractValues() {
    std::vector<Value> out = scalarTypeValues();
    out.emplace_back(std::string("abstract-int"));
    out.emplace_back(std::string("abstract-float"));
    return out;
}

CTS_TEST(g, "vector_zero_value")
    .desc("Tests zero value vector constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type", scalarPlusAbstractValues())
            .beginSubcases()
            .combine("size", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const int size = static_cast<int>(t.param<int64_t>("size"));
        const bool abstract = type == "abstract-int" || type == "abstract-float";
        const std::string param = abstract ? "" : ("<" + type + ">");
        const std::string decl = "vec" + std::to_string(size) + param;
        const std::string enable = type == "f16" ? "enable f16;" : "";
        const std::string comparison = abstract ? "0" : (type + "(0)");
        std::string code = enable + "\n    const x " + (abstract ? "" : (": " + decl)) + " = " +
                           decl + "();\n";
        for (int i = 0; i < size; i++) {
            code += "const_assert x[" + std::to_string(i) + "] == " + comparison + ";\n";
        }
        t.expectCompileResult(true, code);
    });

// ---------------------------------------------------------------------------
// vector_splat
// ---------------------------------------------------------------------------
std::vector<Value> splatTypeValues() {
    return {Value(std::string("bool")),  Value(std::string("i32")),
            Value(std::string("u32")),   Value(std::string("f32")),
            Value(std::string("f16")),   Value(std::string("abstract-int")),
            Value(std::string("abstract-float"))};
}
std::vector<Value> splatEleTypeValues() {
    return {Value(std::string("bool")),           Value(std::string("i32")),
            Value(std::string("u32")),            Value(std::string("f32")),
            Value(std::string("f16")),            Value(std::string("abstract-int")),
            Value(std::string("abstract-float")), Value(std::string("mat2x2f")),
            Value(std::string("mat3x3h")),        Value(std::string("vec2i")),
            Value(std::string("vec3f"))};
}

CTS_TEST(g, "vector_splat")
    .desc("Test vector splat constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type", splatTypeValues())
            .combine("ele_type", splatEleTypeValues())
            .beginSubcases()
            .combine("size", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const std::string eleTypeName = t.param<std::string>("ele_type");
        const int size = static_cast<int>(t.param<int64_t>("size"));
        const TypeRef eleTy = typeByName(eleTypeName);
        const bool abstract = typeName == "abstract-int" || typeName == "abstract-float";
        const std::string param = abstract ? "" : ("<" + typeName + ">");
        const std::string decl = "vec" + std::to_string(size) + param;
        const std::string enable =
            (typeName == "f16" || eleTypeName == "f16") ? "enable f16;" : "";
        const std::string eleValue = createWgsl(eleTy, 1);
        const std::string valueCall = decl;
        const std::string code = enable + "\n    const x " + (abstract ? "" : (": " + decl)) +
                                 " = " + valueCall + "(" + eleValue + ");";
        const TypeRef ty = typeByName(typeName);
        const bool expect =
            (eleTy.cls == TypeClass::Scalar && (isConvertible(eleTy, ty) || isAbstractType(ty))) ||
            (eleTy.cls == TypeClass::Vector && eleTy.width == size);
        t.expectCompileResult(expect, code);
    });

// ---------------------------------------------------------------------------
// vector_copy
// ---------------------------------------------------------------------------
std::vector<Value> sevenTypeValues() {
    return {Value(std::string("bool")),  Value(std::string("i32")),
            Value(std::string("u32")),   Value(std::string("f32")),
            Value(std::string("f16")),   Value(std::string("abstract-int")),
            Value(std::string("abstract-float"))};
}

CTS_TEST(g, "vector_copy")
    .desc("Test vector copy constructors")
    .params([](ParamsBuilder u) {
        return u.combine("decl_type", sevenTypeValues())
            .combine("value_type", sevenTypeValues())
            .beginSubcases()
            .combine("decl_size", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("value_size", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string declType = t.param<std::string>("decl_type");
        const std::string valueType = t.param<std::string>("value_type");
        const int declSize = static_cast<int>(t.param<int64_t>("decl_size"));
        const int valueSize = static_cast<int>(t.param<int64_t>("value_size"));
        const TypeRef declTy = typeByName(declType);
        const TypeRef valueTy = typeByName(valueType);
        const TypeRef valueVecTy = vecT(valueSize, valueTy.kind);
        const std::string enable =
            (requiresF16(declTy) || requiresF16(valueTy)) ? "enable f16;" : "";
        const std::string decl = "vec" + std::to_string(declSize) + "<" + declType + ">";
        const std::string ctor =
            "vec" + std::to_string(declSize) + (isAbstractType(declTy) ? "" : ("<" + declType + ">"));
        const std::string code = enable + "\n    const x " +
                                 (isAbstractType(declTy) ? "" : (": " + decl)) + " = " + ctor + "(" +
                                 createWgsl(valueVecTy, 1) + ");";
        t.expectCompileResult(declSize == valueSize, code);
    });

// ---------------------------------------------------------------------------
// vector_elementwise
// ---------------------------------------------------------------------------
CTS_TEST(g, "vector_elementwise")
    .desc("Test element-wise vector constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type", splatTypeValues())
            .combine("ele_type", splatEleTypeValues())
            .beginSubcases()
            .combine("size", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("num_eles",
                     {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4)), Value(int64_t(5))})
            .combine("full_type", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const std::string eleTypeName = t.param<std::string>("ele_type");
        const int size = static_cast<int>(t.param<int64_t>("size"));
        const int numEles = static_cast<int>(t.param<int64_t>("num_eles"));
        const bool fullType = t.param<bool>("full_type");
        const TypeRef eleTy = typeByName(eleTypeName);
        const bool abstract = typeName == "abstract-int" || typeName == "abstract-float";
        const std::string param = abstract ? "" : ("<" + typeName + ">");
        const std::string decl = "vec" + std::to_string(size) + param;
        const std::string enable =
            (typeName == "f16" || eleTypeName == "f16") ? "enable f16;" : "";
        const std::string eleValue = createWgsl(eleTy, 1);
        const std::string valueCall = fullType ? decl : ("vec" + std::to_string(size));
        std::string code = enable + "\n    const x " + (abstract ? "" : (": " + decl)) + " = " +
                           valueCall + "(";
        for (int i = 0; i < numEles; i++) {
            code += eleValue + ",";
        }
        code += ");";
        const TypeRef ty = typeByName(typeName);
        const int realNumEles = eleTy.cls == TypeClass::Vector ? numEles * eleTy.width : numEles;
        const bool expect =
            !(eleTy.cls == TypeClass::Matrix) && size == realNumEles &&
            (isConvertible(scalarT(scalarKindOf(eleTy)), ty) || typeName == "abstract-int" ||
             typeName == "abstract-float");
        t.expectCompileResult(expect, code);
    });

// ---------------------------------------------------------------------------
// vector_mixed
// ---------------------------------------------------------------------------
CTS_TEST(g, "vector_mixed")
    .desc("Test vector constructors with mixed elements and vectors")
    .params([](ParamsBuilder u) {
        return u.combine("type", splatTypeValues())
            .combine("ele_type", sevenTypeValues())
            .beginSubcases()
            .combine("size", {Value(int64_t(3)), Value(int64_t(4))})
            .combine("num_eles", {Value(int64_t(3)), Value(int64_t(4)), Value(int64_t(5))})
            .combine("full_type", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const std::string eleTypeName = t.param<std::string>("ele_type");
        const int size = static_cast<int>(t.param<int64_t>("size"));
        const int numEles = static_cast<int>(t.param<int64_t>("num_eles"));
        const bool fullType = t.param<bool>("full_type");
        const TypeRef eleTy = typeByName(eleTypeName);
        const bool abstract = typeName == "abstract-int" || typeName == "abstract-float";
        const std::string param = abstract ? "" : ("<" + typeName + ">");
        const std::string decl = "vec" + std::to_string(size) + param;
        const std::string enable =
            (typeName == "f16" || eleTypeName == "f16") ? "enable f16;" : "";
        const std::string v = createWgsl(eleTy, 1);
        const std::string call = fullType ? decl : ("vec" + std::to_string(size));
        const std::string declAnno = abstract ? "" : (": " + decl);
        std::string code = enable + "\n";
        switch (numEles) {
            case 3:
                code += "const x1 " + declAnno + " = " + call + "(" + v + ", vec2(" + v + ", " + v +
                        "));\n";
                code += "const x2 " + declAnno + " = " + call + "(vec2(" + v + ", " + v + "), " + v +
                        ");\n";
                break;
            case 4:
                code += "const x1 " + declAnno + " = " + call + "(" + v + ", vec2(" + v + ", " + v +
                        "), " + v + ");\n";
                code += "const x2 " + declAnno + " = " + call + "(" + v + ", " + v + ", vec2(" + v +
                        ", " + v + "));\n";
                code += "const x3 " + declAnno + " = " + call + "(vec2(" + v + ", " + v + "), " + v +
                        ", " + v + ");\n";
                code += "const x4 " + declAnno + " = " + call + "(vec3(" + v + ", " + v + ", " + v +
                        "), " + v + ");\n";
                code += "const x5 " + declAnno + " = " + call + "(" + v + ", vec3(" + v + ", " + v +
                        ", " + v + "));\n";
                break;
            case 5:
                // This case is always invalid so try a few only.
                code += "const x1 " + declAnno + " = " + call + "(" + v + ", vec3(" + v + ", " + v +
                        "), " + v + ");\n";
                code += "const x1 " + declAnno + " = " + call + "(" + v + ", vec4(" + v + "}), " + v +
                        ");\n";
                break;
            default:
                break;
        }
        const TypeRef ty = typeByName(typeName);
        const bool expect = size == numEles && (isConvertible(eleTy, ty) ||
                                                typeName == "abstract-int" ||
                                                typeName == "abstract-float");
        t.expectCompileResult(expect, code);
    });

// ---------------------------------------------------------------------------
// matrix_zero_value
// ---------------------------------------------------------------------------
CTS_TEST(g, "matrix_zero_value")
    .desc("Tests zero value matrix constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type", {Value(std::string("f32")), Value(std::string("f16"))})
            .beginSubcases()
            .combine("rows", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("cols", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        const int cols = static_cast<int>(t.param<int64_t>("cols"));
        const std::string decl =
            "mat" + std::to_string(cols) + "x" + std::to_string(rows) + "<" + type + ">";
        const std::string enable = type == "f16" ? "enable f16;" : "";
        std::string code = enable + "\n    const x : " + decl + " = " + decl + "();\n";
        for (int c = 0; c < cols; c++) {
            for (int r = 0; r < rows; r++) {
                code += "const_assert x[" + std::to_string(c) + "][" + std::to_string(r) +
                        "] == " + type + "(0);\n";
            }
        }
        t.expectCompileResult(true, code);
    });

// ---------------------------------------------------------------------------
// matrix_copy
// ---------------------------------------------------------------------------
std::vector<Value> matFloatTypeValues() {
    return {Value(std::string("f16")), Value(std::string("f32")),
            Value(std::string("abstract-float"))};
}

CTS_TEST(g, "matrix_copy")
    .desc("Test matrix copy constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type1", matFloatTypeValues())
            .combine("type2", matFloatTypeValues())
            .beginSubcases()
            .combine("c1", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("r1", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("c2", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("r2", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type1 = t.param<std::string>("type1");
        const std::string type2 = t.param<std::string>("type2");
        const int c1 = static_cast<int>(t.param<int64_t>("c1"));
        const int r1 = static_cast<int>(t.param<int64_t>("r1"));
        const int c2 = static_cast<int>(t.param<int64_t>("c2"));
        const int r2 = static_cast<int>(t.param<int64_t>("r2"));
        const TypeRef t1 = typeByName(type1);
        const TypeRef t2 = typeByName(type2);
        const TypeRef m2 = matT(c2, r2, t2.kind);
        const std::string enable = (requiresF16(t1) || requiresF16(t2)) ? "enable f16;" : "";
        const std::string decl =
            "mat" + std::to_string(c1) + "x" + std::to_string(r1) + "<" + type1 + ">";
        const std::string call = "mat" + std::to_string(c1) + "x" + std::to_string(r1) +
                                 (isAbstractType(t1) ? "" : ("<" + type1 + ">"));
        const std::string code = enable + "\n    const m " +
                                 (isAbstractType(t1) ? "" : (": " + decl)) + " = " + call + "(" +
                                 createWgsl(m2, 0) + ");";
        t.expectCompileResult(c1 == c2 && r1 == r2, code);
    });

// ---------------------------------------------------------------------------
// matrix_column
// ---------------------------------------------------------------------------
std::vector<Value> matColTypeValues() {
    return {Value(std::string("f16")),           Value(std::string("f32")),
            Value(std::string("abstract-float")), Value(std::string("i32")),
            Value(std::string("u32")),           Value(std::string("bool"))};
}

CTS_TEST(g, "matrix_column")
    .desc("Test matrix column constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type1", matFloatTypeValues())
            .combine("type2", matColTypeValues())
            .beginSubcases()
            .combine("c1", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("r1", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("c2", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("r2", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type1 = t.param<std::string>("type1");
        const std::string type2 = t.param<std::string>("type2");
        const int c1 = static_cast<int>(t.param<int64_t>("c1"));
        const int r1 = static_cast<int>(t.param<int64_t>("r1"));
        const int c2 = static_cast<int>(t.param<int64_t>("c2"));
        const int r2 = static_cast<int>(t.param<int64_t>("r2"));
        const TypeRef t1 = typeByName(type1);
        const TypeRef t2 = typeByName(type2);
        const std::string enable = (requiresF16(t1) || requiresF16(t2)) ? "enable f16;" : "";
        const TypeRef vecTy2 = vecT(r2, t2.kind);
        std::string values;
        for (int i = 0; i < c2; i++) {
            values += createWgsl(vecTy2, 1) + ",";
        }
        const std::string decl =
            "mat" + std::to_string(c1) + "x" + std::to_string(r1) + "<" + type1 + ">";
        const std::string call = "mat" + std::to_string(c1) + "x" + std::to_string(r1) +
                                 (isAbstractType(t1) ? "" : ("<" + type1 + ">"));
        const std::string code = enable + "\n    const m " +
                                 (isAbstractType(t1) ? "" : (": " + decl)) + " = " + call + "(" +
                                 values + ");";
        const bool expect = isFloatType(t2) && c1 == c2 && r1 == r2 &&
                            (t1.kind == t2.kind || isAbstractType(t1) || isAbstractType(t2));
        t.expectCompileResult(expect, code);
    });

// ---------------------------------------------------------------------------
// matrix_elementwise
// ---------------------------------------------------------------------------
CTS_TEST(g, "matrix_elementwise")
    .desc("Test matrix element-wise constructors")
    .params([](ParamsBuilder u) {
        return u.combine("type1", matFloatTypeValues())
            .combine("type2", matColTypeValues())
            .beginSubcases()
            .combine("c1", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("r1", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("c2", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .combine("r2", {Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type1 = t.param<std::string>("type1");
        const std::string type2 = t.param<std::string>("type2");
        const int c1 = static_cast<int>(t.param<int64_t>("c1"));
        const int r1 = static_cast<int>(t.param<int64_t>("r1"));
        const int c2 = static_cast<int>(t.param<int64_t>("c2"));
        const int r2 = static_cast<int>(t.param<int64_t>("r2"));
        const TypeRef t1 = typeByName(type1);
        const TypeRef t2 = typeByName(type2);
        const std::string enable = (requiresF16(t1) || requiresF16(t2)) ? "enable f16;" : "";
        std::string values;
        for (int i = 0; i < c2 * r2; i++) {
            values += createWgsl(t2, 1) + ",";
        }
        const std::string decl =
            "mat" + std::to_string(c1) + "x" + std::to_string(r1) + "<" + type1 + ">";
        const std::string call = "mat" + std::to_string(c1) + "x" + std::to_string(r1) +
                                 (isAbstractType(t1) ? "" : ("<" + type1 + ">"));
        const std::string code = enable + "\n    const m " +
                                 (isAbstractType(t1) ? "" : (": " + decl)) + " = " + call + "(" +
                                 values + ");";
        const bool expect = isFloatType(t2) && (c1 * r1 == c2 * r2) &&
                            (t1.kind == t2.kind || isAbstractType(t1) || isAbstractType(t2));
        t.expectCompileResult(expect, code);
    });

// ---------------------------------------------------------------------------
// array cases
// ---------------------------------------------------------------------------
struct ArrayCase {
    const char* name;
    const char* element;
    const char* size;  // size may be number-as-string or "" / "o"
    bool valid;
    const char* values;
};
const std::vector<ArrayCase>& kArrayCases() {
    static const std::vector<ArrayCase> v = {
        {"i32", "i32", "4", true, "1,2,3,4"},
        {"f32", "f32", "1", true, "0"},
        {"u32", "u32", "2", true, "2,4"},
        {"valid_array", "array<u32, 2>", "2", true, "array(0,1), array(2,3)"},
        {"invalid_rta", "u32", "", false, "0"},
        {"invalid_override_array", "u32", "o", false, "1"},
        {"valid_struct", "valid_S", "1", true, "valid_S(0)"},
        {"invalid_struct", "invalid_S", "1", false, "array(0)"},
        {"invalid_atomic", "atomic<u32>", "1", false, "0"},
    };
    return v;
}
std::vector<Value> arrayCaseNames() {
    std::vector<Value> out;
    for (const ArrayCase& c : kArrayCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
const ArrayCase& findArrayCase(const std::string& name) {
    for (const ArrayCase& c : kArrayCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ArrayCase dummy{"", "", "", false, ""};
    return dummy;
}

CTS_TEST(g, "array_zero_value")
    .desc("Tests zero value array constructors")
    .params([](ParamsBuilder u) { return u.combine("case", arrayCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const ArrayCase& c = findArrayCase(t.param<std::string>("case"));
        const std::string decl =
            std::string("array<") + c.element + ", " + c.size + ">";
        const std::string code =
            std::string("override o : i32 = 1;\n    struct valid_S {\n      x : u32\n    }\n"
                        "    struct invalid_S {\n      x : array<u32>\n    }\n"
                        "    const x : ") +
            decl + " = " + decl + "();";
        t.expectCompileResult(c.valid, code);
    });

CTS_TEST(g, "array_value")
    .desc("Tests array value constructor")
    .params([](ParamsBuilder u) { return u.combine("case", arrayCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const ArrayCase& c = findArrayCase(t.param<std::string>("case"));
        const std::string decl =
            std::string("array<") + c.element + ", " + c.size + ">";
        const std::string code =
            std::string("override o : i32 = 1;\n    struct valid_S {\n      x : u32\n    }\n"
                        "    struct invalid_S {\n      x : array<u32>\n    }\n"
                        "    const x : ") +
            decl + " = " + decl + "(" + c.values + ");";
        t.expectCompileResult(c.valid, code);
    });

// ---------------------------------------------------------------------------
// struct cases
// ---------------------------------------------------------------------------
struct StructCase {
    const char* name;
    const char* sname;
    const char* decls;
    bool valid;
    const char* values;
};
const std::vector<StructCase>& kStructCases() {
    static const std::vector<StructCase> v = {
        {"i32", "S", "struct S { x : u32 }", true, "0"},
        {"f32x2", "S", "struct S { x : f32, y : f32 }", true, "0,1"},
        {"vec3u", "S", "struct S { x : vec3u }", true, "vec3()"},
        {"valid_array", "S", "struct S { x : array<u32, 2> }", true, "array(1,2)"},
        {"runtime_array", "S", "struct S { x : array<u32> }", false, "array(0)"},
        {"atomic", "S", "struct S { x : atomic<u32> }", false, "0"},
        {"struct", "S",
         "struct S {\n      x : T\n    }\n    struct T {\n      x : u32\n    }", true, "T(0)"},
        {"many_members", "S",
         "struct S {\n      a : bool,\n      b : u32,\n      c : i32,\n      d : vec4f,\n    }",
         true, "false, 1u, 32i, vec4f(1.0f)"},
    };
    return v;
}
std::vector<Value> structCaseNames() {
    std::vector<Value> out;
    for (const StructCase& c : kStructCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
const StructCase& findStructCase(const std::string& name) {
    for (const StructCase& c : kStructCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const StructCase dummy{"", "", "", false, ""};
    return dummy;
}

CTS_TEST(g, "struct_zero_value")
    .desc("Tests zero value struct constructors")
    .params([](ParamsBuilder u) { return u.combine("case", structCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const StructCase& c = findStructCase(t.param<std::string>("case"));
        const std::string code =
            std::string("\n    ") + c.decls + "\n    const x : " + c.sname + " = " + c.sname + "();";
        t.expectCompileResult(c.valid, code);
    });

CTS_TEST(g, "struct_value")
    .desc("Tests struct value constructors")
    .params([](ParamsBuilder u) { return u.combine("case", structCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const StructCase& c = findStructCase(t.param<std::string>("case"));
        const std::string code = std::string("\n    ") + c.decls + "\n    const x : " + c.sname +
                                 " = " + c.sname + "(" + c.values + ");";
        t.expectCompileResult(c.valid, code);
    });

// ---------------------------------------------------------------------------
// must_use
// ---------------------------------------------------------------------------
struct CtorCase {
    const char* name;
    const char* code;
};
const std::vector<CtorCase>& kConstructors() {
    static const std::vector<CtorCase> v = {
        {"u32_0", "u32()"},
        {"i32_0", "i32()"},
        {"bool_0", "bool()"},
        {"f32_0", "f32()"},
        {"f16_0", "f16()"},
        {"vec2_0", "vec2()"},
        {"vec3_0", "vec3()"},
        {"vec4_0", "vec4()"},
        {"mat2x2_0", "mat2x2f()"},
        {"mat2x3_0", "mat2x3f()"},
        {"mat2x4_0", "mat2x4f()"},
        {"mat3x2_0", "mat3x2f()"},
        {"mat3x3_0", "mat3x3f()"},
        {"mat3x4_0", "mat3x4f()"},
        {"mat4x2_0_f16", "mat4x2h()"},
        {"mat4x3_0_f16", "mat4x3h()"},
        {"mat4x4_0_f16", "mat4x4h()"},
        {"S_0", "S()"},
        {"array_0", "array<u32, 4>()"},
        {"u32", "u32(1)"},
        {"i32", "i32(1)"},
        {"bool", "bool(true)"},
        {"f32", "f32(1)"},
        {"f16", "f16(1)"},
        {"vec2f", "vec2<f32>(1)"},
        {"vec3_f16", "vec3<f16>(1)"},
        {"vec4", "vec4(1)"},
        {"mat2x2", "mat2x2f(1,1,1,1)"},
        {"mat2x3", "mat2x3f(1,1,1,1,1,1)"},
        {"mat2x4", "mat2x4f(1,1,1,1,1,1,1,1)"},
        {"mat3x2_f16", "mat3x2<f16>(vec2h(),vec2h(),vec2h())"},
        {"mat3x3_f16", "mat3x3<f16>(vec3h(),vec3h(),vec3h())"},
        {"mat3x4_f16", "mat3x4<f16>(vec4h(),vec4h(),vec4h())"},
        {"mat4x2", "mat4x2(vec2(),vec2(),vec2(),vec2())"},
        {"mat4x3", "mat4x3(vec3(),vec3(),vec3(),vec3())"},
        {"mat4x4", "mat4x4(vec4(),vec4(),vec4(),vec4())"},
        {"S", "S(1,1)"},
        {"array_abs", "array(1,2,3)"},
        {"array", "array<u32, 4>(1,2,3,4)"},
    };
    return v;
}
std::vector<Value> ctorNames() {
    std::vector<Value> out;
    for (const CtorCase& c : kConstructors()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
const CtorCase& findCtor(const std::string& name) {
    for (const CtorCase& c : kConstructors()) {
        if (name == c.name) {
            return c;
        }
    }
    static const CtorCase dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "must_use")
    .desc("Tests that value constructors must be used")
    .params([](ParamsBuilder u) {
        return u.combine("ctor", ctorNames()).combine("use", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ctorName = t.param<std::string>("ctor");
        const bool use = t.param<bool>("use");
        const CtorCase& c = findCtor(ctorName);
        const bool hasF16 = ctorName.find("f16") != std::string::npos;
        const std::string code =
            std::string("\n    ") + (hasF16 ? "enable f16;" : "") +
            "\n    struct S {\n      x : u32,\n      y : f32,\n    }\n    fn foo() {\n      " +
            (use ? "_ =" : "") + " " + c.code + ";\n    }";
        t.expectCompileResult(use, code);
    });

// ---------------------------------------------------------------------------
// partial_eval
// ---------------------------------------------------------------------------
CTS_TEST(g, "partial_eval")
    .desc("Tests that mixed runtime and early eval expressions catch errors")
    .params([](ParamsBuilder u) {
        return u.combine("eleTy", {Value(std::string("i32")), Value(std::string("u32"))})
            .combine("compTy", {Value(std::string("array")), Value(std::string("vec2")),
                                Value(std::string("vec3")), Value(std::string("vec4")),
                                Value(std::string("S"))})
            .combine("stage", {Value(std::string("constant")), Value(std::string("runtime"))})
            .beginSubcases()
            .expand("numEles",
                    [](const ParamRecord& p) {
                        const std::string compTy = valueAs<std::string>(*findParam(p, "compTy"));
                        if (compTy == "array") {
                            return std::vector<Value>{Value(int64_t(2)), Value(int64_t(3))};
                        }
                        if (compTy == "vec2") {
                            return std::vector<Value>{Value(int64_t(2))};
                        }
                        if (compTy == "vec3") {
                            return std::vector<Value>{Value(int64_t(3))};
                        }
                        if (compTy == "vec4") {
                            return std::vector<Value>{Value(int64_t(4))};
                        }
                        // S
                        return std::vector<Value>{Value(int64_t(2))};
                    })
            .expand("index", [](const ParamRecord& p) {
                const int64_t numEles = valueAs<int64_t>(*findParam(p, "numEles"));
                std::vector<Value> out;
                for (int64_t i = 0; i < numEles; ++i) {
                    out.emplace_back(Value(i));
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string eleTy = t.param<std::string>("eleTy");
        const std::string compTy = t.param<std::string>("compTy");
        const std::string stage = t.param<std::string>("stage");
        const int numEles = static_cast<int>(t.param<int64_t>("numEles"));
        const int index = static_cast<int>(t.param<int64_t>("index"));
        // value = eleTy === 'i32' ? 0xfffffffff : -1; abstract-int spelling.
        const std::string valueWgsl = eleTy == "i32" ? "68719476735" : "-1";
        std::string compParams;
        for (int i = 0; i < numEles; i++) {
            if (index == i) {
                if (stage == "constant") {
                    compParams += valueWgsl + ", ";
                } else {
                    compParams += "v, ";
                }
            } else {
                compParams += "v, ";
            }
        }
        const std::string wgsl = std::string("\nstruct S {\n  x : ") + eleTy + ",\n  y : " + eleTy +
                                 ",\n}\n\nfn foo() {\n  var v : " + eleTy + ";\n  let tmp = " +
                                 compTy + "(" + compParams + ");\n}";
        const bool shaderError = stage == "constant";
        t.expectCompileResult(!shaderError, wgsl);
    });

}  // namespace
