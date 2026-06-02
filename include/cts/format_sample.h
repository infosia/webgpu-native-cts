#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "cts/test.h"

namespace cts {

using FormatSampleHook = std::function<std::optional<bool>(std::string_view key, int64_t value)>;

void setFormatSampleHook(FormatSampleHook hook);
const FormatSampleHook& getFormatSampleHook();

inline constexpr std::size_t kFormatSampleThreshold = 6;

struct FormatSampleStats {
    std::size_t testsSampled = 0;
    std::size_t runsKept = 0;
    std::size_t runsDropped = 0;
};

std::vector<ParamsBuilder::ExpandedCase> sampleFormatsInCases(
    std::vector<ParamsBuilder::ExpandedCase> cases,
    const FormatSampleHook& hook,
    std::size_t threshold = kFormatSampleThreshold,
    FormatSampleStats* stats = nullptr);

} // namespace cts
