// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/texture/texture_sync_test.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Texture memory-sync op tables (kAllWriteOps, kAllReadOps, kOpInfo) and checkOpsValidForContext.
//
// Query spelling: upstream case params read/write/first/second are objects {op, in}, stringified
// as JSON ({"op":"t2b-copy","in":"command-encoder"}). The harness Value has no object variant, so
// each is stored as a string holding that same compact JSON (the spelling
// shader/execution/shader_io/compute_builtins already uses for object params).
#pragma once

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/test.h"
#include "cts/webgpu.h"
#include "webgpu/api/operation/memory_sync/operation_context_helper.h"

namespace cts::memory_sync {

inline constexpr std::array<const char*, 6> kAllTextureWriteOps = {
    "write-texture", "b2t-copy", "t2t-copy", "storage", "attachment-store", "attachment-resolve",
};

inline constexpr std::array<const char*, 3> kAllTextureReadOps = {"t2b-copy", "t2t-copy", "sample"};

// Port of kOpInfo.
struct TextureOpInfo {
    std::vector<std::string_view> contexts;
    WGPUTextureUsage readUsage;
    WGPUTextureUsage writeUsage;
};

inline TextureOpInfo textureOpInfo(std::string_view op) {
    if (op == "write-texture") {
        return {{"queue"}, WGPUTextureUsage_None, WGPUTextureUsage_CopyDst};
    }
    if (op == "b2t-copy") {
        return {{"command-encoder"}, WGPUTextureUsage_None, WGPUTextureUsage_CopyDst};
    }
    if (op == "t2t-copy") {
        return {{"command-encoder"}, WGPUTextureUsage_CopySrc, WGPUTextureUsage_CopyDst};
    }
    if (op == "t2b-copy") {
        return {{"command-encoder"}, WGPUTextureUsage_CopySrc, WGPUTextureUsage_None};
    }
    if (op == "storage") {
        return {{"compute-pass-encoder", "render-pass-encoder", "render-bundle-encoder"},
                WGPUTextureUsage_None,
                WGPUTextureUsage_StorageBinding};
    }
    if (op == "sample") {
        return {{"compute-pass-encoder", "render-pass-encoder", "render-bundle-encoder"},
                WGPUTextureUsage_TextureBinding,
                WGPUTextureUsage_None};
    }
    if (op == "attachment-store" || op == "attachment-resolve") {
        return {{"command-encoder"}, WGPUTextureUsage_None, WGPUTextureUsage_RenderAttachment};
    }
    std::abort();
}

inline bool textureOpValidInContext(std::string_view op, std::string_view context) {
    for (std::string_view c : textureOpInfo(op).contexts) {
        if (c == context) {
            return true;
        }
    }
    return false;
}

// In a render pass it is invalid to use a resource as both writable and another usage; also
// storage+storage is racy and cannot be reliably tested, so those pairs are filtered.
inline bool textureCheckImpl(std::string_view op1, std::string_view op2) {
    if (op1 == "attachment-resolve" || op1 == "attachment-store" || op1 == "storage") {
        if (op2 == "attachment-resolve" || op2 == "attachment-store" || op2 == "storage" || op2 == "sample") {
            // Write+other, or racy.
            return false;
        }
    }
    return true;
}

inline bool checkTextureOpsValidForContext(std::string_view op0,
                                           std::string_view op1,
                                           std::string_view context0,
                                           std::string_view context1) {
    if (!textureOpValidInContext(op0, context0) || !textureOpValidInContext(op1, context1)) {
        return false;
    }
    if (isRenderContext(context0) || isRenderContext(context1)) {
        return textureCheckImpl(op0, op1) && textureCheckImpl(op1, op0);
    }
    return true;
}

// An {op, in} parameter object.
struct OpAndContext {
    std::string op;
    std::string in;
};

// JSON spelling of an {op, in} object, key order as upstream: {"op":"...","in":"..."}.
inline Value opAndContextValue(std::string_view op, std::string_view in) {
    std::string json = "{\"op\":\"";
    json += op;
    json += "\",\"in\":\"";
    json += in;
    json += "\"}";
    return Value(json);
}

inline OpAndContext parseOpAndContext(const std::string& json) {
    auto field = [&json](std::string_view key) {
        const std::string marker = "\"" + std::string(key) + "\":\"";
        const size_t begin = json.find(marker);
        if (begin == std::string::npos) {
            std::abort();
        }
        const size_t valueBegin = begin + marker.size();
        const size_t valueEnd = json.find('"', valueBegin);
        if (valueEnd == std::string::npos) {
            std::abort();
        }
        return json.substr(valueBegin, valueEnd - valueBegin);
    };
    return OpAndContext{field("op"), field("in")};
}

// 'rw': boundary;read;write, with checkOpsValidForContext([read, write], _context).
inline std::vector<ParamRecord> textureReadWriteCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* read : kAllTextureReadOps) {
                for (const char* write : kAllTextureWriteOps) {
                    if (checkTextureOpsValidForContext(read, write, context.first, context.second)) {
                        records.push_back(ParamRecord{
                            {"boundary", Value(boundary)},
                            {"read", opAndContextValue(read, context.first)},
                            {"write", opAndContextValue(write, context.second)},
                        });
                    }
                }
            }
        }
    }
    return records;
}

// 'wr': boundary;write;read, with checkOpsValidForContext([write, read], _context).
inline std::vector<ParamRecord> textureWriteReadCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* read : kAllTextureReadOps) {
                for (const char* write : kAllTextureWriteOps) {
                    if (checkTextureOpsValidForContext(write, read, context.first, context.second)) {
                        records.push_back(ParamRecord{
                            {"boundary", Value(boundary)},
                            {"write", opAndContextValue(write, context.first)},
                            {"read", opAndContextValue(read, context.second)},
                        });
                    }
                }
            }
        }
    }
    return records;
}

// 'ww': boundary;first;second.
inline std::vector<ParamRecord> textureWriteWriteCaseRecords() {
    std::vector<ParamRecord> records;
    for (const char* boundary : kOperationBoundaries) {
        for (const ContextPair& context : boundaryContexts(boundary)) {
            for (const char* first : kAllTextureWriteOps) {
                for (const char* second : kAllTextureWriteOps) {
                    if (checkTextureOpsValidForContext(first, second, context.first, context.second)) {
                        records.push_back(ParamRecord{
                            {"boundary", Value(boundary)},
                            {"first", opAndContextValue(first, context.first)},
                            {"second", opAndContextValue(second, context.second)},
                        });
                    }
                }
            }
        }
    }
    return records;
}

} // namespace cts::memory_sync
