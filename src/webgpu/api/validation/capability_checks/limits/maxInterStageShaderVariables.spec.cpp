// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxInterStageShaderVariables.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxInterStageShaderVariablesTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxInterStageShaderVariables"; }
};

TestGroup<MaxInterStageShaderVariablesTest> testGroup = MakeTestGroup<MaxInterStageShaderVariablesTest>(
    "api,validation,capability_checks,limits,maxInterStageShaderVariables",
    "API Validation Tests for maxInterStageShaderVariables.");

constexpr const char* kTestItems[] = {
    "point-list",
    "front_facing",
    "sample_index",
    "sample_mask",
    "primitive_index",
    "subgroup_invocation_id",
    "subgroup_size",
    "sample_mask_out",
};

bool hasItem(const std::vector<std::string>& items, std::string_view item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

std::string joinItems(const std::vector<std::string>& items) {
    if (items.empty()) return "none";
    std::string result = items[0];
    for (size_t i = 1; i < items.size(); ++i) {
        result += "|";
        result += items[i];
    }
    return result;
}

std::vector<std::string> parseItems(const std::string& value) {
    if (value == "none") return {};
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find('|', start);
        items.push_back(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return items;
}

void addCombinations(
    std::vector<Value>& values,
    const std::vector<std::string>& items,
    size_t wanted,
    size_t start,
    std::vector<std::string>* path) {
    if (path->size() == wanted) {
        values.emplace_back(joinItems(*path));
        return;
    }
    for (size_t i = start; i < items.size(); ++i) {
        path->push_back(items[i]);
        addCombinations(values, items, wanted, i + 1, path);
        path->pop_back();
    }
}

std::vector<Value> testItemCombinationValues() {
    std::vector<std::string> allItems(std::begin(kTestItems), std::end(kTestItems));
    std::vector<Value> values;
    values.emplace_back(std::string("none"));
    for (size_t size = 1; size <= 3; ++size) {
        std::vector<std::string> path;
        addCombinations(values, allItems, size, 0, &path);
    }
    values.emplace_back(joinItems(allItems));
    return values;
}

bool requiresSubgroupsFeature(const std::vector<std::string>& items) {
    return hasItem(items, "subgroup_invocation_id") || hasItem(items, "subgroup_size");
}

const char* fragmentInputType(std::string_view input) {
    if (input == "front_facing") return "bool";
    if (input == "sample_index") return "u32";
    if (input == "sample_mask") return "u32";
    if (input == "primitive_index") return "u32";
    if (input == "subgroup_invocation_id") return "u32";
    if (input == "subgroup_size") return "u32";
    return nullptr;
}

WGPURenderPipelineDescriptor getPipelineDescriptor(
    MaxInterStageShaderVariablesTest& t,
    const SpecificLimitTestInputs& inputs,
    const std::vector<std::string>& items,
    WGPUFragmentState* fragment,
    WGPUColorTargetState* target) {
    const int64_t vertexOutputDeductions = hasItem(items, "point-list") ? 1 : 0;
    std::vector<std::string> usedFragInputs;
    for (const std::string& item : items) {
        if (fragmentInputType(item) != nullptr) {
            usedFragInputs.push_back(item);
        }
    }
    const int64_t fragmentInputDeductions = static_cast<int64_t>(usedFragInputs.size());
    const int64_t numVertexOutputVariables = static_cast<int64_t>(inputs.testValue) - vertexOutputDeductions;
    const int64_t numFragmentInputVariables = static_cast<int64_t>(inputs.testValue) - fragmentInputDeductions;
    const int64_t numInterStageVariables = std::min(numVertexOutputVariables, numFragmentInputVariables);

    std::ostringstream fragInputs;
    for (size_t i = 0; i < usedFragInputs.size(); ++i) {
        fragInputs << "  @builtin(" << usedFragInputs[i] << ") i_" << i << ": "
                   << fragmentInputType(usedFragInputs[i]) << ",\n";
    }
    std::ostringstream varyings;
    for (int64_t i = 0; i < numInterStageVariables; ++i) {
        varyings << "  @location(" << i << ") v4_" << i << ": vec4f,\n";
    }

    std::ostringstream code;
    if (hasItem(items, "primitive_index")) {
        code << "enable primitive_index;\n";
    }
    if (requiresSubgroupsFeature(items)) {
        code << "enable subgroups;\n";
    }
    code << "struct VSOut {\n"
         << "  @builtin(position) p: vec4f,\n"
         << varyings.str()
         << "}\n"
         << "struct FSIn {\n"
         << fragInputs.str()
         << varyings.str()
         << "}\n"
         << "struct FSOut {\n"
         << "  @location(0) color: vec4f,\n";
    if (hasItem(items, "sample_mask_out")) {
        code << "  @builtin(sample_mask) sampleMask: u32,\n";
    }
    code << "}\n"
         << "@vertex fn vs() -> VSOut {\n"
         << "  var o: VSOut;\n"
         << "  o.p = vec4f(0);\n"
         << "  return o;\n"
         << "}\n"
         << "@fragment fn fs(i: FSIn) -> FSOut {\n"
         << "  var o: FSOut;\n"
         << "  o.color = vec4f(0);\n"
         << "  return o;\n"
         << "}\n";

    WGPUShaderModule module = t.createShaderModuleTracked(code.str());
    target->format = WGPUTextureFormat_RGBA8Unorm;
    target->writeMask = WGPUColorWriteMask_All;
    *fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment->module = module;
    fragment->entryPoint = sv("fs");
    fragment->targetCount = 1;
    fragment->targets = target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.primitive.topology =
        hasItem(items, "point-list") ? WGPUPrimitiveTopology_PointList : WGPUPrimitiveTopology_TriangleList;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs");
    desc.fragment = fragment;
    return desc;
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .desc("Test using at and over maxInterStageShaderVariables limit in createRenderPipeline(Async)")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u).combine("async", {false, true}).combine("items", testItemCombinationValues());
    })
    .fn([](MaxInterStageShaderVariablesTest& t) {
        const bool async = t.param<bool>("async");
        const std::vector<std::string> items = parseItems(t.param<std::string>("items"));
        std::vector<WGPUFeatureName> features;
        if (hasItem(items, "primitive_index")) {
            if (!wgpuAdapterHasFeature(t.adapter(), WGPUFeatureName_PrimitiveIndex)) {
                t.skip("primitive_index requires primitive-index feature");
            }
            features.push_back(WGPUFeatureName_PrimitiveIndex);
        }
        if (requiresSubgroupsFeature(items)) {
            if (!wgpuAdapterHasFeature(t.adapter(), WGPUFeatureName_Subgroups)) {
                t.skip("subgroup_invocation_id or subgroup_size requires subgroups feature");
            }
            features.push_back(WGPUFeatureName_Subgroups);
        }
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
                WGPURenderPipelineDescriptor desc = getPipelineDescriptor(t, inputs, items, &fragment, &target);
                t.testCreateRenderPipeline(desc, async, inputs.shouldError);
            }, {}, features);
    });

} // namespace
