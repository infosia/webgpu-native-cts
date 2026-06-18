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
std::vector<Value> possibleStorageTextureFormats();
std::vector<Value> shortAddressModes();
std::vector<Value> samplePointMethods();
std::vector<Value> cubeSamplePointMethods();
std::vector<Value> compareFunctions();

bool isPotentiallyFilterableAndFillable(const ParamRecord& record);
bool isFilterNearestOrFormatPossiblyFilterableAsTextureF32(const ParamRecord& record);
bool isDepthTextureFormatParam(const ParamRecord& record);
bool cubeEdgesOnlyForCube(const ParamRecord& record);
bool cubeOffsetsUnsupported(const ParamRecord& record);
bool isStage1Sampled2DSupported(const ParamRecord& record);
bool isIncrement2SampledFormatSupported(const ParamRecord& record);
bool isSampledColorTextureFormatParam(const ParamRecord& record);
bool isSampled1DColorTextureFormatParam(const ParamRecord& record);
bool isComputeStage(const ParamRecord& record);
bool isStorageReadWriteFormatParam(const ParamRecord& record);
bool isStorageReadWriteAccessOrFormatSupported(const ParamRecord& record);
bool isNotWritableStorageInVertexStage(const ParamRecord& record);

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

void executeTextureSampleSampled1D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleSampled2DLodClamp(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleSampled2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleSampled3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleDepth2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleDepth2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleDepth3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleDepthCubeArray(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureSampleGradSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleGradSampled3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleGradSampled2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleGradSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureSampleBiasSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleBiasSampled3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleBiasSampled2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleBiasSampledCubeArray(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureSampleCompare2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleCompareCube(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleCompare2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleCompareCubeArray(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureSampleCompareLevel2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleCompareLevelCube(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleCompareLevel2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureSampleCompareLevelCubeArray(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureSampleBaseClampToEdge2D(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureGatherSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherSampled3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherSampledArray2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherSampledArray3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherDepth2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherDepth3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherDepthArray2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherDepthArray3D(AllFeaturesMaxLimitsGpuTest& t);

void executeTextureGatherCompareArray2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherCompareArray3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherCompareSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureGatherCompareSampled3D(AllFeaturesMaxLimitsGpuTest& t);

bool isGatherFillableFormatParam(const ParamRecord& record);
bool isGatherFilterNearestOrPossiblyFilterableParam(const ParamRecord& record);

std::vector<Value> textureMetadataAspectsForFormat(const ParamRecord& record);
std::vector<Value> textureMetadataSamplesForFormat(const ParamRecord& record);
std::vector<Value> textureMetadataViewDimensions(const ParamRecord& record);
std::vector<Value> textureMetadataStorageViewDimensions(const ParamRecord& record);
std::vector<Value> textureMetadataMipCounts(const ParamRecord& record);
std::vector<Value> textureMetadataBaseMipLevels(const ParamRecord& record);
std::vector<Value> textureMetadataDimensionsLevels(const ParamRecord& record);

void executeTextureDimensionsSampledAndMultisampled(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureDimensionsDepth(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureDimensionsStorage(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureDimensionsExternal(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumLevelsSampled(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumLevelsDepth(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumLayersSampled(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumLayersArrayed(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumLayersStorage(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumSamplesSampled(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureNumSamplesDepth(AllFeaturesMaxLimitsGpuTest& t);

std::vector<Value> textureLoadShaderStages();
std::vector<Value> textureLoadMultisampledFormats();
bool textureLoadFormatCompatibleWith1D(const ParamRecord& record);
bool textureLoadFormatCompatibleWith3D(const ParamRecord& record);
bool textureLoadFormatNotCompressed(const ParamRecord& record);
bool textureLoadFormatNotCompressedFloat(const ParamRecord& record);
bool textureLoadFormatFillable(const ParamRecord& record);
bool textureLoadFormatHasDepth(const ParamRecord& record);
bool textureLoadDepthTextureTypeMatchesFormat(const ParamRecord& record);
std::vector<ParamRecord> textureLoadArrayedCoordinateParams();
bool textureLoadArrayLayerBaseValid(const ParamRecord& record);

void executeTextureLoadSampled1D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadSampled2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadSampled3D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadMultisampled(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadDepth(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadExternal(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadArrayed(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadStorage1D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadStorage2D(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadStorage2DArray(AllFeaturesMaxLimitsGpuTest& t);
void executeTextureLoadStorage3D(AllFeaturesMaxLimitsGpuTest& t);

} // namespace cts::texture_utils
