// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/bufferArrayView.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"
#include "webgpu/shader/execution/expression/call/builtin/buffer_view_utils.h"
#include "webgpu/shader/execution/expression/call/mixed_type_aliasing_utils.h"

using namespace cts;
using namespace cts::buffer_view;
using namespace cts::mixed_type;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,bufferArrayView",
    "\nExecution tests for bufferArrayView\n");

/** Copies the workgroup buffer to 'out' after a write through a view. */
const char* const kPostAssign = R"(
  workgroupBarrier();

  let out_ptr = bufferView<array<u32>>(&out, 0);
  let wg_ptr = bufferView<array<u32>>(&wg_var, 0);
  for (var i = 0; i < 256; i++) {
    (*out_ptr)[i] = (*wg_ptr)[i];
  }
)";

CTS_TEST(testGroup, "array_length")
    .desc("Tests arrayLength from a bufferView")
    .params([](ParamsBuilder u) {
        return u.combine("type", arrayLengthTypeNames())
            .combine("aspace", {"workgroup", "storage", "uniform"})
            .beginSubcases()
            .combine("sized", {false, true})
            .combine("override", {false, true})
            .combine("bufferSize", bufferSizeValues())
            .combine("dynamic_offset", {0, 256})
            .filter([](const ParamRecord& p) {
                const std::string aspace = valueAs<std::string>(*findParam(p, "aspace"));
                if (aspace == "workgroup" && valueAs<int64_t>(*findParam(p, "dynamic_offset")) != 0) {
                    return false;
                }
                if (aspace != "workgroup" && valueAs<bool>(*findParam(p, "override"))) {
                    return false;
                }
                return valueAs<bool>(*findParam(p, "sized")) || aspace == "storage";
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ArrayLengthType& testcase = arrayLengthType(t.param<std::string>("type"));
        const std::string aspace = t.param<std::string>("aspace");
        const int bufferSize = static_cast<int>(t.param<int64_t>("bufferSize"));
        skipIfBufferViewNotSupported(t);
        if (aspace == "uniform" && testcase.uniformStdLayout) {
            skipIfUniformBufferStandardLayoutNotSupported(t);
        }
        if (testcase.f16) {
            skipIfNoShaderF16(t);
        }

        const std::string bSize = t.param<bool>("override") ? "bufferSize" : std::to_string(bufferSize);
        const std::string bufferTy = t.param<bool>("sized") ? "buffer<" + bSize + ">" : "buffer";
        const std::string decl = arrayLengthVarDecl(aspace, bufferTy);

        std::vector<uint32_t> values;
        std::vector<uint32_t> offsets;
        std::vector<uint32_t> sizes;
        // Limit the number of assignments.
        for (size_t o = 0; o < std::size(kOffsets); o += 2) {
            for (const int size : kSizes) {
                const int offset = kOffsets[o];

                // Skip any case that results in an invalid memory reference.
                // Split the writes up one per thread.
                if (isValidArrayLengthCase(testcase, offset, size, bufferSize)) {
                    values.push_back(calculateArrayLength(testcase, offset, size, bufferSize));
                    // Setup the offset and size buffer values so they can be indexed by invocation id.
                    offsets.push_back(static_cast<uint32_t>(offset));
                    sizes.push_back(static_cast<uint32_t>(size));
                }
            }
        }

        if (values.empty()) {
            t.fail("no tests run");
        }

        const std::string addrOf = testcase.access.empty() ? "" : "&";
        const std::string enables = testcase.f16 ? "enable f16;" : "";

        const std::string wgsl = "\n" + enables + "\n\n" + kStructDecls + R"(
override bufferSize : u32 = 0u;
)" + decl + R"(
@group(0) @binding(1) var<storage> offsets : array<u32>;
@group(0) @binding(2) var<storage> sizes : array<u32>; // unused
@group(0) @binding(3) var<storage, read_write> out : array<u32>;

override wgx : u32;
@compute @workgroup_size(wgx)
fn main(@builtin(global_invocation_id) gid : vec3u,
        @builtin(local_invocation_index) lid : u32) {
  )" + (aspace == "workgroup" ? "_ = dummy[0];" : "") + R"(

  if gid.x >= )" + std::to_string(values.size()) + R"( {
    return;
  }

  out[gid.x] = arrayLength()" + addrOf + "bufferArrayView<" + testcase.type + ">(&v, offsets[gid.x], sizes[gid.x])" + testcase.access + R"();
})";

        runLengthTest(
            t,
            wgsl,
            aspace == "uniform" ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage,
            bufferSize,
            static_cast<int>(t.param<int64_t>("dynamic_offset")),
            offsets,
            sizes,
            values);
    });

CTS_TEST(testGroup, "array_length,functions")
    .desc("Tests arrayLength from a bufferView")
    .params([](ParamsBuilder u) {
        return u.combine("type", arrayLengthTypeNames())
            .combine("aspace", {"workgroup", "storage", "uniform"})
            .beginSubcases()
            .combine("call", callValues())
            .combine("bufferSize", bufferSizeValues())
            .combine("dynamic_offset", {0, 256})
            .filter([](const ParamRecord& p) {
                if (valueAs<int64_t>(*findParam(p, "bufferSize")) == 128) {
                    return false;
                }
                return valueAs<std::string>(*findParam(p, "aspace")) != "workgroup" ||
                       valueAs<int64_t>(*findParam(p, "dynamic_offset")) == 0;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ArrayLengthType& testcase = arrayLengthType(t.param<std::string>("type"));
        const std::string aspace = t.param<std::string>("aspace");
        const std::string call = t.param<std::string>("call");
        const int bufferSize = static_cast<int>(t.param<int64_t>("bufferSize"));
        skipIfBufferViewNotSupported(t);
        skipIfUnrestrictedPointerParametersNotSupported(t);
        if (aspace == "uniform" && testcase.uniformStdLayout) {
            skipIfUniformBufferStandardLayoutNotSupported(t);
        }
        if (testcase.f16) {
            skipIfNoShaderF16(t);
        }

        const std::string decl = arrayLengthVarDecl(aspace, "buffer<" + std::to_string(bufferSize) + ">");

        std::vector<uint32_t> values;
        std::vector<uint32_t> offsets;
        std::vector<uint32_t> sizes;
        // Limit the number of assignments.
        for (size_t o = 0; o < std::size(kOffsets); o += 2) {
            for (const int size : kSizes) {
                const int offset = kOffsets[o];
                const int usedSize = call == "unsized" ? bufferSize : bufferSize / 2;

                // Skip any case that results in an invalid memory reference.
                // Split the writes up one per thread.
                if (isValidArrayLengthCase(testcase, offset, size, usedSize)) {
                    values.push_back(calculateArrayLength(testcase, offset, size, usedSize));
                    // Setup the offset and size buffer values so they can be indexed by invocation id.
                    offsets.push_back(static_cast<uint32_t>(offset));
                    sizes.push_back(static_cast<uint32_t>(size));
                }
            }
        }

        if (values.empty()) {
            t.fail("no tests run");
        }

        const std::string addrOf = testcase.access.empty() ? "" : "&";
        const std::string enables = testcase.f16 ? "enable f16;" : "";
        const std::string lengthExpr = "arrayLength(" + addrOf + "bufferArrayView<" + testcase.type +
                                       ">(p, offsets[gidx], sizes[gidx])" + testcase.access + ")";
        const std::string halfSize = std::to_string(bufferSize / 2);

        const std::string wgsl = "\n" + enables + "\n\n" + kStructDecls + R"(
fn unsizedLength(p : ptr<)" + aspace + R"(, buffer>, gidx : u32) -> u32 {
  return )" + lengthExpr + R"(;
}

fn sizedLength(p : ptr<)" + aspace + ", buffer<" + halfSize + R"(>>, gidx : u32) -> u32 {
  return )" + lengthExpr + R"(;
}

fn sized_indirectLength(p : ptr<)" + aspace + ", buffer<" + halfSize + R"(>>, gidx : u32) -> u32 {
  return unsizedLength(p, gidx);
}

fn sized_sized_indirectLength(p : ptr<)" + aspace + ", buffer<" + std::to_string(bufferSize) + R"(>>, gidx : u32) -> u32 {
  return sizedLength(p, gidx);
}

override bufferSize : u32 = 0u;
)" + decl + R"(
@group(0) @binding(1) var<storage> offsets : array<u32>;
@group(0) @binding(2) var<storage> sizes : array<u32>; // unused
@group(0) @binding(3) var<storage, read_write> out : array<u32>;

override wgx : u32;
@compute @workgroup_size(wgx)
fn main(@builtin(global_invocation_id) gid : vec3u,
        @builtin(local_invocation_index) lid : u32) {
  )" + (aspace == "workgroup" ? "_ = dummy[0];" : "") + R"(

  if gid.x >= )" + std::to_string(values.size()) + R"( {
    return;
  }

  out[gid.x] = )" + call + R"(Length(&v, gid.x);
})";

        runLengthTest(
            t,
            wgsl,
            aspace == "uniform" ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage,
            bufferSize,
            static_cast<int>(t.param<int64_t>("dynamic_offset")),
            offsets,
            sizes,
            values);
    });

CTS_TEST(testGroup, "read_layout")
    .desc("Test reading memory layout from a bufferArrayView")
    .params([](ParamsBuilder u) {
        return u.combine("case", layoutCaseNames())
            .beginSubcases()
            .combine("aspace", {"storage", "ro_storage", "uniform", "workgroup"})
            .combine("offset", offsetValues())
            .filter(isValidLayoutCase);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const LayoutCase& testcase = layoutCase(t.param<std::string>("case"));
        const std::string aspace = t.param<std::string>("aspace");
        const int offset = static_cast<int>(t.param<int64_t>("offset"));
        skipIfBufferViewNotSupported(t);
        if (aspace == "uniform" && (testcase.uniformStdLayoutArrayView || testcase.uniformStdLayoutView)) {
            skipIfUniformBufferStandardLayoutNotSupported(t);
        }
        if (testcase.f16) {
            skipIfNoShaderF16(t);
        }

        const std::string enables = testcase.f16 ? "enable f16;" : "";
        const std::string v = aspace == "workgroup" ? "wg_var" : "in";
        const std::string o = std::to_string(offset);

        const std::string wgsl = "\n" + enables + "\n\n" + testcase.decl + R"(

@group(0) @binding(0)
var<)" + (aspace == "uniform" ? "uniform" : "storage") + (aspace == "storage" ? ", read_write" : "") + R"(> in : buffer<256 * 4>;

@group(0) @binding(1) var<storage, read_write> out : u32;

var<workgroup> wg_var : buffer<256 * 4>;

@compute @workgroup_size(1)
fn main() {
  let in_ptr = bufferView<array<vec4u, 256 / 4>>(&in, 0);
  let wg_ptr = bufferView<array<vec4u, 256 / 4>>(&wg_var, 0);
  for (var i = 0; i < 256 / 4; i++) {
    (*wg_ptr)[i] = (*in_ptr)[i];
  }

  workgroupBarrier();

  let p = bufferArrayView<array<)" + testcase.type + ">>(&" + v + ", " + o + ", bufferLength(&" + v + ") - " + o + R"();
  out = u32((*p)[0])" + testcase.access + R"();
}
)";

        runReadLayoutTest(t, testcase, wgsl, aspace, offset);
    });

CTS_TEST(testGroup, "write_layout")
    .desc("Test writing memory layout via a bufferView")
    .params([](ParamsBuilder u) {
        return u.combine("case", layoutCaseNames())
            .beginSubcases()
            .combine("assign", {"let", "call"})
            .combine("aspace", {"storage", "workgroup"})
            .combine("offset", offsetValues())
            .filter(isValidLayoutCase);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const LayoutCase& testcase = layoutCase(t.param<std::string>("case"));
        const std::string aspace = t.param<std::string>("aspace");
        const int offset = static_cast<int>(t.param<int64_t>("offset"));
        skipIfBufferViewNotSupported(t);
        if (testcase.f16) {
            skipIfNoShaderF16(t);
        }

        const std::string enables = testcase.f16 ? "enable f16;" : "";
        const std::string v = aspace == "workgroup" ? "wg_var" : "out";
        const std::string o = std::to_string(offset);
        const std::string callExpr =
            "bufferArrayView<array<" + testcase.type + ">>(&" + v + ", " + o + ", bufferLength(&" + v + ") - " + o + ")";
        const std::string postAssign = aspace == "workgroup" ? kPostAssign : "";

        const std::string wgsl = "\n" + enables + "\n\n" + testcase.decl + R"(

@group(0) @binding(0) var<storage> in : u32;
@group(0) @binding(1) var<storage, read_write> out : buffer<256 * 4>;


var<workgroup> wg_var : buffer<256 * 4>;

@compute @workgroup_size(1)
fn main() {
  let val = )" + (testcase.f16 ? "f16" : testcase.f32 ? "f32" : "u32") + R"((in);
  let ptr = )" + callExpr + R"(;
  (*)" + (t.param<std::string>("assign") == "call" ? callExpr : "ptr") + ")[0]" + testcase.access + R"( = val;

  )" + postAssign + R"(
}
)";

        runWriteLayoutTest(t, testcase, wgsl, offset);
    });

