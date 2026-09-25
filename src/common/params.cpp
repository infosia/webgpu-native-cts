#include "cts/test.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <system_error>

namespace cts {
namespace {

std::string jsonQuote(const std::string& value) {
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

bool recordHasKey(const ParamRecord& record, std::string_view key) {
    return findParam(record, key) != nullptr;
}

void abortOnCaseKeyCollision(const ParamRecord& caseRecord, std::string_view key) {
    if (recordHasKey(caseRecord, key)) {
        std::abort();
    }
}

} // namespace

Value::Value() : data_(int64_t(0)) {}
Value::Value(int value) : data_(static_cast<int64_t>(value)) {}
Value::Value(int64_t value) : data_(value) {}
Value::Value(uint64_t value) : data_(static_cast<int64_t>(value)) {}
Value::Value(bool value) : data_(value) {}
Value::Value(double value) : data_(value) {}
Value::Value(const char* value) : data_(std::string(value)) {}
Value::Value(std::string value) : data_(std::move(value)) {}
Value::Value(Undefined value) : data_(value) {}

Value Value::undef() {
    return Value(Undefined{});
}

const Value::Data& Value::data() const {
    return data_;
}

std::string stringifyValue(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                // TODO: Implement JS-compatible number formatting and upstream magic values
                // (_nan_, _posinfinity_, _neginfinity_, _negzero_) before floats become case params.
                std::array<char, 32> buffer{};
                const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
                if (result.ec != std::errc()) {
                    throw std::runtime_error("failed to stringify double parameter");
                }
                return std::string(buffer.data(), result.ptr);
            } else if constexpr (std::is_same_v<T, Value::Undefined>) {
                return "_undef_";
            } else {
                return jsonQuote(v);
            }
        },
        value.data());
}

std::string stringifyParams(const ParamRecord& params) {
    std::ostringstream out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i != 0) {
            out << ";";
        }
        out << params[i].first << "=" << stringifyValue(params[i].second);
    }
    return out.str();
}

const Value* findParam(const ParamRecord& params, std::string_view key) {
    for (auto it = params.rbegin(); it != params.rend(); ++it) {
        if (it->first == key) {
            return &it->second;
        }
    }
    return nullptr;
}

template <>
int valueAs<int>(const Value& value) {
    return static_cast<int>(std::get<int64_t>(value.data()));
}

template <>
int64_t valueAs<int64_t>(const Value& value) {
    return std::get<int64_t>(value.data());
}

template <>
uint64_t valueAs<uint64_t>(const Value& value) {
    return static_cast<uint64_t>(std::get<int64_t>(value.data()));
}

template <>
double valueAs<double>(const Value& value) {
    if (const auto* stored = std::get_if<double>(&value.data())) {
        return *stored;
    }
    return static_cast<double>(std::get<int64_t>(value.data()));
}

template <>
bool valueAs<bool>(const Value& value) {
    return std::get<bool>(value.data());
}

template <>
std::string valueAs<std::string>(const Value& value) {
    return std::get<std::string>(value.data());
}

ParamsBuilder ParamsBuilder::combine(std::string key, std::initializer_list<Value> values) const {
    return combine(std::move(key), std::vector<Value>(values));
}

ParamsBuilder ParamsBuilder::combine(std::string key, std::vector<Value> values) const {
    ParamsBuilder copy = *this;
    Op op;
    op.kind = Op::Kind::Combine;
    op.subcase = copy.inSubcases_;
    op.key = std::move(key);
    op.values = std::move(values);
    copy.ops_.push_back(std::move(op));
    return copy;
}

ParamsBuilder ParamsBuilder::expand(std::string key, std::function<std::vector<Value>(const ParamRecord&)> expander) const {
    ParamsBuilder copy = *this;
    Op op;
    op.kind = Op::Kind::Expand;
    op.subcase = copy.inSubcases_;
    op.key = std::move(key);
    op.expander = std::move(expander);
    copy.ops_.push_back(std::move(op));
    return copy;
}

ParamsBuilder ParamsBuilder::combineWithParams(std::vector<ParamRecord> records) const {
    ParamsBuilder copy = *this;
    Op op;
    op.kind = Op::Kind::CombineWithParams;
    op.subcase = copy.inSubcases_;
    op.records = std::move(records);
    copy.ops_.push_back(std::move(op));
    return copy;
}

ParamsBuilder ParamsBuilder::filter(std::function<bool(const ParamRecord&)> predicate) const {
    ParamsBuilder copy = *this;
    Op op;
    op.kind = Op::Kind::Filter;
    op.subcase = copy.inSubcases_;
    op.predicate = std::move(predicate);
    copy.ops_.push_back(std::move(op));
    return copy;
}

ParamsBuilder ParamsBuilder::beginSubcases() const {
    ParamsBuilder copy = *this;
    copy.inSubcases_ = true;
    return copy;
}

