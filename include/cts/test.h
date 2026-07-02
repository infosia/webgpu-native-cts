#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "cts/webgpu.h"

namespace cts {

class Fixture;

/// A single test parameter value. Mirrors the JS CTS notion of a parameter:
/// it can hold an integer, boolean, double, string, or the special "undefined"
/// marker, stored in a type-tagged variant.
class Value {
  public:
    /// Tag type representing a JS `undefined` parameter value.
    struct Undefined {};
    using Data = std::variant<int64_t, bool, double, Undefined, std::string>;

    /// Constructs an undefined value.
    Value();
    Value(int value);
    Value(int64_t value);
    Value(uint64_t value);
    Value(bool value);
    Value(double value);
    Value(const char* value);
    Value(std::string value);
    /// Returns a value holding the "undefined" marker.
    static Value undef();

    /// Returns the underlying variant payload.
    const Data& data() const;

  private:
    explicit Value(Undefined value);
    Data data_;
};

/// An ordered list of (key, value) parameter pairs identifying a test case or subcase.
using ParamRecord = std::vector<std::pair<std::string, Value>>;

/// Formats a single value the way it appears in a case query (e.g. `"true"`, `"4"`, quoted strings).
std::string stringifyValue(const Value& value);
/// Formats a full parameter record as the `key=value;...` query suffix.
std::string stringifyParams(const ParamRecord& params);
/// Looks up a parameter by key, returning nullptr when absent.
const Value* findParam(const ParamRecord& params, std::string_view key);

/// Extracts the value as type `T`, throwing/failing on a type mismatch. Specialized per supported T.
template <class T>
T valueAs(const Value& value);

/// Builds the cartesian product of test parameters, mirroring the JS CTS
/// `params.combine/.expand/.filter/.beginSubcases` chaining API. Each method
/// returns a new builder (immutable/fluent); `expand()` materializes the cases.
class ParamsBuilder {
  public:
    /// Multiplies the current cases by each value of `key` (initializer-list form).
    ParamsBuilder combine(std::string key, std::initializer_list<Value> values) const;
    /// Multiplies the current cases by each value of `key` (vector form).
    ParamsBuilder combine(std::string key, std::vector<Value> values) const;
    /// Multiplies cases by values computed per existing record (data-dependent combine).
    ParamsBuilder expand(std::string key, std::function<std::vector<Value>(const ParamRecord&)> expander) const;
    /// Multiplies cases by whole pre-built parameter records.
    ParamsBuilder combineWithParams(std::vector<ParamRecord> records) const;
    /// Drops cases for which the predicate returns false.
    ParamsBuilder filter(std::function<bool(const ParamRecord&)> predicate) const;
    /// Marks subsequent combine/expand operations as producing subcases rather than cases.
    ParamsBuilder beginSubcases() const;

    /// One expanded case: its case-level params plus the subcase records derived after `beginSubcases()`.
    struct ExpandedCase {
        ParamRecord params;
        std::vector<ParamRecord> subcases;
    };

    /// Materializes the configured operations into the final list of cases (with subcases).
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

/// Thrown to skip the current (sub)case; reported as Skip rather than Fail.
class SkipTestCase : public std::runtime_error {
  public:
    explicit SkipTestCase(const std::string& message);
};

/// Thrown on a failed expectation; reported as Fail.
class TestFailed : public std::runtime_error {
  public:
    explicit TestFailed(const std::string& message);
};

/// Base class for a test instance. Holds the active parameter record and provides
/// the expectation/skip/warn primitives shared by every test. Override `init`/`finalize`
/// for per-case setup/teardown (mirrors the JS CTS `Fixture`).
class Fixture {
  public:
    virtual ~Fixture() = default;
    /// Per-case setup, run before the test body.
    virtual void init();
    /// Per-case teardown, run after the test body (even on failure).
    virtual void finalize();

    /// Installs the parameter record for the case about to run.
    void setParams(ParamRecord params);
    /// Returns the active parameter record.
    const ParamRecord& params() const;
    /// True if `key` is present in the parameters.
    bool hasParam(std::string_view key) const;
    /// True if `key` is present and holds the "undefined" marker.
    bool paramIsUndefined(std::string_view key) const;
    /// True if `key` is present and holds a string.
    bool paramIsString(std::string_view key) const;

    /// Returns the parameter `key` as type `T`, failing the case if it is missing.
    template <class T>
    T param(std::string_view key) const {
        const Value* value = findParam(params_, key);
        if (value == nullptr) {
            fail("missing parameter: " + std::string(key));
        }
        return valueAs<T>(*value);
    }

