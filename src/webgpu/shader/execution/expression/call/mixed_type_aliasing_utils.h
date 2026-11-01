// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/mixed_type_aliasing_utils.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Utilities for testing that differently typed pointers to overlapping bytes of a buffer, formed
// with bufferView or bufferArrayView, behave correctly. i.e. that implementations do not apply
// type-based alias analysis. Used by the bufferView, bufferArrayView and pointer aliasing tests.
//
// Port notes:
// - Upstream's Record tables become ordered vectors, so keysOf() order and query identities match.
// - The tables are function-local statics, because test params are built during static
//   initialization of the spec files.
// - 'int' is a C++ keyword, so the parameter and members of that name are 'intType'.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"
#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

namespace cts::mixed_type {

/** @returns the bit pattern of the f32 value 'f' as an i32. */
inline int32_t f32Bits(float f) {
    int32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/** @returns the size in bytes of the scalar type 's'. */
inline int mixedScalarSize(const std::string& s) {
    return s == "f16" ? 2 : 4;
}

/** A scalar lane of a mixed type. */
struct MixedLane {
    /** The scalar type of the lane. */
    std::string elem;
    /** The byte offset of the lane from the start of the value. */
    int offset = 0;
    /** The WGSL accessor suffix for the lane. e.g. '', '[1]', '.a' or '.v[1]' */
    std::string access;
};

struct MixedType;

/** A structure member, with its byte offset. */
struct MixedMember {
    std::string name;
    const MixedType* type = nullptr;
    int offset = 0;
};

/**
 * A scalar, vector or structure type used by the mixed type aliasing tests.
 * A value of the type is treated as a flat list of scalar lanes. Structures may contain padding,
 * which is not part of any lane.
 */
struct MixedType {
    /** The WGSL type name. */
    std::string name;
    /** The size in bytes. This is also the array element stride. */
    int size = 0;
    /** The alignment in bytes. */
    int align = 0;
    /** The scalar lanes, in declaration order. */
    std::vector<MixedLane> lanes;
    /** True if the type is a structure. */
    bool isStruct = false;
    /** The structure members. */
    std::vector<MixedMember> members;
};

inline MixedType mixedScalar(const std::string& elem) {
    const int size = mixedScalarSize(elem);
    MixedType type;
    type.name = elem;
    type.size = size;
    type.align = size;
    type.lanes.push_back(MixedLane{elem, 0, ""});
    return type;
}

inline MixedType mixedVec(int n, const std::string& elem) {
    MixedType type;
    type.name = "vec" + std::to_string(n) + "<" + elem + ">";
    type.size = n * mixedScalarSize(elem);
    type.align = type.size;
    for (int k = 0; k < n; ++k) {
        type.lanes.push_back(MixedLane{elem, k * mixedScalarSize(elem), "[" + std::to_string(k) + "]"});
    }
    return type;
}

/** @returns a structure type, laid out with the WGSL layout rules. */
inline MixedType mixedStruct(
    const std::string& name,
    const std::vector<std::pair<std::string, const MixedType*>>& members) {
    const auto roundUp = [](int n, int k) { return ((n + k - 1) / k) * k; };
    int end = 0;
    int align = 1;
    MixedType type;
    type.name = name;
    type.isStruct = true;
    for (const auto& [memberName, memberType] : members) {
        const int offset = roundUp(end, memberType->align);
        end = offset + memberType->size;
        align = std::max(align, memberType->align);
        type.members.push_back(MixedMember{memberName, memberType, offset});
    }
    for (const MixedMember& member : type.members) {
        for (const MixedLane& lane : member.type->lanes) {
            type.lanes.push_back(
                MixedLane{lane.elem, member.offset + lane.offset, "." + member.name + lane.access});
        }
    }
    type.size = roundUp(end, align);
    type.align = align;
    return type;
}

/** The types used by the tests. Members are declared in dependency order. */
struct MixedTypes {
    MixedType kI32 = mixedScalar("i32");
    MixedType kU32 = mixedScalar("u32");
    MixedType kF32 = mixedScalar("f32");
    MixedType kF16 = mixedScalar("f16");
    MixedType kVec2I = mixedVec(2, "i32");
    MixedType kVec4I = mixedVec(4, "i32");
    MixedType kVec4U = mixedVec(4, "u32");
    MixedType kVec2F = mixedVec(2, "f32");
    MixedType kVec4F = mixedVec(4, "f32");
    MixedType kVec2H = mixedVec(2, "f16");
    MixedType kVec4H = mixedVec(4, "f16");
    // Structures without padding.
    MixedType kStructI = mixedStruct("SI", {{"a", &kI32}, {"b", &kI32}, {"c", &kI32}, {"d", &kI32}});
    MixedType kStructF = mixedStruct("SF", {{"a", &kF32}, {"b", &kF32}, {"c", &kF32}, {"d", &kF32}});
    MixedType kStructV = mixedStruct("SV", {{"v", &kVec2I}, {"s", &kI32}, {"t", &kI32}});
    // Structures with padding.
    /** 4 bytes of padding between 'a' and 'b'. */
    MixedType kPaddedI = mixedStruct("PI", {{"a", &kI32}, {"b", &kVec2I}});
    /** 4 bytes of padding between 'a' and 'b'. */
    MixedType kPaddedF = mixedStruct("PF", {{"a", &kF32}, {"b", &kVec2F}});
    /** 4 bytes of trailing padding. */
    MixedType kPaddedTrailingF = mixedStruct("PT", {{"v", &kVec2F}, {"s", &kF32}});
    /** 2 bytes of padding between 'a' and 'b', within the first 4-byte word. */
    MixedType kPaddedH = mixedStruct("PH", {{"a", &kF16}, {"b", &kF32}});
};

inline const MixedTypes& mixedTypes() {
    static const MixedTypes types;
    return types;
}

/** @returns true if the type 't' has any f16 lanes. */
inline bool mixedTypeUsesF16(const MixedType& t) {
    return std::any_of(t.lanes.begin(), t.lanes.end(), [](const MixedLane& l) { return l.elem == "f16"; });
}

/** @returns the WGSL name of the type 't'. */
inline const std::string& mixedTypeName(const MixedType& t) {
    return t.name;
}

inline std::string joinStrings(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += separator;
        }
        out += parts[i];
    }
    return out;
}

