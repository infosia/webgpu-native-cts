// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/statement/assignment/vector.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port note: the two near-identical upstream shader templates are built by buildVectorShader().

#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"
#include "webgpu/shader/execution/flow_control/harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,statement,assignment,vector",
    "\nExecution tests for vector assignment statements.\n");

/** Mapping from component name to corresponding element index. */
int componentIndex(const std::string& component) {
    const std::string names = "xyzwrgba";
    return static_cast<int>(names.find(component)) % 4;
}

std::vector<Value> vectorElementTypes() {
    return {"bool", "i32", "u32", "f32", "f16"};
}

std::vector<Value> addressSpaces() {
    return {"function", "private", "workgroup", "storage"};
}

/** The filter shared by the tests that combine the bool element type and the address spaces. */
bool isHostShareableCase(const ParamRecord& p) {
    return valueAs<std::string>(*findParam(p, "address_space")) != "storage" ||
           valueAs<std::string>(*findParam(p, "elementType")) != "bool";
}

int elementSize(const std::string& elementType) {
    return elementType == "f16" ? 2 : 4;
}

/** Writes 'value' at the byte offset 'offset' of 'bytes', with the host layout of 'elementType'. */
void writeElement(uint8_t* bytes, size_t offset, const std::string& elementType, double value) {
    if (elementType == "f16") {
        const uint16_t bits = expression::access::f16BitsOfDouble(value);
        std::memcpy(bytes + offset, &bits, sizeof(bits));
    } else if (elementType == "f32") {
        const float f = static_cast<float>(value);
        std::memcpy(bytes + offset, &f, sizeof(f));
    } else if (elementType == "i32") {
        const int32_t i = static_cast<int32_t>(value);
        std::memcpy(bytes + offset, &i, sizeof(i));
    } else {
        const uint32_t u = static_cast<uint32_t>(value);
        std::memcpy(bytes + offset, &u, sizeof(u));
    }
}

std::vector<double> firstValues(const std::array<double, 4>& values, int width) {
    return std::vector<double>(values.begin(), values.begin() + width);
}

int alignUp(int n, int alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

/**
 * Builds the compute pipeline, initializes input and output buffers, dispatches
 * the compute pass, and verifies the output buffer values against expected values.
 */
void runVectorComputeAndVerify(
    GpuTest& t,
    const std::string& wgsl,
    const std::array<uint8_t, 64>& hostBuffer,
    const std::string& addressSpace,
    const std::string& elementType,
    const std::vector<double>& expectedValues) {
    WGPUComputePipeline pipeline = atomics::createComputePipelineAuto(t, wgsl);

    WGPUBufferDescriptor inputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    inputDesc.size = 64;
    inputDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer inputBuffer = t.createBufferTracked(inputDesc);
    t.queueWriteBuffer(inputBuffer, 0, hostBuffer.data(), hostBuffer.size());

    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = 64;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    std::vector<atomics::BufferBinding> entries = {{inputBuffer, 64}, {outputBuffer, 64}};

    if (addressSpace == "storage") {
        WGPUBufferDescriptor storageDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        storageDesc.size = 64;
        storageDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        entries.push_back({t.createBufferTracked(storageDesc), 64});
    }

    WGPUBindGroup bindGroup = atomics::makeAutoBindGroup(t, pipeline, entries);
    atomics::runComputePassX(t, pipeline, bindGroup, 1);

    const size_t elemBytes = static_cast<size_t>(elementSize(elementType));
    std::vector<uint8_t> expected(expectedValues.size() * elemBytes);
    for (size_t i = 0; i < expectedValues.size(); ++i) {
        writeElement(expected.data(), i * elemBytes, elementType, expectedValues[i]);
    }
    t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size());
}

/**
 * @returns the compute shader of the vector assignment tests, which initializes the vector 'v' from
 * 'inputs.initial_vector', runs 'assignmentStatement', and writes the elements of 'v' to 'outputs'.
 */
