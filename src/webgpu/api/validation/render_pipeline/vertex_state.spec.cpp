// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/vertex_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,vertex_state",
    R"(
This test dedicatedly tests validation of GPUVertexState of createRenderPipeline.
)");

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

static constexpr const char* kVertexShaderNoInput =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 0.0);\n"
    "}\n";

static constexpr const char* kFragmentShader =
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";

enum class VertexFormatKind { Uint, Sint, Unorm, Snorm, Float };

struct VertexFormatInfo {
    const char* name;
    WGPUVertexFormat format;
    VertexFormatKind kind;
    uint64_t byteSize;
};

static constexpr std::array<VertexFormatInfo, 41> kVertexFormats = {{
    {"uint8", WGPUVertexFormat_Uint8, VertexFormatKind::Uint, 1},
    {"uint8x2", WGPUVertexFormat_Uint8x2, VertexFormatKind::Uint, 2},
    {"uint8x4", WGPUVertexFormat_Uint8x4, VertexFormatKind::Uint, 4},
    {"sint8", WGPUVertexFormat_Sint8, VertexFormatKind::Sint, 1},
    {"sint8x2", WGPUVertexFormat_Sint8x2, VertexFormatKind::Sint, 2},
    {"sint8x4", WGPUVertexFormat_Sint8x4, VertexFormatKind::Sint, 4},
    {"unorm8", WGPUVertexFormat_Unorm8, VertexFormatKind::Unorm, 1},
    {"unorm8x2", WGPUVertexFormat_Unorm8x2, VertexFormatKind::Unorm, 2},
    {"unorm8x4", WGPUVertexFormat_Unorm8x4, VertexFormatKind::Unorm, 4},
    {"snorm8", WGPUVertexFormat_Snorm8, VertexFormatKind::Snorm, 1},
    {"snorm8x2", WGPUVertexFormat_Snorm8x2, VertexFormatKind::Snorm, 2},
    {"snorm8x4", WGPUVertexFormat_Snorm8x4, VertexFormatKind::Snorm, 4},
    {"uint16", WGPUVertexFormat_Uint16, VertexFormatKind::Uint, 2},
    {"uint16x2", WGPUVertexFormat_Uint16x2, VertexFormatKind::Uint, 4},
    {"uint16x4", WGPUVertexFormat_Uint16x4, VertexFormatKind::Uint, 8},
    {"sint16", WGPUVertexFormat_Sint16, VertexFormatKind::Sint, 2},
    {"sint16x2", WGPUVertexFormat_Sint16x2, VertexFormatKind::Sint, 4},
    {"sint16x4", WGPUVertexFormat_Sint16x4, VertexFormatKind::Sint, 8},
    {"unorm16", WGPUVertexFormat_Unorm16, VertexFormatKind::Unorm, 2},
    {"unorm16x2", WGPUVertexFormat_Unorm16x2, VertexFormatKind::Unorm, 4},
    {"unorm16x4", WGPUVertexFormat_Unorm16x4, VertexFormatKind::Unorm, 8},
    {"snorm16", WGPUVertexFormat_Snorm16, VertexFormatKind::Snorm, 2},
    {"snorm16x2", WGPUVertexFormat_Snorm16x2, VertexFormatKind::Snorm, 4},
    {"snorm16x4", WGPUVertexFormat_Snorm16x4, VertexFormatKind::Snorm, 8},
    {"float16", WGPUVertexFormat_Float16, VertexFormatKind::Float, 2},
    {"float16x2", WGPUVertexFormat_Float16x2, VertexFormatKind::Float, 4},
    {"float16x4", WGPUVertexFormat_Float16x4, VertexFormatKind::Float, 8},
    {"float32", WGPUVertexFormat_Float32, VertexFormatKind::Float, 4},
    {"float32x2", WGPUVertexFormat_Float32x2, VertexFormatKind::Float, 8},
    {"float32x3", WGPUVertexFormat_Float32x3, VertexFormatKind::Float, 12},
    {"float32x4", WGPUVertexFormat_Float32x4, VertexFormatKind::Float, 16},
    {"uint32", WGPUVertexFormat_Uint32, VertexFormatKind::Uint, 4},
    {"uint32x2", WGPUVertexFormat_Uint32x2, VertexFormatKind::Uint, 8},
    {"uint32x3", WGPUVertexFormat_Uint32x3, VertexFormatKind::Uint, 12},
    {"uint32x4", WGPUVertexFormat_Uint32x4, VertexFormatKind::Uint, 16},
    {"sint32", WGPUVertexFormat_Sint32, VertexFormatKind::Sint, 4},
    {"sint32x2", WGPUVertexFormat_Sint32x2, VertexFormatKind::Sint, 8},
    {"sint32x3", WGPUVertexFormat_Sint32x3, VertexFormatKind::Sint, 12},
    {"sint32x4", WGPUVertexFormat_Sint32x4, VertexFormatKind::Sint, 16},
    {"unorm10-10-10-2", WGPUVertexFormat_Unorm10_10_10_2, VertexFormatKind::Unorm, 4},
    {"unorm8x4-bgra", WGPUVertexFormat_Unorm8x4BGRA, VertexFormatKind::Unorm, 4},
}};

