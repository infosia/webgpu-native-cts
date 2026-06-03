#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "cts/webgpu.h"

namespace cts {

class Fixture;

class Value {
  public:
    struct Undefined {};
    using Data = std::variant<int64_t, bool, double, Undefined, std::string>;

    Value();
    Value(int value);
    Value(int64_t value);
    Value(uint64_t value);
    Value(bool value);
    Value(double value);
    Value(const char* value);
    Value(std::string value);
    static Value undef();

    const Data& data() const;

  private:
    explicit Value(Undefined value);
    Data data_;
};

using ParamRecord = std::vector<std::pair<std::string, Value>>;

std::string stringifyValue(const Value& value);
std::string stringifyParams(const ParamRecord& params);
const Value* findParam(const ParamRecord& params, std::string_view key);

template <class T>
T valueAs(const Value& value);

class ParamsBuilder {
  public:
    ParamsBuilder combine(std::string key, std::initializer_list<Value> values) const;
    ParamsBuilder combine(std::string key, std::vector<Value> values) const;
    ParamsBuilder expand(std::string key, std::function<std::vector<Value>(const ParamRecord&)> expander) const;
    ParamsBuilder combineWithParams(std::vector<ParamRecord> records) const;
    ParamsBuilder filter(std::function<bool(const ParamRecord&)> predicate) const;
    ParamsBuilder beginSubcases() const;

    struct ExpandedCase {
        ParamRecord params;
        std::vector<ParamRecord> subcases;
    };

    std::vector<ExpandedCase> expand() const;

  private:
    struct Op {
        enum class Kind {
            Combine,
            Expand,
            CombineWithParams,
            Filter,
        };

        Kind kind = Kind::Combine;
        bool subcase = false;
        std::string key;
        std::vector<Value> values;
        std::vector<ParamRecord> records;
        std::function<std::vector<Value>(const ParamRecord&)> expander;
        std::function<bool(const ParamRecord&)> predicate;
    };

    std::vector<Op> ops_;
    bool inSubcases_ = false;
};

class SkipTestCase : public std::runtime_error {
  public:
    explicit SkipTestCase(const std::string& message);
};

class TestFailed : public std::runtime_error {
  public:
    explicit TestFailed(const std::string& message);
};

class Fixture {
  public:
    virtual ~Fixture() = default;
    virtual void init();
    virtual void finalize();

    void setParams(ParamRecord params);
    const ParamRecord& params() const;
    bool hasParam(std::string_view key) const;
    bool paramIsUndefined(std::string_view key) const;
    bool paramIsString(std::string_view key) const;

    template <class T>
    T param(std::string_view key) const {
        const Value* value = findParam(params_, key);
        if (value == nullptr) {
            fail("missing parameter: " + std::string(key));
        }
        return valueAs<T>(*value);
    }

    void expect(bool condition, const std::string& message = "");
    [[noreturn]] void fail(const std::string& message) const;
    [[noreturn]] void skip(const std::string& message) const;
    void warn(const std::string& message);
    const std::vector<std::string>& warnings() const;
    void recordUncapturedError(std::string message);
    bool hasUncapturedError() const;
    const std::string& uncapturedError() const;

  private:
    ParamRecord params_;
    std::vector<std::string> warnings_;
    std::string uncapturedError_;
};

enum class TestStatus {
    Pass,
    Skip,
    Warn,
    Fail,
    Crash,
};

struct SubcaseResult {
    std::string query;
    TestStatus status = TestStatus::Pass;
    std::string message;
};

struct ExpectationSet {
    std::unordered_set<std::string> exact;
    std::vector<std::string> prefixes;
};

bool expectationMatches(const ExpectationSet& expectations, const std::string& query);

using FixtureFactory = std::function<std::unique_ptr<Fixture>()>;
using TestFn = std::function<void(Fixture&)>;
using ParamsFn = std::function<ParamsBuilder(ParamsBuilder)>;

struct TestSpec {
    std::string name;
    std::string desc;
    FixtureFactory fixtureFactory;
    TestFn fn;
    ParamsFn paramsFn;
    bool unimplemented = false;
    std::string unimplementedReason;
};

struct SpecFile {
    std::string path;
    std::string desc;
    std::vector<TestSpec> tests;
};

class Registry {
  public:
    static Registry& instance();
    SpecFile& addFile(std::string path, std::string desc);
    const std::vector<SpecFile>& files() const;

  private:
    std::vector<SpecFile> files_;
};

template <class F>
class TestGroup {
  public:
    TestGroup(std::string path, std::string desc)
        : file_(&Registry::instance().addFile(std::move(path), std::move(desc))) {}

    SpecFile& file() {
        return *file_;
    }

  private:
    SpecFile* file_;
};

template <class F>
TestGroup<F> MakeTestGroup(std::string path, std::string desc) {
    return TestGroup<F>(std::move(path), std::move(desc));
}

template <class F>
class TestBuilder {
  public:
    TestBuilder(TestGroup<F>& group, std::string name)
        : group_(group), spec_{} {
        spec_.name = std::move(name);
        spec_.fixtureFactory = [] { return std::make_unique<F>(); };
    }

    TestBuilder& desc(std::string desc) {
        spec_.desc = std::move(desc);
        return *this;
    }

    TestBuilder& params(ParamsFn paramsFn) {
        spec_.paramsFn = std::move(paramsFn);
        return *this;
    }

    int fn(std::function<void(F&)> fn) {
        spec_.fn = [fn = std::move(fn)](Fixture& fixture) { fn(static_cast<F&>(fixture)); };
        group_.file().tests.push_back(std::move(spec_));
        return 0;
    }

    int unimplemented(std::string reason = "unimplemented") {
        spec_.unimplemented = true;
        spec_.unimplementedReason = std::move(reason);
        group_.file().tests.push_back(std::move(spec_));
        return 0;
    }

  private:
    TestGroup<F>& group_;
    TestSpec spec_;
};

template <class F>
TestBuilder<F> makeTestBuilder(TestGroup<F>& group, std::string name) {
    return TestBuilder<F>(group, std::move(name));
}

#define CTS_CONCAT_IMPL(a, b) a##b
#define CTS_CONCAT(a, b) CTS_CONCAT_IMPL(a, b)
#define CTS_TEST(group, name) static auto CTS_CONCAT(cts_test_registrar_, __LINE__) = cts::makeTestBuilder(group, name)

struct RunOptions {
    bool list = false;
    bool listCases = false;
    bool isolate = false;
    bool sampleFormats = false;
    int workers = 0;
    int shardIndex = -1;
    int shardCount = 0;
    size_t shardFrom = 0;
    bool shardResults = false;
    std::string runCaseQuery;
    std::string expectationsPath;
    std::string crashListPath;
    std::string emitCrashListPath;
    std::string executablePath;
    std::vector<std::string> forwardedArgs;
    std::vector<std::string> queries;
};

int runQueries(const RunOptions& options);
int writeListingJson(const std::string& path);
std::vector<std::string> crashListLines(const std::vector<SubcaseResult>& results);
bool caseBelongsToShard(size_t index, int shardIndex, int shardCount);
std::optional<SubcaseResult> parseResultLine(const std::string& line);
std::vector<SubcaseResult> runSyntheticFailureForSelfTest();
void setCurrentTest(Fixture* fixture);

} // namespace cts