/** @returns a WGSL constructor expression for a value of type 't', given an expression per lane. */
inline std::string mixedTypeCtorFromLanes(const MixedType& t, const std::vector<std::string>& lanes) {
    if (t.isStruct) {
        size_t next = 0;
        std::vector<std::string> members;
        for (const MixedMember& member : t.members) {
            const size_t n = member.type->lanes.size();
            members.push_back(mixedTypeCtorFromLanes(
                *member.type,
                std::vector<std::string>(lanes.begin() + static_cast<std::ptrdiff_t>(next),
                                         lanes.begin() + static_cast<std::ptrdiff_t>(next + n))));
            next += n;
        }
        return t.name + "(" + joinStrings(members, ", ") + ")";
    }
    return t.lanes.size() == 1 ? lanes[0] : t.name + "(" + joinStrings(lanes, ", ") + ")";
}

/**
 * @returns a WGSL constructor expression for a value of type 't', where lane 'k' has the value
 * 'base + k', and 'base' is a WGSL scalar expression.
 */
inline std::string mixedTypeCtor(const MixedType& t, const std::string& base) {
    std::vector<std::string> lanes;
    for (size_t k = 0; k < t.lanes.size(); ++k) {
        const std::string& elem = t.lanes[k].elem;
        lanes.push_back(k == 0 ? elem + "(" + base + ")"
                               : elem + "(" + base + ") + " + elem + "(" + std::to_string(k) + ")");
    }
    return mixedTypeCtorFromLanes(t, lanes);
}

/** @returns the WGSL declarations of the structures used by the types 'types', including nested. */
inline std::string mixedTypeStructDecls(const std::vector<const MixedType*>& types) {
    std::vector<std::pair<std::string, std::string>> decls;
    const std::function<void(const MixedType&)> visit = [&](const MixedType& t) {
        const bool seen = std::any_of(decls.begin(), decls.end(), [&](const auto& d) { return d.first == t.name; });
        if (!t.isStruct || seen) {
            return;
        }
        for (const MixedMember& member : t.members) {
            visit(*member.type);
        }
        std::vector<std::string> members;
        for (const MixedMember& member : t.members) {
            members.push_back(member.name + " : " + member.type->name);
        }
        decls.emplace_back(t.name, "struct " + t.name + " { " + joinStrings(members, ", ") + " }");
    };
    for (const MixedType* type : types) {
        visit(*type);
    }
    std::vector<std::string> values;
    for (const auto& decl : decls) {
        values.push_back(decl.second);
    }
    return joinStrings(values, "\n");
}