std::vector<Value> vertexFormatValues() {
    std::vector<Value> values;
    values.reserve(kVertexFormats.size());
    for (const VertexFormatInfo& info : kVertexFormats) values.emplace_back(std::string(info.name));
    return values;
}

const VertexFormatInfo& vertexFormatInfo(std::string_view name) {
    for (const VertexFormatInfo& info : kVertexFormats) {
        if (name == info.name) return info;
    }
    std::abort();
}

const char* requiredBaseType(VertexFormatKind kind) {
    switch (kind) {
        case VertexFormatKind::Uint:
            return "u32";
        case VertexFormatKind::Sint:
            return "i32";
        case VertexFormatKind::Unorm:
        case VertexFormatKind::Snorm:
        case VertexFormatKind::Float:
            return "f32";
    }
    std::abort();
}

struct LimitVariant {
    int64_t mult;
    int64_t add;
};

uint64_t makeLimitVariant(uint32_t limit, LimitVariant variant) {
    return static_cast<uint64_t>(static_cast<int64_t>(limit) * variant.mult + variant.add);
}

uint64_t makeValueTestVariant(uint64_t base, LimitVariant variant) {
    return static_cast<uint64_t>(static_cast<int64_t>(base) * variant.mult + variant.add);
}

std::vector<ParamRecord> variants(std::string_view prefix, std::initializer_list<LimitVariant> items) {
    std::vector<ParamRecord> records;
    for (const LimitVariant& item : items) {
        records.push_back(ParamRecord{{std::string(prefix) + "_mult", item.mult},
                                      {std::string(prefix) + "_add", item.add}});
    }
    return records;
}

LimitVariant readVariant(AllFeaturesMaxLimitsGpuTest& t, const char* prefix) {
    return LimitVariant{t.param<int64_t>(std::string(prefix) + "_mult"),
                        t.param<int64_t>(std::string(prefix) + "_add")};
}

struct BufferSpec {
    uint64_t arrayStride = 0;
    std::vector<WGPUVertexAttribute> attributes;
    bool present = true;
};

using BufferSpecs = std::vector<BufferSpec>;

void ensureBuffer(BufferSpecs& buffers, uint64_t index) {
    if (buffers.size() <= index) buffers.resize(static_cast<size_t>(index + 1));
}

