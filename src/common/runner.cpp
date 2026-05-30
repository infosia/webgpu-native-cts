#include "cts/test.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "common/query.h"

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
    }
    return "fail";
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

} // namespace

int runQueries(const RunOptions& options) {
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

    std::vector<SubcaseResult> results = collectRuns(queries);
    size_t pass = 0;
    size_t skip = 0;
    size_t warn = 0;
    size_t fail = 0;
    for (const SubcaseResult& result : results) {
        std::cout << statusName(result.status) << " " << result.query;
        if (!result.message.empty()) {
            std::cout << " " << result.message;
        }
        std::cout << "\n";
        pass += result.status == TestStatus::Pass ? 1 : 0;
        skip += result.status == TestStatus::Skip ? 1 : 0;
        warn += result.status == TestStatus::Warn ? 1 : 0;
        fail += result.status == TestStatus::Fail ? 1 : 0;
    }
    std::cout << "summary: pass=" << pass << " skip=" << skip << " warn=" << warn << " fail=" << fail << "\n";
    return fail == 0 ? 0 : 1;
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
