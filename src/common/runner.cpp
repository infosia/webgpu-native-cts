#include "cts/test.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string_view>
#include <sstream>

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

std::string sanitizeResultMessage(std::string message) {
    for (char& ch : message) {
        if (ch == '\t' || ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return message;
}

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
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

struct CaseRun {
    std::string file;
    const TestSpec* test = nullptr;
    ParamRecord params;
    std::vector<ParamRecord> subcases;
    std::string query;
};

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

std::optional<SubcaseResult> parseIsolatedResultLine(const std::string& query, const std::string& output) {
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with("RESULT\t")) {
            continue;
        }
        const size_t statusStart = std::string_view("RESULT\t").size();
        const size_t messageStart = line.find('\t', statusStart);
        const std::string status =
            messageStart == std::string::npos ? line.substr(statusStart) : line.substr(statusStart, messageStart - statusStart);
        const std::string message = messageStart == std::string::npos ? "" : line.substr(messageStart + 1);
        const std::optional<TestStatus> parsedStatus = statusFromName(status);
        if (parsedStatus) {
            return SubcaseResult{query, *parsedStatus, message};
        }
        return SubcaseResult{query, TestStatus::Crash, "unknown RESULT status: " + status};
    }
    return std::nullopt;
}

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

SubcaseResult runIsolatedChild(const RunOptions& options, const std::string& query) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE stdoutWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &inheritable, 0)) {
        return SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("CreatePipe", GetLastError())};
    }
    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD error = GetLastError();
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("SetHandleInformation", error)};
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
        return SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("CreateFileA(NUL)", error)};
    }

    std::vector<std::string> args;
    args.push_back(options.executablePath);
    for (const std::string& arg : options.forwardedArgs) {
        args.push_back(arg);
    }
    args.push_back("--run-case");
    args.push_back(query);

    std::string commandLine;
    for (const std::string& arg : args) {
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
        return SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("CreateProcessA", error)};
    }

    CloseHandle(stdoutWrite);
    CloseHandle(nulHandle);

    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        DWORD bytesRead = 0;
        if (ReadFile(stdoutRead, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            if (bytesRead == 0) {
                break;
            }
            output.append(buffer.data(), bytesRead);
            continue;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE) {
            break;
        }
        CloseHandle(stdoutRead);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("ReadFile", error)};
    }
    CloseHandle(stdoutRead);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return SubcaseResult{query, TestStatus::Crash, windowsErrorMessage("GetExitCodeProcess", error)};
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (exitCode != 0) {
        return SubcaseResult{query, TestStatus::Crash, windowsExitMessage(exitCode)};
    }

    std::optional<SubcaseResult> parsed = parseIsolatedResultLine(query, output);
    if (!parsed) {
        return SubcaseResult{query, TestStatus::Crash, "no RESULT line"};
    }
    return *parsed;
#else
    std::array<int, 2> stdoutPipe{};
    if (pipe(stdoutPipe.data()) != 0) {
        return SubcaseResult{query, TestStatus::Crash, "pipe failed: " + std::string(std::strerror(errno))};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return SubcaseResult{query, TestStatus::Crash, "fork failed: " + std::string(std::strerror(errno))};
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

        std::vector<std::string> args;
        args.push_back(options.executablePath);
        for (const std::string& arg : options.forwardedArgs) {
            args.push_back(arg);
        }
        args.push_back("--run-case");
        args.push_back(query);

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
    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t n = read(stdoutPipe[0], buffer.data(), buffer.size());
        if (n > 0) {
            output.append(buffer.data(), static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close(stdoutPipe[0]);
        return SubcaseResult{query, TestStatus::Crash, "read failed: " + std::string(std::strerror(errno))};
    }
    close(stdoutPipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return SubcaseResult{query, TestStatus::Crash, "waitpid failed: " + std::string(std::strerror(errno))};
    }

    if (WIFSIGNALED(status)) {
        return SubcaseResult{query, TestStatus::Crash, signalMessage(WTERMSIG(status))};
    }
    if (!WIFEXITED(status)) {
        return SubcaseResult{query, TestStatus::Crash, "child did not exit normally"};
    }
    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0) {
        return SubcaseResult{query, TestStatus::Crash, "child exited " + std::to_string(exitCode)};
    }

    std::optional<SubcaseResult> parsed = parseIsolatedResultLine(query, output);
    if (!parsed) {
        return SubcaseResult{query, TestStatus::Crash, "no RESULT line"};
    }
    return *parsed;
#endif
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
        results.push_back(runIsolatedChild(options, cases[i].query));
    }
    return results;
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

std::vector<std::string> workerArgs(
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
    return args;
}

WorkerState spawnWorker(
    const RunOptions& options,
    const std::vector<std::string>& queryTexts,
    int shard,
    int workers,
    const std::vector<size_t>& positions,
    size_t next) {
#if defined(_WIN32)
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

        std::vector<std::string> args = workerArgs(options, queryTexts, shard, workers, positions, next);

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
        std::string line = worker.buffer.substr(0, newline);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
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
        recordWorkerLine(worker, worker.buffer, cases, resultsByCase);
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
    return spawnWorker(options, queryTexts, worker.shard, options.workers, worker.positions, worker.next);
}