void addTestAttributes(
    std::vector<WGPUVertexAttribute>& attributes,
    std::optional<WGPUVertexAttribute> testAttribute,
    bool testAttributeAtStart,
    uint32_t extraAttributeCount,
    const std::vector<uint32_t>& skippedLocations) {
    uint32_t currentLocation = 0;
    uint32_t added = 0;
    while (added != extraAttributeCount) {
        bool skip = false;
        for (uint32_t loc : skippedLocations) {
            if (loc == currentLocation) skip = true;
        }
        if (!skip) {
            WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
            attr.format = WGPUVertexFormat_Float32;
            attr.shaderLocation = currentLocation;
            attr.offset = 0;
            attributes.push_back(attr);
            ++added;
        }
        ++currentLocation;
    }
    if (testAttribute.has_value()) {
        if (testAttributeAtStart) {
            attributes.insert(attributes.begin(), *testAttribute);
        } else {
            attributes.push_back(*testAttribute);
        }
    }
}

std::string generateTestVertexShader(const std::vector<std::pair<std::string, uint32_t>>& inputs) {
    std::string interfaces;
    std::string body;
    uint32_t count = 0;
    for (const auto& input : inputs) {
        interfaces += "  @location(" + std::to_string(input.second) + ") input" +
            std::to_string(count) + " : " + input.first + ",\n";
        body += "  var i" + std::to_string(count) + " : " + input.first + " = input.input" +
            std::to_string(count) + ";\n";
        ++count;
    }
    return "struct Inputs {\n" + interfaces +
        "};\n"
        "@vertex fn main(input : Inputs) -> @builtin(position) vec4<f32> {\n" +
        body +
        "  return vec4<f32>(0.0, 0.0, 0.0, 0.0);\n"
        "}\n";
}

struct PipelineHolder {
    std::string vertexCode = kVertexShaderNoInput;
    WGPUShaderModule vertexModule = nullptr;
    WGPUShaderModule fragmentModule = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    std::vector<std::vector<WGPUVertexAttribute>> attributes;
    std::vector<WGPUVertexBufferLayout> layouts;
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
};

PipelineHolder makePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    const BufferSpecs& specs,
    std::string vertexShader = kVertexShaderNoInput) {
    PipelineHolder h;
    h.vertexCode = std::move(vertexShader);
    h.vertexModule = t.createShaderModuleTracked(h.vertexCode);
    h.fragmentModule = t.createShaderModuleTracked(kFragmentShader);
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    h.pipelineLayout = t.createPipelineLayoutTracked(layoutDesc);
    h.attributes.reserve(specs.size());
    h.layouts.reserve(specs.size());
    for (const BufferSpec& spec : specs) {
        h.attributes.push_back(spec.attributes);
        WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        layout.arrayStride = spec.arrayStride;
        layout.attributeCount = h.attributes.back().size();
        layout.attributes = h.attributes.back().empty() ? nullptr : h.attributes.back().data();
        h.layouts.push_back(layout);
    }
    h.colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    h.colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    h.fragment = WGPU_FRAGMENT_STATE_INIT;
    h.fragment.module = h.fragmentModule;
    h.fragment.entryPoint = sv("main");
    h.fragment.targetCount = 1;
    h.fragment.targets = &h.colorTarget;
    h.desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    h.desc.layout = h.pipelineLayout;
    h.desc.vertex.module = h.vertexModule;
    h.desc.vertex.entryPoint = sv("main");
    h.desc.vertex.bufferCount = h.layouts.size();
    h.desc.vertex.buffers = h.layouts.empty() ? nullptr : h.layouts.data();
    h.desc.fragment = &h.fragment;
    h.desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    return h;
}

void testVertexState(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    const BufferSpecs& buffers,
    std::string vertexShader = kVertexShaderNoInput) {
    auto h = makePipeline(t, buffers, std::move(vertexShader));
    // Native validation is eager; all createRenderPipelineAsync cases map to this sync path.
    t.expectValidationError([&] { t.createRenderPipelineTracked(h.desc); }, !success);
}

