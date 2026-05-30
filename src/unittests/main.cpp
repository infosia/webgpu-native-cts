#include <iostream>
#include <string>

#include "common/query.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        cts::ParamsBuilder builder;
        auto cases = builder.combine("case", {1, 2}).beginSubcases().combine("subcase", {true, false}).expand();
        require(cases.size() == 2, "case expansion count");
        require(cases[0].subcases.size() == 2, "subcase expansion count");

        auto filtered = cts::ParamsBuilder()
            .combine("x", {1, 2, 3})
            .filter([](const cts::ParamRecord& params) {
                return cts::valueAs<int>(*cts::findParam(params, "x")) >= 2;
            })
            .expand();
        require(filtered.size() == 2, "case filter count");

        auto combined = cts::ParamsBuilder()
            .combine("outer", {7, 8})
            .combineWithParams({
                cts::ParamRecord{{"a", 1}, {"b", true}},
                cts::ParamRecord{{"a", 2}, {"b", false}},
            })
            .beginSubcases()
            .combine("sub", {1, 2, 3})
            .filter([](const cts::ParamRecord& params) {
                return cts::valueAs<int>(*cts::findParam(params, "sub")) != 2;
            })
            .expand();
        require(combined.size() == 4, "combineWithParams case count");
        require(combined[0].subcases.size() == 2, "subcase filter count");
        require(cts::findParam(combined[0].params, "a") != nullptr, "combineWithParams record key");

        require(cts::stringifyValue(cts::Value(1)) == "1", "int stringify");
        require(cts::stringifyValue(cts::Value(true)) == "true", "bool stringify");
        require(cts::stringifyValue(cts::Value(0.5)) == "0.5", "double stringify");
        require(cts::stringifyValue(cts::Value::undef()) == "_undef_", "undefined stringify");
        require(cts::stringifyValue(cts::Value("abc")) == "\"abc\"", "string stringify");
        require(cts::valueAs<double>(cts::Value(0.5)) == 0.5, "double valueAs double");
        require(cts::valueAs<double>(cts::Value(1)) == 1.0, "double valueAs int");
        require(!cts::kUncompressedTextureFormats.empty(), "uncompressed texture formats non-empty");
        require(cts::kUncompressedTextureFormats.size() == 49, "uncompressed texture format count");
        const cts::TextureBlockInfo rgba8 = cts::getBlockInfoForTextureFormat(WGPUTextureFormat_RGBA8Unorm);
        require(rgba8.blockWidth == 1, "rgba8unorm block width");
        require(rgba8.blockHeight == 1, "rgba8unorm block height");
        require(rgba8.bytesPerBlock == 4, "rgba8unorm bytes per block");

        cts::Fixture fixture;
        fixture.setParams({{"x", cts::Value::undef()}, {"y", 1}});
        require(fixture.paramIsUndefined("x"), "paramIsUndefined true");
        require(!fixture.paramIsUndefined("y"), "paramIsUndefined false");
        require(!fixture.paramIsUndefined("missing"), "paramIsUndefined missing");

        cts::Query query = cts::parseQuery("webgpu:api,validation,buffer,create:limit:sizeAddition=0");
        cts::ParamRecord params{{"sizeAddition", cts::Value(0)}};
        require(cts::queryMatchesCase(query, "api,validation,buffer,create", "limit", params), "single-case query");
        require(cts::caseQuery("api,validation,buffer,create", "limit", params) ==
                    "webgpu:api,validation,buffer,create:limit:sizeAddition=0",
                "case query stringify");

        auto failures = cts::runSyntheticFailureForSelfTest();
        require(failures.size() == 1, "synthetic failure result count");
        require(failures[0].status == cts::TestStatus::Fail, "synthetic failure status");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
