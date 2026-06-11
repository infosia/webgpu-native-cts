// Ported from gpuweb/cts src/webgpu/api/operation/shader_module/compilation_info.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//
// 1. Fixture: AllFeaturesMaxLimitsGpuTest as specified by the phaseY5 spec table.
//
// 2. getCompilationInfo async pattern: wgpuShaderModuleGetCompilationInfo is
//    asynchronous. The harness's WGPUInstance (cache().instance) is not exposed
//    through the public API, so we cannot call processEventsUntil on it from
//    test bodies. Each test therefore creates a private WGPUInstance+adapter+device
//    for shader-module creation and compilation-info calls only. This is safe because
//    these tests require no GPU execution or readback — only shader compilation.
//    The CompilationInfoContext RAII struct mirrors the OwnedDeviceContext pattern
//    from api/operation/buffers/map.spec.cpp.
//
// 3. Param identity: upstream kAllShaderSources / kInvalidShaderSources are arrays of
//    objects whose fields starting with '_' (e.g. _code, _errorLine, _errorLinePos)
//    are excluded from the upstream query string. The non-'_' fields ('name', 'valid')
//    appear as query params. C++ params include everything (no '_' exclusion), so only
//    'name' and 'valid' are made into params; '_code', '_errorLine', and '_errorLinePos'
//    are derived from 'name' via lookup functions at runtime. Query identity for the
//    (name, valid) pair matches upstream exactly.
//
// 4. Unicode strings: kValid/kInvalidUnicodeCode contain the Japanese text
//    "頂点シェーダー 👩‍💻" (vertex-shader programmer-emoji) stored as UTF-8 hex
//    escape sequences to avoid source-encoding issues on all platforms.
//    kInvalidCarriageReturnCode contains a literal \r\n (bytes 0x0D 0x0A) between
//    the opening-brace line and the error line, matching the upstream concatenation.
//    kInvalidUnicodeMultiByteCode contains 7 cat emoji (U+1F408, 4 bytes each) plus
//    an invalid '?' character — matching upstream kInvalidShaderSources[3].
//
// 5. _errorLinePos for unicode-multi-byte-characters: upstream _errorLinePos=19 is
//    expressed in UTF-16 code units (per the GPUCompilationMessage JS spec: the 7 cat
//    emoji U+1F408 are surrogate pairs, each 2 UTF-16 units; so 2+"/*" + 14+cats + 2+"*/"
//    = 18 units before '?', giving pos 19).  The webgpu.h WGPUCompilationMessage spec
//    says linePos is in UTF-8 code units, which gives 2+28+2=32 bytes before '?',
//    i.e. pos 33.  Native backends (Dawn, yawgpu) report one of the two values
//    depending on internal encoding.  The port accepts either 19 or 33 as correct.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Shader source strings (upstream kValidShaderSources / kInvalidShaderSources)
// ---------------------------------------------------------------------------

// valid, name="ascii"
static const char kValidAsciiCode[] =
    "\n"
    "      @vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "        return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "      }";

// valid, name="unicode"
// Contains Japanese "頂点シェーダー" (UTF-8: E9 A0 82 E7 82 B9 E3 82 B7 E3 82 A7 E3 83 BC
// E3 83 80 E3 83 BC) and programmer emoji "👩‍💻" (F0 9F 91 A9 E2 80 8D F0 9F 92 BB).
static const char kValidUnicodeCode[] =
    "\n"
    "      // "
    "\xe9\xa0\x82\xe7\x82\xb9\xe3\x82\xb7\xe3\x82\xa7\xe3\x83\xbc\xe3\x83\x80\xe3\x83\xbc"
    " "
    "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
    "\n"
    "      @vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "        return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "      }";

// invalid, name="ascii", _errorLine=4
static const char kInvalidAsciiCode[] =
    "\n"
    "      @vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "        // Expected Error: unknown function 'unknown'\n"
    "        return unknown(0.0, 0.0, 0.0, 1.0);\n"
    "      }";

// invalid, name="unicode", _errorLine=5
static const char kInvalidUnicodeCode[] =
    "\n"
    "      // "
    "\xe9\xa0\x82\xe7\x82\xb9\xe3\x82\xb7\xe3\x82\xa7\xe3\x83\xbc\xe3\x83\x80\xe3\x83\xbc"
    " "
    "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
    "\n"
    "      @vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "        // Expected Error: unknown function 'unknown'\n"
    "        return unknown(0.0, 0.0, 0.0, 1.0);\n"
    "      }";

