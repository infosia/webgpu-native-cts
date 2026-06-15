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

/// Lifecycle state a resource can be put in for validation tests: a valid
/// resource, one created invalid (via an error scope), or one explicitly destroyed.
enum class ResourceState {
    Valid,
    Invalid,
    Destroyed,
};

/// All resource states, for parameterizing over them.
constexpr std::array<ResourceState, 3> kResourceStates = {
    ResourceState::Valid,
    ResourceState::Invalid,
    ResourceState::Destroyed,
};

/// Returns the query-string identifier for a resource state (`"valid"` / `"invalid"` / `"destroyed"`).
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

/// Parses a resource-state identifier back into the enum (aborts on an unknown value).
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

/// Returns the resource states as parameter `Value`s for use in a params builder.
inline std::vector<Value> resourceStateValues() {
    std::vector<Value> values;
    values.reserve(kResourceStates.size());
    for (ResourceState state : kResourceStates) {
        values.emplace_back(std::string(resourceStateIdentifier(state)));
    }
    return values;
}

/// Fixture for tests that need a real GPU device. Acquires an adapter/device/queue
/// in `init`, tracks every resource it creates so they are released in `finalize`,
/// and provides the WebGPU CTS `GPUTest` helpers (buffer/texture readback,
/// validation-error expectations, format-capability skips, tracked resource creation).
class GpuTest : public Fixture {
  public:
    void init() override;
    void finalize() override;

