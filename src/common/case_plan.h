#pragma once

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
};

void serializeCasePlan(const std::string& path, const std::vector<CaseRun>& cases);
std::vector<CaseRun> loadCasePlan(const std::string& path);

} // namespace cts