    /// Asserts `condition`; fails the case with `message` when false.
    void expect(bool condition, const std::string& message = "");
    /// Fails the case immediately (throws TestFailed).
    [[noreturn]] void fail(const std::string& message) const;
    /// Skips the case immediately (throws SkipTestCase).
    [[noreturn]] void skip(const std::string& message) const;
    /// Records a non-fatal warning; the case is reported as Warn unless it also fails.
    void warn(const std::string& message);
    /// Returns the warnings accumulated during the case.
    const std::vector<std::string>& warnings() const;
    /// Records a WebGPU uncaptured-error message observed during the case.
    void recordUncapturedError(std::string message);
    /// True if an uncaptured error was recorded.
    bool hasUncapturedError() const;
    /// Returns the most recently recorded uncaptured-error message.
    const std::string& uncapturedError() const;

  private:
    ParamRecord params_;
    std::vector<std::string> warnings_;
    std::string uncapturedError_;
};

/// Outcome of a (sub)case run, in increasing severity.
enum class TestStatus {
    Pass,
    Skip,
    Warn,
    Fail,
    Crash,
};

/// Result of running a single (sub)case: its query, status, any message, and attempt count.
struct SubcaseResult {
    std::string query;
    TestStatus status = TestStatus::Pass;
    std::string message;
    int attempts = 1;
};

struct RetryOutcome {
    size_t reportIndex = 0;
    bool flaky = false;
};

/// A loaded expectations file: full case queries (`exact`) and query prefixes
/// (`prefixes`) whose matching cases are treated as expected failures.
struct ExpectationSet {
    std::unordered_set<std::string> exact;
    std::vector<std::string> prefixes;
};

// Max concurrent GPU-driving children when --workers is auto/0. GPU/VRAM pressure, not CPU, is the
// limiter; high counts can wedge the OS, so the auto default is capped low on purpose.
inline constexpr int kDefaultMaxWorkers = 8;

enum class BaselineDelta {
    Unchanged,
    Regressed,
    Fixed,
    New,
    Removed,
};

struct RunOptions;

/// True if `query` is covered by an exact entry or any prefix in `expectations`.
bool expectationMatches(const ExpectationSet& expectations, const std::string& query);
/// Loads an expectations file (one query/prefix per line) from `path`.
ExpectationSet loadExpectations(const std::string& path);
/// One JSONL line for a case result, with the expectation overlay already applied.
std::string resultJsonLine(const SubcaseResult& r, bool expected);
/// Parse a baseline JSONL file into query -> effective status.
std::unordered_map<std::string, std::string> loadBaseline(const std::string& path);
/// Classify one query's current effective status against a loaded baseline.
BaselineDelta classifyDelta(
    const std::string& effectiveNow,
    const std::unordered_map<std::string, std::string>& baseline,
    const std::string& query);
/// Resolves --workers auto/0 to the capped default, preserving explicit serial workers=1.
int resolveWorkers(const RunOptions& options);
/// Chooses the reported attempt for one isolated case retry sequence.
RetryOutcome chooseRetryOutcome(const std::vector<TestStatus>& attempts);

/// Creates a fresh fixture instance for a case.
using FixtureFactory = std::function<std::unique_ptr<Fixture>()>;
/// The test body, invoked with the (already-initialized) fixture.
using TestFn = std::function<void(Fixture&)>;
/// Transforms the seed builder into the test's full parameter builder.
using ParamsFn = std::function<ParamsBuilder(ParamsBuilder)>;

/// A registered test: its leaf name, description, fixture factory, body, params,
/// and an optional "unimplemented" marker (with reason) used to stub ports.
struct TestSpec {
    std::string name;
    std::string desc;
    FixtureFactory fixtureFactory;
    TestFn fn;
    ParamsFn paramsFn;
    bool unimplemented = false;
    std::string unimplementedReason;
};

/// One `.spec` file's worth of tests, keyed by its CTS file path.
struct SpecFile {
    std::string path;
    std::string desc;
    std::vector<TestSpec> tests;
};

/// Process-wide catalog of every registered spec file, populated at static-init
/// time by `MakeTestGroup`/`CTS_TEST`.
class Registry {
  public:
    /// Returns the singleton registry.
    static Registry& instance();
    /// Registers (or returns the existing) spec file for `path`.
    SpecFile& addFile(std::string path, std::string desc);
    /// Returns all registered spec files.
    const std::vector<SpecFile>& files() const;

  private:
    std::vector<SpecFile> files_;
};

/// Handle to a spec file's tests, parameterized by the fixture type `F` used by
/// its cases. Returned by `MakeTestGroup`; feed it to `makeTestBuilder`/`CTS_TEST`.
template <class F>
class TestGroup {
  public:
    TestGroup(std::string path, std::string desc)
        : file_(&Registry::instance().addFile(std::move(path), std::move(desc))) {}

    /// Returns the backing spec file in the registry.
    SpecFile& file() {
        return *file_;
    }