    /// The primary device under test.
    virtual WGPUDevice device() const;
    /// A second device on a different adapter, lazily created, for device-mismatch validation tests.
    WGPUDevice mismatchedDevice();
    /// The primary device's default queue.
    virtual WGPUQueue queue() const;
    /// Returns the device's supported limits.
    WGPULimits getLimits() const;
    /// Returns the device's compatibility-mode limits.
    WGPUCompatibilityModeLimits getCompatibilityModeLimits() const;
    /// Creates a buffer initialized with `data` and the given `usage` (mapped-at-creation).
    WGPUBuffer makeBufferWithContents(const void* data, size_t size, WGPUBufferUsage usage);
    /// Reads back `src` and asserts the `size` bytes from `srcByteOffset` equal `expected`.
    void expectGPUBufferValuesEqual(
        WGPUBuffer src,
        const void* expected,
        size_t size,
        uint64_t srcByteOffset = 0);
    /// Reads back `byteLength` bytes of `src` and runs `check`; a returned string fails the case.
    void expectGPUBufferValuesPassCheck(
        WGPUBuffer src,
        const std::function<std::optional<std::string>(const uint8_t* actual, size_t len)>& check,
        uint64_t srcByteOffset,
        size_t byteLength);
    /// Reads back `buffer` and compares it to `expected` interpreted as texels of `format` over `size`.
    void expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
        const uint8_t* expected,
        size_t expectedLen,
        WGPUBuffer buffer,
        WGPUTextureFormat format,
        WGPUExtent3D size,
        TexelCopyBufferLayout dataLayout);
    /// Writes `data` into `buffer` at `bufferOffset` via the queue.
    void queueWriteBuffer(WGPUBuffer buffer, uint64_t bufferOffset, const void* data, size_t size);
    /// Writes `data` into a region of texture `dst` via the queue.
    void queueWriteTexture(
        WGPUTexture dst,
        WGPUExtent3D copySize,
        const WGPUTexelCopyBufferLayout& layout,
        const void* data,
        size_t size,
        uint32_t mipLevel = 0,
        WGPUOrigin3D origin = WGPUOrigin3D{0, 0, 0});
    /// Encodes a buffer→texture copy on `encoder`.
    void copyBufferToTexture(
        WGPUCommandEncoder encoder,
        WGPUBuffer src,
        uint32_t bytesPerRow,
        WGPUTexture dst,
        WGPUExtent3D size);
    /// Encodes a texture→buffer copy on `encoder`.
    void copyTextureToBuffer(
        WGPUCommandEncoder encoder,
        WGPUTexture src,
        WGPUBuffer dst,
        uint32_t bytesPerRow,
        WGPUExtent3D size);
    /// Encodes a texture→texture copy on `encoder`.
    void copyTextureToTexture(
        WGPUCommandEncoder encoder,
        WGPUTexture src,
        WGPUTexture dst,
        WGPUExtent3D size);
    /// Submits no work and blocks until the queue reports all prior work done.
    void onSubmittedWorkDoneSync();
    /// Issues `n` onSubmittedWorkDone callbacks; if `checkOrder`, asserts they fire in order.
    void onSubmittedWorkDoneMany(uint32_t n, bool checkOrder);
    /// Creates a buffer and tracks it for automatic release in `finalize`.
    WGPUBuffer createBufferTracked(const WGPUBufferDescriptor& desc);
    /// Creates a buffer on the mismatched device (tracked separately).
    WGPUBuffer createBufferOnMismatchedDevice(const WGPUBufferDescriptor& desc);
    /// Creates a buffer and puts it into the requested lifecycle state (valid/invalid/destroyed).
    WGPUBuffer createBufferWithState(ResourceState state, const WGPUBufferDescriptor& desc);
    /// Returns a deliberately-invalid (error) buffer for validation tests.
    WGPUBuffer getErrorBuffer();
    /// Creates a sampler and tracks it for release.
    WGPUSampler createSamplerTracked(const WGPUSamplerDescriptor& desc);
    /// Creates a texture and tracks it for release.
    WGPUTexture createTextureTracked(const WGPUTextureDescriptor& desc);
    /// Creates a texture and puts it into the requested lifecycle state.
    WGPUTexture createTextureWithState(ResourceState state, const WGPUTextureDescriptor& desc);
    /// Creates a texture view and tracks it for release.
    WGPUTextureView createViewTracked(WGPUTexture texture, const WGPUTextureViewDescriptor& desc);
    /// Creates a shader module from WGSL and tracks it for release.
    WGPUShaderModule createShaderModuleTracked(std::string_view wgsl);
    /// Creates a bind-group layout and tracks it for release.
    WGPUBindGroupLayout createBindGroupLayoutTracked(const WGPUBindGroupLayoutDescriptor& desc);
    /// Creates a bind-group layout on the mismatched device (tracked separately).
    WGPUBindGroupLayout createBindGroupLayoutOnMismatchedDevice(const WGPUBindGroupLayoutDescriptor& desc);
    /// Creates a bind group and tracks it for release.
    WGPUBindGroup createBindGroupTracked(const WGPUBindGroupDescriptor& desc);
    /// Creates a pipeline layout and tracks it for release.
    WGPUPipelineLayout createPipelineLayoutTracked(const WGPUPipelineLayoutDescriptor& desc);
    /// Creates a render pipeline and tracks it for release.
    WGPURenderPipeline createRenderPipelineTracked(const WGPURenderPipelineDescriptor& desc);
    /// Creates a compute pipeline and tracks it for release.
    WGPUComputePipeline createComputePipelineTracked(const WGPUComputePipelineDescriptor& desc);
    /// Creates a command encoder and tracks it for release.
    WGPUCommandEncoder createCommandEncoderTracked();
    /// Finishes `encoder` into a command buffer and tracks it for release.
    WGPUCommandBuffer finishTracked(WGPUCommandEncoder encoder);
    /// Runs `body` inside an error scope and asserts an error did/didn't occur per `shouldError`.
    void expectValidationError(const std::function<void()>& body, bool shouldError);
    /// Maps `buffer` and asserts the map succeeds or fails per `expectSuccess`.
    void expectMapAsync(WGPUBuffer buffer,
                        WGPUMapMode mode,
                        bool expectSuccess,
                        size_t offset = 0,
                        size_t size = WGPU_WHOLE_MAP_SIZE);
    /// Skips the case if the device does not support transient (memoryless) attachments.
    void skipIfTransientAttachmentNotSupported();
    /// Skips the case if `format` is not supported by the device.
    void skipIfTextureFormatNotSupported(WGPUTextureFormat format);
    /// Skips the case if `format` cannot be used with the given texture `dimension`.
    void skipIfTextureFormatAndDimensionNotCompatible(WGPUTextureFormat format, WGPUTextureDimension dimension);
    /// Skips the case if the view `dimension` is not supported.
    void skipIfTextureViewDimensionNotSupported(WGPUTextureViewDimension dimension);
    /// Skips the case if `format` cannot be a render attachment.
    void skipIfTextureFormatNotUsableAsRenderAttachment(WGPUTextureFormat format);
    /// Skips the case if `format` does not support the requested `usage`.
    void skipIfTextureFormatDoesNotSupportUsage(WGPUTextureUsage usage, WGPUTextureFormat format);
    /// True if `dimension`+`format` form a device-supported combination.
    bool textureDimensionAndFormatCompatibleForDevice(WGPUTextureDimension dimension, WGPUTextureFormat format);
    /// True if `format` can be used as a color render target.
    bool isTextureFormatColorRenderable(WGPUTextureFormat format);
    /// True if `format` can be used as a render attachment (color or depth/stencil).
    bool isTextureFormatUsableAsRenderAttachment(WGPUTextureFormat format);
    /// True if `format` supports write-only storage-texture access.
    bool isTextureFormatUsableAsWriteOnlyStorageTexture(WGPUTextureFormat format);
    /// True if `format` supports read-only storage-texture access.
    bool isTextureFormatUsableAsReadOnlyStorageTexture(WGPUTextureFormat format);
    /// True if `format` supports read-write storage-texture access.
    bool isTextureFormatUsableAsReadWriteStorageTexture(WGPUTextureFormat format);
    /// True if `format` supports the given storage `access` mode.
    bool isTextureFormatUsableWithStorageAccessMode(WGPUTextureFormat format, WGPUStorageTextureAccess access);
    /// True if `format` can be used with sample counts greater than one.
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

/// GpuTest variant that requests a device with every available feature enabled and
/// all limits raised to their maximum, for tests that need the fullest capability set.
class AllFeaturesMaxLimitsGpuTest : public GpuTest {
  public:
    void init() override;
    WGPUDevice device() const override;
    WGPUQueue queue() const override;
};

} // namespace cts
