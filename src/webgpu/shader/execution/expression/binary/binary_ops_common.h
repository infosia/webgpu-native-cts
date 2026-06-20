// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/*.cache.ts and
// src/webgpu/util/math.ts + case.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Shared case-generation helpers for the integer/abstract-int binary expression ports. Mirrors the
// upstream interesting-value ranges (sparse{I32,U32,I64}Range, vector{I32,U32,I64}Range), the
// quantize functions, and the generateBinaryTo* / generate*VectorBinaryToVector* generators (which
// are the const cartesian / scalar-vector spread builders). All values are computed exactly; the
// op callbacks return std::optional to model the upstream 'undefined' (skip-case) result.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

namespace cts {
namespace expression {
namespace binary_ops {

// ---- Interesting value ranges (math.ts) -----------------------------------------------------

// sparseI32Range() == kInterestingI32Values. Note kValue.i32.negative.max == 0, so the first two
// entries are 0 (negative.max and trunc(negative.max/2)).
inline const std::vector<int32_t>& sparseI32Range() {
    static const std::vector<int32_t> v = {0, 0, -256, -10, -1, 0, 1, 10, 256, 1073741823,
                                           2147483647};
    return v;
}

// sparseU32Range() == kInterestingU32Values.
inline const std::vector<uint32_t>& sparseU32Range() {
    static const std::vector<uint32_t> v = {0u, 1u, 10u, 256u, 2147483647u, 4294967295u};
    return v;
}

// sparseI64Range() == kInterestingI64Values. kValue.i64.negative.max == 0 (so first two are 0).
inline const std::vector<int64_t>& sparseI64Range() {
    static const std::vector<int64_t> v = {0,
                                           0,
                                           -256,
                                           -10,
                                           -1,
                                           0,
                                           1,
                                           10,
                                           256,
                                           4611686018427387903LL,
                                           9223372036854775807LL};
    return v;
}

// vectorI32Range(dim): the interesting-value spread vectors (kVectorI32Values).
inline std::vector<std::vector<int32_t>> vectorI32Range(int dim) {
    std::vector<std::vector<int32_t>> out;
    for (int32_t f : sparseI32Range()) {
        if (dim == 2) {
            out.push_back({f, 1});
            out.push_back({-1, f});
        } else if (dim == 3) {
            out.push_back({f, 1, -2});
            out.push_back({-1, f, 2});
            out.push_back({1, -2, f});
        } else {
            out.push_back({f, -1, 2, 3});
            out.push_back({1, f, -2, 3});
            out.push_back({1, 2, f, -3});
            out.push_back({-1, 2, -3, f});
        }
    }
    return out;
}

// vectorU32Range(dim): kVectorU32Values.
inline std::vector<std::vector<uint32_t>> vectorU32Range(int dim) {
    std::vector<std::vector<uint32_t>> out;
    for (uint32_t f : sparseU32Range()) {
        if (dim == 2) {
            out.push_back({f, 1u});
            out.push_back({1u, f});
        } else if (dim == 3) {
            out.push_back({f, 1u, 2u});
            out.push_back({1u, f, 2u});
            out.push_back({1u, 2u, f});
        } else {
            out.push_back({f, 1u, 2u, 3u});
            out.push_back({1u, f, 2u, 3u});
            out.push_back({1u, 2u, f, 3u});
            out.push_back({1u, 2u, 3u, f});
        }
    }
    return out;
}

// vectorI64Range(dim): kVectorI64Values.
inline std::vector<std::vector<int64_t>> vectorI64Range(int dim) {
    std::vector<std::vector<int64_t>> out;
    for (int64_t f : sparseI64Range()) {
        if (dim == 2) {
            out.push_back({f, 1});
            out.push_back({-1, f});
        } else if (dim == 3) {
            out.push_back({f, 1, -2});
            out.push_back({-1, f, 2});
            out.push_back({1, -2, f});
        } else {
            out.push_back({f, -1, 2, 3});
            out.push_back({1, f, -2, 3});
            out.push_back({1, 2, f, -3});
            out.push_back({-1, 2, -3, f});
        }
    }
    return out;
}

// ---- Op callback types ----------------------------------------------------------------------

using I32Op = std::function<std::optional<int32_t>(int32_t, int32_t)>;
using U32Op = std::function<std::optional<uint32_t>(uint32_t, uint32_t)>;
using I64Op = std::function<std::optional<int64_t>(int64_t, int64_t)>;

// ---- i32 scalar / scalar-vector / vector-scalar generators ----------------------------------

// generateBinaryToI32Cases: cartesian product, i32 result.
inline std::vector<Case> generateBinaryToI32Cases(
    const std::vector<int32_t>& p0, const std::vector<int32_t>& p1, const I32Op& op) {
    std::vector<Case> cases;
    // cartesianProduct(p0, p1): p0 varies fastest.
    for (int32_t b : p1) {
        for (int32_t a : p0) {
            std::optional<int32_t> r = op(a, b);
            if (r.has_value()) {
                cases.push_back({{CaseValue(i32(a)), CaseValue(i32(b))}, CaseValue(i32(*r))});
            }
        }
    }
    return cases;
}

inline std::vector<Case> generateBinaryToU32Cases(
    const std::vector<uint32_t>& p0, const std::vector<uint32_t>& p1, const U32Op& op) {
    std::vector<Case> cases;
    for (uint32_t b : p1) {
        for (uint32_t a : p0) {
            std::optional<uint32_t> r = op(a, b);
            if (r.has_value()) {
                cases.push_back({{CaseValue(u32(a)), CaseValue(u32(b))}, CaseValue(u32(*r))});
            }
        }
    }
    return cases;
}

// generateI32VectorBinaryToVectorCases: scalar (p0) op vector (vectors), i32 vector result.
inline std::vector<Case> generateI32VectorBinaryToVectorCases(
    const std::vector<int32_t>& scalars,
    const std::vector<std::vector<int32_t>>& vectors,
    const I32Op& op) {
    std::vector<Case> cases;
    for (int32_t s : scalars) {
        for (const std::vector<int32_t>& vec : vectors) {
            std::vector<Scalar> res;
            bool ok = true;
            for (int32_t v : vec) {
                std::optional<int32_t> r = op(s, v);
                if (!r.has_value()) {
                    ok = false;
                    break;
                }
                res.push_back(i32(*r));
            }
            if (!ok) {
                continue;
            }
            std::vector<Scalar> vs;
            for (int32_t v : vec) {
                vs.push_back(i32(v));
            }
            cases.push_back(
                {{CaseValue(i32(s)), CaseValue::vec(vs)}, CaseValue::vec(res)});
        }
    }
    return cases;
}

// generateVectorI32BinaryToVectorCases: vector (vectors) op scalar (p1), i32 vector result.
inline std::vector<Case> generateVectorI32BinaryToVectorCases(
    const std::vector<std::vector<int32_t>>& vectors,
    const std::vector<int32_t>& scalars,
    const I32Op& op) {
    std::vector<Case> cases;
    for (int32_t s : scalars) {
        for (const std::vector<int32_t>& vec : vectors) {
            std::vector<Scalar> res;
            bool ok = true;
            for (int32_t v : vec) {
                std::optional<int32_t> r = op(v, s);
                if (!r.has_value()) {
                    ok = false;
                    break;
                }
                res.push_back(i32(*r));
            }
            if (!ok) {
                continue;
            }
            std::vector<Scalar> vs;
            for (int32_t v : vec) {
                vs.push_back(i32(v));
            }
            cases.push_back(
                {{CaseValue::vec(vs), CaseValue(i32(s))}, CaseValue::vec(res)});
        }
    }
    return cases;
}

inline std::vector<Case> generateU32VectorBinaryToVectorCases(
    const std::vector<uint32_t>& scalars,
    const std::vector<std::vector<uint32_t>>& vectors,
    const U32Op& op) {
    std::vector<Case> cases;
    for (uint32_t s : scalars) {
        for (const std::vector<uint32_t>& vec : vectors) {
            std::vector<Scalar> res;
            bool ok = true;
            for (uint32_t v : vec) {
                std::optional<uint32_t> r = op(s, v);
                if (!r.has_value()) {
                    ok = false;
                    break;
                }
                res.push_back(u32(*r));
            }
            if (!ok) {
                continue;
            }
            std::vector<Scalar> vs;
            for (uint32_t v : vec) {
                vs.push_back(u32(v));
            }
            cases.push_back(
                {{CaseValue(u32(s)), CaseValue::vec(vs)}, CaseValue::vec(res)});
        }
    }
    return cases;
}

inline std::vector<Case> generateVectorU32BinaryToVectorCases(
    const std::vector<std::vector<uint32_t>>& vectors,
    const std::vector<uint32_t>& scalars,
    const U32Op& op) {
    std::vector<Case> cases;
    for (uint32_t s : scalars) {
        for (const std::vector<uint32_t>& vec : vectors) {
            std::vector<Scalar> res;
            bool ok = true;
            for (uint32_t v : vec) {
                std::optional<uint32_t> r = op(v, s);
                if (!r.has_value()) {
                    ok = false;
                    break;
                }
                res.push_back(u32(*r));
            }
            if (!ok) {
                continue;
            }
            std::vector<Scalar> vs;
            for (uint32_t v : vec) {
                vs.push_back(u32(v));
            }
            cases.push_back(
                {{CaseValue::vec(vs), CaseValue(u32(s))}, CaseValue::vec(res)});
        }
    }
    return cases;
}

// ---- abstract-int (i64) generators ----------------------------------------------------------

inline std::vector<Case> generateBinaryToI64Cases(
    const std::vector<int64_t>& p0, const std::vector<int64_t>& p1, const I64Op& op) {
    std::vector<Case> cases;
    for (int64_t b : p1) {
        for (int64_t a : p0) {
            std::optional<int64_t> r = op(a, b);
            if (r.has_value()) {
                cases.push_back(
                    {{CaseValue(abstractInt64(a)), CaseValue(abstractInt64(b))},
                     CaseValue(abstractInt64(*r))});
            }
        }
    }
    return cases;
}

inline std::vector<Case> generateI64VectorBinaryToVectorCases(
    const std::vector<int64_t>& scalars,
    const std::vector<std::vector<int64_t>>& vectors,
    const I64Op& op) {
    std::vector<Case> cases;
    for (int64_t s : scalars) {
        for (const std::vector<int64_t>& vec : vectors) {
            std::vector<Scalar> res;
            bool ok = true;
            for (int64_t v : vec) {
                std::optional<int64_t> r = op(s, v);
                if (!r.has_value()) {
                    ok = false;
                    break;
                }
                res.push_back(abstractInt64(*r));
            }
            if (!ok) {
                continue;
            }
            std::vector<Scalar> vs;
            for (int64_t v : vec) {
                vs.push_back(abstractInt64(v));
            }
            cases.push_back(
                {{CaseValue(abstractInt64(s)), CaseValue::vec(vs)}, CaseValue::vec(res)});
        }
    }
    return cases;
}

inline std::vector<Case> generateVectorI64BinaryToVectorCases(
    const std::vector<std::vector<int64_t>>& vectors,
    const std::vector<int64_t>& scalars,
    const I64Op& op) {
    std::vector<Case> cases;
    for (int64_t s : scalars) {
        for (const std::vector<int64_t>& vec : vectors) {
            std::vector<Scalar> res;
            bool ok = true;
            for (int64_t v : vec) {
                std::optional<int64_t> r = op(v, s);
                if (!r.has_value()) {
                    ok = false;
                    break;
                }
                res.push_back(abstractInt64(*r));
            }
            if (!ok) {
                continue;
            }
            std::vector<Scalar> vs;
            for (int64_t v : vec) {
                vs.push_back(abstractInt64(v));
            }
            cases.push_back(
                {{CaseValue::vec(vs), CaseValue(abstractInt64(s))}, CaseValue::vec(res)});
        }
    }
    return cases;
}

// ---- Overflow-safe i64 arithmetic (no __int128, MSVC-portable) -------------------------------

inline bool i64AddOverflow(int64_t a, int64_t b, int64_t& out) {
    // Overflow iff signs equal and result sign differs.
    uint64_t ua = static_cast<uint64_t>(a);
    uint64_t ub = static_cast<uint64_t>(b);
    uint64_t ur = ua + ub;
    out = static_cast<int64_t>(ur);
    return ((a > 0 && b > 0 && out < 0) || (a < 0 && b < 0 && out >= 0));
}

inline bool i64SubOverflow(int64_t a, int64_t b, int64_t& out) {
    uint64_t ua = static_cast<uint64_t>(a);
    uint64_t ub = static_cast<uint64_t>(b);
    uint64_t ur = ua - ub;
    out = static_cast<int64_t>(ur);
    // Overflow iff signs of a and b differ and result sign differs from a.
    return ((a >= 0 && b < 0 && out < 0) || (a < 0 && b >= 0 && out >= 0));
}

inline bool i64MulOverflow(int64_t a, int64_t b, int64_t& out) {
    uint64_t ur = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
    out = static_cast<int64_t>(ur);
    if (a == 0 || b == 0) {
        return false;
    }
    // Recover by division: out / a must equal b (and watch for INT64_MIN * -1).
    if (a == INT64_MIN || b == INT64_MIN) {
        // Only non-overflow products with INT64_MIN are *0 (handled) or *1.
        return !(a == 1 || b == 1);
    }
    return (out / a) != b;
}

}  // namespace binary_ops
}  // namespace expression
}  // namespace cts