/**
 * @returns WGSL declarations required by the mixed type ops, where 'intType' is the type of the
 * loaded view, and 'other' is the type of the stored view. This declares the structures used by the
 * types and the functions:
 *   'mixed_sub(a, b)' - lane-wise 'a - b'
 *   'mixed_xor(a, b)' - lane-wise 'a ^ b'
 * If either type uses f16, the shader must also 'enable f16;'.
 */
inline std::string mixedTypeDecls(const MixedType& intType, const MixedType& other) {
    const std::string& ty = mixedTypeName(intType);
    const auto laneWise = [&](const std::string& name, const std::string& op) {
        const std::string signature = "fn " + name + "(a : " + ty + ", b : " + ty + ") -> " + ty + " {";
        if (!intType.isStruct) {
            return signature + " return a " + op + " b; }";
        }
        std::vector<std::string> lanes;
        for (const MixedLane& lane : intType.lanes) {
            lanes.push_back("  r" + lane.access + " = a" + lane.access + " " + op + " b" + lane.access + ";");
        }
        return signature + "\n  var r : " + ty + ";\n" + joinStrings(lanes, "\n") + "\n  return r;\n}";
    };
    return mixedTypeStructDecls({&intType, &other}) + "\n" + laneWise("mixed_sub", "-") + "\n" +
           laneWise("mixed_xor", "^") + "\n";
}

/**
 * @returns WGSL statements that write each lane of the value 'value' of type 't', as an i32 bit
 * pattern, to consecutive elements of the i32 array 'array'. All lanes of 't' must be 32-bit.
 */
inline std::string mixedTypeWriteLanes(const MixedType& t, const std::string& value, const std::string& array) {
    std::vector<std::string> statements;
    for (size_t k = 0; k < t.lanes.size(); ++k) {
        statements.push_back(array + "[" + std::to_string(k) + "] = bitcast<i32>(" + value + t.lanes[k].access + ");");
    }
    return joinStrings(statements, "\n  ");
}

/**
 * A pair of view types.
 * 'intType' is a type with only i32 or u32 lanes, and at most 4 lanes. It is the only view that is
 * loaded from, which keeps the expected results exact: float values are never loaded, so denormal
 * flushing and NaN canonicalization cannot affect the results.
 * 'other' is a type that is only stored to.
 */
struct MixedTypePair {
    std::string name;
    const MixedType* intType = nullptr;
    const MixedType* other = nullptr;
};