CTS_TEST(testGroup, "read")
    .desc("Test reading various types from bufferArrayView")
    .params([](ParamsBuilder u) {
        return u.combine("base_type", {"u32", "i32", "f32", "f16"})
            .beginSubcases()
            .combine("wrap", {"none", "vector", "array", "matrix"})
            .combine("width", {1, 2, 3, 4})
            .combine("aspace", {"workgroup", "storage", "ro_storage", "uniform"})
            .combine("offset", {0, 4, 8, 12, 16, 32, 48, 64})
            .filter([](const ParamRecord& p) {
                const std::string aspace = valueAs<std::string>(*findParam(p, "aspace"));
                const std::string wrap = valueAs<std::string>(*findParam(p, "wrap"));
                if (aspace == "uniform" && wrap == "array") {
                    return false;
                }
                if (!isValidReadWriteCase(p)) {
                    return false;
                }
                const int width = static_cast<int>(valueAs<int64_t>(*findParam(p, "width")));
                const int align = wrapAlignment(valueAs<std::string>(*findParam(p, "base_type")), wrap, width);
                return !(aspace == "uniform" && align % 16 != 0);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string baseType = t.param<std::string>("base_type");
        const std::string wrap = t.param<std::string>("wrap");
        const std::string aspace = t.param<std::string>("aspace");
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const int offset = static_cast<int>(t.param<int64_t>("offset"));
        skipIfBufferViewNotSupported(t);
        if (baseType == "f16") {
            skipIfNoShaderF16(t);
        }

        const std::string ty = wrapTypeName(baseType, wrap, width);

        const int bufferSize = 128;
        const std::string size = std::to_string(bufferSize);
        const std::string enables = baseType == "f16" ? "enable f16;" : "";
        const std::string v = aspace == "workgroup" ? "wg_var" : "in";
        const std::string o = std::to_string(offset);
        const std::string wgsl = "\n" + enables + R"(


@group(0) @binding(0)
var<)" + (aspace == "uniform" ? "uniform" : "storage") + (aspace == "storage" ? ", read_write" : "") + "> in : buffer<" + size + R"(>;

@group(0) @binding(1) var<storage, read_write> out : )" + ty + R"(;

var<workgroup> wg_var : buffer<)" + size + R"(>;

@compute @workgroup_size(1)
fn main() {
  let in_ptr = bufferView<array<vec4u, )" + size + R"( / 16>>(&in, 0);
  let wg_ptr = bufferView<array<vec4u, )" + size + R"( / 16>>(&wg_var, 0);
  for (var i = 0; i < 256 / 4; i++) {
    (*wg_ptr)[i] = (*in_ptr)[i];
  }

  workgroupBarrier();

  let p = bufferArrayView<array<)" + ty + ">>(&" + v + ", " + o + ", bufferLength(&" + v + ") - " + o + R"();
  out = (*p)[0];
}
)";

        runReadWriteTest(true, t, wgsl, baseType, wrapElementCount(wrap, width), aspace, offset, bufferSize);
    });

CTS_TEST(testGroup, "write")
    .desc("Test writing various types via bufferArrayView")
    .params([](ParamsBuilder u) {
        return u.combine("base_type", {"u32", "i32", "f32", "f16"})
            .beginSubcases()
            .combine("wrap", {"none", "vector", "array", "matrix"})
            .combine("width", {1, 2, 3, 4})
            .combine("aspace", {"workgroup", "storage"})
            .combine("offset", {0, 4, 8, 12, 16, 32, 48, 64})
            .combine("assign", {"let", "call"})
            .combine("swizzle", {false, true})
            .filter([](const ParamRecord& p) {
                if (valueAs<std::string>(*findParam(p, "wrap")) != "vector" &&
                    valueAs<bool>(*findParam(p, "swizzle"))) {
                    return false;
                }
                return isValidReadWriteCase(p);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string baseType = t.param<std::string>("base_type");
        const std::string wrap = t.param<std::string>("wrap");
        const std::string aspace = t.param<std::string>("aspace");
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const int offset = static_cast<int>(t.param<int64_t>("offset"));
        const bool useSwizzle = t.param<bool>("swizzle");
        skipIfBufferViewNotSupported(t);
        if (baseType == "f16") {
            skipIfNoShaderF16(t);
        }
        if (useSwizzle && !hasSwizzleAssignmentLanguageFeature()) {
            t.skip("Missing swizzle_assignment language feature");
        }

        const std::string swizzle =
            wrap == "vector" && useSwizzle ? std::string(".xyzw").substr(0, static_cast<size_t>(1 + width)) : "";
        const std::string ty = wrapTypeName(baseType, wrap, width);

        const int bufferSize = 128;
        const std::string size = std::to_string(bufferSize);
        const std::string enables = baseType == "f16" ? "enable f16;" : "";
        const std::string v = aspace == "workgroup" ? "wg_var" : "out";
        const std::string o = std::to_string(offset);
        const std::string postAssign = aspace == "workgroup" ? kPostAssign : "";

        const std::string callExpr =
            "bufferArrayView<array<" + ty + ">>(&" + v + ", " + o + ", bufferLength(&" + v + ") - " + o + ")";
        const std::string wgsl = "\n" + enables + R"(

@group(0) @binding(0) var<storage> in : )" + ty + R"(;
@group(0) @binding(1) var<storage, read_write> out : buffer<)" + size + R"(>;


var<workgroup> wg_var : buffer<)" + size + R"(>;

@compute @workgroup_size(1)
fn main() {
  let ptr = )" + callExpr + R"(;
  (*)" + (t.param<std::string>("assign") == "call" ? callExpr : "ptr") + ")[0]" + swizzle + R"( = in;

  )" + postAssign + R"(
}
)";

        runReadWriteTest(false, t, wgsl, baseType, wrapElementCount(wrap, width), aspace, offset, bufferSize);
    });

CTS_TEST(testGroup, "mixed_types_aliasing")
    .desc(R"(Test that differently typed element pointers formed with bufferArrayView behave correctly when
they refer to overlapping bytes of a buffer, and both are formed and used within a single function.

Aliasing within a function is valid WGSL, so an implementation must not assume that differently
typed views do not alias (e.g. by applying C++ style type-based alias analysis).

 * 'pair' selects the element types of the two views, covering combinations of scalars, vectors,
   and structures (with and without padding), including f16 types.
 * 'overlap' selects whether the elements start at the same byte, partially overlap, or only
   overlap padding.
 * 'access' selects how the element is located:
   - 'constant_base': a constant byte offset, and element index 0.
   - 'dynamic_base': a runtime byte offset, and element index 0.
   - 'dynamic_index': a constant byte offset one element before the target, and a runtime element
     index of 1. The two views therefore start at different offsets.
 * If 'aliased' is false, the second view is formed on a different buffer.)")
    .params([](ParamsBuilder u) {
        return u.combine("buffer", mixedTypeBuffers())
            .combine("pair", mixedTypePairNames())
            .combine("overlap", mixedTypeOverlaps())
            .filter(isValidMixedTypeOverlap)
            .combine("access", {"constant_base", "dynamic_base", "dynamic_index"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", mixedTypeOpNames());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const MixedTypePair& pair = mixedTypePair(t.param<std::string>("pair"));
        const MixedTypeViewWords words = *mixedTypeWords(*pair.intType, *pair.other, t.param<std::string>("overlap"));
        const std::string access = t.param<std::string>("access");
        // The dynamic base adds 'input.p[3] * 16' bytes, which preserves the alignment of all types.
        const int dynamicWords = 4 * kMixedTypeIdx;

        MixedTypeAliasingParams params;
        params.buffer = t.param<std::string>("buffer");
        params.intType = pair.intType;
        params.other = pair.other;
        params.intWord = words.intWord;
        params.otherWord = words.otherWord;
        params.view = [&](const MixedType& type, const std::string& buffer, int word) {
            // The element stride in bytes. All of the tested types have a stride equal to their size.
            const int stride = type.size;
            std::string base;
            std::string index = "0";
            if (access == "constant_base") {
                base = std::to_string(word * 4) + "u";
            } else if (access == "dynamic_base") {
                base = std::to_string((word - dynamicWords) * 4) + "u + u32(input.p[3]) * 16u";
            } else {
                base = std::to_string(word * 4 - stride * kMixedTypeIdx) + "u";
                index = "input.p[3]";
            }
            // Each view spans 32 bytes, which keeps all views within the buffer.
            return "&(*bufferArrayView<array<" + mixedTypeName(type) + ">>(&" + buffer + ", " + base + ", 32u))[" +
                   index + "]";
        };
        params.aliased = t.param<bool>("aliased");
        params.op = &mixedTypeOp(t.param<std::string>("op"));
        runMixedTypeAliasingTest(t, params);
    });

} // namespace