std::vector<SubcaseResult> collectParallelRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    FormatSampleStats* stats) {
    const std::vector<CaseRun> cases = collectCases(queries, options.sampleFormats, stats);
    std::vector<std::vector<SubcaseResult>> resultsByCase(cases.size());
    std::vector<std::string> queryTexts = options.queries;

    std::vector<WorkerState> workers;
    for (int shard = 0; shard < options.workers; ++shard) {
        std::vector<size_t> positions;
        for (size_t i = 0; i < cases.size(); ++i) {
            if (caseBelongsToShard(i, shard, options.workers)) {
                positions.push_back(i);
            }
        }
        if (!positions.empty()) {
            workers.push_back(spawnWorker(options, queryTexts, shard, options.workers, positions, 0));
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
                finishWorker(std::move(workers[i]), options, queryTexts, cases, resultsByCase);
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
                finishWorker(std::move(workers[i]), options, queryTexts, cases, resultsByCase);
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

int printRunResults(const std::vector<SubcaseResult>& results, const ExpectationSet& expectations) {
    size_t pass = 0;
    size_t skip = 0;
    size_t warn = 0;
    size_t fail = 0;
    size_t crash = 0;
    size_t xfail = 0;
    size_t xpass = 0;
    for (const SubcaseResult& result : results) {
        const bool expected = expectationMatches(expectations, result.query);
        std::string status = statusName(result.status);
        bool unexpectedFailure = result.status == TestStatus::Fail || result.status == TestStatus::Crash;
        if (expected && (result.status == TestStatus::Fail || result.status == TestStatus::Crash)) {
            status = "xfail";
            unexpectedFailure = false;
            ++xfail;
        } else if (expected && result.status == TestStatus::Pass) {
            status = "xpass";
            ++xpass;
        }

        std::cout << status << " " << result.query;
        if (!result.message.empty()) {
            std::cout << " " << result.message;
        }
        std::cout << "\n";
        pass += result.status == TestStatus::Pass ? 1 : 0;
        skip += result.status == TestStatus::Skip ? 1 : 0;
        warn += result.status == TestStatus::Warn ? 1 : 0;
        fail += unexpectedFailure && result.status == TestStatus::Fail ? 1 : 0;
        crash += unexpectedFailure && result.status == TestStatus::Crash ? 1 : 0;
    }
    std::cout << "summary: pass=" << pass << " skip=" << skip << " warn=" << warn
              << " fail=" << fail << " crash=" << crash
              << " xfail=" << xfail << " xpass=" << xpass << "\n";
    return fail == 0 && crash == 0 ? 0 : 1;
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

bool caseBelongsToShard(size_t index, int shardIndex, int shardCount) {
    if (shardCount <= 0) {
        return true;
    }
    return shardIndex >= 0 && shardIndex < shardCount && index % static_cast<size_t>(shardCount) == static_cast<size_t>(shardIndex);
}

std::optional<SubcaseResult> parseResultLine(const std::string& line) {
    if (!line.starts_with("RESULT\t")) {
        return std::nullopt;
    }
    const size_t statusStart = std::string_view("RESULT\t").size();
    const size_t queryStart = line.find('\t', statusStart);
    if (queryStart == std::string::npos) {
        return std::nullopt;
    }
    const size_t messageStart = line.find('\t', queryStart + 1);
    const std::string status = line.substr(statusStart, queryStart - statusStart);
    const std::string query = messageStart == std::string::npos
        ? line.substr(queryStart + 1)
        : line.substr(queryStart + 1, messageStart - queryStart - 1);
    const std::string message = messageStart == std::string::npos ? "" : sanitizeResultMessage(line.substr(messageStart + 1));
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
    if (!options.runCaseQuery.empty()) {
        try {
            return runSingleCase(options);
        } catch (const std::exception& e) {
            std::cout << "RESULT\tfail\t" << e.what() << "\n";
            return 0;
        }
    }

    std::vector<Query> queries;
    for (const std::string& text : options.queries) {
        queries.push_back(parseQuery(text));
    }

    if (options.workers >= 2) {
        if (options.isolate || !options.crashListPath.empty()) {
            std::cerr << "--workers is incompatible with --isolate and --crash-list\n";
            return 1;
        }
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
    if (options.workers >= 2) {
        try {
            results = collectParallelRuns(options, queries, &sampleStats);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
    } else if (options.shardResults) {
        (void)collectShardResultRuns(options, queries, &sampleStats);
        return 0;
    } else if (!options.crashListPath.empty()) {
        ExpectationSet crashList;
        try {
            crashList = loadExpectations(options.crashListPath);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
        results = collectSelectiveRuns(options, queries, crashList, &sampleStats);
    } else if (options.isolate) {
        results = collectIsolatedRuns(options, queries, &sampleStats);
        if (!options.emitCrashListPath.empty()) {
            try {
                writeCrashList(options.emitCrashListPath, results);
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                return 1;
            }
        }
    } else {
        results = collectRuns(queries, options, &sampleStats);
    }
    if (options.sampleFormats && !options.shardResults && sampleStats.runsDropped > 0) {
        std::cerr << "format-sample: thinned " << sampleStats.testsSampled
                  << " tests, dropped " << sampleStats.runsDropped
                  << " of " << (sampleStats.runsKept + sampleStats.runsDropped)
                  << " format-swept subcases.\n";
    }
    return printRunResults(results, expectations);
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
