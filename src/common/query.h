#pragma once

#include <string>

#include "cts/test.h"

namespace cts {

struct Query {
    std::string suite;
    std::string file;
    std::string test;
    std::string params;
};

Query parseQuery(const std::string& text);
bool queryMatchesFile(const Query& query, const std::string& file);
bool queryMatchesTest(const Query& query, const std::string& file, const std::string& test);
bool queryMatchesCase(const Query& query, const std::string& file, const std::string& test, const ParamRecord& params);
std::string caseQuery(const std::string& file, const std::string& test, const ParamRecord& params);

} // namespace cts
