// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxComputeWorkgroupStorageSize.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <array>
#include <sstream>
#include <string>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxComputeWorkgroupStorageSizeTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxComputeWorkgroupStorageSize"; }
};

TestGroup<MaxComputeWorkgroupStorageSizeTest> testGroup = MakeTestGroup<MaxComputeWorkgroupStorageSizeTest>(
    "api,validation,capability_checks,limits,maxComputeWorkgroupStorageSize",
    "API Validation Tests for maxComputeWorkgroupStorageSize.");

constexpr uint64_t kSmallestWorkgroupVarSize = 16;

struct WGSLTypeInfo {
    const char* type;
    uint64_t alignOf;
    uint64_t sizeOf;
    bool requireF16;
};

constexpr std::array<WGSLTypeInfo, 37> kWGSLTypes = {{
    {"f16", 2, 2, true}, {"vec2<f16>", 4, 4, true}, {"vec3<f16>", 8, 6, true},
    {"vec4<f16>", 8, 8, true}, {"mat2x2<f16>", 4, 8, true}, {"mat3x2<f16>", 4, 12, true},
    {"mat4x2<f16>", 4, 16, true}, {"mat2x3<f16>", 8, 16, true}, {"mat3x3<f16>", 8, 24, true},
    {"mat4x3<f16>", 8, 32, true}, {"mat2x4<f16>", 8, 16, true}, {"mat3x4<f16>", 8, 24, true},
    {"mat4x4<f16>", 8, 32, true}, {"f32", 4, 4, false}, {"i32", 4, 4, false}, {"u32", 4, 4, false},
    {"vec2<f32>", 8, 8, false}, {"vec2<i32>", 8, 8, false}, {"vec2<u32>", 8, 8, false},
    {"vec3<f32>", 16, 12, false}, {"vec3<i32>", 16, 12, false}, {"vec3<u32>", 16, 12, false},
    {"vec4<f32>", 16, 16, false}, {"vec4<i32>", 16, 16, false}, {"vec4<u32>", 16, 16, false},
    {"mat2x2<f32>", 8, 16, false}, {"mat3x2<f32>", 8, 24, false}, {"mat4x2<f32>", 8, 32, false},
    {"mat2x3<f32>", 16, 32, false}, {"mat3x3<f32>", 16, 48, false}, {"mat4x3<f32>", 16, 64, false},
    {"mat2x4<f32>", 16, 32, false}, {"mat3x4<f32>", 16, 48, false}, {"mat4x4<f32>", 16, 64, false},
    {"S1", 16, 48, false}, {"S2", 4, 112, false}, {"S3", 16, 32, false},
}};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

uint64_t roundDown(uint64_t value, uint64_t multiple) {
    return (value / multiple) * multiple;
}

std::vector<Value> wgslTypeValues() {
    std::vector<Value> values;
    for (const WGSLTypeInfo& info : kWGSLTypes) values.emplace_back(std::string(info.type));
    return values;
}

const WGSLTypeInfo& wgslInfo(const std::string& type) {
    for (const WGSLTypeInfo& info : kWGSLTypes) {
        if (type == info.type) return info;
    }
    std::abort();
}

WGPUShaderModule moduleForWorkgroupStorage(MaxComputeWorkgroupStorageSizeTest& t, const std::string& wgslType, uint64_t size, std::string* codeOut) {
    const WGSLTypeInfo& info = wgslInfo(wgslType);
    const uint64_t unitSize = alignTo(info.sizeOf, info.alignOf);
    const uint64_t units = size / unitSize;
    const uint64_t sizeUsed = alignTo(units * unitSize, 16);
    const uint64_t extra = (size - sizeUsed) / kSmallestWorkgroupVarSize;
    std::ostringstream code;
    if (info.requireF16) code << "enable f16;\n";
    code << "struct S1 { a: f32, b: vec4f, c: u32, };\n"
         << "struct S2 { a: array<vec3f, 7>, };\n"
         << "struct S3 { a: vec3f, b: vec2f, };\n"
         << "var<workgroup> d0: array<" << wgslType << ", " << units << ">;\n";
    if (extra != 0) code << "var<workgroup> d1: array<vec4<f32>, " << extra << ">;\n";
    code << "@compute @workgroup_size(1) fn main() { _ = d0;";
    if (extra != 0) code << " _ = d1;";
    code << " }\n";
    *codeOut = code.str();
    return t.createShaderModuleTracked(*codeOut);
}

uint64_t requestedLimitValue(const std::string& limitTest, uint64_t defaultLimit, uint64_t maximumLimit) {
    if (limitTest == "atDefault") return defaultLimit;
    if (limitTest == "underDefault") return defaultLimit - kSmallestWorkgroupVarSize;
    if (limitTest == "betweenDefaultAndMaximum") return roundDown((defaultLimit + maximumLimit) / 2, kSmallestWorkgroupVarSize);
    if (limitTest == "atMaximum") return maximumLimit;
    if (limitTest == "overMaximum") return maximumLimit + kSmallestWorkgroupVarSize;
    std::abort();
}

uint64_t testValueFor(const std::string& testValueName, uint64_t requestedLimit) {
    return testValueName == "atLimit" ? requestedLimit : requestedLimit + kSmallestWorkgroupVarSize;
}

CTS_TEST(testGroup, "createComputePipeline,at_over")
    .desc("Test using createComputePipeline(Async) at and over maxComputeWorkgroupStorageSize limit")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u).combine("async", {false, true}).combine("wgslType", wgslTypeValues());
    })
    .fn([](MaxComputeWorkgroupStorageSizeTest& t) {
        const std::string wgslType = t.param<std::string>("wgslType");
        const WGSLTypeInfo& info = wgslInfo(wgslType);
        const bool hasF16 = wgpuAdapterHasFeature(t.adapter(), WGPUFeatureName_ShaderF16) != WGPU_FALSE;
        if (info.requireF16 && !hasF16) return;
        std::vector<WGPUFeatureName> features;
        if (hasF16) features.push_back(WGPUFeatureName_ShaderF16);
        const uint64_t requested = requestedLimitValue(t.param<std::string>("limitTest"), t.defaultLimit, t.adapterLimit);
        const uint64_t testValue = testValueFor(t.param<std::string>("testValueName"), requested);
        t.testDeviceWithSpecificLimits(requested, testValue, [&](const SpecificLimitTestInputs& inputs) {
            std::string code;
            WGPUShaderModule module = moduleForWorkgroupStorage(t, wgslType, inputs.testValue, &code);
            t.testCreatePipeline("createComputePipeline", t.param<bool>("async"), module, inputs.shouldError, code);
        }, {}, features);
    });

} // namespace
