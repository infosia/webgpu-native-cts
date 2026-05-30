#include "common/query.h"

#include <stdexcept>
#include <vector>

namespace cts {
namespace {

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = text.find(delim, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

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
    std::vector<std::string> parts = split(text, ':');
    if (parts.size() < 3 || parts.size() > 4 || parts[0] != "webgpu") {
        throw std::runtime_error("invalid query: " + text);
    }
    Query query;
    query.suite = parts[0];
    query.file = parts[1];
    query.test = parts[2];
    query.params = parts.size() == 4 ? parts[3] : "*";
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