std::string buildVectorShader(
    const std::string& elementType,
    int width,
    const std::string& addressSpace,
    const std::string& inputFields,
    const std::string& assignmentStatement) {
    const std::string w = std::to_string(width);
    const std::string storageElemType = elementType == "bool" ? "u32" : elementType;
    const std::string outputElemType = storageElemType;
    const std::string vecType = "vec" + w + "<" + elementType + ">";
    const std::string storageVecType = "vec" + w + "<" + storageElemType + ">";
    const std::string varRef = addressSpace == "storage" ? "storage_buffer.v" : "v";

    std::string storageDecl;
    if (addressSpace == "storage") {
        storageDecl = "struct Storage {\n  v : " + storageVecType +
                      ",\n};\n@group(0) @binding(2) var<storage, read_write> storage_buffer : Storage;";
    }
    std::string init;
    if (addressSpace == "storage") {
        init = "storage_buffer.v = inputs.initial_vector;";
    } else if (elementType == "bool") {
        init = varRef + " = inputs.initial_vector != vec" + w + "<u32>(0u);";
    } else {
        init = varRef + " = inputs.initial_vector;";
    }

    return std::string("\n") + (elementType == "f16" ? "enable f16;" : "") + R"(

struct Inputs {
  initial_vector : )" + storageVecType + R"(,
  )" + inputFields + R"(
};

@group(0) @binding(0) var<uniform> inputs : Inputs;

struct Outputs {
  data : array<)" + outputElemType + R"(>,
};

@group(0) @binding(1) var<storage, read_write> outputs : Outputs;

)" + storageDecl + "\n" + (addressSpace == "private" ? "var<private> v : " + vecType + ";" : "") + "\n" +
           (addressSpace == "workgroup" ? "var<workgroup> v : " + vecType + ";" : "") + R"(

@compute @workgroup_size(1)
fn main() {
  )" + (addressSpace == "function" ? "var v : " + vecType + ";" : "") + R"(
  )" + init + R"(

  )" + assignmentStatement + R"(

  for (var i = 0u; i < )" + w + R"(u; i++) {
    outputs.data[i] = )" + (elementType == "bool" ? "u32(" + varRef + "[i])" : varRef + "[i]") + R"(;
  }
}
)";
}

enum class AccessKind { Component, ConstantIndex, DynamicIndex };

/**
 * Parameters for running a vector element write execution test.
 */
struct RunVectorElementWriteArgs {
    std::string elementType;
    int width = 0;
    std::string addressSpace;
    std::string memoryView;
    AccessKind kind = AccessKind::Component;
    std::string component;
    int index = 0;
    std::string indexType;
    std::string compoundOp;
};

/**
 * Builds, runs, and checks the output of a vector element assignment test.
 *
 * This function tests that assigning to a single element of a vector variable
 * (using a component accessor, a constant index, or a dynamic index) in various address spaces
 * correctly updates that element while preserving the values of all other untouched elements.
 *
 * @param t The test fixture object.
 * @param args The test configuration arguments.
 */
void runVectorElementWriteTest(GpuTest& t, const RunVectorElementWriteArgs& args) {
    // Check whether optional device features are required.
    if (args.elementType == "f16" && wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16) == WGPU_FALSE) {
        t.skip("Missing shader-f16 feature");
    }

    // Determine the target element index being updated.
    const int targetIndex = args.kind == AccessKind::Component ? componentIndex(args.component) : args.index;

    // Populate distinct initial values and a replacement value so that modifying
    // any element or failing to modify is clearly detectable.
    std::vector<double> initialValues;
    double replacementValue = 0;

    if (args.elementType == "bool") {
        initialValues = firstValues({0, 0, 0, 0}, args.width);
        replacementValue = 1;
    } else if (args.elementType == "i32") {
        initialValues = firstValues({-10, -20, -30, -40}, args.width);
        replacementValue = -5;
    } else if (args.elementType == "u32") {
        initialValues = firstValues({10, 20, 30, 40}, args.width);
        replacementValue = 5;
    } else {
        initialValues = firstValues({10.0, 20.0, 30.0, 40.0}, args.width);
        replacementValue = 2.5;
    }

    std::vector<double> expectedValues = initialValues;
    double& targetValue = expectedValues[static_cast<size_t>(targetIndex)];
    if (args.compoundOp == "+=") {
        targetValue += replacementValue;
    } else if (args.compoundOp == "*=") {
        targetValue *= replacementValue;
    } else {
        targetValue = replacementValue;
    }

    const std::string storageElemType = args.elementType == "bool" ? "u32" : args.elementType;

    const std::string varRef = args.addressSpace == "storage" ? "storage_buffer.v" : "v";
    const std::string target = args.memoryView == "ptr" ? "(*ptr)" : varRef;
    const std::string ptrDecl = args.memoryView == "ptr" ? "let ptr = &" + varRef + ";\n  " : "";

    std::string lhs;
    std::string structIndexField;

    if (args.kind == AccessKind::Component) {
        lhs = target + "." + args.component;
    } else if (args.kind == AccessKind::ConstantIndex) {
        lhs = target + "[" + std::to_string(args.index) + "]";
    } else {
        structIndexField = "index : " + args.indexType + ",";
        lhs = target + "[inputs.index]";
    }

    const std::string rhs = args.elementType == "bool" ? "inputs.value != 0u" : "inputs.value";
    const std::string op = args.compoundOp.empty() ? "=" : args.compoundOp;
    const std::string assignmentStatement = ptrDecl + lhs + " " + op + " " + rhs + ";";

    // Construct WGSL compute shader.
    const std::string wgsl = buildVectorShader(
        args.elementType,
        args.width,
        args.addressSpace,
        "value : " + storageElemType + ",\n  " + structIndexField,
        assignmentStatement);

    // Prepare input buffer data matching WGSL struct natural alignment.
    std::array<uint8_t, 64> hostBuffer{};
    const int elemBytes = elementSize(args.elementType);
    const int valueOffset = args.width * elemBytes;

    for (int i = 0; i < args.width; ++i) {
        writeElement(hostBuffer.data(), static_cast<size_t>(i * elemBytes), args.elementType,
                     initialValues[static_cast<size_t>(i)]);
    }
    writeElement(hostBuffer.data(), static_cast<size_t>(valueOffset), args.elementType, replacementValue);

    if (args.kind == AccessKind::DynamicIndex) {
        const int indexOffset = alignUp((args.width + 1) * elemBytes, 4);
        writeElement(hostBuffer.data(), static_cast<size_t>(indexOffset), args.indexType, args.index);
    }

    runVectorComputeAndVerify(t, wgsl, hostBuffer, args.addressSpace, args.elementType, expectedValues);
}

