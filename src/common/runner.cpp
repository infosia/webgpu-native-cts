#include "cts/test.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "common/case_plan.h"
#include "common/query.h"
#include "cts/format_sample.h"

#if !defined(_WIN32)
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <array>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <windows.h>
#endif

namespace cts {
namespace {

const char* statusName(TestStatus status) {
    switch (status) {
    case TestStatus::Pass:
        return "pass";
    case TestStatus::Skip:
        return "skip";
    case TestStatus::Warn:
        return "warn";
    case TestStatus::Fail:
        return "fail";
    case TestStatus::Crash:
        return "crash";
    }
    return "fail";
}

std::optional<TestStatus> statusFromName(std::string_view status) {
    if (status == "pass") {
        return TestStatus::Pass;
    }
    if (status == "skip") {
        return TestStatus::Skip;
    }
    if (status == "warn") {
        return TestStatus::Warn;
    }
    if (status == "fail") {
        return TestStatus::Fail;
    }
    if (status == "crash") {
        return TestStatus::Crash;
    }
    return std::nullopt;
}

bool isFailureStatus(TestStatus status) {
    return status == TestStatus::Fail || status == TestStatus::Crash;
}

bool isFlakyResult(const SubcaseResult& result) {
    return result.attempts > 1 && !isFailureStatus(result.status);
}

std::string sanitizeResultMessage(std::string message) {
    for (char& ch : message) {
        if (ch == '\t' || ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return message;
}

std::string_view dropTrailingCR(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

void setStdoutBinaryForResultProtocol() {
#if defined(_WIN32)
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
}

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

char hexDigit(unsigned value) {
    return static_cast<char>(value < 10 ? '0' + value : 'A' + (value - 10));
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char ch : text) {
        if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\\') {
            out += "\\\\";
        } else if (ch < 0x20) {
            out += "\\u00";
            out.push_back(hexDigit(ch >> 4));
            out.push_back(hexDigit(ch & 0x0F));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::optional<std::string> jsonStringField(std::string_view line, std::string_view field) {
    const std::string needle = "\"" + std::string(field) + "\":";
    const size_t fieldPos = line.find(needle);
    if (fieldPos == std::string_view::npos) {
        return std::nullopt;
    }
    size_t pos = fieldPos + needle.size();
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
        ++pos;
    }
    if (pos >= line.size() || line[pos] != '"') {
        return std::nullopt;
    }
    ++pos;

    std::string value;
    while (pos < line.size()) {
        const char ch = line[pos++];
        if (ch == '"') {
            return value;
        }
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= line.size()) {
            return std::nullopt;
        }
        const char escaped = line[pos++];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            value.push_back(escaped);
            break;
        case 'b':
            value.push_back('\b');
            break;
        case 'f':
            value.push_back('\f');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        case 'u': {
            if (pos + 4 > line.size()) {
                return std::nullopt;
            }
            int codepoint = 0;
            for (int i = 0; i < 4; ++i) {
                const int digit = hexValue(line[pos++]);
                if (digit < 0) {
                    return std::nullopt;
                }
                codepoint = codepoint * 16 + digit;
            }
            if (codepoint <= 0x7F) {
                value.push_back(static_cast<char>(codepoint));
            }
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string effectiveStatusName(const SubcaseResult& result, bool expected) {
    if (expected && (result.status == TestStatus::Fail || result.status == TestStatus::Crash)) {
        return "xfail";
    }
    if (expected && result.status == TestStatus::Pass) {
        return "xpass";
    }
    return statusName(result.status);
}

bool isBadEffectiveStatus(const std::string& status) {
    return status == "fail" || status == "crash";
}

} // namespace

RetryOutcome chooseRetryOutcome(const std::vector<TestStatus>& attempts) {
    if (attempts.empty()) {
        return RetryOutcome{};
    }

    size_t reportIndex = attempts.size() - 1;
    for (size_t i = 0; i < attempts.size(); ++i) {
        if (!isFailureStatus(attempts[i])) {
            reportIndex = i;
            break;
        }
    }
    return RetryOutcome{reportIndex, attempts.size() > 1 && !isFailureStatus(attempts[reportIndex])};
}

ExpectationSet loadExpectations(const std::string& path) {
    ExpectationSet expectations;
    if (path.empty()) {
        return expectations;
    }

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open expectations file: " + path);
    }

    std::string line;
    while (std::getline(in, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.ends_with(":*")) {
            expectations.prefixes.push_back(line.substr(0, line.size() - 1));
        } else {
            expectations.exact.insert(line);
        }
    }
    return expectations;
}

std::unordered_map<std::string, std::string> loadBaseline(const std::string& path) {
    std::unordered_map<std::string, std::string> baseline;
    if (path.empty()) {
        return baseline;
    }

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open baseline file: " + path);
    }

    std::string line;
    while (std::getline(in, line)) {
        std::optional<std::string> query = jsonStringField(line, "query");
        if (!query) {
            continue;
        }
        std::optional<std::string> effective = jsonStringField(line, "effective");
        if (!effective) {
            effective = jsonStringField(line, "status");
        }
        if (effective) {
            baseline[*query] = *effective;
        }
    }
    return baseline;
}

std::string resultJsonLine(const SubcaseResult& r, bool expected) {
    std::ostringstream out;
    out << "{\"query\":\"" << jsonEscape(r.query)
        << "\",\"status\":\"" << statusName(r.status)
        << "\",\"message\":\"" << jsonEscape(r.message)
        << "\",\"expected\":" << (expected ? "true" : "false")
        << ",\"effective\":\"" << effectiveStatusName(r, expected) << "\""
        << ",\"attempts\":" << r.attempts;
    if (isFlakyResult(r)) {
        out << ",\"flaky\":true";
    }
    out << "}";
    return out.str();
}

namespace {

ParamsBuilder buildParams(const TestSpec& test) {
    if (!test.paramsFn) {
        return ParamsBuilder();
    }
    return test.paramsFn(ParamsBuilder());
}

std::vector<ParamsBuilder::ExpandedCase> expandedCases(const TestSpec& test) {
    return buildParams(test).expand();
}

std::vector<ParamsBuilder::ExpandedCase> sampledExpandedCases(
    const TestSpec& test,
    bool sampleFormats,
    FormatSampleStats* stats) {
    auto cases = expandedCases(test);
    if (sampleFormats) {
        cases = sampleFormatsInCases(std::move(cases), getFormatSampleHook(), kFormatSampleThreshold, stats);
    }
    return cases;
}

void printTestListLine(const std::string& file, const TestSpec& test, bool sampleFormats) {
    const auto cases = sampledExpandedCases(test, sampleFormats, nullptr);
    size_t subcases = 0;
    for (const auto& c : cases) {
        subcases += c.subcases.size();
    }
    std::cout << "webgpu:" << file << ":" << test.name << ":*"
              << " cases=" << cases.size() << " subcases=" << subcases;
    if (test.unimplemented) {
        std::cout << " unimplemented";
    }
    std::cout << "\n";
}

std::vector<CaseRun> collectCases(
    const std::vector<Query>& queries,
    bool sampleFormats,
    FormatSampleStats* stats);
std::vector<SubcaseResult> runCase(const CaseRun& c);

bool caseSelectedByShard(size_t index, const RunOptions& options) {
    if (index < options.shardFrom) {
        return false;
    }
    return caseBelongsToShard(index, options.shardIndex, options.shardCount);
}

SubcaseResult runOne(const std::string& query, const TestSpec& test, const ParamRecord& params) {
    if (test.unimplemented) {
        return SubcaseResult{query, TestStatus::Skip, test.unimplementedReason};
    }

    std::unique_ptr<Fixture> fixture = test.fixtureFactory();
    fixture->setParams(params);
    setCurrentTest(fixture.get());
    try {
        fixture->init();
        test.fn(*fixture);
        fixture->finalize();
        setCurrentTest(nullptr);
        if (fixture->hasUncapturedError()) {
            return SubcaseResult{query, TestStatus::Fail, fixture->uncapturedError()};
        }
        return SubcaseResult{query, fixture->warnings().empty() ? TestStatus::Pass : TestStatus::Warn, ""};
    } catch (const SkipTestCase& e) {
        try {
            fixture->finalize();
        } catch (...) {
        }
        setCurrentTest(nullptr);
        return SubcaseResult{query, TestStatus::Skip, e.what()};
    } catch (const TestFailed& e) {
        try {
            fixture->finalize();
        } catch (...) {
        }
        setCurrentTest(nullptr);
        if (fixture->hasUncapturedError()) {
            return SubcaseResult{query, TestStatus::Fail, fixture->uncapturedError()};
        }
        return SubcaseResult{query, TestStatus::Fail, e.what()};
    } catch (const std::exception& e) {
        try {
            fixture->finalize();
        } catch (...) {
        }
        setCurrentTest(nullptr);
        if (fixture->hasUncapturedError()) {
            return SubcaseResult{query, TestStatus::Fail, fixture->uncapturedError()};
        }
        return SubcaseResult{query, TestStatus::Fail, e.what()};
    }
}

std::vector<SubcaseResult> collectRuns(
    const std::vector<Query>& queries,
    const RunOptions& options,
    FormatSampleStats* stats) {
    std::vector<SubcaseResult> results;
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    for (size_t i = 0; i < cases.size(); ++i) {
        if (!caseSelectedByShard(i, options)) {
            continue;
        }
        std::vector<SubcaseResult> caseResults = runCase(cases[i]);
        results.insert(results.end(), caseResults.begin(), caseResults.end());
    }
    return results;
}

std::vector<CaseRun> collectCases(
    const std::vector<Query>& queries,
    bool sampleFormats,
    FormatSampleStats* stats) {
    std::vector<CaseRun> runs;
    for (const SpecFile& file : Registry::instance().files()) {
        for (const TestSpec& test : file.tests) {
            bool testSelected = false;
            for (const Query& q : queries) {
                testSelected = testSelected || queryMatchesTest(q, file.path, test.name);
            }
            if (!testSelected) {
                continue;
            }
            const auto cases = sampledExpandedCases(test, sampleFormats, stats);
            for (const auto& c : cases) {
                bool selected = false;
                for (const Query& q : queries) {
                    selected = selected || queryMatchesCase(q, file.path, test.name, c.params);
                }
                if (!selected) {
                    continue;
                }
                runs.push_back(CaseRun{
                    file.path,
                    &test,
                    c.params,
                    c.subcases,
                    caseQuery(file.path, test.name, c.params),
                });
            }
        }
    }
    return runs;
}

std::vector<SubcaseResult> runCase(const CaseRun& c) {
    std::vector<SubcaseResult> results;
    if (c.subcases.empty()) {
        results.push_back(runOne(c.query, *c.test, c.params));
    } else {
        for (const ParamRecord& subcase : c.subcases) {
            ParamRecord merged = c.params;
            merged.insert(merged.end(), subcase.begin(), subcase.end());
            results.push_back(runOne(c.query, *c.test, merged));
        }
    }
    return results;
}

SubcaseResult aggregateCaseResult(const std::string& query, const std::vector<SubcaseResult>& results) {
    if (results.empty()) {
        return SubcaseResult{query, TestStatus::Skip, "no selected subcases"};
    }

    std::optional<SubcaseResult> firstSkip;
    std::optional<SubcaseResult> firstWarn;
    for (const SubcaseResult& result : results) {
        if (result.status == TestStatus::Crash) {
            return SubcaseResult{query, TestStatus::Crash, result.message};
        }
        if (result.status == TestStatus::Fail) {
            return SubcaseResult{query, TestStatus::Fail, result.message};
        }
        if (result.status == TestStatus::Warn && !firstWarn) {
            firstWarn = result;
        }
        if (result.status == TestStatus::Skip && !firstSkip) {
            firstSkip = result;
        }
    }
    if (firstWarn) {
        return SubcaseResult{query, TestStatus::Warn, firstWarn->message};
    }
    const bool allSkip = std::all_of(results.begin(), results.end(), [](const SubcaseResult& result) {
        return result.status == TestStatus::Skip;
    });
    if (allSkip && firstSkip) {
        return SubcaseResult{query, TestStatus::Skip, firstSkip->message};
    }
    return SubcaseResult{query, TestStatus::Pass, ""};
}

int runSingleCase(const RunOptions& options) {
    const std::string& runCaseQuery = options.runCaseQuery;
    Query query = parseQuery(runCaseQuery);
    std::vector<CaseRun> cases = collectCases({query}, options.sampleFormats, nullptr);
    if (cases.empty() && options.sampleFormats) {
        cases = collectCases({query}, false, nullptr);
    }
    if (cases.size() != 1 || cases[0].query != runCaseQuery) {
        std::cout << "RESULT\tfail\texpected exactly one full case query\n";
        return 0;
    }

    const SubcaseResult aggregate = aggregateCaseResult(cases[0].query, runCase(cases[0]));
    std::cout << "RESULT\t" << statusName(aggregate.status) << "\t" << aggregate.message << "\n";
    return 0;
}

} // namespace

std::optional<SubcaseResult> parseIsolatedResultLine(const std::string& query, const std::string& output) {
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        std::string_view resultLine = dropTrailingCR(line);
        if (!resultLine.starts_with("RESULT\t")) {
            continue;
        }
        const size_t statusStart = std::string_view("RESULT\t").size();
        const size_t messageStart = resultLine.find('\t', statusStart);
        const std::string status = messageStart == std::string::npos
            ? std::string(resultLine.substr(statusStart))
            : std::string(resultLine.substr(statusStart, messageStart - statusStart));
        const std::string message = messageStart == std::string::npos ? "" : std::string(resultLine.substr(messageStart + 1));
        const std::optional<TestStatus> parsedStatus = statusFromName(status);
        if (parsedStatus) {
            return SubcaseResult{query, *parsedStatus, message};
        }
        return SubcaseResult{query, TestStatus::Crash, "unknown RESULT status: " + status};
    }
    return std::nullopt;
}

namespace {

#if defined(_WIN32)
std::string windowsErrorMessage(const char* operation, DWORD error) {
    return std::string(operation) + " failed: Windows error " + std::to_string(error);
}

std::string windowsExitMessage(DWORD exitCode) {
    std::ostringstream message;
    message << "child exited 0x" << std::hex << std::uppercase << exitCode;
    if (exitCode == 0xC0000005) {
        message << " (access violation)";
    } else if (exitCode == 0xC0000409) {
        message << " (stack buffer overrun/fast fail)";
    }
    return message.str();
}

std::string quoteWindowsArg(const std::string& arg) {
    std::string quoted;
    quoted.reserve(arg.size() + 2);
    quoted.push_back('"');
    size_t backslashes = 0;
    for (const char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

class TemporaryCasePlan {
  public:
    explicit TemporaryCasePlan(const std::vector<CaseRun>& cases) {
        char tempDir[MAX_PATH + 1]{};
        const DWORD tempDirLength = GetTempPathA(static_cast<DWORD>(sizeof(tempDir)), tempDir);
        if (tempDirLength == 0) {
            throw std::runtime_error(windowsErrorMessage("GetTempPathA", GetLastError()));
        }
        if (tempDirLength >= static_cast<DWORD>(sizeof(tempDir))) {
            throw std::runtime_error("GetTempPathA failed: temp path exceeds MAX_PATH");
        }

        char tempPath[MAX_PATH + 1]{};
        if (GetTempFileNameA(tempDir, "cts", 0, tempPath) == 0) {
            throw std::runtime_error(windowsErrorMessage("GetTempFileNameA", GetLastError()));
        }
        path_ = tempPath;

        try {
            serializeCasePlan(path_, cases);
        } catch (...) {
            (void)DeleteFileA(path_.c_str());
            path_.clear();
            throw;
        }
    }

    TemporaryCasePlan(const TemporaryCasePlan&) = delete;
    TemporaryCasePlan& operator=(const TemporaryCasePlan&) = delete;

    ~TemporaryCasePlan() {
        if (!path_.empty()) {
            (void)DeleteFileA(path_.c_str());
        }
    }

    const std::string& path() const {
        return path_;
    }

  private:
    std::string path_;
};
#endif

std::string signalMessage(int signal) {
#if !defined(_WIN32)
    const char* name = strsignal(signal);
    if (name != nullptr) {
        return std::string("signal ") + std::to_string(signal) + " (" + name + ")";
    }
#endif
    return "signal " + std::to_string(signal);
}

using Clock = std::chrono::steady_clock;

struct ChildExit {
    bool clean = false;
    std::string message;
};

struct IsolatedChildState {
    size_t position = 0;
    std::string query;
    Clock::time_point started;
    std::string output;
    std::optional<ChildExit> exit;
#if defined(_WIN32)
    PROCESS_INFORMATION proc{};
    HANDLE readPipe = INVALID_HANDLE_VALUE;
    bool pipeDrained = false;
#else
    pid_t pid = -1;
    int fd = -1;
#endif
};

struct IsolatedChildStart {
    std::optional<IsolatedChildState> child;
    std::optional<SubcaseResult> result;
};

std::vector<std::string> isolatedChildArgs(const RunOptions& options, const std::string& query) {
    std::vector<std::string> args;
    args.push_back(options.executablePath);
    for (const std::string& arg : options.forwardedArgs) {
        args.push_back(arg);
    }
    args.push_back("--run-case");
    args.push_back(query);
    return args;
}

void closeIsolatedReadPipe(IsolatedChildState& child) {
#if defined(_WIN32)
    if (child.readPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(child.readPipe);
        child.readPipe = INVALID_HANDLE_VALUE;
    }
    child.pipeDrained = true;
#else
    if (child.fd >= 0) {
        close(child.fd);
        child.fd = -1;
    }
#endif
}

IsolatedChildStart startIsolatedChild(const RunOptions& options, const std::string& query, size_t position) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE stdoutWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &inheritable, 0)) {
        return IsolatedChildStart{std::nullopt, SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("CreatePipe", GetLastError())}};
    }
    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return IsolatedChildStart{std::nullopt, SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("SetHandleInformation", error)}};
    }