/** The pairs of view types tested. */
inline const std::vector<MixedTypePair>& mixedTypePairs() {
    static const std::vector<MixedTypePair> pairs = [] {
        const MixedTypes& k = mixedTypes();
        return std::vector<MixedTypePair>{
            // scalar - scalar
            {"i32_f32", &k.kI32, &k.kF32},
            {"u32_f32", &k.kU32, &k.kF32},
            {"i32_i32", &k.kI32, &k.kI32},
            // vector - scalar
            {"vec2i_f32", &k.kVec2I, &k.kF32},
            {"vec4i_f32", &k.kVec4I, &k.kF32},
            {"vec4u_f32", &k.kVec4U, &k.kF32},
            {"vec4u_u32", &k.kVec4U, &k.kU32},
            // scalar - vector
            {"i32_vec2f", &k.kI32, &k.kVec2F},
            {"i32_vec4f", &k.kI32, &k.kVec4F},
            {"i32_vec4u", &k.kI32, &k.kVec4U},
            {"i32_vec4i", &k.kI32, &k.kVec4I},
            // vector - vector
            {"vec2i_vec2f", &k.kVec2I, &k.kVec2F},
            {"vec4i_vec4f", &k.kVec4I, &k.kVec4F},
            {"vec4u_vec4f", &k.kVec4U, &k.kVec4F},
            {"vec4i_vec2f", &k.kVec4I, &k.kVec2F},
            {"vec2i_vec4f", &k.kVec2I, &k.kVec4F},
            {"vec4i_vec4u", &k.kVec4I, &k.kVec4U},
            {"vec4i_vec4i", &k.kVec4I, &k.kVec4I},
            // struct - scalar
            {"structi_f32", &k.kStructI, &k.kF32},
            {"structv_f32", &k.kStructV, &k.kF32},
            {"i32_structf", &k.kI32, &k.kStructF},
            // struct - vector
            {"structi_vec4f", &k.kStructI, &k.kVec4F},
            {"structi_vec2f", &k.kStructI, &k.kVec2F},
            {"vec4i_structf", &k.kVec4I, &k.kStructF},
            // struct - struct
            {"structi_structf", &k.kStructI, &k.kStructF},
            {"structv_structf", &k.kStructV, &k.kStructF},
            // f16. The f16 stores only write part of a 4-byte word.
            {"i32_f16", &k.kI32, &k.kF16},
            {"u32_f16", &k.kU32, &k.kF16},
            {"i32_vec2h", &k.kI32, &k.kVec2H},
            {"i32_vec4h", &k.kI32, &k.kVec4H},
            {"vec4i_f16", &k.kVec4I, &k.kF16},
            {"vec2i_vec4h", &k.kVec2I, &k.kVec4H},
            {"vec4i_vec4h", &k.kVec4I, &k.kVec4H},
            // padded struct. Stores of padded structures must not write the padding.
            {"paddedi_f32", &k.kPaddedI, &k.kF32},
            {"paddedi_vec4f", &k.kPaddedI, &k.kVec4F},
            {"i32_paddedf", &k.kI32, &k.kPaddedF},
            {"vec4i_paddedf", &k.kVec4I, &k.kPaddedF},
            {"i32_paddedtf", &k.kI32, &k.kPaddedTrailingF},
            {"vec4i_paddedtf", &k.kVec4I, &k.kPaddedTrailingF},
            {"paddedi_paddedf", &k.kPaddedI, &k.kPaddedF},
            {"paddedi_structf", &k.kPaddedI, &k.kStructF},
            {"structi_paddedf", &k.kStructI, &k.kPaddedF},
            // padded struct with f16
            {"i32_paddedh", &k.kI32, &k.kPaddedH},
            {"vec4i_paddedh", &k.kVec4I, &k.kPaddedH},
            {"paddedi_paddedh", &k.kPaddedI, &k.kPaddedH},
        };
    }();
    return pairs;
}

inline const MixedTypePair& mixedTypePair(const std::string& name) {
    for (const MixedTypePair& pair : mixedTypePairs()) {
        if (pair.name == name) {
            return pair;
        }
    }
    std::abort();
}

/** keysOf(kMixedTypePairs) */
inline std::vector<Value> mixedTypePairNames() {
    std::vector<Value> names;
    for (const MixedTypePair& pair : mixedTypePairs()) {
        names.emplace_back(pair.name);
    }
    return names;
}

/**
 * How the two views overlap. Views always start on a 4-byte boundary.
 *  'same_start' - Both views start at the same byte.
 *  'offset'     - The views start at different bytes, and a lane of one view overlaps a lane of
 *                 the other. Not all pairs of types can do this while respecting alignment.
 *  'padding'    - The views overlap, but only lanes of one view overlap padding of the other.
 *                 Only possible for structures with padding.
 */
inline std::vector<Value> mixedTypeOverlaps() {
    return {"same_start", "offset", "padding"};
}

struct MixedTypeViewWords {
    int intWord = 0;
    int otherWord = 0;
};

/**
 * @returns the word offsets of the 'intType' and 'other' views for the given overlap, or nullopt if
 * the types cannot be positioned to satisfy the overlap. Word offsets are always at least 4.
 */
inline std::optional<MixedTypeViewWords> mixedTypeWords(
    const MixedType& intType,
    const MixedType& other,
    const std::string& overlap) {
    const auto lanesOverlap = [&](int a, int b) {
        for (const MixedLane& li : intType.lanes) {
            for (const MixedLane& lo : other.lanes) {
                const int i0 = a * 4 + li.offset;
                const int o0 = b * 4 + lo.offset;
                if (i0 < o0 + mixedScalarSize(lo.elem) && o0 < i0 + mixedScalarSize(li.elem)) {
                    return true;
                }
            }
        }
        return false;
    };
    const auto viewsOverlap = [&](int a, int b) { return a * 4 < b * 4 + other.size && b * 4 < a * 4 + intType.size; };
    for (int a = 4; a < 12; ++a) {
        if ((a * 4) % intType.align != 0) {
            continue;
        }
        for (int b = 4; b < 12; ++b) {
            if ((b * 4) % other.align != 0) {
                continue;
            }
            bool ok = false;
            if (overlap == "same_start") {
                ok = a == b;
            } else if (overlap == "offset") {
                ok = a != b && lanesOverlap(a, b);
            } else if (overlap == "padding") {
                ok = viewsOverlap(a, b) && !lanesOverlap(a, b);
            }
            if (ok) {
                return MixedTypeViewWords{a, b};
            }
        }
    }
    return std::nullopt;
}