CTS_TEST(g, "max_vertex_buffer_limit")
    .desc("Test that only up to <maxVertexBuffers> vertex buffers are allowed.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("countVariant", {{0, 0}, {0, 1}, {1, 0}, {1, 1}}))
            .combine("lastEmpty", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t count = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "countVariant")));
        const bool lastEmpty = t.param<bool>("lastEmpty");
        BufferSpecs buffers(count);
        for (uint32_t i = 0; i < count; ++i) {
            buffers[i].arrayStride = 0;
            if (!lastEmpty && i == count - 1) {
                WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
                attr.format = WGPUVertexFormat_Float32;
                attr.shaderLocation = 0;
                attr.offset = 0;
                buffers[i].attributes.push_back(attr);
            }
        }
        testVertexState(t, count <= limits.maxVertexBuffers, buffers);
    });

CTS_TEST(g, "max_vertex_attribute_limit")
    .desc("Test that only up to <maxVertexAttributes> vertex attributes are allowed.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("attribCountVariant", {{0, 0}, {0, 1}, {1, 0}, {1, 1}}))
            .combine("attribsPerBuffer", {0, 1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t attribCount = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "attribCountVariant")));
        const uint32_t perBuffer = static_cast<uint32_t>(t.param<int>("attribsPerBuffer"));
        BufferSpecs buffers;
        uint32_t added = 0;
        while (added != attribCount) {
            uint32_t target = perBuffer == 0 ? attribCount : std::min(attribCount, added + perBuffer);
            if (buffers.size() == limits.maxVertexBuffers - 1) target = attribCount;
            BufferSpec spec;
            while (added != target) {
                WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
                attr.format = WGPUVertexFormat_Float32;
                attr.offset = 0;
                attr.shaderLocation = added;
                spec.attributes.push_back(attr);
                ++added;
            }
            buffers.push_back(spec);
        }
        testVertexState(t, attribCount <= limits.maxVertexAttributes, buffers);
    });

CTS_TEST(g, "max_vertex_buffer_array_stride_limit")
    .desc("Test that the vertex buffer arrayStride must be at most <maxVertexBufferArrayStride>.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("vertexBufferIndexVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("arrayStrideVariant", {{0, 0}, {0, 4}, {0, 256}, {1, -4}, {1, 0}, {1, 4}}));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t index = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexVariant")));
        const uint64_t stride = makeLimitVariant(limits.maxVertexBufferArrayStride, readVariant(t, "arrayStrideVariant"));
        BufferSpecs buffers;
        ensureBuffer(buffers, index);
        buffers[index].arrayStride = stride;
        testVertexState(t, stride <= limits.maxVertexBufferArrayStride, buffers);
    });

CTS_TEST(g, "vertex_buffer_array_stride_limit_alignment")
    .desc("Test that the vertex buffer arrayStride must be a multiple of 4 (including 0).")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("vertexBufferIndexVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("arrayStrideVariant", {{0, 0}, {0, 1}, {0, 2}, {0, 4}, {1, -4}, {1, -2}, {1, 0}}));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t index = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexVariant")));
        const uint64_t stride = makeLimitVariant(limits.maxVertexBufferArrayStride, readVariant(t, "arrayStrideVariant"));
        BufferSpecs buffers;
        ensureBuffer(buffers, index);
        buffers[index].arrayStride = stride;
        testVertexState(t, stride % 4 == 0, buffers);
    });

CTS_TEST(g, "vertex_attribute_shaderLocation_limit")
    .desc("Test shaderLocation must be less than maxVertexAttributes.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("vertexBufferIndexVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("extraAttributeCountVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combine("testAttributeAtStart", {false, true})
            .combineWithParams(variants("testShaderLocationVariant", {{0, 0}, {0, 1}, {1, -1}, {1, 0}}));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t index = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexVariant")));
        const uint32_t extra = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "extraAttributeCountVariant")));
        const uint32_t location = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "testShaderLocationVariant")));
        WGPUVertexAttribute testAttr = WGPU_VERTEX_ATTRIBUTE_INIT;
        testAttr.format = WGPUVertexFormat_Float32;
        testAttr.offset = 0;
        testAttr.shaderLocation = location;
        BufferSpecs buffers;
        ensureBuffer(buffers, index);
        buffers[index].arrayStride = 256;
        addTestAttributes(buffers[index].attributes, testAttr, t.param<bool>("testAttributeAtStart"), extra, {location});
        testVertexState(t, location < limits.maxVertexAttributes, buffers);
    });