// invalid, name="unicode-multi-byte-characters", _errorLine=1, _errorLinePos=19
// 7 cat emoji (U+1F408, F0 9F 90 88 each) bracketed in /* ... */ followed by '?'
// which is invalid. See upstream kInvalidShaderSources[3].
static const char kInvalidUnicodeMultiByteCode[] =
    "/*"
    "\xf0\x9f\x90\x88\xf0\x9f\x90\x88\xf0\x9f\x90\x88\xf0\x9f\x90\x88"
    "\xf0\x9f\x90\x88\xf0\x9f\x90\x88\xf0\x9f\x90\x88"
    "*/"
    "?\n"
    "// Expected Error: invalid character found";

// invalid, name="carriage-return", _errorLine=5
// Constructed as a std::string (not a char[]) to embed \r\n without a raw string
// since MSVC may treat raw strings with \r\n differently in headers. See upstream
// kInvalidShaderSources[2] which concatenates two template literals with '\r\n'.
static const std::string kInvalidCarriageReturnCode =
    std::string(
        "\n"
        "      @vertex fn main() -> @builtin(position) vec4<f32> {") +
    "\r\n" +
    std::string(
        "\n"
        "        // Expected Error: unknown function 'unknown'\n"
        "        return unknown(0.0, 0.0, 0.0, 1.0);\n"
        "      }");

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

// Returns the WGSL source for the given (name, valid) pair.
// "name" is the upstream name field (e.g. "ascii", "unicode").
// "valid" disambiguates valid vs invalid shaders with the same name.
static std::string lookupShaderCode(const std::string& name, bool valid) {
    if (valid) {
        if (name == "ascii")   { return kValidAsciiCode; }
        if (name == "unicode") { return kValidUnicodeCode; }
    } else {
        if (name == "ascii")                           { return kInvalidAsciiCode; }
        if (name == "unicode")                         { return kInvalidUnicodeCode; }
        if (name == "carriage-return")                 { return kInvalidCarriageReturnCode; }
        if (name == "unicode-multi-byte-characters")   { return kInvalidUnicodeMultiByteCode; }
    }
    std::abort();
}

// Returns the expected error line number (1-based) for invalid shaders by name.
// All callers of this function only call it for invalid shader cases.
static uint64_t lookupErrorLine(const std::string& name) {
    if (name == "ascii")                         { return 4; }
    if (name == "unicode")                       { return 5; }
    if (name == "carriage-return")               { return 5; }
    if (name == "unicode-multi-byte-characters") { return 1; }
    return 0;
}

// Returns the expected error line-position values (1-based), or {0,0} if absent.
// _errorLinePos is only set for "unicode-multi-byte-characters".
//
// The upstream JS test uses _errorLinePos=19 (UTF-16 code units per the
// GPUCompilationMessage JS spec: the 7 cat emoji U+1F408 are surrogate pairs
// so each counts as 2, giving 2+14+2=18 before '?' → pos 19).
//
// The webgpu.h C spec says linePos is in UTF-8 code units.  For the same
// string the UTF-8 byte count before '?' is 2+28+2=32, so '?' is at pos 33.
//
// Native backends (Dawn, yawgpu) may report either value depending on their
// internal encoding.  We accept both 19 (UTF-16 units) and 33 (UTF-8 bytes)
// as correct for the unicode-multi-byte-characters case.
// linePos1 and linePos2 are both acceptable values; a value of 0 means unused.
struct LinePosExpected {
    uint64_t linePos1;
    uint64_t linePos2;
};
static LinePosExpected lookupErrorLinePos(const std::string& name) {
    if (name == "unicode-multi-byte-characters") {
        // Accept UTF-16 code-unit count (19) OR UTF-8 byte count (33).
        return LinePosExpected{19u, 33u};
    }
    return LinePosExpected{0u, 0u};
}

// ---------------------------------------------------------------------------
// Param tables
// ---------------------------------------------------------------------------

// Mirrors upstream kAllShaderSources — non-'_' fields only (name, valid).
// Query identity: name=ascii;valid=true / name=ascii;valid=false / etc.
static std::vector<ParamRecord> kAllShaderSourceParams() {
    return {
        ParamRecord{{"name", "ascii"},                        {"valid", true}},
        ParamRecord{{"name", "unicode"},                      {"valid", true}},
        ParamRecord{{"name", "ascii"},                        {"valid", false}},
        ParamRecord{{"name", "unicode"},                      {"valid", false}},
        ParamRecord{{"name", "carriage-return"},              {"valid", false}},
        ParamRecord{{"name", "unicode-multi-byte-characters"}, {"valid", false}},
    };
}