/**
 * Builds, runs, and checks the output of a full vector assignment test (v = rhs).
 *
 * @param t The test fixture object.
 * @param elementType The vector element type.
 * @param width The vector width.
 * @param addressSpace The address space of the vector.
 * @param memoryView Whether to assign through a reference or a pointer.
 * @param rhsSource The source of the right hand side.
 */
void runVectorFullAssignmentTest(
    GpuTest& t,
    const std::string& elementType,
    int width,
    const std::string& addressSpace,
    const std::string& memoryView,
    const std::string& rhsSource) {
    if (elementType == "f16" && wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16) == WGPU_FALSE) {
        t.skip("Missing shader-f16 feature");
    }

    std::vector<double> initialValues;
    std::vector<double> replacementValues;

    if (elementType == "bool") {
        initialValues = firstValues({0, 0, 0, 0}, width);
        replacementValues = firstValues({1, 0, 1, 1}, width);
    } else if (elementType == "i32") {
        initialValues = firstValues({-1, -2, -3, -4}, width);
        replacementValues = firstValues({-10, -20, -30, -40}, width);
    } else if (elementType == "u32") {
        initialValues = firstValues({1, 2, 3, 4}, width);
        replacementValues = firstValues({10, 20, 30, 40}, width);
    } else {
        initialValues = firstValues({1.0, 2.0, 3.0, 4.0}, width);
        replacementValues = firstValues({10.5, 20.5, 30.5, 40.5}, width);
    }

    const std::string w = std::to_string(width);
    const std::string storageElemType = elementType == "bool" ? "u32" : elementType;
    const std::string vecType = "vec" + w + "<" + elementType + ">";
    const std::string storageVecType = "vec" + w + "<" + storageElemType + ">";

    const std::string varRef = addressSpace == "storage" ? "storage_buffer.v" : "v";
    const std::string target = memoryView == "ptr" ? "(*ptr)" : varRef;
    const std::string ptrDecl = memoryView == "ptr" ? "let ptr = &" + varRef + ";\n  " : "";

    std::string suffix;
    if (elementType == "f16") {
        suffix = "h";
    } else if (elementType == "f32") {
        suffix = "f";
    } else if (elementType == "i32") {
        suffix = "i";
    } else if (elementType == "u32") {
        suffix = "u";
    }
    std::ostringstream rhsLiterals;
    for (size_t i = 0; i < replacementValues.size(); ++i) {
        if (i > 0) {
            rhsLiterals << ", ";
        }
        if (elementType == "bool") {
            rhsLiterals << (replacementValues[i] != 0 ? "true" : "false");
        } else {
            rhsLiterals << replacementValues[i] << suffix;
        }
    }

    std::string rhsDecl;
    std::string rhsExpr;
    if (rhsSource == "constructor") {
        rhsExpr = vecType + "(" + rhsLiterals.str() + ")";
    } else if (rhsSource == "variable") {
        rhsDecl = "let rhs_val = " + vecType + "(" + rhsLiterals.str() + ");\n  ";
        rhsExpr = "rhs_val";
    } else {
        rhsExpr = elementType == "bool" ? "inputs.replacement_vector != vec" + w + "<u32>(0u)"
                                        : "inputs.replacement_vector";
    }

    const std::string assignmentStatement = rhsDecl + ptrDecl + target + " = " + rhsExpr + ";";

    const std::string wgsl = buildVectorShader(
        elementType, width, addressSpace, "replacement_vector : " + storageVecType + ",", assignmentStatement);

    // Prepare input buffer data matching WGSL struct natural alignment.
    std::array<uint8_t, 64> hostBuffer{};
    const int elemBytes = elementSize(elementType);
    const int vecAlign = (width == 2 ? 2 : 4) * elemBytes;
    const int replacementOffset = alignUp(width * elemBytes, vecAlign);

    for (int i = 0; i < width; ++i) {
        writeElement(hostBuffer.data(), static_cast<size_t>(i * elemBytes), elementType,
                     initialValues[static_cast<size_t>(i)]);
        writeElement(hostBuffer.data(), static_cast<size_t>(replacementOffset + i * elemBytes), elementType,
                     replacementValues[static_cast<size_t>(i)]);
    }

    runVectorComputeAndVerify(t, wgsl, hostBuffer, addressSpace, elementType, replacementValues);
}

