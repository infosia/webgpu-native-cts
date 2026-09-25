#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

} // namespace cts
