#include "cts/format_sample.h"

#include <map>
#include <set>
#include <string>
#include <utility>

namespace cts {
namespace {

FormatSampleHook& hookStorage() {
    static FormatSampleHook hook;
    return hook;
}

ParamRecord mergedRunParams(const ParamsBuilder::ExpandedCase& c, const ParamRecord* subcase) {
    ParamRecord merged = c.params;
    if (subcase != nullptr) {
        merged.insert(merged.end(), subcase->begin(), subcase->end());
    }
    return merged;
}

std::vector<ParamRecord> runParamsForCase(const ParamsBuilder::ExpandedCase& c) {
    std::vector<ParamRecord> runs;
    if (c.subcases.empty()) {
        runs.push_back(c.params);
        return runs;
    }
    runs.reserve(c.subcases.size());
    for (const ParamRecord& subcase : c.subcases) {
        runs.push_back(mergedRunParams(c, &subcase));
    }
    return runs;
}

} // namespace

void setFormatSampleHook(FormatSampleHook hook) {
    hookStorage() = std::move(hook);
}

const FormatSampleHook& getFormatSampleHook() {
    return hookStorage();
}

std::vector<ParamsBuilder::ExpandedCase> sampleFormatsInCases(
    std::vector<ParamsBuilder::ExpandedCase> cases,
    const FormatSampleHook& hook,
    std::size_t threshold,
    FormatSampleStats* stats) {
    if (!hook) {
        return cases;
    }

    std::map<std::string, std::set<std::string>> recognizedValues;
    std::vector<std::vector<bool>> keepByCase;
    keepByCase.reserve(cases.size());
    std::size_t totalRuns = 0;
    std::size_t keptRuns = 0;

    for (const ParamsBuilder::ExpandedCase& c : cases) {
        const std::vector<ParamRecord> runs = runParamsForCase(c);
        std::vector<bool> caseKeep;
        caseKeep.reserve(runs.size());
        for (const ParamRecord& run : runs) {
            bool keep = true;
            for (const auto& [key, value] : run) {
                const std::optional<bool> verdict = hook(key, value);
                if (!verdict) {
                    continue;
                }
                recognizedValues[key].insert(stringifyValue(value));
                keep = keep && *verdict;
            }
            caseKeep.push_back(keep);
            ++totalRuns;
            keptRuns += keep ? 1 : 0;
        }
        keepByCase.push_back(std::move(caseKeep));
    }

    bool overThreshold = false;
    for (const auto& [_, values] : recognizedValues) {
        if (values.size() > threshold) {
            overThreshold = true;
            break;
        }
    }
    if (!overThreshold || keptRuns == 0 || keptRuns == totalRuns) {
        return cases;
    }

    std::vector<ParamsBuilder::ExpandedCase> sampled;
    sampled.reserve(cases.size());
    for (std::size_t caseIndex = 0; caseIndex < cases.size(); ++caseIndex) {
        const ParamsBuilder::ExpandedCase& c = cases[caseIndex];
        const std::vector<bool>& keep = keepByCase[caseIndex];
        if (c.subcases.empty()) {
            if (!keep.empty() && keep[0]) {
                sampled.push_back(c);
            }
            continue;
        }

        ParamsBuilder::ExpandedCase sampledCase;
        sampledCase.params = c.params;
        sampledCase.subcases.reserve(c.subcases.size());
        for (std::size_t subcaseIndex = 0; subcaseIndex < c.subcases.size(); ++subcaseIndex) {
            if (subcaseIndex < keep.size() && keep[subcaseIndex]) {
                sampledCase.subcases.push_back(c.subcases[subcaseIndex]);
            }
        }
        if (!sampledCase.subcases.empty()) {
            sampled.push_back(std::move(sampledCase));
        }
    }

    if (stats != nullptr) {
        ++stats->testsSampled;
        stats->runsKept += keptRuns;
        stats->runsDropped += totalRuns - keptRuns;
    }
    return sampled;
}

} // namespace cts
