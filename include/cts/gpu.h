#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

#include "cts/test.h"
#include "cts/webgpu.h"

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

class GpuTest : public Fixture {
  public:
    void init() override;
    void finalize() override;

    virtual WGPUDevice device() const;
    virtual WGPUQueue queue() const;
    WGPULimits getLimits() const;
    WGPUBuffer createBufferTracked(const WGPUBufferDescriptor& desc);
    WGPUBuffer createBufferWithState(ResourceState state, const WGPUBufferDescriptor& desc);
    WGPUBuffer getErrorBuffer();
    WGPUSampler createSamplerTracked(const WGPUSamplerDescriptor& desc);
    WGPUTexture createTextureTracked(const WGPUTextureDescriptor& desc);
    WGPUTexture createTextureWithState(ResourceState state, const WGPUTextureDescriptor& desc);
    WGPUTextureView createViewTracked(WGPUTexture texture, const WGPUTextureViewDescriptor& desc);
    WGPUBindGroupLayout createBindGroupLayoutTracked(const WGPUBindGroupLayoutDescriptor& desc);
    WGPUPipelineLayout createPipelineLayoutTracked(const WGPUPipelineLayoutDescriptor& desc);
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
    bool isTextureFormatMultisampled(WGPUTextureFormat format);

  private:
    std::vector<WGPUBuffer> buffers_;
    std::vector<WGPUSampler> samplers_;
    std::vector<WGPUTexture> textures_;
    std::vector<WGPUTextureView> textureViews_;
    std::vector<WGPUBindGroupLayout> bindGroupLayouts_;
    std::vector<WGPUPipelineLayout> pipelineLayouts_;
    std::vector<WGPUCommandEncoder> encoders_;
    std::vector<WGPUCommandBuffer> commandBuffers_;
};

class AllFeaturesMaxLimitsGpuTest : public GpuTest {
  public:
    void init() override;
    WGPUDevice device() const override;
    WGPUQueue queue() const override;
};

} // namespace cts
