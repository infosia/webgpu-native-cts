#include "cts/test.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string_view>
#include <sstream>

#include "common/query.h"

#if !defined(_WIN32)
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
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

void printTestListLine(const std::string& file, const TestSpec& test) {
    const auto cases = expandedCases(test);
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

std::vector<SubcaseResult> collectRuns(const std::vector<Query>& queries) {
    std::vector<SubcaseResult> results;
    for (const SpecFile& file : Registry::instance().files()) {
        for (const TestSpec& test : file.tests) {
            const auto cases = expandedCases(test);
            for (const auto& c : cases) {
                bool selected = false;
                for (const Query& q : queries) {
                    selected = selected || queryMatchesCase(q, file.path, test.name, c.params);
                }
                if (!selected) {
                    continue;
                }

                if (c.subcases.empty()) {
                    results.push_back(runOne(caseQuery(file.path, test.name, c.params), test, c.params));
                } else {
                    for (const ParamRecord& subcase : c.subcases) {
                        ParamRecord merged = c.params;
                        merged.insert(merged.end(), subcase.begin(), subcase.end());
                        results.push_back(runOne(caseQuery(file.path, test.name, c.params), test, merged));
                    }
                }
            }
        }
    }
    return results;
}

std::vector<CaseRun> collectCases(const std::vector<Query>& queries) {
    std::vector<CaseRun> runs;
    for (const SpecFile& file : Registry::instance().files()) {
        for (const TestSpec& test : file.tests) {
            const auto cases = expandedCases(test);
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

int runSingleCase(const std::string& runCaseQuery) {
    Query query = parseQuery(runCaseQuery);
    std::vector<CaseRun> cases = collectCases({query});
    if (cases.size() != 1 || cases[0].query != runCaseQuery) {
        std::cout << "RESULT\tfail\texpected exactly one full case query\n";
        return 0;
    }

    const SubcaseResult aggregate = aggregateCaseResult(cases[0].query, runCase(cases[0]));
    std::cout << "RESULT\t" << statusName(aggregate.status) << "\t" << aggregate.message << "\n";
    return 0;
}

std::optional<SubcaseResult> parseResultLine(const std::string& query, const std::string& output) {
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
        if (status == "pass") {
            return SubcaseResult{query, TestStatus::Pass, message};
        }
        if (status == "skip") {
            return SubcaseResult{query, TestStatus::Skip, message};
        }
        if (status == "warn") {
            return SubcaseResult{query, TestStatus::Warn, message};
        }
        if (status == "fail") {
            return SubcaseResult{query, TestStatus::Fail, message};
        }
        if (status == "crash") {
            return SubcaseResult{query, TestStatus::Crash, message};
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

    std::optional<SubcaseResult> parsed = parseResultLine(query, output);
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

    std::optional<SubcaseResult> parsed = parseResultLine(query, output);
    if (!parsed) {
        return SubcaseResult{query, TestStatus::Crash, "no RESULT line"};
    }
    return *parsed;
#endif
}

std::vector<SubcaseResult> collectIsolatedRuns(const RunOptions& options, const std::vector<Query>& queries) {
    std::vector<SubcaseResult> results;
    for (const CaseRun& c : collectCases(queries)) {
        results.push_back(runIsolatedChild(options, c.query));
    }
    return results;
}

std::vector<SubcaseResult> collectSelectiveRuns(
    const RunOptions& options,
    const std::vector<Query>& queries,
    const ExpectationSet& crashList) {
    std::vector<SubcaseResult> results;
    for (const CaseRun& c : collectCases(queries)) {
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
            return runSingleCase(options.runCaseQuery);
        } catch (const std::exception& e) {
            std::cout << "RESULT\tfail\t" << e.what() << "\n";
            return 0;
        }
    }

    std::vector<Query> queries;
    for (const std::string& text : options.queries) {
        queries.push_back(parseQuery(text));
    }

    if (options.list || options.listCases) {
        for (const SpecFile& file : Registry::instance().files()) {
            for (const TestSpec& test : file.tests) {
                bool selected = false;
                for (const Query& query : queries) {
                    selected = selected || queryMatchesTest(query, file.path, test.name);
                }
                if (!selected) {
                    continue;
                }
                if (options.list) {
                    printTestListLine(file.path, test);
                }
                if (options.listCases) {
                    for (const auto& c : expandedCases(test)) {
                        for (const Query& query : queries) {
                            if (queryMatchesCase(query, file.path, test.name, c.params)) {
                                std::cout << caseQuery(file.path, test.name, c.params) << "\n";
                                break;
                            }
                        }
                    }
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
    if (!options.crashListPath.empty()) {
        ExpectationSet crashList;
        try {
            crashList = loadExpectations(options.crashListPath);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
        results = collectSelectiveRuns(options, queries, crashList);
    } else if (options.isolate) {
        results = collectIsolatedRuns(options, queries);
        if (!options.emitCrashListPath.empty()) {
            try {
                writeCrashList(options.emitCrashListPath, results);
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                return 1;
            }
        }
    } else {
        results = collectRuns(queries);
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
