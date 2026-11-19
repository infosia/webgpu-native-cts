#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/query.h"
#include "cts/format_sample.h"
#include "cts/test.h"

namespace cts {

struct CaseRun {
    std::string file;
    const TestSpec* test = nullptr;
    ParamRecord params;
    std::vector<ParamRecord> subcases;
    std::string query;
    size_t expectedResultCount = 1;
};

struct PlannedCase {
    size_t position = 0;
    CaseRun run;
};

void serializeCasePlan(
    const std::string& path,
    const std::vector<CaseRun>& cases,
    const std::vector<size_t>& positions);
std::vector<PlannedCase> loadCasePlan(const std::string& path);
bool caseSelectedByShard(size_t position, const RunOptions& options);
bool singleCasePlanMatchesRunCase(const std::vector<PlannedCase>& cases, const std::string& runCaseQuery);

/// Collects the registered cases selected by `queries`, in registry order. Without
/// `sampleFormats`, subcases are expanded only for cases whose case-level parameters match a
/// query; with it, each selected test is fully expanded so format sampling sees every subcase.
std::vector<CaseRun> collectCases(
    const std::vector<Query>& queries,
    bool sampleFormats,
    FormatSampleStats* stats);

} // namespace cts