CTS_TEST(g, "vertex_attribute_shaderLocation_unique")
    .desc("Test that shaderLocation must be unique in the vertex state.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("vertexBufferIndexAVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("vertexBufferIndexBVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combine("testAttributeAtStartA", {false, true})
            .combine("testAttributeAtStartB", {false, true})
            .combineWithParams(variants("shaderLocationAVariant", {{0, 0}, {0, 1}, {0, 7}, {1, -1}}))
            .combineWithParams(variants("shaderLocationBVariant", {{0, 0}, {0, 1}, {0, 7}, {1, -1}}))
            .combine("extraAttributeCount", {0, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t indexA = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexAVariant")));
        const uint32_t indexB = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexBVariant")));
        const uint32_t locA = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "shaderLocationAVariant")));
        const uint32_t locB = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "shaderLocationBVariant")));
        BufferSpecs buffers;
        ensureBuffer(buffers, std::max(indexA, indexB));
        buffers[indexA].arrayStride = 256;
        buffers[indexB].arrayStride = 256;
        WGPUVertexAttribute attrA = WGPU_VERTEX_ATTRIBUTE_INIT;
        attrA.format = WGPUVertexFormat_Float32;
        attrA.shaderLocation = locA;
        WGPUVertexAttribute attrB = WGPU_VERTEX_ATTRIBUTE_INIT;
        attrB.format = WGPUVertexFormat_Float32;
        attrB.shaderLocation = locB;
        if (indexA == indexB) {
            addTestAttributes(buffers[indexA].attributes, attrA, t.param<bool>("testAttributeAtStartA"),
                              static_cast<uint32_t>(t.param<int>("extraAttributeCount")), {locA, locB});
            addTestAttributes(buffers[indexA].attributes, attrB, t.param<bool>("testAttributeAtStartB"), 0, {});
        } else {
            addTestAttributes(buffers[indexA].attributes, attrA, t.param<bool>("testAttributeAtStartA"),
                              static_cast<uint32_t>(t.param<int>("extraAttributeCount")), {locA, locB});
            addTestAttributes(buffers[indexB].attributes, attrB, t.param<bool>("testAttributeAtStartB"), 0, {});
        }
        testVertexState(t, locA != locB, buffers);
    });

CTS_TEST(g, "vertex_shader_input_location_limit")
    .desc("Test that vertex shader's input's location decoration must be less than maxVertexAttributes.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams(variants("testLocationVariant", {{0, 0}, {0, 1}, {1, -1}, {1, 0}, {0, 2147483647}}));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t location = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "testLocationVariant")));
        WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
        attr.format = WGPUVertexFormat_Float32;
        attr.offset = 0;
        attr.shaderLocation = location;
        BufferSpecs buffers(1);
        buffers[0].arrayStride = 512;
        buffers[0].attributes.push_back(attr);
        testVertexState(t, location < limits.maxVertexAttributes, buffers,
                        generateTestVertexShader({{"vec4<f32>", location}}));
    });