/** The params filter shared by the tests: keeps a 'pair' and 'overlap' that can be positioned. */
inline bool isValidMixedTypeOverlap(const ParamRecord& p) {
    const MixedTypePair& pair = mixedTypePair(valueAs<std::string>(*findParam(p, "pair")));
    return mixedTypeWords(*pair.intType, *pair.other, valueAs<std::string>(*findParam(p, "overlap"))).has_value();
}

/** A simulated memory of 4-byte words, used to calculate the expected results of mixed type tests. */
struct MixedTypeMemory {
    std::vector<int32_t> mem;
    int32_t ld(int ptr) const {
        return mem[static_cast<size_t>(ptr)];
    }
    void st(int ptr, int32_t value) {
        mem[static_cast<size_t>(ptr)] = value;
    }
};

/** The runtime arguments passed to a mixed type op. */
struct MixedTypeArgs {
    /** A value to write through the 'intType' view. Also used as a second value for the 'other' view. */
    int va = 0;
    /** A value to write through the 'other' view. */
    int vf = 0;
    /** A loop count. */
    int n = 0;
};

/**
 * Stores a value of type 't' where lane 'k' is 'base + k', at word 'ptr'.
 * Only the bytes of the lanes are written. Padding is left untouched.
 */
inline void simStore(MixedTypeMemory& m, const MixedType& t, int ptr, int base) {
    for (size_t k = 0; k < t.lanes.size(); ++k) {
        const MixedLane& lane = t.lanes[k];
        const int v = base + static_cast<int>(k);
        const int word = ptr + lane.offset / 4;
        if (lane.elem == "i32" || lane.elem == "u32") {
            m.st(word, v);
        } else if (lane.elem == "f32") {
            m.st(word, f32Bits(static_cast<float>(v)));
        } else {
            const int shift = (lane.offset % 4) * 8;
            const uint32_t mask = uint32_t{0xffff} << shift;
            const uint32_t bits = uint32_t{expression::access::f16BitsOfDouble(v)} << shift;
            const uint32_t old = static_cast<uint32_t>(m.ld(word));
            m.st(word, static_cast<int32_t>((old & ~mask) | (bits & mask)));
        }
    }
}

/** @returns the lanes of a value of type 't' at word 'ptr', as i32 bit patterns. */
inline std::vector<int32_t> simLoad(const MixedTypeMemory& m, const MixedType& t, int ptr) {
    std::vector<int32_t> values;
    for (const MixedLane& lane : t.lanes) {
        if (lane.elem != "i32" && lane.elem != "u32") {
            std::abort(); // Only integer views are loaded.
        }
        values.push_back(m.ld(ptr + lane.offset / 4));
    }
    return values;
}

/**
 * An operation on a pointer 'pi' to an integer type, and a pointer 'pf' to another type, that may
 * refer to overlapping memory. Shaders using these ops must include mixedTypeDecls(intType, other).
 */
struct MixedTypeOp {
    std::string name;
    /**
     * @returns a WGSL function body that returns a value of type 'intType', and may use:
     *   'pi' - a pointer to 'intType'
     *   'pf' - a pointer to 'other'
     *   'va' - an i32 value
     *   'vf' - an f32 value, whose value is exactly representable as an f16
     *   'n'  - an i32 loop count
     */
    std::string (*wgsl)(const MixedType& intType, const MixedType& other);
    /**
     * Simulates 'wgsl', where 'pi' and 'pf' are word offsets into 'm'.
     * @returns the lanes of the returned value, as i32 bit patterns.
     */
    std::vector<int32_t> (*sim)(
        MixedTypeMemory& m,
        const MixedType& intType,
        const MixedType& other,
        int pi,
        int pf,
        const MixedTypeArgs& args);
};

