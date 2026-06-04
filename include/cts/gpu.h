#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/test.h"
#include "cts/webgpu.h"
#include "webgpu/util/texture_layout.h"

namespace cts {

enum class ResourceState {
    Valid,
    Invalid,
    Destroyed,
};

constexpr std::array<ResourceState, 3> kResourceStates = {
    ResourceState::Valid,
    ResourceState::Invalid,
    ResourceState::Destroyed,
};

inline std::string_view resourceStateIdentifier(ResourceState state) {
    switch (state) {
        case ResourceState::Valid:
            return "valid";
        case ResourceState::Invalid:
            return "invalid";
        case ResourceState::Destroyed:
            return "destroyed";
    }
    std::abort();
}

inline ResourceState parseResourceState(std::string_view identifier) {
    if (identifier == "valid") {
        return ResourceState::Valid;
    }
    if (identifier == "invalid") {
        return ResourceState::Invalid;
    }
    if (identifier == "destroyed") {
        return ResourceState::Destroyed;
    }
    std::abort();
}

inline std::vector<Value> resourceStateValues() {
    std::vector<Value> values;
    values.reserve(kResourceStates.size());
    for (ResourceState state : kResourceStates) {
        values.emplace_back(std::string(resourceStateIdentifier(state)));
    }
    return values;
}

class GpuTest : public Fixture {
  public:
    void init() override;
    void finalize() override;