// Mirrors upstream kInvalidShaderSources — non-'_' fields only (name, valid=false).
static std::vector<ParamRecord> kInvalidShaderSourceParams() {
    return {
        ParamRecord{{"name", "ascii"},                         {"valid", false}},
        ParamRecord{{"name", "unicode"},                       {"valid", false}},
        ParamRecord{{"name", "carriage-return"},               {"valid", false}},
        ParamRecord{{"name", "unicode-multi-byte-characters"}, {"valid", false}},
    };
}

// ---------------------------------------------------------------------------
// Private compilation context
// (see porting note 2: we need our own instance to pump events for
//  wgpuShaderModuleGetCompilationInfo since cache().instance is internal)
// ---------------------------------------------------------------------------

struct CompilationInfoContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;

    CompilationInfoContext() = default;
    CompilationInfoContext(const CompilationInfoContext&) = delete;
    CompilationInfoContext& operator=(const CompilationInfoContext&) = delete;

    ~CompilationInfoContext() {
        if (device != nullptr) {
            wgpuDeviceRelease(device);
        }
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
        }
    }
};

static void buildCompilationContext(
    AllFeaturesMaxLimitsGpuTest& t,
    CompilationInfoContext& ctx) {
    ctx.instance = createInstance();
    if (ctx.instance == nullptr) {
        t.fail("failed to create WGPUInstance for compilation info");
    }
    AdapterResult adapterResult = requestAdapterSync(ctx.instance, nullptr);
    if (adapterResult.status != WGPURequestAdapterStatus_Success ||
        adapterResult.adapter == nullptr) {
        t.fail("failed to request adapter: " + adapterResult.message);
    }
    ctx.adapter = adapterResult.adapter;

    WGPUDeviceDescriptor devDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
    DeviceResult devResult = requestDeviceSync(ctx.instance, ctx.adapter, &devDesc);
    if (devResult.status != WGPURequestDeviceStatus_Success || devResult.device == nullptr) {
        t.fail("failed to request device: " + devResult.message);
    }
    ctx.device = devResult.device;
}

// Create a shader module on the private context device.
// Wraps the creation in a validation error scope; fails the test if the expected
// validation-error behaviour is not observed.
// Returns the shader module (always non-null; may be an error object for invalid shaders).
static WGPUShaderModule createShaderModuleLocal(
    AllFeaturesMaxLimitsGpuTest& t,
    const CompilationInfoContext& ctx,
    const std::string& code,
    bool shouldError) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = WGPUStringView{code.data(), code.size()};

    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;

    wgpuDevicePushErrorScope(ctx.device, WGPUErrorFilter_Validation);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device, &desc);
    ScopeResult scopeResult = popErrorScopeSync(ctx.instance, ctx.device);

    if (scopeResult.status != WGPUPopErrorScopeStatus_Success) {
        if (shaderModule != nullptr) {
            wgpuShaderModuleRelease(shaderModule);
        }
        t.fail("popErrorScope failed: " + scopeResult.message);
    }
    const bool hadError = scopeResult.type != WGPUErrorType_NoError;
    if (shouldError && !hadError) {
        if (shaderModule != nullptr) {
            wgpuShaderModuleRelease(shaderModule);
        }
        t.fail("expected a validation error for invalid shader, got none");
    }
    if (!shouldError && hadError) {
        if (shaderModule != nullptr) {
            wgpuShaderModuleRelease(shaderModule);
        }
        t.fail("unexpected validation error for valid shader: " + scopeResult.message);
    }
    if (shaderModule == nullptr) {
        t.fail("createShaderModule returned null");
    }
    return shaderModule;
}

// ---------------------------------------------------------------------------
// Synchronous wrapper for wgpuShaderModuleGetCompilationInfo
// ---------------------------------------------------------------------------

