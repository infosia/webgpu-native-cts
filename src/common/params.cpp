#include "cts/test.h"

#include <sstream>

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

} // namespace

Value::Value() : data_(int64_t(0)) {}
Value::Value(int value) : data_(static_cast<int64_t>(value)) {}
Value::Value(int64_t value) : data_(value) {}
Value::Value(uint64_t value) : data_(static_cast<int64_t>(value)) {}
Value::Value(bool value) : data_(value) {}
Value::Value(const char* value) : data_(std::string(value)) {}
Value::Value(std::string value) : data_(std::move(value)) {}

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
    copy.ops_.push_back(Op{copy.inSubcases_, std::move(key), std::move(values)});
    return copy;
}

ParamsBuilder ParamsBuilder::beginSubcases() const {
    ParamsBuilder copy = *this;
    copy.inSubcases_ = true;
    return copy;
}

std::vector<ParamsBuilder::ExpandedCase> ParamsBuilder::expand() const {
    std::vector<ParamRecord> cases(1);
    std::vector<ParamRecord> subcases(1);
    bool hasSubcaseOps = false;

    for (const Op& op : ops_) {
        hasSubcaseOps = hasSubcaseOps || op.subcase;
        std::vector<ParamRecord>& target = op.subcase ? subcases : cases;
        std::vector<ParamRecord> next;
        for (const ParamRecord& record : target) {
            for (const Value& value : op.values) {
                ParamRecord copy = record;
                copy.emplace_back(op.key, value);
                next.push_back(std::move(copy));
            }
        }
        target = std::move(next);
    }

    std::vector<ExpandedCase> expanded;
    for (const ParamRecord& record : cases) {
        expanded.push_back(ExpandedCase{record, hasSubcaseOps ? subcases : std::vector<ParamRecord>()});
    }
    return expanded;
}

} // namespace cts
