// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/bufferView.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
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
    "shader,execution,expression,call,builtin,bufferView",
    "\nExecution tests for bufferView\n");

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
        const std::vector<uint32_t> sizes = {0};
        for (const int offset : kOffsets) {
            // Skip any case that results in an invalid memory reference.
            // Split the writes up one per thread.
            if (isValidArrayLengthCase(testcase, offset, 0, bufferSize)) {
                values.push_back(calculateArrayLength(testcase, offset, 0, bufferSize));
                // Setup the offset and size buffer values so they can be indexed by invocation id.
                offsets.push_back(static_cast<uint32_t>(offset));
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
fn main(@builtin(global_invocation_id) gid : vec3u) {
  )" + (aspace == "workgroup" ? "_ = dummy[0];" : "") + R"(

  if gid.x >= )" + std::to_string(values.size()) + R"( {
    return;
  }

  out[gid.x] = arrayLength()" + addrOf + "bufferView<" + testcase.type + ">(&v, offsets[gid.x])" + testcase.access + R"();
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
    .desc("Tests arrayLength from a bufferView through various function calls")
    .params([](ParamsBuilder u) {
        return u.combine("type", arrayLengthTypeNames())
            .combine("aspace", {"workgroup", "storage", "uniform"})
            .beginSubcases()
            .combine("call", callValues())
            .combine("bufferSize", bufferSizeValues())
            .combine("dynamic_offset", {0, 256})
            .filter([](const ParamRecord& p) {
                const std::string type = valueAs<std::string>(*findParam(p, "type"));
                if (type == "array_S_2" || type == "mat4x4f" || type == "mat4x3f") {
                    return valueAs<int64_t>(*findParam(p, "bufferSize")) > 128;
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
        const std::vector<uint32_t> sizes = {0};
        for (const int offset : kOffsets) {
            const int usedSize = call == "unsized" ? bufferSize : bufferSize / 2;
            // Skip any case that results in an invalid memory reference.
            // Split the writes up one per thread.
            if (isValidArrayLengthCase(testcase, offset, 0, usedSize)) {
                values.push_back(calculateArrayLength(testcase, offset, 0, usedSize));
                // Setup the offset and size buffer values so they can be indexed by invocation id.
                offsets.push_back(static_cast<uint32_t>(offset));
            }
        }

        if (values.empty()) {
            t.fail("no tests run");
        }

        const std::string addrOf = testcase.access.empty() ? "" : "&";
        const std::string enables = testcase.f16 ? "enable f16;" : "";
        const std::string lengthExpr =
            "arrayLength(" + addrOf + "bufferView<" + testcase.type + ">(p, offsets[gidx])" + testcase.access + ")";
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
fn main(@builtin(global_invocation_id) gid : vec3u) {
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
    .desc("Test reading memory layout from a bufferView")
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
        if (aspace == "uniform" && testcase.uniformStdLayoutView) {
            skipIfUniformBufferStandardLayoutNotSupported(t);
        }
        if (testcase.f16) {
            skipIfNoShaderF16(t);
        }

        const std::string enables = testcase.f16 ? "enable f16;" : "";
        const std::string v = aspace == "workgroup" ? "wg_var" : "in";

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

  let p = bufferView<)" + testcase.type + ">(&" + v + ", " + std::to_string(offset) + R"();
  out = u32((*p))" + testcase.access + R"();
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
        const std::string callExpr = "bufferView<" + testcase.type + ">(&" + v + ", " + std::to_string(offset) + ")";
        const std::string postAssign = aspace == "workgroup" ? kPostAssign : "";

        const std::string wgsl = "\n" + enables + "\n\n" + testcase.decl + R"(

@group(0) @binding(0) var<storage> in : u32;
@group(0) @binding(1) var<storage, read_write> out : buffer<256 * 4>;


var<workgroup> wg_var : buffer<256 * 4>;

@compute @workgroup_size(1)
fn main() {
  let val = )" + (testcase.f16 ? "f16" : testcase.f32 ? "f32" : "u32") + R"((in);
  let ptr = )" + callExpr + R"(;
  (*)" + (t.param<std::string>("assign") == "call" ? callExpr : "ptr") + ")" + testcase.access + R"( = val;

  )" + postAssign + R"(
}
)";

        runWriteLayoutTest(t, testcase, wgsl, offset);
    });

CTS_TEST(testGroup, "read")
    .desc("Test reading various types from bufferView")
    .params([](ParamsBuilder u) {
        return u.combine("base_type", {"u32", "i32", "f32", "f16"})
            .beginSubcases()
            .combine("wrap", {"none", "vector", "array", "matrix"})
            .combine("width", {1, 2, 3, 4})
            .combine("aspace", {"workgroup", "storage", "ro_storage", "uniform"})
            .combine("offset", {0, 4, 8, 12, 16, 32, 48, 64})
            .filter([](const ParamRecord& p) {
                if (valueAs<std::string>(*findParam(p, "aspace")) == "uniform" &&
                    valueAs<std::string>(*findParam(p, "wrap")) == "array") {
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
        skipIfBufferViewNotSupported(t);
        if (baseType == "f16") {
            skipIfNoShaderF16(t);
        }

        const std::string ty = wrapTypeName(baseType, wrap, width);

        const int bufferSize = 128;
        const std::string size = std::to_string(bufferSize);
        const std::string enables = baseType == "f16" ? "enable f16;" : "";
        const std::string v = aspace == "workgroup" ? "wg_var" : "in";
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

  let p = bufferView<)" + ty + ">(&" + v + ", " + std::to_string(offset) + R"();
  out = (*p);
}
)";

        runReadWriteTest(true, t, wgsl, baseType, wrapElementCount(wrap, width), aspace, offset, bufferSize);
    });

CTS_TEST(testGroup, "write")
    .desc("Test writing various types via bufferView")
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
        const std::string postAssign = aspace == "workgroup" ? kPostAssign : "";

        const std::string callExpr = "bufferView<" + ty + ">(&" + v + ", " + std::to_string(offset) + ")";
        const std::string wgsl = "\n" + enables + R"(

@group(0) @binding(0) var<storage> in : )" + ty + R"(;
@group(0) @binding(1) var<storage, read_write> out : buffer<)" + size + R"(>;


var<workgroup> wg_var : buffer<)" + size + R"(>;

@compute @workgroup_size(1)
fn main() {
  let ptr = )" + callExpr + R"(;
  (*)" + (t.param<std::string>("assign") == "call" ? callExpr : "ptr") + ")" + swizzle + R"( = in;

  )" + postAssign + R"(
}
)";

        runReadWriteTest(false, t, wgsl, baseType, wrapElementCount(wrap, width), aspace, offset, bufferSize);
    });

CTS_TEST(testGroup, "mixed_types_aliasing")
    .desc(R"(Test that differently typed pointers formed with bufferView behave correctly when they are views
of overlapping bytes of a buffer, and both are formed and used within a single function.

Aliasing within a function is valid WGSL, so an implementation must not assume that differently
typed views do not alias (e.g. by applying C++ style type-based alias analysis).

 * 'pair' selects the types of the two views, covering combinations of scalars, vectors, and
   structures (with and without padding), including f16 types.
 * 'overlap' selects whether the views start at the same byte, partially overlap, or only overlap
   padding.
 * 'offset' selects whether the byte offset passed to bufferView is a constant or a runtime value.
 * If 'aliased' is false, the second view is formed on a different buffer.)")
    .params([](ParamsBuilder u) {
        return u.combine("buffer", mixedTypeBuffers())
            .combine("pair", mixedTypePairNames())
            .combine("overlap", mixedTypeOverlaps())
            .filter(isValidMixedTypeOverlap)
            .combine("offset", {"constant", "dynamic"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", mixedTypeOpNames());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const MixedTypePair& pair = mixedTypePair(t.param<std::string>("pair"));
        const MixedTypeViewWords words = *mixedTypeWords(*pair.intType, *pair.other, t.param<std::string>("overlap"));
        const bool constantOffset = t.param<std::string>("offset") == "constant";
        // The dynamic offset adds 'input.p[3] * 16' bytes, which preserves the alignment of all types.
        const int dynamicWords = 4 * kMixedTypeIdx;

        MixedTypeAliasingParams params;
        params.buffer = t.param<std::string>("buffer");
        params.intType = pair.intType;
        params.other = pair.other;
        params.intWord = words.intWord;
        params.otherWord = words.otherWord;
        params.view = [&](const MixedType& type, const std::string& buffer, int word) {
            const std::string prefix = "bufferView<" + mixedTypeName(type) + ">(&" + buffer + ", ";
            return constantOffset ? prefix + std::to_string(word * 4) + "u)"
                                  : prefix + std::to_string((word - dynamicWords) * 4) + "u + u32(input.p[3]) * 16u)";
        };
        params.aliased = t.param<bool>("aliased");
        params.op = &mixedTypeOp(t.param<std::string>("op"));
        runMixedTypeAliasingTest(t, params);
    });

} // namespace