struct CompilationMessage {
    WGPUCompilationMessageType type = WGPUCompilationMessageType_Error;
    uint64_t lineNum = 0;
    uint64_t linePos = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct GetCompilationInfoState {
    bool completed = false;
    WGPUCompilationInfoRequestStatus status = WGPUCompilationInfoRequestStatus_CallbackCancelled;
    std::vector<CompilationMessage> messages;
};

static void onGetCompilationInfo(
    WGPUCompilationInfoRequestStatus status,
    const WGPUCompilationInfo* info,
    void* userdata1,
    void* /*userdata2*/) {
    auto* state = static_cast<GetCompilationInfoState*>(userdata1);
    state->completed = true;
    state->status = status;
    if (info != nullptr) {
        for (size_t i = 0; i < info->messageCount; ++i) {
            const WGPUCompilationMessage& src = info->messages[i];
            CompilationMessage dst;
            dst.type    = src.type;
            dst.lineNum = src.lineNum;
            dst.linePos = src.linePos;
            dst.offset  = src.offset;
            dst.length  = src.length;
            state->messages.push_back(dst);
        }
    }
}

static std::vector<CompilationMessage> getCompilationInfoSync(
    AllFeaturesMaxLimitsGpuTest& t,
    const CompilationInfoContext& ctx,
    WGPUShaderModule shaderModule) {
    GetCompilationInfoState state;

    WGPUCompilationInfoCallbackInfo callbackInfo = WGPU_COMPILATION_INFO_CALLBACK_INFO_INIT;
    callbackInfo.mode      = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback  = onGetCompilationInfo;
    callbackInfo.userdata1 = &state;

    (void)wgpuShaderModuleGetCompilationInfo(shaderModule, callbackInfo);

    if (!processEventsUntil(ctx.instance, [&] { return state.completed; })) {
        t.fail("getCompilationInfo timed out");
    }
    if (state.status != WGPUCompilationInfoRequestStatus_Success) {
        t.fail("getCompilationInfo failed (status=" +
               std::to_string(static_cast<int>(state.status)) + ")");
    }
    return state.messages;
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,shader_module,compilation_info",
    R"(
ShaderModule CompilationInfo tests.
)");

// ---------------------------------------------------------------------------
// Test: getCompilationInfo_returns
// ---------------------------------------------------------------------------

CTS_TEST(g, "getCompilationInfo_returns")
    .desc(
        R"(
    Test that getCompilationInfo() can be called on any ShaderModule.

    - Test for both valid and invalid shader modules.
    - Test for shader modules containing only ASCII and those containing unicode characters.
    - Test that the compilation info for valid shader modules contains no errors.
    - Test that the compilation info for invalid shader modules contains at least one error.)")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(kAllShaderSourceParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string name = t.param<std::string>("name");
        const bool valid       = t.param<bool>("valid");
        const std::string code = lookupShaderCode(name, valid);

        CompilationInfoContext ctx;
        buildCompilationContext(t, ctx);

        WGPUShaderModule shaderModule = createShaderModuleLocal(t, ctx, code, /*shouldError=*/!valid);

        const std::vector<CompilationMessage> messages = getCompilationInfoSync(t, ctx, shaderModule);
        wgpuShaderModuleRelease(shaderModule);

        uint64_t errorCount = 0;
        for (const CompilationMessage& msg : messages) {
            if (msg.type == WGPUCompilationMessageType_Error) {
                ++errorCount;
            }
        }

        if (valid) {
            t.expect(errorCount == 0, "Expected zero GPUCompilationMessages of type 'error'");
        } else {
            t.expect(errorCount > 0, "Expected at least one GPUCompilationMessages of type 'error'");
        }
    });

// ---------------------------------------------------------------------------
// Test: line_number_and_position
// ---------------------------------------------------------------------------