    virtual WGPUDevice device() const;
    WGPUDevice mismatchedDevice();
    virtual WGPUQueue queue() const;
    WGPULimits getLimits() const;
    WGPUCompatibilityModeLimits getCompatibilityModeLimits() const;
    WGPUBuffer makeBufferWithContents(const void* data, size_t size, WGPUBufferUsage usage);
    void expectGPUBufferValuesEqual(
        WGPUBuffer src,
        const void* expected,
        size_t size,
        uint64_t srcByteOffset = 0);
    void expectGPUBufferValuesPassCheck(
        WGPUBuffer src,
        const std::function<std::optional<std::string>(const uint8_t* actual, size_t len)>& check,
        uint64_t srcByteOffset,
        size_t byteLength);
    void expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
        const uint8_t* expected,
        size_t expectedLen,
        WGPUBuffer buffer,
        WGPUTextureFormat format,
        WGPUExtent3D size,
        TexelCopyBufferLayout dataLayout);
    void queueWriteBuffer(WGPUBuffer buffer, uint64_t bufferOffset, const void* data, size_t size);
    void queueWriteTexture(
        WGPUTexture dst,
        WGPUExtent3D copySize,
        const WGPUTexelCopyBufferLayout& layout,
        const void* data,
        size_t size,
        uint32_t mipLevel = 0,
        WGPUOrigin3D origin = WGPUOrigin3D{0, 0, 0});
    void copyBufferToTexture(
        WGPUCommandEncoder encoder,
        WGPUBuffer src,
        uint32_t bytesPerRow,
        WGPUTexture dst,
        WGPUExtent3D size);
    void copyTextureToBuffer(
        WGPUCommandEncoder encoder,
        WGPUTexture src,
        WGPUBuffer dst,
        uint32_t bytesPerRow,
        WGPUExtent3D size);
    void copyTextureToTexture(
        WGPUCommandEncoder encoder,
        WGPUTexture src,
        WGPUTexture dst,
        WGPUExtent3D size);
    void onSubmittedWorkDoneSync();
    void onSubmittedWorkDoneMany(uint32_t n, bool checkOrder);
    WGPUBuffer createBufferTracked(const WGPUBufferDescriptor& desc);
    WGPUBuffer createBufferOnMismatchedDevice(const WGPUBufferDescriptor& desc);
    WGPUBuffer createBufferWithState(ResourceState state, const WGPUBufferDescriptor& desc);
    WGPUBuffer getErrorBuffer();
    WGPUSampler createSamplerTracked(const WGPUSamplerDescriptor& desc);
    WGPUTexture createTextureTracked(const WGPUTextureDescriptor& desc);
    WGPUTexture createTextureWithState(ResourceState state, const WGPUTextureDescriptor& desc);
    WGPUTextureView createViewTracked(WGPUTexture texture, const WGPUTextureViewDescriptor& desc);
    WGPUShaderModule createShaderModuleTracked(std::string_view wgsl);
    WGPUBindGroupLayout createBindGroupLayoutTracked(const WGPUBindGroupLayoutDescriptor& desc);
    WGPUBindGroupLayout createBindGroupLayoutOnMismatchedDevice(const WGPUBindGroupLayoutDescriptor& desc);
    WGPUBindGroup createBindGroupTracked(const WGPUBindGroupDescriptor& desc);
    WGPUPipelineLayout createPipelineLayoutTracked(const WGPUPipelineLayoutDescriptor& desc);
    WGPURenderPipeline createRenderPipelineTracked(const WGPURenderPipelineDescriptor& desc);
    WGPUComputePipeline createComputePipelineTracked(const WGPUComputePipelineDescriptor& desc);
    WGPUCommandEncoder createCommandEncoderTracked();
    WGPUCommandBuffer finishTracked(WGPUCommandEncoder encoder);
    void expectValidationError(const std::function<void()>& body, bool shouldError);
    void expectMapAsync(WGPUBuffer buffer,
                        WGPUMapMode mode,
                        bool expectSuccess,
                        size_t offset = 0,
                        size_t size = WGPU_WHOLE_MAP_SIZE);
    void skipIfTransientAttachmentNotSupported();
    void skipIfTextureFormatNotSupported(WGPUTextureFormat format);
    void skipIfTextureFormatAndDimensionNotCompatible(WGPUTextureFormat format, WGPUTextureDimension dimension);
    void skipIfTextureViewDimensionNotSupported(WGPUTextureViewDimension dimension);
    void skipIfTextureFormatNotUsableAsRenderAttachment(WGPUTextureFormat format);
    void skipIfTextureFormatDoesNotSupportUsage(WGPUTextureUsage usage, WGPUTextureFormat format);
    bool textureDimensionAndFormatCompatibleForDevice(WGPUTextureDimension dimension, WGPUTextureFormat format);
    bool isTextureFormatColorRenderable(WGPUTextureFormat format);
    bool isTextureFormatUsableAsRenderAttachment(WGPUTextureFormat format);
    bool isTextureFormatUsableAsWriteOnlyStorageTexture(WGPUTextureFormat format);
    bool isTextureFormatUsableAsReadOnlyStorageTexture(WGPUTextureFormat format);
    bool isTextureFormatUsableAsReadWriteStorageTexture(WGPUTextureFormat format);
    bool isTextureFormatUsableWithStorageAccessMode(WGPUTextureFormat format, WGPUStorageTextureAccess access);
    bool isTextureFormatMultisampled(WGPUTextureFormat format);

  private:
    std::vector<WGPUBuffer> buffers_;
    std::vector<WGPUSampler> samplers_;
    std::vector<WGPUTexture> textures_;
    std::vector<WGPUTextureView> textureViews_;
    std::vector<WGPUShaderModule> shaderModules_;
    std::vector<WGPUBindGroupLayout> bindGroupLayouts_;
    std::vector<WGPUBindGroupLayout> mismatchedDeviceBindGroupLayouts_;
    std::vector<WGPUBindGroup> bindGroups_;
    std::vector<WGPUPipelineLayout> pipelineLayouts_;
    std::vector<WGPURenderPipeline> renderPipelines_;
    std::vector<WGPUComputePipeline> computePipelines_;
    std::vector<WGPUCommandEncoder> encoders_;
    std::vector<WGPUCommandBuffer> commandBuffers_;
    std::vector<WGPUBuffer> mismatchedDeviceBuffers_;
    WGPUAdapter mismatchedAdapter_ = nullptr;
    WGPUDevice mismatchedDevice_ = nullptr;
};

class AllFeaturesMaxLimitsGpuTest : public GpuTest {
  public:
    void init() override;
    WGPUDevice device() const override;
    WGPUQueue queue() const override;
};

} // namespace cts