    HANDLE nulHandle = CreateFileA(
        "NUL",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inheritable,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (nulHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return IsolatedChildStart{std::nullopt, SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("CreateFileA(NUL)", error)}};
    }

    std::string commandLine;
    for (const std::string& arg : isolatedChildArgs(options, query)) {
        if (!commandLine.empty()) {
            commandLine.push_back(' ');
        }
        commandLine += quoteWindowsArg(arg);
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = nulHandle;

    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessA(
        options.executablePath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &startup,
        &process);
    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        CloseHandle(nulHandle);
        return IsolatedChildStart{std::nullopt, SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("CreateProcessA", error)}};
    }

    CloseHandle(stdoutWrite);
    CloseHandle(nulHandle);

    IsolatedChildState child;
    child.position = position;
    child.query = query;
    child.started = Clock::now();
    child.proc = process;
    child.readPipe = stdoutRead;
    return IsolatedChildStart{std::move(child), std::nullopt};
#else
    std::array<int, 2> stdoutPipe{};
    if (pipe(stdoutPipe.data()) != 0) {
        return IsolatedChildStart{std::nullopt, SubcaseResult{query, TestStatus::Crash, "pipe failed: " + std::string(std::strerror(errno))}};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return IsolatedChildStart{std::nullopt, SubcaseResult{query, TestStatus::Crash, "fork failed: " + std::string(std::strerror(errno))}};
    }

    if (pid == 0) {
        close(stdoutPipe[0]);
        if (dup2(stdoutPipe[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(stdoutPipe[1]);
        const int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            (void)dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        std::vector<std::string> args = isolatedChildArgs(options, query);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (std::string& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        execv(options.executablePath.c_str(), argv.data());
        _exit(127);
    }

    close(stdoutPipe[1]);
    const int flags = fcntl(stdoutPipe[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(stdoutPipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    IsolatedChildState child;
    child.position = position;
    child.query = query;
    child.started = Clock::now();
    child.pid = pid;
    child.fd = stdoutPipe[0];
    return IsolatedChildStart{std::move(child), std::nullopt};
#endif
}

#if !defined(_WIN32)
ChildExit isolatedExitFromStatus(int status) {
    if (WIFSIGNALED(status)) {
        return ChildExit{false, signalMessage(WTERMSIG(status))};
    }
    if (WIFEXITED(status)) {
        const int exitCode = WEXITSTATUS(status);
        return ChildExit{exitCode == 0, exitCode == 0 ? "" : "child exited " + std::to_string(exitCode)};
    }
    return ChildExit{false, "child did not exit normally"};
}
#endif

std::optional<ChildExit> tryReapIsolatedChild(IsolatedChildState& child) {
#if defined(_WIN32)
    (void)child;
    return std::nullopt;
#else
    if (child.exit) {
        return child.exit;
    }
    if (child.pid <= 0) {
        return std::nullopt;
    }
    int status = 0;
    const pid_t reaped = waitpid(child.pid, &status, WNOHANG);
    if (reaped == 0) {
        return std::nullopt;
    }
    if (reaped < 0) {
        if (errno == EINTR) {
            return std::nullopt;
        }
        child.pid = -1;
        child.exit = ChildExit{false, "waitpid failed: " + std::string(std::strerror(errno))};
        return child.exit;
    }
    child.pid = -1;
    child.exit = isolatedExitFromStatus(status);
    return child.exit;
#endif
}

ChildExit waitIsolatedChildExit(IsolatedChildState& child) {
    if (child.exit) {
        return *child.exit;
    }
#if defined(_WIN32)
    WaitForSingleObject(child.proc.hProcess, INFINITE);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(child.proc.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        CloseHandle(child.proc.hThread);
        CloseHandle(child.proc.hProcess);
        return ChildExit{false, windowsErrorMessage("GetExitCodeProcess", error)};
    }
    CloseHandle(child.proc.hThread);
    CloseHandle(child.proc.hProcess);
    return ChildExit{exitCode == 0, exitCode == 0 ? "" : windowsExitMessage(exitCode)};
#else
    int status = 0;
    while (waitpid(child.pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return ChildExit{false, "waitpid failed: " + std::string(std::strerror(errno))};
    }
    child.pid = -1;
    return isolatedExitFromStatus(status);
#endif
}

SubcaseResult finishIsolatedChild(IsolatedChildState child) {
    closeIsolatedReadPipe(child);
    const ChildExit exit = waitIsolatedChildExit(child);
    if (!exit.clean) {
        return SubcaseResult{child.query, TestStatus::Crash, exit.message};
    }

    std::optional<SubcaseResult> parsed = parseIsolatedResultLine(child.query, child.output);
    if (!parsed) {
        return SubcaseResult{child.query, TestStatus::Crash, "no RESULT line"};
    }
    return *parsed;
}

void reapKilledIsolatedChild(IsolatedChildState& child) {
    if (child.exit) {
        return;
    }
#if defined(_WIN32)
    (void)TerminateProcess(child.proc.hProcess, 1);
    WaitForSingleObject(child.proc.hProcess, INFINITE);
    CloseHandle(child.proc.hThread);
    CloseHandle(child.proc.hProcess);
#else
    if (child.pid > 0) {
        (void)kill(child.pid, SIGKILL);
        int status = 0;
        while (waitpid(child.pid, &status, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        child.pid = -1;
    }
#endif
}

SubcaseResult abortIsolatedChild(IsolatedChildState child, const std::string& message) {
    closeIsolatedReadPipe(child);
    reapKilledIsolatedChild(child);
    return SubcaseResult{child.query, TestStatus::Crash, message};
}

SubcaseResult timeoutIsolatedChild(IsolatedChildState child, long timeoutMs) {
    closeIsolatedReadPipe(child);
    reapKilledIsolatedChild(child);
    return SubcaseResult{
        child.query,
        TestStatus::Crash,
        "case timed out after " + std::to_string(timeoutMs) + " ms",
    };
}

bool isolatedChildTimedOut(const IsolatedChildState& child, long timeoutMs, Clock::time_point now) {
    if (timeoutMs <= 0) {
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - child.started).count();
    return elapsed >= timeoutMs;
}

int isolatedPollTimeoutMs(const std::vector<IsolatedChildState>& children, long timeoutMs) {
    if (timeoutMs <= 0) {
        return -1;
    }
    const auto now = Clock::now();
    long remainingMin = (std::numeric_limits<long>::max)();
    for (const IsolatedChildState& child : children) {
        const long elapsed = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - child.started).count());
        const long remaining = timeoutMs - elapsed;
        remainingMin = (std::min)(remainingMin, remaining);
    }
    if (remainingMin <= 0) {
        return 0;
    }
    return static_cast<int>((std::min)(remainingMin, static_cast<long>((std::numeric_limits<int>::max)())));
}

void recordIsolatedResult(
    std::vector<std::optional<SubcaseResult>>& resultsByCase,
    size_t position,
    SubcaseResult result,
    std::vector<size_t>* completedPositions = nullptr) {
    resultsByCase[position] = std::move(result);
    if (completedPositions != nullptr) {
        completedPositions->push_back(position);
    }
}

bool finishExitedPipeDrainedIsolatedChildren(
    std::vector<IsolatedChildState>& children,
    std::vector<std::optional<SubcaseResult>>& resultsByCase,
    std::vector<size_t>* completedPositions) {
#if defined(_WIN32)
    (void)children;
    (void)resultsByCase;
    (void)completedPositions;
    return false;
#else
    bool changed = false;
    for (size_t i = 0; i < children.size();) {
        if (children[i].fd >= 0 || !tryReapIsolatedChild(children[i])) {
            ++i;
            continue;
        }
        const size_t position = children[i].position;
        recordIsolatedResult(resultsByCase, position, finishIsolatedChild(std::move(children[i])), completedPositions);
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
        changed = true;
    }
    return changed;
#endif
}

bool reapTimedOutIsolatedChildren(
    const RunOptions& options,
    std::vector<IsolatedChildState>& children,
    std::vector<std::optional<SubcaseResult>>& resultsByCase,
    std::vector<size_t>* completedPositions) {
    bool changed = false;
    const auto now = Clock::now();
    for (size_t i = 0; i < children.size();) {
        if (!isolatedChildTimedOut(children[i], options.caseTimeoutMs, now)) {
            ++i;
            continue;
        }
        const size_t position = children[i].position;
        recordIsolatedResult(
            resultsByCase,
            position,
            timeoutIsolatedChild(std::move(children[i]), options.caseTimeoutMs),
            completedPositions);
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
        changed = true;
    }
    return changed;
}

void pumpIsolatedChildren(
    const RunOptions& options,
    std::vector<IsolatedChildState>& children,
    std::vector<std::optional<SubcaseResult>>& resultsByCase,
    std::vector<size_t>* completedPositions = nullptr) {
    if (children.empty()) {
        return;
    }
    if (finishExitedPipeDrainedIsolatedChildren(children, resultsByCase, completedPositions)) {
        return;
    }
    if (reapTimedOutIsolatedChildren(options, children, resultsByCase, completedPositions)) {
        return;
    }

#if defined(_WIN32)
    std::vector<char> buffer(4096);
    bool hadActivity = false;
    for (size_t i = 0; i < children.size();) {
        IsolatedChildState& child = children[i];
        if (!child.pipeDrained) {
            DWORD bytesAvailable = 0;
            if (!PeekNamedPipe(child.readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE) {
                    closeIsolatedReadPipe(child);
                    hadActivity = true;
                } else {
                    const size_t position = child.position;
                    recordIsolatedResult(
                        resultsByCase,
                        position,
                        abortIsolatedChild(std::move(child), windowsErrorMessage("PeekNamedPipe", error)),
                        completedPositions);
                    children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
                    hadActivity = true;
                    continue;
                }
            } else if (bytesAvailable > 0) {
                if (buffer.size() < bytesAvailable) {
                    buffer.resize(bytesAvailable);
                }
                DWORD bytesRead = 0;
                if (!ReadFile(child.readPipe, buffer.data(), bytesAvailable, &bytesRead, nullptr)) {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE) {
                        closeIsolatedReadPipe(child);
                    } else {
                        const size_t position = child.position;
                        recordIsolatedResult(
                            resultsByCase,
                            position,
                            abortIsolatedChild(std::move(child), windowsErrorMessage("ReadFile", error)),
                            completedPositions);
                        children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
                        hadActivity = true;
                        continue;
                    }
                } else if (bytesRead == 0) {
                    closeIsolatedReadPipe(child);
                } else {
                    child.output.append(buffer.data(), bytesRead);
                }
                hadActivity = true;
            }
        }

        const DWORD wait = WaitForSingleObject(child.proc.hProcess, 0);
        if (child.pipeDrained && wait == WAIT_OBJECT_0) {
            const size_t position = child.position;
            recordIsolatedResult(resultsByCase, position, finishIsolatedChild(std::move(child)), completedPositions);
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
            hadActivity = true;
            continue;
        }
        if (wait == WAIT_FAILED) {
            const DWORD error = GetLastError();
            const size_t position = child.position;
            recordIsolatedResult(
                resultsByCase,
                position,
                abortIsolatedChild(std::move(child), windowsErrorMessage("WaitForSingleObject", error)),
                completedPositions);
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
            hadActivity = true;
            continue;
        }
        ++i;
    }
    if (!hadActivity) {
        const int timeoutMs = isolatedPollTimeoutMs(children, options.caseTimeoutMs);
        const DWORD sleepMs = timeoutMs < 0 ? 2 : static_cast<DWORD>((std::min)(timeoutMs, 2));
        Sleep(sleepMs);
    }
#else
    std::vector<pollfd> fds;
    fds.reserve(children.size());
    for (const IsolatedChildState& child : children) {
        fds.push_back(pollfd{child.fd, POLLIN | POLLHUP, 0});
    }

    int timeoutMs = isolatedPollTimeoutMs(children, options.caseTimeoutMs);
    const bool hasPipeDrainedChild = std::any_of(children.begin(), children.end(), [](const IsolatedChildState& child) {
        return child.fd < 0;
    });
    if (hasPipeDrainedChild) {
        timeoutMs = timeoutMs < 0 ? 2 : (std::min)(timeoutMs, 2);
    }
    const int ready = poll(fds.data(), fds.size(), timeoutMs);
    if (ready < 0) {
        if (errno == EINTR) {
            return;
        }
        throw std::runtime_error("poll failed: " + std::string(std::strerror(errno)));
    }
    if (ready == 0) {
        return;
    }

    std::array<char, 4096> buffer{};
    for (size_t i = 0; i < children.size();) {
        const short revents = fds[i].revents;
        if ((revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) == 0) {
            ++i;
            continue;
        }

        bool closed = false;
        while (true) {
            const ssize_t n = read(children[i].fd, buffer.data(), buffer.size());
            if (n > 0) {
                children[i].output.append(buffer.data(), static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                closeIsolatedReadPipe(children[i]);
                closed = true;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            const size_t position = children[i].position;
            recordIsolatedResult(
                resultsByCase,
                position,
                abortIsolatedChild(std::move(children[i]), "read failed: " + std::string(std::strerror(errno))),
                completedPositions);
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
            fds.erase(fds.begin() + static_cast<std::ptrdiff_t>(i));
            closed = false;
            break;
        }
        if (i >= children.size()) {
            break;
        }
        if (!closed) {
            ++i;
            continue;
        }

        if (!tryReapIsolatedChild(children[i])) {
            ++i;
            continue;
        }
        const size_t position = children[i].position;
        recordIsolatedResult(resultsByCase, position, finishIsolatedChild(std::move(children[i])), completedPositions);
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
        fds.erase(fds.begin() + static_cast<std::ptrdiff_t>(i));
    }
#endif
}

SubcaseResult runIsolatedChild(const RunOptions& options, const std::string& query) {
    IsolatedChildStart started = startIsolatedChild(options, query, 0);
    if (started.result) {
        return *started.result;
    }

    std::vector<IsolatedChildState> children;
    children.push_back(std::move(*started.child));
    std::vector<std::optional<SubcaseResult>> result(1);
    while (!result[0]) {
        pumpIsolatedChildren(options, children, result);
        if (children.empty() && !result[0]) {
            return SubcaseResult{query, TestStatus::Crash, "isolated child ended without result"};
        }
    }
    return *result[0];
}

SubcaseResult finalizeRetryResult(const std::vector<SubcaseResult>& attempts) {
    std::vector<TestStatus> statuses;
    statuses.reserve(attempts.size());
    for (const SubcaseResult& attempt : attempts) {
        statuses.push_back(attempt.status);
    }

    const RetryOutcome outcome = chooseRetryOutcome(statuses);
    SubcaseResult result = attempts[outcome.reportIndex];
    result.attempts = static_cast<int>(attempts.size());
    if (outcome.flaky) {
        std::cerr << "flaky: " << result.query << " failed then passed on retry " << outcome.reportIndex << "\n";
    }
    return result;
}

bool shouldRetryIsolatedAttempt(const RunOptions& options, const std::vector<SubcaseResult>& attempts) {
    if (options.retries <= 0 || attempts.empty() || !isFailureStatus(attempts.back().status)) {
        return false;
    }
    return attempts.size() <= static_cast<size_t>(options.retries);
}

SubcaseResult runIsolatedChildWithRetries(const RunOptions& options, const std::string& query) {
    std::vector<SubcaseResult> attempts;
    while (true) {
        attempts.push_back(runIsolatedChild(options, query));
        if (!shouldRetryIsolatedAttempt(options, attempts)) {
            return finalizeRetryResult(attempts);
        }
    }
}

std::vector<SubcaseResult> collectIsolatedRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    FormatSampleStats* stats) {
    std::vector<SubcaseResult> results;
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    for (size_t i = 0; i < cases.size(); ++i) {
        if (!caseSelectedByShard(i, options)) {
            continue;
        }
        results.push_back(runIsolatedChildWithRetries(options, cases[i].query));
    }
    return results;
}

void startQueuedIsolatedChildren(
    const RunOptions& options,
    const std::vector<CaseRun>& cases,
    const std::vector<size_t>& queue,
    size_t& next,
    size_t maxChildren,
    std::vector<IsolatedChildState>& children,
    std::vector<std::optional<SubcaseResult>>& resultsByCase,
    std::vector<size_t>* completedPositions) {
    while (children.size() < maxChildren && next < queue.size()) {
        const size_t position = queue[next++];
        IsolatedChildStart started = startIsolatedChild(options, cases[position].query, position);
        if (started.result) {
            recordIsolatedResult(resultsByCase, position, std::move(*started.result), completedPositions);
            continue;
        }
        children.push_back(std::move(*started.child));
    }
}

void processCompletedIsolatedAttempts(
    const RunOptions& options,
    std::vector<size_t>& queue,
    std::vector<size_t>& completedPositions,
    std::vector<std::optional<SubcaseResult>>& completedByCase,
    std::vector<std::vector<SubcaseResult>>& attemptsByCase,
    std::vector<std::optional<SubcaseResult>>& resultsByCase) {
    for (size_t position : completedPositions) {
        if (!completedByCase[position]) {
            continue;
        }

        std::vector<SubcaseResult>& attempts = attemptsByCase[position];
        attempts.push_back(std::move(*completedByCase[position]));
        completedByCase[position].reset();

        if (shouldRetryIsolatedAttempt(options, attempts)) {
            queue.push_back(position);
            continue;
        }
        resultsByCase[position] = finalizeRetryResult(attempts);
    }
    completedPositions.clear();
}

std::vector<SubcaseResult> collectIsolatedParallelRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    FormatSampleStats* stats) {
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    std::vector<size_t> queue;
    queue.reserve(cases.size());
    for (size_t i = 0; i < cases.size(); ++i) {
        if (caseSelectedByShard(i, options)) {
            queue.push_back(i);
        }
    }

    std::vector<std::optional<SubcaseResult>> resultsByCase(cases.size());
    std::vector<std::optional<SubcaseResult>> completedByCase(cases.size());
    std::vector<std::vector<SubcaseResult>> attemptsByCase(cases.size());
    std::vector<size_t> completedPositions;
    std::vector<IsolatedChildState> children;
    size_t next = 0;
    const size_t maxChildren = static_cast<size_t>((std::max)(1, resolveWorkers(options)));

    while (next < queue.size() || !children.empty()) {
        startQueuedIsolatedChildren(
            options,
            cases,
            queue,
            next,
            maxChildren,
            children,
            completedByCase,
            &completedPositions);
        if (!children.empty()) {
            pumpIsolatedChildren(options, children, completedByCase, &completedPositions);
        }
        processCompletedIsolatedAttempts(
            options,
            queue,
            completedPositions,
            completedByCase,
            attemptsByCase,
            resultsByCase);
    }

    std::vector<SubcaseResult> merged;
    for (size_t i = 0; i < cases.size(); ++i) {
        if (!caseSelectedByShard(i, options)) {
            continue;
        }
        if (!resultsByCase[i]) {
            if (!attemptsByCase[i].empty()) {
                merged.push_back(finalizeRetryResult(attemptsByCase[i]));
            } else {
                merged.push_back(SubcaseResult{cases[i].query, TestStatus::Crash, "isolated worker produced no result"});
            }
            continue;
        }
        merged.push_back(std::move(*resultsByCase[i]));
    }
    return merged;
}

std::vector<SubcaseResult> collectSelectiveRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    const ExpectationSet& crashList,
    FormatSampleStats* stats) {
    std::vector<SubcaseResult> results;
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    for (size_t i = 0; i < cases.size(); ++i) {
        if (!caseSelectedByShard(i, options)) {
            continue;
        }
        const CaseRun& c = cases[i];
        if (expectationMatches(crashList, c.query)) {
            results.push_back(runIsolatedChild(options, c.query));
        } else {
            results.push_back(aggregateCaseResult(c.query, runCase(c)));
        }
    }
    return results;
}

void writeCrashList(const std::string& path, const std::vector<SubcaseResult>& results) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open crash-list output file: " + path);
    }
    const std::vector<std::string> lines = crashListLines(results);
    for (const std::string& line : lines) {
        out << line << "\n";
    }
    std::cerr << "emitted " << lines.size() << " crashing cases to " << path << "\n";
}

size_t expectedResultCount(const CaseRun& c) {
    return c.subcases.empty() ? 1 : c.subcases.size();
}

void emitShardResults(const std::vector<SubcaseResult>& results) {
    for (const SubcaseResult& result : results) {
        std::cout << "RESULT\t" << statusName(result.status)
                  << "\t" << result.query
                  << "\t" << sanitizeResultMessage(result.message)
                  << "\n";
        std::cout.flush();
    }
}

#if !defined(_WIN32)
void runForkedWorkerCases(
    const std::vector<CaseRun>& cases,
    const std::vector<size_t>& positions,
    size_t next) {
    setStdoutBinaryForResultProtocol();
    try {
        for (size_t i = next; i < positions.size(); ++i) {
            const size_t position = positions[i];
            emitShardResults(runCase(cases[position]));
        }
    } catch (...) {
        _exit(1);
    }
    _exit(0);
}
#endif

std::vector<SubcaseResult> collectShardResultRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    FormatSampleStats* stats) {
    std::vector<SubcaseResult> results;
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    for (size_t i = 0; i < cases.size(); ++i) {
        if (!caseSelectedByShard(i, options)) {
            continue;
        }
        std::vector<SubcaseResult> caseResults = runCase(cases[i]);
        emitShardResults(caseResults);
        results.insert(results.end(), caseResults.begin(), caseResults.end());
    }
    return results;
}

std::vector<SubcaseResult> collectShardResultRunsFromPlan(const RunOptions& options) {
    std::vector<SubcaseResult> results;
    const std::vector<CaseRun> cases = loadCasePlan(options.casePlanPath);
    for (size_t i = 0; i < cases.size(); ++i) {
        if (!caseSelectedByShard(i, options)) {
            continue;
        }
        std::vector<SubcaseResult> caseResults = runCase(cases[i]);
        emitShardResults(caseResults);
        results.insert(results.end(), caseResults.begin(), caseResults.end());
    }
    return results;
}

struct WorkerState {
    int shard = 0;
#if defined(_WIN32)
    PROCESS_INFORMATION proc{};
    HANDLE readPipe = INVALID_HANDLE_VALUE;
    bool pipeDrained = false;
#else
    pid_t pid = -1;
    int fd = -1;
#endif
    std::string buffer;
    std::vector<size_t> positions;
    size_t next = 0;
};

std::string shardArg(int shard, int workers) {
    return std::to_string(shard) + "/" + std::to_string(workers);
}

[[maybe_unused]] std::vector<std::string> workerArgs(
    const RunOptions& options,
    const std::vector<std::string>& queryTexts,
    int shard,
    int workers,
    const std::vector<size_t>& positions,
    size_t next) {
    std::vector<std::string> args;
    args.push_back(options.executablePath);
    for (const std::string& arg : options.forwardedArgs) {
        args.push_back(arg);
    }
    for (const std::string& query : queryTexts) {
        args.push_back(query);
    }
    args.push_back("--shard");
    args.push_back(shardArg(shard, workers));
    args.push_back("--shard-results");
    if (next < positions.size() && positions[next] > 0) {
        args.push_back("--shard-from");
        args.push_back(std::to_string(positions[next]));
    }
    if (!options.casePlanPath.empty()) {
        args.push_back("--case-plan");
        args.push_back(options.casePlanPath);
    }
    return args;
}

WorkerState spawnWorker(
    const RunOptions& options,
    const std::vector<std::string>& queryTexts,
    const std::vector<CaseRun>& cases,
    int shard,
    int workers,
    const std::vector<size_t>& positions,
    size_t next) {
#if defined(_WIN32)
    (void)cases;
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE stdoutWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &inheritable, 0)) {
        throw std::runtime_error(windowsErrorMessage("CreatePipe", GetLastError()));
    }
    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        throw std::runtime_error(windowsErrorMessage("SetHandleInformation", error));
    }

    HANDLE nulHandle = CreateFileA(
        "NUL",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inheritable,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (nulHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        throw std::runtime_error(windowsErrorMessage("CreateFileA(NUL)", error));
    }

    std::string commandLine;
    for (const std::string& arg : workerArgs(options, queryTexts, shard, workers, positions, next)) {
        if (!commandLine.empty()) {
            commandLine.push_back(' ');
        }
        commandLine += quoteWindowsArg(arg);
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = nulHandle;

    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessA(
        options.executablePath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &startup,
        &process);
    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        CloseHandle(nulHandle);
        throw std::runtime_error(windowsErrorMessage("CreateProcessA", error));
    }

    CloseHandle(stdoutWrite);
    CloseHandle(nulHandle);
    WorkerState worker;
    worker.shard = shard;
    worker.proc = process;
    worker.readPipe = stdoutRead;
    worker.positions = positions;
    worker.next = next;
    return worker;
#else
    std::array<int, 2> stdoutPipe{};
    if (pipe(stdoutPipe.data()) != 0) {
        throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }

    if (pid == 0) {
        close(stdoutPipe[0]);
        if (dup2(stdoutPipe[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(stdoutPipe[1]);
        const int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            (void)dup2(devNull, STDERR_FILENO);
            close(devNull);
        }

        runForkedWorkerCases(cases, positions, next);
    }

    (void)queryTexts;
    (void)options;
    (void)workers;
    close(stdoutPipe[1]);
    const int flags = fcntl(stdoutPipe[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(stdoutPipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    return WorkerState{shard, pid, stdoutPipe[0], "", positions, next};
#endif
}

struct WorkerExit {
    bool clean = false;
    std::string message;
};

WorkerExit waitWorkerExit(WorkerState& worker) {
#if defined(_WIN32)
    WaitForSingleObject(worker.proc.hProcess, INFINITE);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(worker.proc.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        CloseHandle(worker.proc.hThread);
        CloseHandle(worker.proc.hProcess);
        throw std::runtime_error(windowsErrorMessage("GetExitCodeProcess", error));
    }
    CloseHandle(worker.proc.hThread);
    CloseHandle(worker.proc.hProcess);
    return WorkerExit{exitCode == 0, exitCode == 0 ? "" : windowsExitMessage(exitCode)};
#else
    int status = 0;
    while (waitpid(worker.pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        throw std::runtime_error("waitpid failed: " + std::string(std::strerror(errno)));
    }
    if (WIFSIGNALED(status)) {
        return WorkerExit{false, signalMessage(WTERMSIG(status))};
    }
    if (WIFEXITED(status)) {
        const int exitCode = WEXITSTATUS(status);
        return WorkerExit{exitCode == 0, exitCode == 0 ? "" : "child exited " + std::to_string(exitCode)};
    }
    return WorkerExit{false, "child did not exit normally"};
#endif
}

void advanceCompletedWorkerCases(
    WorkerState& worker,
    const std::vector<CaseRun>& cases,
    const std::vector<std::vector<SubcaseResult>>& resultsByCase) {
    while (worker.next < worker.positions.size()) {
        const size_t position = worker.positions[worker.next];
        if (resultsByCase[position].size() < expectedResultCount(cases[position])) {
            break;
        }
        ++worker.next;
    }
}

void recordWorkerLine(
    WorkerState& worker,
    const std::string& line,
    const std::vector<CaseRun>& cases,
    std::vector<std::vector<SubcaseResult>>& resultsByCase) {
    std::optional<SubcaseResult> parsed = parseResultLine(line);
    if (!parsed || worker.next >= worker.positions.size()) {
        return;
    }

    const size_t position = worker.positions[worker.next];
    const std::string& expectedQuery = cases[position].query;
    if (parsed->query != expectedQuery) {
        resultsByCase[position].clear();
        resultsByCase[position].push_back(SubcaseResult{
            expectedQuery,
            TestStatus::Crash,
            "shard worker reported unexpected query: " + parsed->query,
        });
        ++worker.next;
        return;
    }

    resultsByCase[position].push_back(*parsed);
    advanceCompletedWorkerCases(worker, cases, resultsByCase);
}

void drainWorkerLines(
    WorkerState& worker,
    const char* data,
    size_t size,
    const std::vector<CaseRun>& cases,
    std::vector<std::vector<SubcaseResult>>& resultsByCase) {
    worker.buffer.append(data, size);
    while (true) {
        const size_t newline = worker.buffer.find('\n');
        if (newline == std::string::npos) {
            break;
        }
        std::string line(dropTrailingCR(std::string_view(worker.buffer).substr(0, newline)));
        worker.buffer.erase(0, newline + 1);
        recordWorkerLine(worker, line, cases, resultsByCase);
    }
}

std::optional<WorkerState> finishWorker(
    WorkerState worker,
    const RunOptions& options,
    const std::vector<std::string>& queryTexts,
    const std::vector<CaseRun>& cases,
    std::vector<std::vector<SubcaseResult>>& resultsByCase) {
    if (!worker.buffer.empty()) {
        std::string line(dropTrailingCR(worker.buffer));
        recordWorkerLine(worker, line, cases, resultsByCase);
        worker.buffer.clear();
    }

    const WorkerExit exit = waitWorkerExit(worker);

    advanceCompletedWorkerCases(worker, cases, resultsByCase);
    if (exit.clean && worker.next >= worker.positions.size()) {
        return std::nullopt;
    }

    if (worker.next < worker.positions.size()) {
        const size_t crashedPosition = worker.positions[worker.next];
        resultsByCase[crashedPosition].clear();
        resultsByCase[crashedPosition].push_back(SubcaseResult{
            cases[crashedPosition].query,
            TestStatus::Crash,
            exit.clean ? "shard worker ended before reporting case" : "shard worker aborted: " + exit.message,
        });
        ++worker.next;
    }

    if (worker.next >= worker.positions.size()) {
        return std::nullopt;
    }
    return spawnWorker(options, queryTexts, cases, worker.shard, options.workers, worker.positions, worker.next);
}

std::vector<SubcaseResult> collectParallelRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    FormatSampleStats* stats) {
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    std::vector<std::vector<SubcaseResult>> resultsByCase(cases.size());
    std::vector<std::string> queryTexts = options.queries;
    RunOptions workerOptions = options;
#if defined(_WIN32)
    TemporaryCasePlan casePlan(cases);
    workerOptions.casePlanPath = casePlan.path();
#endif

    std::vector<WorkerState> workers;
    for (int shard = 0; shard < options.workers; ++shard) {
        std::vector<size_t> positions;
        for (size_t i = 0; i < cases.size(); ++i) {
            if (caseBelongsToShard(i, shard, options.workers)) {
                positions.push_back(i);
            }
        }
        if (!positions.empty()) {
            workers.push_back(spawnWorker(workerOptions, queryTexts, cases, shard, options.workers, positions, 0));
        }
    }

#if defined(_WIN32)
    std::vector<char> buffer(4096);
    while (!workers.empty()) {
        bool hadActivity = false;
        for (size_t i = 0; i < workers.size();) {
            WorkerState& worker = workers[i];
            if (!worker.pipeDrained) {
                DWORD bytesAvailable = 0;
                if (!PeekNamedPipe(worker.readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE) {
                        CloseHandle(worker.readPipe);
                        worker.readPipe = INVALID_HANDLE_VALUE;
                        worker.pipeDrained = true;
                        hadActivity = true;
                    } else {
                        CloseHandle(worker.readPipe);
                        throw std::runtime_error(windowsErrorMessage("PeekNamedPipe", error));
                    }
                } else if (bytesAvailable > 0) {
                    if (buffer.size() < bytesAvailable) {
                        buffer.resize(bytesAvailable);
                    }
                    DWORD bytesRead = 0;
                    if (!ReadFile(worker.readPipe, buffer.data(), bytesAvailable, &bytesRead, nullptr)) {
                        const DWORD error = GetLastError();
                        if (error == ERROR_BROKEN_PIPE) {
                            CloseHandle(worker.readPipe);
                            worker.readPipe = INVALID_HANDLE_VALUE;
                            worker.pipeDrained = true;
                            hadActivity = true;
                        } else {
                            CloseHandle(worker.readPipe);
                            throw std::runtime_error(windowsErrorMessage("ReadFile", error));
                        }
                    } else if (bytesRead == 0) {
                        CloseHandle(worker.readPipe);
                        worker.readPipe = INVALID_HANDLE_VALUE;
                        worker.pipeDrained = true;
                        hadActivity = true;
                    } else {
                        drainWorkerLines(worker, buffer.data(), bytesRead, cases, resultsByCase);
                        hadActivity = true;
                    }
                }
            }

            if (!worker.pipeDrained || WaitForSingleObject(worker.proc.hProcess, 0) != WAIT_OBJECT_0) {
                ++i;
                continue;
            }

            std::optional<WorkerState> replacement =
                finishWorker(std::move(workers[i]), workerOptions, queryTexts, cases, resultsByCase);
            workers.erase(workers.begin() + static_cast<std::ptrdiff_t>(i));
            if (replacement) {
                workers.push_back(std::move(*replacement));
            }
            hadActivity = true;
        }
        if (!hadActivity) {
            Sleep(2);
        }
    }
#else
    std::array<char, 4096> buffer{};
    while (!workers.empty()) {
        std::vector<pollfd> fds;
        fds.reserve(workers.size());
        for (const WorkerState& worker : workers) {
            fds.push_back(pollfd{worker.fd, POLLIN | POLLHUP, 0});
        }

        int ready = poll(fds.data(), fds.size(), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("poll failed: " + std::string(std::strerror(errno)));
        }

        for (size_t i = 0; i < workers.size();) {
            const short revents = fds[i].revents;
            if ((revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) == 0) {
                ++i;
                continue;
            }

            bool closed = false;
            while (true) {
                const ssize_t n = read(workers[i].fd, buffer.data(), buffer.size());
                if (n > 0) {
                    drainWorkerLines(workers[i], buffer.data(), static_cast<size_t>(n), cases, resultsByCase);
                    continue;
                }
                if (n == 0) {
                    close(workers[i].fd);
                    closed = true;
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                close(workers[i].fd);
                throw std::runtime_error("read failed: " + std::string(std::strerror(errno)));
            }

            if (!closed) {
                ++i;
                continue;
            }

            std::optional<WorkerState> replacement =
                finishWorker(std::move(workers[i]), workerOptions, queryTexts, cases, resultsByCase);
            workers.erase(workers.begin() + static_cast<std::ptrdiff_t>(i));
            if (replacement) {
                workers.push_back(std::move(*replacement));
            }
        }
    }
#endif

    std::vector<SubcaseResult> merged;
    for (size_t i = 0; i < cases.size(); ++i) {
        if (resultsByCase[i].empty()) {
            merged.push_back(SubcaseResult{cases[i].query, TestStatus::Crash, "shard worker produced no result"});
            continue;
        }
        merged.insert(merged.end(), resultsByCase[i].begin(), resultsByCase[i].end());
    }
    return merged;
}

struct RunSummary {
    size_t pass = 0;
    size_t skip = 0;
    size_t warn = 0;
    size_t fail = 0;
    size_t crash = 0;
    size_t xfail = 0;
    size_t xpass = 0;
    size_t flaky = 0;
};

void writeJsonOutput(
    const std::string& path,
    const std::vector<SubcaseResult>& results,
    const ExpectationSet& expectations,
    const RunSummary& summary) {
    if (path.empty()) {
        return;
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open JSONL output file: " + path);
    }
    for (const SubcaseResult& result : results) {
        out << resultJsonLine(result, expectationMatches(expectations, result.query)) << "\n";
    }
    out << "{\"summary\":true"
        << ",\"pass\":" << summary.pass
        << ",\"skip\":" << summary.skip
        << ",\"warn\":" << summary.warn
        << ",\"fail\":" << summary.fail
        << ",\"crash\":" << summary.crash
        << ",\"xfail\":" << summary.xfail
        << ",\"xpass\":" << summary.xpass
        << ",\"flaky\":" << summary.flaky
        << "}\n";
}

void mergeCurrentEffective(
    std::unordered_map<std::string, std::string>& current,
    std::vector<std::string>& order,
    const std::string& query,
    const std::string& effective) {
    auto found = current.find(query);
    if (found == current.end()) {
        current.emplace(query, effective);
        order.push_back(query);
        return;
    }
    if (!isBadEffectiveStatus(found->second) && isBadEffectiveStatus(effective)) {
        found->second = effective;
    }
}

struct BaselineReport {
    std::vector<std::string> regressed;
    std::vector<std::string> fixed;
    std::vector<std::string> added;
    std::vector<std::string> removed;
};

BaselineReport buildBaselineReport(
    const std::vector<SubcaseResult>& results,
    const ExpectationSet& expectations,
    const std::unordered_map<std::string, std::string>& baseline) {
    std::unordered_map<std::string, std::string> current;
    std::vector<std::string> currentOrder;
    for (const SubcaseResult& result : results) {
        const bool expected = expectationMatches(expectations, result.query);
        mergeCurrentEffective(current, currentOrder, result.query, effectiveStatusName(result, expected));
    }

    BaselineReport report;
    for (const std::string& query : currentOrder) {
        const BaselineDelta delta = classifyDelta(current.at(query), baseline, query);
        if (delta == BaselineDelta::Regressed) {
            report.regressed.push_back(query);
        } else if (delta == BaselineDelta::Fixed) {
            report.fixed.push_back(query);
        } else if (delta == BaselineDelta::New) {
            report.added.push_back(query);
        }
    }
    for (const auto& [query, effective] : baseline) {
        (void)effective;
        if (!current.contains(query) && classifyDelta("", baseline, query) == BaselineDelta::Removed) {
            report.removed.push_back(query);
        }
    }
    std::sort(report.removed.begin(), report.removed.end());
    return report;
}

void printBaselineList(const char* label, const std::vector<std::string>& queries) {
    if (queries.empty()) {
        return;
    }
    constexpr size_t kMaxPrinted = 20;
    std::cout << "baseline " << label << ":\n";
    const size_t printed = (std::min)(queries.size(), kMaxPrinted);
    for (size_t i = 0; i < printed; ++i) {
        std::cout << "  " << queries[i] << "\n";
    }
    if (printed < queries.size()) {
        std::cout << "  ... " << (queries.size() - printed) << " more\n";
    }
}

BaselineReport printBaselineReport(
    const std::vector<SubcaseResult>& results,
    const ExpectationSet& expectations,
    const std::string& baselinePath) {
    const std::unordered_map<std::string, std::string> baseline = loadBaseline(baselinePath);
    BaselineReport report = buildBaselineReport(results, expectations, baseline);
    std::cout << "baseline: Regressed=" << report.regressed.size()
              << " Fixed=" << report.fixed.size()
              << " New=" << report.added.size()
              << " Removed=" << report.removed.size()
              << "\n";
    printBaselineList("Regressed", report.regressed);
    printBaselineList("Fixed", report.fixed);
    printBaselineList("New", report.added);
    printBaselineList("Removed", report.removed);
    return report;
}

int printRunResults(
    const std::vector<SubcaseResult>& results,
    const ExpectationSet& expectations,
    const RunOptions& options) {
    RunSummary summary;
    for (const SubcaseResult& result : results) {
        const bool expected = expectationMatches(expectations, result.query);
        std::string status = statusName(result.status);
        bool unexpectedFailure = result.status == TestStatus::Fail || result.status == TestStatus::Crash;
        if (expected && (result.status == TestStatus::Fail || result.status == TestStatus::Crash)) {
            status = "xfail";
            unexpectedFailure = false;
            ++summary.xfail;
        } else if (expected && result.status == TestStatus::Pass) {
            status = "xpass";
            ++summary.xpass;
        }

        std::cout << status << " " << result.query;
        if (!result.message.empty()) {
            std::cout << " " << result.message;
        }
        std::cout << "\n";
        summary.pass += result.status == TestStatus::Pass ? 1 : 0;
        summary.skip += result.status == TestStatus::Skip ? 1 : 0;
        summary.warn += result.status == TestStatus::Warn ? 1 : 0;
        summary.fail += unexpectedFailure && result.status == TestStatus::Fail ? 1 : 0;
        summary.crash += unexpectedFailure && result.status == TestStatus::Crash ? 1 : 0;
        summary.flaky += isFlakyResult(result) ? 1 : 0;
    }
    std::cout << "summary: pass=" << summary.pass << " skip=" << summary.skip << " warn=" << summary.warn
              << " fail=" << summary.fail << " crash=" << summary.crash
              << " xfail=" << summary.xfail << " xpass=" << summary.xpass
              << " flaky=" << summary.flaky << "\n";

    writeJsonOutput(options.outputPath, results, expectations, summary);
    if (!options.baselinePath.empty()) {
        const BaselineReport report = printBaselineReport(results, expectations, options.baselinePath);
        return report.regressed.empty() ? 0 : 1;
    }
    return summary.fail == 0 && summary.crash == 0 ? 0 : 1;
}

} // namespace

bool expectationMatches(const ExpectationSet& expectations, const std::string& query) {
    if (expectations.exact.contains(query)) {
        return true;
    }
    for (const std::string& prefix : expectations.prefixes) {
        if (query.size() >= prefix.size() && query.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

BaselineDelta classifyDelta(
    const std::string& effectiveNow,
    const std::unordered_map<std::string, std::string>& baseline,
    const std::string& query) {
    const auto found = baseline.find(query);
    if (found == baseline.end()) {
        return effectiveNow.empty() ? BaselineDelta::Unchanged : BaselineDelta::New;
    }
    if (effectiveNow.empty()) {
        return BaselineDelta::Removed;
    }

    const bool nowBad = isBadEffectiveStatus(effectiveNow);
    const bool baselineBad = isBadEffectiveStatus(found->second);
    if (nowBad && !baselineBad) {
        return BaselineDelta::Regressed;
    }
    if (!nowBad && baselineBad) {
        return BaselineDelta::Fixed;
    }
    return BaselineDelta::Unchanged;
}

int resolveWorkers(const RunOptions& options) {
    if (options.workers == 1) {
        return 1;
    }
    if (options.workers <= 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        const int detected = hardware == 0 ? 1 : static_cast<int>(hardware);
        return (std::max)(1, (std::min)(detected, kDefaultMaxWorkers));
    }
    return options.workers;
}

bool caseBelongsToShard(size_t index, int shardIndex, int shardCount) {
    if (shardCount <= 0) {
        return true;
    }
    return shardIndex >= 0 && shardIndex < shardCount && index % static_cast<size_t>(shardCount) == static_cast<size_t>(shardIndex);
}

std::optional<SubcaseResult> parseResultLine(const std::string& line) {
    std::string_view resultLine = dropTrailingCR(line);
    if (!resultLine.starts_with("RESULT\t")) {
        return std::nullopt;
    }
    const size_t statusStart = std::string_view("RESULT\t").size();
    const size_t queryStart = resultLine.find('\t', statusStart);
    if (queryStart == std::string::npos) {
        return std::nullopt;
    }
    const size_t messageStart = resultLine.find('\t', queryStart + 1);
    const std::string status = std::string(resultLine.substr(statusStart, queryStart - statusStart));
    const std::string query = messageStart == std::string::npos
        ? std::string(resultLine.substr(queryStart + 1))
        : std::string(resultLine.substr(queryStart + 1, messageStart - queryStart - 1));
    const std::string message =
        messageStart == std::string::npos ? "" : sanitizeResultMessage(std::string(resultLine.substr(messageStart + 1)));
    const std::optional<TestStatus> parsedStatus = statusFromName(status);
    if (!parsedStatus) {
        return SubcaseResult{query, TestStatus::Crash, "unknown RESULT status: " + status};
    }
    return SubcaseResult{query, *parsedStatus, message};
}

std::vector<std::string> crashListLines(const std::vector<SubcaseResult>& results) {
    std::set<std::string> lines;
    for (const SubcaseResult& result : results) {
        if (result.status == TestStatus::Crash) {
            lines.insert(result.query);
        }
    }
    return std::vector<std::string>(lines.begin(), lines.end());
}

int runQueries(const RunOptions& options) {
    if (options.retries > 0 && !options.isolate) {
        std::cerr << "--retries requires --isolate; ignoring --retries\n";
    }

    if (!options.runCaseQuery.empty()) {
        setStdoutBinaryForResultProtocol();
        try {
            return runSingleCase(options);
        } catch (const std::exception& e) {
            std::cout << "RESULT\tfail\t" << e.what() << "\n";
            return 0;
        }
    }

    const int workerCount = resolveWorkers(options);
    RunOptions runOptions = options;
    runOptions.workers = workerCount;
    if (!runOptions.isolate) {
        runOptions.retries = 0;
    }

    if (options.workersSpecified && !options.crashListPath.empty()) {
        std::cerr << "--workers is incompatible with --crash-list\n";
        return 1;
    }

    std::vector<Query> queries;
    try {
        for (const std::string& text : options.queries) {
            queries.push_back(parseQuery(text));
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    if (options.list || options.listCases) {
        if (options.list) {
            for (const SpecFile& file : Registry::instance().files()) {
                for (const TestSpec& test : file.tests) {
                    bool selected = false;
                    for (const Query& query : queries) {
                        selected = selected || queryMatchesTest(query, file.path, test.name);
                    }
                    if (selected) {
                        printTestListLine(file.path, test, options.sampleFormats);
                    }
                }
            }
        }
        if (options.listCases) {
            const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, nullptr);
            for (size_t i = 0; i < cases.size(); ++i) {
                if (caseSelectedByShard(i, options)) {
                    std::cout << cases[i].query << "\n";
                }
            }
        }
        return 0;
    }

    ExpectationSet expectations;
    try {
        expectations = loadExpectations(options.expectationsPath);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::vector<SubcaseResult> results;
    FormatSampleStats sampleStats;
    if (options.sampleFormats && !options.shardResults) {
        std::cerr << "format-sample: representative-formats mode ON - non-representative format cases are SKIPPED; this is NOT full conformance coverage.\n";
    }
    try {
        if (options.shardResults) {
            setStdoutBinaryForResultProtocol();
            if (!runOptions.casePlanPath.empty()) {
                (void)collectShardResultRunsFromPlan(runOptions);
            } else {
                (void)collectShardResultRuns(runOptions, queries, &sampleStats);
            }
            return 0;
        } else if (!options.crashListPath.empty()) {
            ExpectationSet crashList = loadExpectations(options.crashListPath);
            results = collectSelectiveRuns(runOptions, queries, crashList, &sampleStats);
        } else if (options.isolate) {
            if (workerCount >= 2) {
                results = collectIsolatedParallelRuns(runOptions, queries, &sampleStats);
            } else {
                results = collectIsolatedRuns(runOptions, queries, &sampleStats);
            }
            if (!options.emitCrashListPath.empty()) {
                writeCrashList(options.emitCrashListPath, results);
            }
        } else if (workerCount >= 2) {
            results = collectParallelRuns(runOptions, queries, &sampleStats);
        } else {
            results = collectRuns(queries, runOptions, &sampleStats);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    if (options.sampleFormats && !options.shardResults && sampleStats.runsDropped > 0) {
        std::cerr << "format-sample: thinned " << sampleStats.testsSampled
                  << " tests, dropped " << sampleStats.runsDropped
                  << " of " << (sampleStats.runsKept + sampleStats.runsDropped)
                  << " format-swept subcases.\n";
    }
    try {
        return printRunResults(results, expectations, options);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

int writeListingJson(const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        std::cerr << "failed to open " << path << "\n";
        return 1;
    }
    out << "{\n  \"files\": [\n";
    const auto& files = Registry::instance().files();
    for (size_t i = 0; i < files.size(); ++i) {
        const SpecFile& file = files[i];
        out << "    {\"path\":\"" << file.path << "\",\"tests\":[";
        for (size_t j = 0; j < file.tests.size(); ++j) {
            const TestSpec& test = file.tests[j];
            const auto cases = expandedCases(test);
            size_t subcases = 0;
            for (const auto& c : cases) {
                subcases += c.subcases.size();
            }
            out << "{\"name\":\"" << test.name << "\",\"cases\":" << cases.size()
                << ",\"subcases\":" << subcases
                << ",\"unimplemented\":" << (test.unimplemented ? "true" : "false") << "}";
            if (j + 1 != file.tests.size()) {
                out << ",";
            }
        }
        out << "]}";
        if (i + 1 != files.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
    return 0;
}

std::vector<SubcaseResult> runSyntheticFailureForSelfTest() {
    struct FailingFixture : Fixture {};
    TestSpec test;
    test.name = "synthetic";
    test.fixtureFactory = [] { return std::make_unique<FailingFixture>(); };
    test.fn = [](Fixture& fixture) { fixture.fail("synthetic failure"); };
    return {runOne("webgpu:synthetic:file:test:*", test, {})};
}

} // namespace cts