CTS_TEST(g, "vertex_shader_input_location_in_vertex_state")
    .desc("Test that a vertex shader input must have a corresponding attribute in the vertex state.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams(variants("vertexBufferIndexVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("extraAttributeCountVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combine("testAttributeAtStart", {false, true})
            .combineWithParams(variants("testShaderLocationVariant", {{0, 0}, {0, 1}, {0, 4}, {0, 5}, {1, -1}}));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t index = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexVariant")));
        const uint32_t extra = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "extraAttributeCountVariant")));
        const uint32_t location = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "testShaderLocationVariant")));
        const std::string shader = generateTestVertexShader({{"vec4<f32>", location}});
        BufferSpecs buffers;
        ensureBuffer(buffers, index);
        buffers[index].arrayStride = 256;
        addTestAttributes(buffers[index].attributes, std::nullopt, true, extra, {location});
        testVertexState(t, false, buffers, shader);
        WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
        attr.format = WGPUVertexFormat_Float32;
        attr.shaderLocation = location;
        attr.offset = 0;
        addTestAttributes(buffers[index].attributes, attr, t.param<bool>("testAttributeAtStart"), 0, {});
        testVertexState(t, true, buffers, shader);
    });

CTS_TEST(g, "vertex_shader_type_matches_attribute_format")
    .desc("Test that the vertex shader declaration must have a type compatible with the vertex format.")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .combine("shaderBaseType", {std::string("u32"), std::string("i32"), std::string("f32")})
            .expand("shaderType", [](const ParamRecord& p) {
                const std::string base = valueAs<std::string>(*findParam(p, "shaderBaseType"));
                return std::vector<Value>{base, "vec2<" + base + ">", "vec3<" + base + ">", "vec4<" + base + ">"};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& info = vertexFormatInfo(t.param<std::string>("format"));
        const std::string shaderBaseType = t.param<std::string>("shaderBaseType");
        const std::string shaderType = t.param<std::string>("shaderType");
        WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
        attr.format = info.format;
        attr.shaderLocation = 0;
        BufferSpecs buffers(1);
        buffers[0].attributes.push_back(attr);
        testVertexState(t, shaderBaseType == requiredBaseType(info.kind), buffers,
                        generateTestVertexShader({{shaderType, 0}}));
    });

std::vector<Value> offsetVariantValuesForFormat(const ParamRecord& p) {
    const VertexFormatInfo& info = vertexFormatInfo(valueAs<std::string>(*findParam(p, "format")));
    std::vector<Value> values;
    const std::array<LimitVariant, 9> variantsToUse = {{
        {0, 0}, {0, static_cast<int64_t>(info.byteSize / 2)}, {0, static_cast<int64_t>(info.byteSize)},
        {0, 2}, {0, 4}, {1, -static_cast<int64_t>(info.byteSize)},
        {1, -static_cast<int64_t>(info.byteSize + info.byteSize / 2)},
        {1, -static_cast<int64_t>(info.byteSize + 4)}, {1, -static_cast<int64_t>(info.byteSize + 2)},
    }};
    for (const LimitVariant& v : variantsToUse) {
        const std::string key = std::to_string(v.mult) + ":" + std::to_string(v.add);
        bool exists = false;
        for (const Value& old : values) {
            if (valueAs<std::string>(old) == key) exists = true;
        }
        if (!exists) values.emplace_back(key);
    }
    return values;
}

LimitVariant parseVariantString(const std::string& key) {
    const size_t colon = key.find(':');
    return LimitVariant{std::stoll(key.substr(0, colon)), std::stoll(key.substr(colon + 1))};
}

CTS_TEST(g, "vertex_attribute_offset_alignment")
    .desc("Test that vertex attribute offsets must be aligned to the format's component byte size.")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .combineWithParams(variants("arrayStrideVariant", {{0, 256}, {1, 0}}))
            .expand("offsetVariant", offsetVariantValuesForFormat)
            .beginSubcases()
            .combineWithParams(variants("vertexBufferIndexVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("extraAttributeCountVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combine("testAttributeAtStart", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const VertexFormatInfo& info = vertexFormatInfo(t.param<std::string>("format"));
        const uint64_t stride = makeLimitVariant(limits.maxVertexBufferArrayStride, readVariant(t, "arrayStrideVariant"));
        const uint64_t offset = makeValueTestVariant(stride, parseVariantString(t.param<std::string>("offsetVariant")));
        const uint32_t index = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexVariant")));
        const uint32_t extra = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "extraAttributeCountVariant")));
        WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
        attr.format = info.format;
        attr.offset = offset;
        attr.shaderLocation = 0;
        BufferSpecs buffers;
        ensureBuffer(buffers, index);
        buffers[index].arrayStride = stride;
        addTestAttributes(buffers[index].attributes, attr, t.param<bool>("testAttributeAtStart"), extra, {0});
        testVertexState(t, offset % std::min<uint64_t>(4, info.byteSize) == 0, buffers);
    });

