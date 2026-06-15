#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "cts/test.h"

namespace cts {

/// Decides, for a `(key, value)` parameter, whether it is a format param that should
/// participate in sampling: returns true to keep, false to drop, nullopt if it is not a format param.
using FormatSampleHook = std::function<std::optional<bool>(std::string_view key, const Value& value)>;

/// Installs the process-wide format-sample hook used by `--sample-formats`.
void setFormatSampleHook(FormatSampleHook hook);
/// Returns the currently installed format-sample hook.
const FormatSampleHook& getFormatSampleHook();

/// Minimum number of format-keyed cases a test must have before sampling kicks in.
inline constexpr std::size_t kFormatSampleThreshold = 6;

/// Tallies of what format sampling did: tests touched, runs kept, runs dropped.
struct FormatSampleStats {
    std::size_t testsSampled = 0;
    std::size_t runsKept = 0;
    std::size_t runsDropped = 0;
};

/// Returns `cases` with format-only variants thinned out (when a test exceeds `threshold`
/// format cases), using `hook` to identify format params; accumulates counts into `stats`.
std::vector<ParamsBuilder::ExpandedCase> sampleFormatsInCases(
    std::vector<ParamsBuilder::ExpandedCase> cases,
    const FormatSampleHook& hook,
    std::size_t threshold = kFormatSampleThreshold,
    FormatSampleStats* stats = nullptr);

} // namespace cts
