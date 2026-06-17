// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/texture_utils.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#pragma once

#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "cts/webgpu.h"

namespace cts::texture_utils {

std::vector<Value> shortShaderStages();
std::vector<Value> allTextureFormats();
std::vector<Value> depthStencilFormats();
std::vector<Value> shortAddressModes();
std::vector<Value> samplePointMethods();
std::vector<Value> cubeSamplePointMethods();

bool isPotentiallyFilterableAndFillable(const ParamRecord& record);
bool isFilterNearestOrFormatPossiblyFilterableAsTextureF32(const ParamRecord& record);
bool isDepthTextureFormatParam(const ParamRecord& record);
bool cubeEdgesOnlyForCube(const ParamRecord& record);
bool cubeOffsetsUnsupported(const ParamRecord& record);
bool isStage1Sampled2DSupported(const ParamRecord& record);
bool isIncrement2SampledFormatSupported(const ParamRecord& record);
bool isSampledColorTextureFormatParam(const ParamRecord& record);
bool isComputeStage(const ParamRecord& record);

ParamsBuilder addSampledTextureCommonParams(ParamsBuilder u, bool includeModeU, bool includeModeV);
ParamsBuilder addDepthTextureCommonParams(ParamsBuilder u);
std::vector<ParamRecord> lodClampParams();
std::vector<ParamRecord> depth3DViewDimensionParams();

void executeTextureSampleLevelStage1(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampled1D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampled2DLodClamp(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampled2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampled3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampled3DLodClamp(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelDepth2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelDepth2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleLevelDepth3D(AllFeaturesMaxLimitsGpuTest& t);

} // namespace cts::texture_utils