std::vector<ParamsBuilder::ExpandedCase> ParamsBuilder::expand() const {
    std::vector<ParamRecord> cases(1);
    bool hasSubcaseOps = false;

    for (const Op& op : ops_) {
        if (op.subcase) {
            hasSubcaseOps = true;
            continue;
        }
        if (op.kind == Op::Kind::Filter) {
            cases.erase(
                std::remove_if(cases.begin(), cases.end(), [&](const ParamRecord& record) {
                    return !op.predicate(record);
                }),
                cases.end());
        } else if (op.kind == Op::Kind::CombineWithParams) {
            std::vector<ParamRecord> next;
            next.reserve(cases.size() * op.records.size());
            for (ParamRecord& record : cases) {
                for (size_t i = 0; i < op.records.size(); ++i) {
                    const ParamRecord& additions = op.records[i];
                    ParamRecord copy = i + 1 == op.records.size() ? std::move(record) : record;
                    copy.insert(copy.end(), additions.begin(), additions.end());
                    next.push_back(std::move(copy));
                }
            }
            cases = std::move(next);
        } else if (op.kind == Op::Kind::Expand) {
            std::vector<ParamRecord> next;
            for (ParamRecord& record : cases) {
                std::vector<Value> values = op.expander(record);
                for (size_t i = 0; i < values.size(); ++i) {
                    ParamRecord copy = i + 1 == values.size() ? std::move(record) : record;
                    copy.emplace_back(op.key, std::move(values[i]));
                    next.push_back(std::move(copy));
                }
            }
            cases = std::move(next);
        } else {
            std::vector<ParamRecord> next;
            next.reserve(cases.size() * op.values.size());
            for (ParamRecord& record : cases) {
                for (size_t i = 0; i < op.values.size(); ++i) {
                    const Value& value = op.values[i];
                    ParamRecord copy = i + 1 == op.values.size() ? std::move(record) : record;
                    copy.emplace_back(op.key, value);
                    next.push_back(std::move(copy));
                }
            }
            cases = std::move(next);
        }
    }

    std::vector<ExpandedCase> expanded;
    expanded.reserve(cases.size());
    for (ParamRecord& caseRecord : cases) {
        if (!hasSubcaseOps) {
            expanded.push_back(ExpandedCase{std::move(caseRecord), {}});
            continue;
        }

        std::vector<ParamRecord> subcases(1, caseRecord);
        for (const Op& op : ops_) {
            if (!op.subcase) {
                continue;
            }
            if (op.kind == Op::Kind::Filter) {
                subcases.erase(
                    std::remove_if(subcases.begin(), subcases.end(), [&](const ParamRecord& record) {
                        return !op.predicate(record);
                    }),
                    subcases.end());
            } else if (op.kind == Op::Kind::CombineWithParams) {
                for (const ParamRecord& additions : op.records) {
                    for (const auto& addition : additions) {
                        abortOnCaseKeyCollision(caseRecord, addition.first);
                    }
                }
                std::vector<ParamRecord> next;
                next.reserve(subcases.size() * op.records.size());
                for (ParamRecord& record : subcases) {
                    for (size_t i = 0; i < op.records.size(); ++i) {
                        const ParamRecord& additions = op.records[i];
                        ParamRecord copy = i + 1 == op.records.size() ? std::move(record) : record;
                        copy.insert(copy.end(), additions.begin(), additions.end());
                        next.push_back(std::move(copy));
                    }
                }
                subcases = std::move(next);
            } else if (op.kind == Op::Kind::Expand) {
                abortOnCaseKeyCollision(caseRecord, op.key);
                std::vector<ParamRecord> next;
                for (ParamRecord& record : subcases) {
                    std::vector<Value> values = op.expander(record);
                    for (size_t i = 0; i < values.size(); ++i) {
                        ParamRecord copy = i + 1 == values.size() ? std::move(record) : record;
                        copy.emplace_back(op.key, std::move(values[i]));
                        next.push_back(std::move(copy));
                    }
                }
                subcases = std::move(next);
            } else {
                abortOnCaseKeyCollision(caseRecord, op.key);
                std::vector<ParamRecord> next;
                next.reserve(subcases.size() * op.values.size());
                for (ParamRecord& record : subcases) {
                    for (size_t i = 0; i < op.values.size(); ++i) {
                        const Value& value = op.values[i];
                        ParamRecord copy = i + 1 == op.values.size() ? std::move(record) : record;
                        copy.emplace_back(op.key, value);
                        next.push_back(std::move(copy));
                    }
                }
                subcases = std::move(next);
            }
        }

        if (subcases.empty()) {
            continue;
        }

        std::vector<ParamRecord> subcaseOnlyRecords;
        subcaseOnlyRecords.reserve(subcases.size());
        for (ParamRecord& subcase : subcases) {
            ParamRecord subcaseOnly;
            for (auto& param : subcase) {
                if (!recordHasKey(caseRecord, param.first)) {
                    subcaseOnly.push_back(std::move(param));
                }
            }
            subcaseOnlyRecords.push_back(std::move(subcaseOnly));
        }
        expanded.push_back(ExpandedCase{std::move(caseRecord), std::move(subcaseOnlyRecords)});
    }
    return expanded;
}

} // namespace cts