  private:
    SpecFile* file_;
};

/// Registers (or reuses) the spec file at `path` and returns a group bound to fixture `F`.
template <class F>
TestGroup<F> MakeTestGroup(std::string path, std::string desc) {
    return TestGroup<F>(std::move(path), std::move(desc));
}

/// Fluent builder that accumulates one `TestSpec` and registers it into the group.
/// Mirrors the JS CTS `g.test(...).desc(...).params(...).fn(...)` chain.
template <class F>
class TestBuilder {
  public:
    TestBuilder(TestGroup<F>& group, std::string name)
        : group_(group), spec_{} {
        spec_.name = std::move(name);
        spec_.fixtureFactory = [] { return std::make_unique<F>(); };
    }

    /// Sets the test description; returns `*this` for chaining.
    TestBuilder& desc(std::string desc) {
        spec_.desc = std::move(desc);
        return *this;
    }

    /// Sets the parameter generator; returns `*this` for chaining.
    TestBuilder& params(ParamsFn paramsFn) {
        spec_.paramsFn = std::move(paramsFn);
        return *this;
    }

    /// Sets the test body and registers the spec. Returns 0 so it can initialize a static.
    int fn(std::function<void(F&)> fn) {
        spec_.fn = [fn = std::move(fn)](Fixture& fixture) { fn(static_cast<F&>(fixture)); };
        group_.file().tests.push_back(std::move(spec_));
        return 0;
    }

    /// Registers the spec as unimplemented (a recorded stub). Returns 0 like `fn`.
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

/// Starts a test builder named `name` within `group`.
template <class F>
TestBuilder<F> makeTestBuilder(TestGroup<F>& group, std::string name) {
    return TestBuilder<F>(group, std::move(name));
}

#define CTS_CONCAT_IMPL(a, b) a##b
#define CTS_CONCAT(a, b) CTS_CONCAT_IMPL(a, b)
/// Declares a test in `group` named `name` at static-init time. Continue with
/// `.desc(...).params(...).fn(...)` (or `.unimplemented(...)`).
#define CTS_TEST(group, name) static auto CTS_CONCAT(cts_test_registrar_, __LINE__) = cts::makeTestBuilder(group, name)

/// Runner configuration parsed from the CLI: what to run (queries / single case),
/// listing modes, sharding, worker/isolation behavior, expectation & crash-list
/// paths, and arguments forwarded to isolated child processes.
struct RunOptions {
    bool list = false;             ///< List matching test (case-group) queries and exit.
    bool listCases = false;        ///< List individual case queries and exit.
    bool isolate = false;          ///< Run each case in its own child process.
    bool sampleFormats = false;    ///< Sample a subset of format params instead of running all.
    int workers = 1;               ///< Parallel worker processes (0 = auto, 1 = serial).
    int retries = 0;               ///< Extra per-case attempts on Fail/Crash (--isolate only).
    bool workersSpecified = false; ///< True when --workers was present on the CLI.
    long caseTimeoutMs = 0;        ///< Per isolated child watchdog in milliseconds (0 = off).
    int shardIndex = -1;           ///< This shard's index when sharding (0-based; -1 = no sharding).
    int shardCount = 0;            ///< Total shard count when sharding.
    size_t shardFrom = 0;          ///< Starting case offset within the shard.
    bool shardResults = false;     ///< Emit per-shard result output.
    std::string casePlanPath;      ///< Serialized case plan loaded by shard-result workers.
    std::string runCaseQuery;      ///< When set, run exactly this one full case query (child mode).
    std::string expectationsPath;  ///< Path to the expected-failures file.
    std::string crashListPath;     ///< Path to a known-crash list to skip.
    std::string emitCrashListPath; ///< Path to write a newly discovered crash list.
    std::string outputPath;        ///< Path to write JSONL result output.
    std::string baselinePath;      ///< Path to a prior JSONL result output to diff against.
    std::string executablePath;    ///< Path to this executable, used to spawn isolated children.
    std::vector<std::string> forwardedArgs; ///< Extra args passed through to child processes.
    std::vector<std::string> queries;       ///< Test queries selecting what to run.
};

/// Runs the queries described by `options`; returns a process exit code.
int runQueries(const RunOptions& options);
/// Writes the suite catalog (the `listing.json` test index) to `path`. Returns an exit code.
int writeListingJson(const std::string& path);
/// Renders the Fail/Crash results as crash-list lines (queries) for the crash-list file.
std::vector<std::string> crashListLines(const std::vector<SubcaseResult>& results);
/// True if the case at `index` belongs to shard `shardIndex` of `shardCount`.
bool caseBelongsToShard(size_t index, int shardIndex, int shardCount);
/// Parses a single `RESULT\t<status>\t<message>` line into a result, or nullopt.
std::optional<SubcaseResult> parseResultLine(const std::string& line);
/// Scans an isolated child's stdout `output` for its RESULT line for `query`.
std::optional<SubcaseResult> parseIsolatedResultLine(const std::string& query, const std::string& output);
/// Produces a deliberate failing result, used by the harness self-test.
std::vector<SubcaseResult> runSyntheticFailureForSelfTest();
/// Sets the fixture that thread-local error callbacks attribute uncaptured errors to.
void setCurrentTest(Fixture* fixture);

} // namespace cts
