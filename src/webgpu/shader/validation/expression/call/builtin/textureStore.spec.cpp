// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureStore.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureStore() builtin: coords / array_index / value
// parameter types and incompatible storage texture types. The
// kValidTextureStoreParameterTypes table (coords arg type pair + array-index
// flag per storage texture type) and kTextureColorTypeToType are ported locally.
// Storage-format-gated cases skip via skipIfTextureFormatNotSupported /
// skipIfTextureFormatNotUsableWithStorageAccessMode('write-only').

#include <array>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/expression/call/builtin/shader_builtin_utils.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,textureStore",
    "Validation tests for the textureStore() builtin.");

// kValidTextureStoreParameterTypes (object key order preserved).
struct StoreArgs {
    std::string textureType;
    bt::Type coordsArg0;
    bt::Type coordsArg1;
    bool hasArrayIndexArg;
};
const std::vector<StoreArgs>& kValidTextureStoreParameterTypes() {
    using bt::ScalarKind;
    static const std::vector<StoreArgs> v = {
        {"texture_storage_1d", bt::scalar(ScalarKind::I32), bt::scalar(ScalarKind::U32), false},
        {"texture_storage_2d", bt::vec(2, ScalarKind::I32), bt::vec(2, ScalarKind::U32), false},
        {"texture_storage_2d_array", bt::vec(2, ScalarKind::I32), bt::vec(2, ScalarKind::U32), true},
        {"texture_storage_3d", bt::vec(3, ScalarKind::I32), bt::vec(3, ScalarKind::U32), false},
    };
    return v;
}
const StoreArgs* storeArgsByName(const std::string& name) {
    for (const StoreArgs& s : kValidTextureStoreParameterTypes()) {
        if (s.textureType == name) {
            return &s;
        }
    }
    return nullptr;
}
std::vector<Value> storeTextureTypeKeys() {
    std::vector<Value> out;
    for (const StoreArgs& s : kValidTextureStoreParameterTypes()) {
        out.emplace_back(s.textureType);
    }
    return out;
}

// Replace the first occurrence of ", read" with ", write".
std::string replaceReadWithWrite(std::string s) {
    const std::string from = ", read";
    const std::string to = ", write";
    const std::string::size_type pos = s.find(from);
    if (pos != std::string::npos) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

CTS_TEST(g, "coords_argument")
    .desc("Validates that only incorrect coords arguments are rejected by textureStore")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", storeTextureTypeKeys())
            .combine("coordType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-1), Value(0), Value(1)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "coordType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string coordType = t.param<std::string>("coordType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type coordArgType = bt::typeByName(coordType);
        const StoreArgs* s = storeArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(coordArgType, static_cast<long long>(value));
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string format = "rgba8unorm";
        const std::string valueWGSL = "vec4f(0)";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
            ",write>;\n@fragment fn fs() -> @location(0) vec4f {\n  textureStore(t, " + coordWGSL +
            arrayWGSL + ", " + valueWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, s->coordsArg0) ||
                                   bt::isConvertible(coordArgType, s->coordsArg1);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument")
    .desc("Validates that only incorrect array_index arguments are rejected by textureStore")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", storeTextureTypeKeys())
            .filter([](const ParamRecord& p) {
                const StoreArgs* s =
                    storeArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                return s != nullptr && s->hasArrayIndexArg;
            })
            .combine("arrayIndexType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-9), Value(-8), Value(0), Value(7), Value(8)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "arrayIndexType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string arrayIndexType = t.param<std::string>("arrayIndexType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type arrayIndexArgType = bt::typeByName(arrayIndexType);
        const StoreArgs* s = storeArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArg0, 0);
        const std::string arrayWGSL =
            bt::createWgsl(arrayIndexArgType, static_cast<long long>(value));
        const std::string format = "rgba8unorm";
        const std::string valueWGSL = "vec4f(0)";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
            ", write>;\n@fragment fn fs() -> @location(0) vec4f {\n  textureStore(t, " + coordWGSL +
            ", " + arrayWGSL + ", " + valueWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::I32)) ||
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::U32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "value_argument")
    .desc("Validates that only incorrect value arguments are rejected by textureStore")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", storeTextureTypeKeys())
            .combine("valueType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("format", b::kPossibleStorageTextureFormats())
            .combine("value", {Value(0), Value(1), Value(2)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string valueType = t.param<std::string>("valueType");
        const std::string format = t.param<std::string>("format");
        const int64_t value = t.param<int64_t>("value");
        b::skipIfTextureFormatNotSupported(t, format);
        b::skipIfTextureFormatNotUsableWithStorageAccessMode(t, "write-only", format);

        const bt::Type valueArgType = bt::typeByName(valueType);
        const StoreArgs* s = storeArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArg0, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string valueWGSL = bt::createWgsl(valueArgType, static_cast<long long>(value));

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
            ", write>;\n@fragment fn fs() -> @location(0) vec4f {\n  textureStore(t, " + coordWGSL +
            arrayWGSL + ", " + valueWGSL + ");\n  return vec4f(0);\n}\n";
        const b::StorageFormatInfo* fi = b::storageFormatByName(format);
        const bt::Type requiredValueType = b::textureColorTypeToType(fi->colorType);
        const bool expectSuccess = bt::isConvertible(valueArgType, requiredValueType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type,storage")
    .desc("Validates that incompatible texture types don't work with textureStore")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .beginSubcases()
            .combine("textureType", storeTextureTypeKeys())
            .combine("format", b::kPossibleStorageTextureFormats());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        const std::string format = t.param<std::string>("format");
        const StoreArgs* s = storeArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArg0, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const b::StorageFormatInfo* fi = b::storageFormatByName(format);
        const bt::Type valueType = b::textureColorTypeToType(fi->colorType);
        const std::string valueWGSL = bt::createWgsl(valueType, 0);

        const std::string code =
            "\n@group(0) @binding(1) var t: " + replaceReadWithWrite(testTextureType) +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  textureStore(t, " + coordWGSL +
            arrayWGSL + ", " + valueWGSL + ");\n  return vec4f(0);\n}\n";

        const b::BaseAndSample bas = b::getSampleAndBaseTextureTypeForTextureType(testTextureType);
        const std::string baseTestTextureType = bas.base;
        const bt::Type sampleType = bas.sample;

        bool expectSuccess = false;
        const StoreArgs* types = storeArgsByName(baseTestTextureType);
        if (types != nullptr) {
            const bool typesMatch = types->coordsArg0 == s->coordsArg0 &&
                                    types->hasArrayIndexArg == s->hasArrayIndexArg &&
                                    bt::isConvertible(valueType, sampleType);
            expectSuccess = typesMatch;
        }
        t.expectCompileResult(expectSuccess, code);
    });

}  // namespace