/**
 * Operations that produce different results if an implementation assumes that differently typed
 * pointers do not alias (i.e. applies C/C++ style type-based alias analysis).
 */
inline const std::vector<MixedTypeOp>& mixedTypeOps() {
    static const std::vector<MixedTypeOp> ops = {
        {"int_store_other_store_int_load",
         [](const MixedType& intType, const MixedType& other) {
             return "\n  *pi = " + mixedTypeCtor(intType, "va") + ";\n  *pf = " + mixedTypeCtor(other, "vf") +
                    ";\n  return *pi;";
         },
         [](MixedTypeMemory& m, const MixedType& intType, const MixedType& other, int pi, int pf,
            const MixedTypeArgs& args) {
             simStore(m, intType, pi, args.va);
             simStore(m, other, pf, args.vf);
             return simLoad(m, intType, pi);
         }},
        {"other_store_int_load",
         [](const MixedType&, const MixedType& other) {
             return "\n  *pf = " + mixedTypeCtor(other, "vf") + ";\n  return *pi;";
         },
         [](MixedTypeMemory& m, const MixedType& intType, const MixedType& other, int pi, int pf,
            const MixedTypeArgs& args) {
             simStore(m, other, pf, args.vf);
             return simLoad(m, intType, pi);
         }},
        {"int_load_other_store_int_load",
         [](const MixedType&, const MixedType& other) {
             return "\n  let before = *pi;\n  *pf = " + mixedTypeCtor(other, "vf") +
                    ";\n  return mixed_sub(*pi, before);";
         },
         [](MixedTypeMemory& m, const MixedType& intType, const MixedType& other, int pi, int pf,
            const MixedTypeArgs& args) {
             const std::vector<int32_t> before = simLoad(m, intType, pi);
             simStore(m, other, pf, args.vf);
             std::vector<int32_t> after = simLoad(m, intType, pi);
             for (size_t k = 0; k < after.size(); ++k) {
                 after[k] = static_cast<int32_t>(static_cast<uint32_t>(after[k]) - static_cast<uint32_t>(before[k]));
             }
             return after;
         }},
        // The first store to '*pf' is only dead if '*pi' does not alias '*pf'.
        {"dead_other_store",
         [](const MixedType&, const MixedType& other) {
             return "\n  *pf = " + mixedTypeCtor(other, "vf") + ";\n  let tmp = *pi;\n  *pf = " +
                    mixedTypeCtor(other, "va") + ";\n  return tmp;";
         },
         [](MixedTypeMemory& m, const MixedType& intType, const MixedType& other, int pi, int pf,
            const MixedTypeArgs& args) {
             simStore(m, other, pf, args.vf);
             std::vector<int32_t> tmp = simLoad(m, intType, pi);
             simStore(m, other, pf, args.va);
             return tmp;
         }},
        // The load of '*pi' must not be hoisted out of the loop.
        {"loop_other_store_int_load",
         [](const MixedType& intType, const MixedType& other) {
             return "\n  var acc = " + mixedTypeName(intType) +
                    "();\n  for (var i = 0; i < n; i++) {\n    *pf = " + mixedTypeCtor(other, "i") +
                    ";\n    acc = mixed_xor(acc, *pi);\n  }\n  return acc;";
         },
         [](MixedTypeMemory& m, const MixedType& intType, const MixedType& other, int pi, int pf,
            const MixedTypeArgs& args) {
             std::vector<int32_t> acc(intType.lanes.size(), 0);
             for (int i = 0; i < args.n; ++i) {
                 simStore(m, other, pf, i);
                 const std::vector<int32_t> loaded = simLoad(m, intType, pi);
                 for (size_t k = 0; k < acc.size(); ++k) {
                     acc[k] ^= loaded[k];
                 }
             }
             return acc;
         }},
    };
    return ops;
}

inline const MixedTypeOp& mixedTypeOp(const std::string& name) {
    for (const MixedTypeOp& op : mixedTypeOps()) {
        if (op.name == name) {
            return op;
        }
    }
    std::abort();
}

/** keysOf(kMixedTypeOps) */
inline std::vector<Value> mixedTypeOpNames() {
    std::vector<Value> names;
    for (const MixedTypeOp& op : mixedTypeOps()) {
        names.emplace_back(op.name);
    }
    return names;
}

/** The number of 4-byte words in each of the buffers 'A' and 'B' used by runMixedTypeAliasingTest. */
inline constexpr int kMixedTypeBufferWords = 32;

