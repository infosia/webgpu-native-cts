#include "common/query.h"

#include <stdexcept>

namespace cts {
namespace {

bool wildcardMatch(const std::string& pattern, const std::string& value) {
    if (pattern == "*" || pattern.empty()) {
        return true;
    }
    if (pattern.ends_with("*")) {
        return value.starts_with(pattern.substr(0, pattern.size() - 1));
    }
    return pattern == value;
}

} // namespace

Query parseQuery(const std::string& text) {
    const size_t suiteEnd = text.find(':');
    const size_t fileEnd =
        suiteEnd == std::string::npos ? std::string::npos : text.find(':', suiteEnd + 1);
    if (suiteEnd == std::string::npos || fileEnd == std::string::npos ||
        text.substr(0, suiteEnd) != "webgpu") {
        throw std::runtime_error("invalid query: " + text);
    }
    const size_t testEnd = text.find(':', fileEnd + 1);

    Query query;
    query.suite = text.substr(0, suiteEnd);
    query.file = text.substr(suiteEnd + 1, fileEnd - suiteEnd - 1);
    if (testEnd == std::string::npos) {
        query.test = text.substr(fileEnd + 1);
        query.params = "*";
    } else {
        query.test = text.substr(fileEnd + 1, testEnd - fileEnd - 1);
        query.params = text.substr(testEnd + 1);
    }
    return query;
}

bool queryMatchesFile(const Query& query, const std::string& file) {
    return wildcardMatch(query.file, file);
}

bool queryMatchesTest(const Query& query, const std::string& file, const std::string& test) {
    return queryMatchesFile(query, file) && wildcardMatch(query.test, test);
}

bool queryMatchesCase(const Query& query, const std::string& file, const std::string& test, const ParamRecord& params) {
    if (!queryMatchesTest(query, file, test)) {
        return false;
    }
    if (query.params == "*" || query.params.empty()) {
        return true;
    }
    return query.params == stringifyParams(params);
}

std::string caseQuery(const std::string& file, const std::string& test, const ParamRecord& params) {
    std::string query = "webgpu:" + file + ":" + test + ":";
    const std::string paramsText = stringifyParams(params);
    return query + (paramsText.empty() ? "*" : paramsText);
}

} // namespace cts