CTS_TEST(g, "line_number_and_position")
    .desc(
        R"(
    Test that line numbers reported by compilationInfo either point at an appropriate line and
    position or at 0:0, indicating an unknown position.

    - Test for invalid shader modules containing containing at least one error.
    - Test for shader modules containing only ASCII and those containing unicode characters.)")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(kInvalidShaderSourceParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string name         = t.param<std::string>("name");
        const std::string code         = lookupShaderCode(name, /*valid=*/false);
        const uint64_t errorLine       = lookupErrorLine(name);
        const LinePosExpected linePosEx = lookupErrorLinePos(name); // linePos1==0 means absent

        CompilationInfoContext ctx;
        buildCompilationContext(t, ctx);

        WGPUShaderModule shaderModule =
            createShaderModuleLocal(t, ctx, code, /*shouldError=*/true);

        const std::vector<CompilationMessage> messages = getCompilationInfoSync(t, ctx, shaderModule);
        wgpuShaderModuleRelease(shaderModule);

        bool foundAppropriateError = false;
        for (const CompilationMessage& msg : messages) {
            if (msg.type != WGPUCompilationMessageType_Error) {
                continue;
            }

            // GPUCompilationMessage must specify both lineNum+linePos, or neither.
            t.expect(
                (msg.lineNum == 0) == (msg.linePos == 0),
                "Got message.lineNum " + std::to_string(msg.lineNum) +
                    ", .linePos " + std::to_string(msg.linePos) +
                    ", but GPUCompilationMessage should specify both or neither");

            if (msg.lineNum == 0) {
                // Unknown position (0:0) is always acceptable per spec.
                foundAppropriateError = true;
                break;
            }

            if (msg.lineNum == errorLine) {
                foundAppropriateError = true;
                if (linePosEx.linePos1 != 0u) {
                    // Accept either the UTF-16 code-unit position (linePos1=19) or the
                    // UTF-8 byte position (linePos2=33); both are valid for native backends.
                    const bool posOk =
                        (msg.linePos == linePosEx.linePos1) ||
                        (msg.linePos == linePosEx.linePos2);
                    t.expect(
                        posOk,
                        "Got message.linePos " + std::to_string(msg.linePos) +
                            ", expected " + std::to_string(linePosEx.linePos1) +
                            " (UTF-16 units) or " + std::to_string(linePosEx.linePos2) +
                            " (UTF-8 bytes)");
                }
                break;
            }
        }

        t.expect(
            foundAppropriateError,
            "Expected to find an error which corresponded with the erroneous line");
    });

// ---------------------------------------------------------------------------
// Test: offset_and_length
// ---------------------------------------------------------------------------

CTS_TEST(g, "offset_and_length")
    .desc(
        R"(Test that message offsets and lengths are valid and align with any reported lineNum and linePos.

    - Test for valid and invalid shader modules.
    - Test for shader modules containing only ASCII and those containing unicode characters.)")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(kAllShaderSourceParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string name = t.param<std::string>("name");
        const bool valid       = t.param<bool>("valid");
        const std::string code = lookupShaderCode(name, valid);

        CompilationInfoContext ctx;
        buildCompilationContext(t, ctx);

        WGPUShaderModule shaderModule =
            createShaderModuleLocal(t, ctx, code, /*shouldError=*/!valid);

        const std::vector<CompilationMessage> messages = getCompilationInfoSync(t, ctx, shaderModule);
        wgpuShaderModuleRelease(shaderModule);

        const uint64_t codeLen = static_cast<uint64_t>(code.size());

        for (const CompilationMessage& msg : messages) {
            // Any offsets and lengths should reference valid spans of the shader code.
            t.expect(
                msg.offset <= codeLen && msg.offset + msg.length <= codeLen,
                "message.offset and .length should be within the shader source "
                "(offset=" + std::to_string(msg.offset) +
                ", length=" + std::to_string(msg.length) +
                ", codeLen=" + std::to_string(codeLen) + ")");

            // If a valid line number and position are given, the offset must point to the
            // same location in the shader source: lineOffset + linePos - 1 == offset.
            if (msg.lineNum != 0 && msg.linePos != 0) {
                // Walk the source to find the byte-start of line msg.lineNum (1-based).
                uint64_t lineOffset = 0;
                bool lineFound = true;
                for (uint64_t lineIdx = 0; lineIdx < msg.lineNum - 1; ++lineIdx) {
                    const size_t found = code.find('\n', static_cast<size_t>(lineOffset));
                    if (found == std::string::npos) {
                        lineFound = false;
                        break;
                    }
                    lineOffset = static_cast<uint64_t>(found) + 1;
                }
                t.expect(lineFound,
                    "message.lineNum " + std::to_string(msg.lineNum) +
                    " is beyond the end of the shader source");

                if (lineFound) {
                    const uint64_t expectedOffset = lineOffset + msg.linePos - 1;
                    t.expect(
                        msg.offset == expectedOffset,
                        "message.lineNum (" + std::to_string(msg.lineNum) +
                            ") and .linePos (" + std::to_string(msg.linePos) +
                            ") point to offset " + std::to_string(expectedOffset) +
                            " (" + std::to_string(lineOffset) +
                            " + " + std::to_string(msg.linePos) +
                            " - 1) but .offset is " + std::to_string(msg.offset));
                }
            }
        }
    });

} // namespace
