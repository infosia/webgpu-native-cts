// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxStorageBuffersInVertexStage.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"
using namespace cts; using namespace cts::capability_limits;
namespace {
class MaxStorageBuffersInVertexStageTest : public LimitTest { public: const char* limitName() const override { return "maxStorageBuffersInVertexStage"; } };
TestGroup<MaxStorageBuffersInVertexStageTest> testGroup = MakeTestGroup<MaxStorageBuffersInVertexStageTest>("api,validation,capability_checks,limits,maxStorageBuffersInVertexStage", "API Validation Tests for maxStorageBuffersInVertexStage.");
std::vector<LimitRequestEntry> extraLimits() { return {adapterLimitRequest("maxBindingsPerBindGroup"), adapterLimitRequest("maxBindGroups")}; }
CTS_TEST(testGroup, "createBindGroupLayout,at_over").params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("order", kReorderOrderValues()); }).fn([](MaxStorageBuffersInVertexStageTest& t) {
    const ReorderOrder order = parseReorderOrder(t.param<std::string>("order"));
    t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"), [&](const MaximumLimitTestInputs& in) { if (t.getAdapterLimit("maxBindingsPerBindGroup") < in.testValue) t.skip("maxBindingsPerBindGroup is less than test value"); t.expectValidationErrorOnLimitDevice([&] { createPerStageBindGroupLayout(t, WGPUShaderStage_Vertex, PerStageResourceKind::ReadOnlyStorageBuffer, order, in.testValue); }, in.shouldError); }, extraLimits());
});
CTS_TEST(testGroup, "createPipelineLayout,at_over").params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("order", kReorderOrderValues()); }).fn([](MaxStorageBuffersInVertexStageTest& t) {
    const ReorderOrder order = parseReorderOrder(t.param<std::string>("order"));
    t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"), [&](const MaximumLimitTestInputs& in) { testPerStageCreatePipelineLayout(t, WGPUShaderStage_Vertex, PerStageResourceKind::ReadOnlyStorageBuffer, order, in); }, extraLimits());
});
CTS_TEST(testGroup, "createPipeline,at_over").params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}).beginSubcases().combine("order", kReorderOrderValues()).combine("bindGroupTest", kBindGroupTestValues()); }).fn([](MaxStorageBuffersInVertexStageTest& t) {
    const bool async = t.param<bool>("async"); const std::string bindGroupTest = t.param<std::string>("bindGroupTest"); const ReorderOrder order = parseReorderOrder(t.param<std::string>("order"));
    t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"), [&](const MaximumLimitTestInputs& in) { WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT; const WGPULimits limits = queryLimits(in.device, &compat); if (bindGroupTest == "sameGroup" && in.testValue > limits.maxBindingsPerBindGroup) t.skip("can not test bindings in same group"); t.skipIfNotEnoughStorageBuffersInStage(WGPUShaderStage_Vertex, in.testValue); const std::string code = getPerStageWGSLForBindingCombination("vertex", order, bindGroupTest, [](int i, int j) { return "var<storage> u" + std::to_string(j) + "_" + std::to_string(i) + ": f32"; }, [](int i, int j) { return "_ = u" + std::to_string(j) + "_" + std::to_string(i) + ";"; }, limits.maxBindGroups, in.testValue); WGPUShaderModule module = t.createShaderModuleTracked(code); t.testCreatePipeline("createRenderPipeline", async, module, in.shouldError, code); }, extraLimits());
});
static int dependentLimitRegistrars = (testMaxStorageXXXInYYYStageDeviceCreationWithDependentLimit(testGroup, "maxStorageBuffersInVertexStage", "maxStorageBuffersPerShaderStage"), 0);
} // namespace