std::vector<Value> containedOffsetVariantValues(const ParamRecord& p) {
    const VertexFormatInfo& info = vertexFormatInfo(valueAs<std::string>(*findParam(p, "format")));
    std::vector<LimitVariant> variantsToUse = {{0, 0}, {0, 4}, {1, -static_cast<int64_t>(info.byteSize)},
                                               {1, -static_cast<int64_t>(info.byteSize) + 4}};
    if (info.byteSize != 4) {
        variantsToUse.push_back({0, static_cast<int64_t>(info.byteSize)});
        variantsToUse.push_back({1, 0});
    }
    std::vector<Value> values;
    for (const LimitVariant& v : variantsToUse) {
        values.emplace_back(std::to_string(v.mult) + ":" + std::to_string(v.add));
    }
    return values;
}

CTS_TEST(g, "vertex_attribute_contained_in_stride")
    .desc("Test that vertex attribute [offset, offset + formatSize) must be contained in arrayStride if arrayStride is not 0.")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .combineWithParams(variants("arrayStrideVariant", {{0, 0}, {0, 256}, {1, -4}, {1, 0}}))
            .expand("offsetVariant", containedOffsetVariantValues)
            .combineWithParams(variants("vertexBufferIndexVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combineWithParams(variants("extraAttributeCountVariant", {{0, 0}, {0, 1}, {1, -1}}))
            .combine("testAttributeAtStart", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const VertexFormatInfo& info = vertexFormatInfo(t.param<std::string>("format"));
        const uint64_t arrayStride = makeLimitVariant(limits.maxVertexBufferArrayStride, readVariant(t, "arrayStrideVariant"));
        const uint64_t strideForOffset = arrayStride != 0 ? arrayStride : limits.maxVertexBufferArrayStride;
        const uint64_t offset = makeValueTestVariant(strideForOffset, parseVariantString(t.param<std::string>("offsetVariant")));
        const uint32_t index = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexBuffers, readVariant(t, "vertexBufferIndexVariant")));
        const uint32_t extra = static_cast<uint32_t>(makeLimitVariant(limits.maxVertexAttributes, readVariant(t, "extraAttributeCountVariant")));
        WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
        attr.format = info.format;
        attr.offset = offset;
        attr.shaderLocation = 0;
        BufferSpecs buffers;
        ensureBuffer(buffers, index);
        buffers[index].arrayStride = arrayStride;
        addTestAttributes(buffers[index].attributes, attr, t.param<bool>("testAttributeAtStart"), extra, {0});
        const uint64_t limit = arrayStride == 0 ? limits.maxVertexBufferArrayStride : arrayStride;
        testVertexState(t, offset + info.byteSize <= limit, buffers);
    });

CTS_TEST(g, "many_attributes_overlapping")
    .desc("Test that it is valid to have many vertex attributes overlap")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        BufferSpecs buffers(1);
        const std::array<WGPUVertexFormat, 3> formats = {
            WGPUVertexFormat_Float32x4, WGPUVertexFormat_Uint32x4, WGPUVertexFormat_Sint32x4};
        for (uint32_t i = 0; i < limits.maxVertexAttributes; ++i) {
            WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
            attr.format = formats[i % formats.size()];
            attr.offset = uint64_t(i) * 4u;
            attr.shaderLocation = i;
            buffers[0].attributes.push_back(attr);
        }
        testVertexState(t, true, buffers);
    });

} // namespace