/**
 * The runtime parameters passed to the shader of runMixedTypeAliasingTest in 'input.p'.
 *  p[0]: 'va'  - an i32 value to write.
 *  p[1]: 'vf'  - an f32 value to write, passed as an i32 and converted to f32.
 *  p[2]: 'n'   - a loop count.
 *  p[3]: 'idx' - a runtime value that views may use to form offsets and indices.
 */
inline constexpr std::array<int32_t, 4> kMixedTypeParams = {1000, 2000, 4, 1};

/** The value of 'input.p[3]', which may be used by views to form offsets and indices. */
inline constexpr int kMixedTypeIdx = kMixedTypeParams[3];

/**
 * The kinds of buffer that the views are formed on:
 *  'workgroup'       - var<workgroup> of type buffer<N>
 *  'storage'         - var<storage, read_write> of type buffer<N>
 *  'storage_unsized' - var<storage, read_write> of type buffer
 */
inline std::vector<Value> mixedTypeBuffers() {
    return {"workgroup", "storage", "storage_unsized"};
}

struct MixedTypeAliasingParams {
    /** The kind of buffer the views are formed on. */
    std::string buffer;
    /** The type of the view that is loaded from. */
    const MixedType* intType = nullptr;
    /** The type of the view that is only stored to. */
    const MixedType* other = nullptr;
    /** The word offset of the 'intType' view. */
    int intWord = 0;
    /** The word offset of the 'other' view. */
    int otherWord = 0;
    /**
     * @returns a WGSL expression for a pointer to a value of type 'type' at word offset 'word' of the
     * buffer variable named 'buffer'. May use 'input.p[3]' as a runtime value.
     */
    std::function<std::string(const MixedType& type, const std::string& buffer, int word)> view;
    /**
     * If true, the 'other' view is formed in the same buffer as the 'intType' view.
     * Otherwise it is formed in a different buffer.
     */
    bool aliased = false;
    /** The operation to perform. */
    const MixedTypeOp* op = nullptr;
    /**
     * If true, the views are formed in the entry point and passed to 'f' as pointer parameters.
     * Otherwise the views are formed within 'f'.
     */
    bool pointerParams = false;
    /** WGSL directives placed at the start of the shader. */
    std::string directives;
};

/**
 * Runs a test where two differently typed pointers are formed from buffer views and used within a
 * single function, or passed to a function as pointer parameters if 'pointerParams' is true.
 * If 'aliased' is true, then both views refer to the same buffer, otherwise the 'other' view refers
 * to a different buffer.
 *
 * The shader initializes buffers 'A' and 'B' from 'input', calls the op, and writes the lanes of
 * the result of the op, followed by the contents of 'A' and 'B', to 'output'.
 */
inline void runMixedTypeAliasingTest(GpuTest& t, const MixedTypeAliasingParams& params) {
    if (!hasBufferViewLanguageFeature()) {
        t.skip("Missing buffer_view language feature");
    }
    const bool usesF16 = mixedTypeUsesF16(*params.intType) || mixedTypeUsesF16(*params.other);
    if (usesF16 && wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16) == WGPU_FALSE) {
        t.skip("Missing shader-f16 feature");
    }

    const int N = kMixedTypeBufferWords;
    const std::string bytes = std::to_string(N * 4);
    const std::string words = std::to_string(N);
    std::string decls;
    if (params.buffer == "workgroup") {
        decls = "var<workgroup> A : buffer<" + bytes + ">;\nvar<workgroup> B : buffer<" + bytes + ">;";
    } else if (params.buffer == "storage") {
        decls = "@group(0) @binding(2) var<storage, read_write> A : buffer<" + bytes +
                ">;\n@group(0) @binding(3) var<storage, read_write> B : buffer<" + bytes + ">;";
    } else {
        decls =
            "@group(0) @binding(2) var<storage, read_write> A : buffer;\n"
            "@group(0) @binding(3) var<storage, read_write> B : buffer;";
    }

    const std::string& intTy = mixedTypeName(*params.intType);
    const std::string& otherTy = mixedTypeName(*params.other);
    const std::string writeResult = mixedTypeWriteLanes(*params.intType, "r", "output.r");
    const std::string viewI = params.view(*params.intType, "A", params.intWord);
    const std::string viewF = params.view(*params.other, params.aliased ? "A" : "B", params.otherWord);
    // Only storage pointers may specify an access mode.
    const auto ptr = [&](const std::string& ty) {
        return params.buffer == "workgroup" ? "ptr<workgroup, " + ty + ">" : "ptr<storage, " + ty + ", read_write>";
    };
    const std::string opBody = params.op->wgsl(*params.intType, *params.other);
    std::string fn;
    std::string call;
    if (params.pointerParams) {
        fn = "fn f(pi : " + ptr(intTy) + ", pf : " + ptr(otherTy) + ",\n     va : i32, vf : f32, n : i32) -> " +
             intTy + " {\n  " + opBody + "\n}";
        call = "f(" + viewI + ", " + viewF + ", input.p[0], f32(input.p[1]), input.p[2])";
    } else {
        fn = "fn f(va : i32, vf : f32, n : i32) -> " + intTy + " {\n  let pi = " + viewI + ";\n  let pf = " + viewF +
             ";\n  " + opBody + "\n}";
        call = "f(input.p[0], f32(input.p[1]), input.p[2])";
    }

    const std::string wgsl = params.directives + "\n" + (usesF16 ? "enable f16;" : "") + R"(