/** @returns the indices of a vector of the 'width' param: [...Array(u.width).keys()]. */
std::vector<Value> vectorIndices(const ParamRecord& p) {
    std::vector<Value> indices;
    const int64_t width = valueAs<int64_t>(*findParam(p, "width"));
    for (int64_t i = 0; i < width; ++i) {
        indices.emplace_back(i);
    }
    return indices;
}

CTS_TEST(testGroup, "vector_component")
    .desc("Tests writing to a vector component via component accessor (e.g. v.x = val).")
    .params([](ParamsBuilder u) {
        return u.combine("elementType", vectorElementTypes())
            .combine("width", {2, 3, 4})
            .beginSubcases()
            .combine("address_space", addressSpaces())
            .filter(isHostShareableCase)
            .combine("memory_view", {"ref", "ptr"})
            .combine("component_set", {"xyzw", "rgba"})
            .expand("component", [](const ParamRecord& p) {
                const std::string set = valueAs<std::string>(*findParam(p, "component_set"));
                const int64_t width = valueAs<int64_t>(*findParam(p, "width"));
                std::vector<Value> components;
                for (int64_t i = 0; i < width; ++i) {
                    components.emplace_back(set.substr(static_cast<size_t>(i), 1));
                }
                return components;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        RunVectorElementWriteArgs args;
        args.elementType = t.param<std::string>("elementType");
        args.width = static_cast<int>(t.param<int64_t>("width"));
        args.addressSpace = t.param<std::string>("address_space");
        args.memoryView = t.param<std::string>("memory_view");
        args.kind = AccessKind::Component;
        args.component = t.param<std::string>("component");
        runVectorElementWriteTest(t, args);
    });

CTS_TEST(testGroup, "vector_constant_index")
    .desc("Tests writing to a vector element via constant index (e.g. v[0] = val).")
    .params([](ParamsBuilder u) {
        return u.combine("elementType", vectorElementTypes())
            .combine("width", {2, 3, 4})
            .beginSubcases()
            .combine("address_space", addressSpaces())
            .filter(isHostShareableCase)
            .combine("memory_view", {"ref", "ptr"})
            .expand("index", vectorIndices);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        RunVectorElementWriteArgs args;
        args.elementType = t.param<std::string>("elementType");
        args.width = static_cast<int>(t.param<int64_t>("width"));
        args.addressSpace = t.param<std::string>("address_space");
        args.memoryView = t.param<std::string>("memory_view");
        args.kind = AccessKind::ConstantIndex;
        args.index = static_cast<int>(t.param<int64_t>("index"));
        runVectorElementWriteTest(t, args);
    });

CTS_TEST(testGroup, "vector_dynamic_index")
    .desc("Tests writing to a vector element via dynamic index loaded from uniform buffer (e.g. v[index] = val).")
    .params([](ParamsBuilder u) {
        return u.combine("elementType", vectorElementTypes())
            .combine("width", {2, 3, 4})
            .combine("indexType", {"i32", "u32"})
            .beginSubcases()
            .combine("address_space", addressSpaces())
            .filter(isHostShareableCase)
            .combine("memory_view", {"ref", "ptr"})
            .expand("index", vectorIndices);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        RunVectorElementWriteArgs args;
        args.elementType = t.param<std::string>("elementType");
        args.width = static_cast<int>(t.param<int64_t>("width"));
        args.addressSpace = t.param<std::string>("address_space");
        args.memoryView = t.param<std::string>("memory_view");
        args.kind = AccessKind::DynamicIndex;
        args.index = static_cast<int>(t.param<int64_t>("index"));
        args.indexType = t.param<std::string>("indexType");
        runVectorElementWriteTest(t, args);
    });

CTS_TEST(testGroup, "vector_element_compound")
    .desc("Tests compound assignment to a vector element (e.g. v.x += val, v[i] += val).")
    .params([](ParamsBuilder u) {
        return u.combine("elementType", {"i32", "u32", "f32", "f16"})
            .combine("width", {2, 3, 4})
            .combine("op", {"add", "mul"})
            .beginSubcases()
            .combine("access", {"component", "constant_index", "dynamic_index"})
            .combine("address_space", addressSpaces())
            .combine("memory_view", {"ref", "ptr"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int targetIdx = 1;
        const std::string access = t.param<std::string>("access");
        RunVectorElementWriteArgs args;
        args.elementType = t.param<std::string>("elementType");
        args.width = static_cast<int>(t.param<int64_t>("width"));
        args.addressSpace = t.param<std::string>("address_space");
        args.memoryView = t.param<std::string>("memory_view");
        if (access == "component") {
            args.kind = AccessKind::Component;
            args.component = "y";
        } else if (access == "constant_index") {
            args.kind = AccessKind::ConstantIndex;
            args.index = targetIdx;
        } else {
            args.kind = AccessKind::DynamicIndex;
            args.index = targetIdx;
            args.indexType = "u32";
        }
        args.compoundOp = t.param<std::string>("op") == "add" ? "+=" : "*=";
        runVectorElementWriteTest(t, args);
    });

CTS_TEST(testGroup, "vector_full_assignment")
    .desc("Tests whole-vector assignment (e.g. v = rhs).")
    .params([](ParamsBuilder u) {
        return u.combine("elementType", vectorElementTypes())
            .combine("width", {2, 3, 4})
            .combine("rhsSource", {"constructor", "variable", "uniform"})
            .beginSubcases()
            .combine("address_space", addressSpaces())
            .filter(isHostShareableCase)
            .combine("memory_view", {"ref", "ptr"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runVectorFullAssignmentTest(
            t,
            t.param<std::string>("elementType"),
            static_cast<int>(t.param<int64_t>("width")),
            t.param<std::string>("address_space"),
            t.param<std::string>("memory_view"),
            t.param<std::string>("rhsSource"));
    });

CTS_TEST(testGroup, "indexed_assignment_eval_order")
    .desc("Tests that the lhs components and rhs of an indexed vector assignment are evaluated left-to-right.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  arr[0] = vec4u(1, 10, 1, 1);
  %%
  arr[foo()][bar()] = baz();
  %%
  if (all(arr[0] == vec4u(1, 99, 1, 1))) {
    %%
  } else {
    %%
  }
)",
                {f.expect_order(0), f.expect_order(4), f.expect_order(5), f.expect_not_reached()});
            wgsl.extra = wgslTemplate(R"(
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  %%
  return 0;
}
fn bar() -> u32 {
  %%
  return 1;
}
fn baz() -> u32 {
  %%
  return 99;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

CTS_TEST(testGroup, "indexed_compound_assignment_eval_order")
    .desc("Tests that compound assignment on a vector element evaluates left-to-right and evaluates the index expression only once.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            FlowControlWgsl wgsl;
            wgsl.entrypoint = wgslTemplate(R"(
  arr[0] = vec4u(1, 10, 1, 1);
  %%
  arr[foo()][bar()] += baz();
  %%
  if (all(arr[0] == vec4u(1, 15, 1, 1))) {
    %%
  } else {
    %%
  }
)",
                {f.expect_order(0), f.expect_order(4), f.expect_order(5), f.expect_not_reached()});
            wgsl.extra = wgslTemplate(R"(
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  %%
  return 0;
}
fn bar() -> u32 {
  %%
  return 1;
}
fn baz() -> u32 {
  %%
  return 5;
}
)",
                {f.expect_order(1), f.expect_order(2), f.expect_order(3)});
            return wgsl;
        });
    });

} // namespace
