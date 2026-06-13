// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxComputeInvocationsPerWorkgroup.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <array>
#include <cstdint>
#include <limits>
#include <string>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxComputeInvocationsPerWorkgroupTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxComputeInvocationsPerWorkgroup"; }
};

TestGroup<MaxComputeInvocationsPerWorkgroupTest> testGroup = MakeTestGroup<MaxComputeInvocationsPerWorkgroupTest>(
    "api,validation,capability_checks,limits,maxComputeInvocationsPerWorkgroup",
    "API Validation Tests for maxComputeInvocationsPerWorkgroup.");

std::array<uint64_t, 3> getClosestSizeOverLimit(const std::array<uint64_t, 3>& size, uint64_t limit) {
    uint64_t closest = std::numeric_limits<uint64_t>::max();
    std::array<uint64_t, 3> closestSize = {1, 1, 1};
    const uint64_t depthLimit = std::min(limit, size[2]);
    for (uint64_t depth = 1; depth <= depthLimit; ++depth) {
        for (uint64_t height = 1; height <= size[1]; ++height) {
            const uint64_t planeSize = depth * height;
            if (planeSize <= limit) {
                const uint64_t width = std::min(size[0], (limit + planeSize - 1) / planeSize);
                const uint64_t num = width * planeSize;
                const uint64_t dist = num > limit ? num - limit : 0;
                if (dist > 0 && dist < closest) {
                    closest = dist;
                    closestSize = {width, height, depth};
                }
            }
        }
    }
    return closestSize;
}

std::array<uint64_t, 3> getClosestSizeUnderOrAtLimit(const std::array<uint64_t, 3>& size, uint64_t limit) {
    uint64_t closest = std::numeric_limits<uint64_t>::max();
    std::array<uint64_t, 3> closestSize = {1, 1, 1};
    const uint64_t depthLimit = std::min(limit, size[2]);
    for (uint64_t depth = 1; depth <= depthLimit; ++depth) {
        for (uint64_t height = 1; height <= size[1]; ++height) {
            const uint64_t planeSize = depth * height;
            if (planeSize <= limit) {
                const uint64_t width = std::min(size[0], limit / planeSize);
                const uint64_t num = width * planeSize;
                const uint64_t dist = limit - num;
                if (dist < closest) {
                    closest = dist;
                    closestSize = {width, height, depth};
                }
            }
        }
    }
    return closestSize;
}

std::array<uint64_t, 3> getTestWorkgroupSize(
    MaxComputeInvocationsPerWorkgroupTest& t,
    const std::string& testValueName,
    uint64_t requestedLimit) {
    const std::array<uint64_t, 3> maxDimensions = {
        t.getDefaultLimit("maxComputeWorkgroupSizeX"),
        t.getDefaultLimit("maxComputeWorkgroupSizeY"),
        t.getDefaultLimit("maxComputeWorkgroupSizeZ"),
    };
    if (testValueName == "atLimit") {
        return getClosestSizeUnderOrAtLimit(maxDimensions, requestedLimit);
    }
    return getClosestSizeOverLimit(maxDimensions, requestedLimit);
}

CTS_TEST(testGroup, "createComputePipeline,at_over")
    .desc("Test using createComputePipeline(Async) at and over maxComputeInvocationsPerWorkgroup limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}); })
    .fn([](MaxComputeInvocationsPerWorkgroupTest& t) {
        const std::string limitTest = t.param<std::string>("limitTest");
        const std::string testValueName = t.param<std::string>("testValueName");
        const bool async = t.param<bool>("async");
        const uint64_t requestedLimit = getLimitValue(t.defaultLimit, t.adapterLimit, limitTest);
        const std::array<uint64_t, 3> workgroupSize = getTestWorkgroupSize(t, testValueName, requestedLimit);
        const uint64_t testValue = workgroupSize[0] * workgroupSize[1] * workgroupSize[2];

        t.testDeviceWithSpecificLimits(requestedLimit, testValue, [&](const SpecificLimitTestInputs& inputs) {
            std::string code;
            WGPUShaderModule module = t.getModuleForWorkgroupSize(workgroupSize, &code);
            t.testCreatePipeline("createComputePipeline", async, module, inputs.shouldError, code);
        });
    });

} // namespace