struct In {
  a : array<i32, )" + words + R"(>,
  b : array<i32, )" + words + R"(>,
  p : array<i32, 4>,
}

struct Out {
  r : array<i32, 4>,
  a : array<i32, )" + words + R"(>,
  b : array<i32, )" + words + R"(>,
}

@group(0) @binding(0) var<storage, read> input : In;
@group(0) @binding(1) var<storage, read_write> output : Out;

)" + decls + "\n\n" + mixedTypeDecls(*params.intType, *params.other) + "\n\n" + fn + R"(

@compute @workgroup_size(1)
fn main() {
  *bufferView<array<i32, )" + words + R"(>>(&A, 0) = input.a;
  *bufferView<array<i32, )" + words + R"(>>(&B, 0) = input.b;
  let r = )" + call + R"(;
  )" + writeResult + R"(
  output.a = *bufferView<array<i32, )" + words + R"(>>(&A, 0);
  output.b = *bufferView<array<i32, )" + words + R"(>>(&B, 0);
}
)";

    // Simulate the op. 'A' is at word 0, and 'B' is at word N.
    std::vector<int32_t> init;
    for (int i = 0; i < N; ++i) {
        init.push_back(10 + i);
    }
    for (int i = 0; i < N; ++i) {
        init.push_back(100 + i);
    }
    MixedTypeMemory memory;
    memory.mem = init;
    const MixedTypeArgs args{kMixedTypeParams[0], kMixedTypeParams[1], kMixedTypeParams[2]};
    const int pi = params.intWord;
    const int pf = (params.aliased ? 0 : N) + params.otherWord;
    const std::vector<int32_t> r = params.op->sim(memory, *params.intType, *params.other, pi, pf, args);
    std::vector<int32_t> expected(4, 0);
    std::copy(r.begin(), r.end(), expected.begin());
    expected.insert(expected.end(), memory.mem.begin(), memory.mem.end());

    WGPUComputePipeline pipeline = atomics::createComputePipelineAuto(t, wgsl);

    std::vector<int32_t> inputData = init;
    inputData.insert(inputData.end(), kMixedTypeParams.begin(), kMixedTypeParams.end());
    const uint64_t inputSize = inputData.size() * sizeof(int32_t);
    WGPUBuffer inputBuffer =
        t.makeBufferWithContents(inputData.data(), static_cast<size_t>(inputSize), WGPUBufferUsage_Storage);
    const uint64_t outputSize = expected.size() * sizeof(int32_t);
    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = outputSize;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    std::vector<atomics::BufferBinding> entries = {{inputBuffer, inputSize}, {outputBuffer, outputSize}};
    if (params.buffer != "workgroup") {
        for (int binding = 2; binding <= 3; ++binding) {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = static_cast<uint64_t>(N) * 4;
            desc.usage = WGPUBufferUsage_Storage;
            entries.push_back({t.createBufferTracked(desc), desc.size});
        }
    }
    WGPUBindGroup bg = atomics::makeAutoBindGroup(t, pipeline, entries);

    atomics::runComputePassX(t, pipeline, bg, 1);

    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), static_cast<size_t>(outputSize));
}

} // namespace cts::mixed_type
