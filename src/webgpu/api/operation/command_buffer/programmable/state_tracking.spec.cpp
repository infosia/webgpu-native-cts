// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/programmable/state_tracking.spec.ts @ d116bcccd18
// (with the ProgrammableStateTest helper from programmable_state_test.ts).
// Ports all 6 upstream tests over every programmable encoder type (compute pass, render pass,
// render bundle).
//
// Porting notes:
// - skipIfNeedsStorageBuffersInFragmentStageAndHaveNone only acts in compatibility mode, which this
//   port never runs, so it is a no-op here.
// - Bind-group layouts and pipelines depend only on (encoder type, group indices, binding type,
//   algorithm), so they are cached per device instead of per test instance.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

const WGPUBufferUsage kBufferUsage =
    WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform;

// kProgrammableEncoderTypes = ['compute pass', ...kRenderEncodeTypes]
std::vector<Value> programmableEncoderTypes() {
    return {Value("compute pass"), Value("render pass"), Value("render bundle")};
}

struct BindGroupIndices {
    uint32_t a = 0;
    uint32_t b = 1;
    uint32_t out = 2;
};

// A pipeline of either kind, as returned by createBindingStatePipeline.
struct StatePipeline {
    WGPUComputePipeline compute = nullptr;
    WGPURenderPipeline render = nullptr;
};

// Per-device cache of the helper bind-group layouts and pipelines.
struct ProgrammableStateCache : DeviceScopedObject {
    struct PerDevice {
        std::map<std::string, WGPUBindGroupLayout> bindGroupLayouts;
        std::map<std::string, StatePipeline> pipelines;
    };
    std::unordered_map<WGPUDevice, PerDevice> byDevice;

    ~ProgrammableStateCache() override {
        for (auto& [device, perDevice] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : perDevice.pipelines) {
                (void)key;
                if (pipeline.compute != nullptr) {
                    wgpuComputePipelineRelease(pipeline.compute);
                }
                if (pipeline.render != nullptr) {
                    wgpuRenderPipelineRelease(pipeline.render);
                }
            }
            for (auto& [key, layout] : perDevice.bindGroupLayouts) {
                (void)key;
                wgpuBindGroupLayoutRelease(layout);
            }
        }
    }
};

class ProgrammableStateTest : public AllFeaturesMaxLimitsGpuTest {
  public:
    // Upstream skipIfNeedsStorageBuffersInFragmentStageAndHaveNone: returns early unless
    // isCompatibility; this port only runs core mode.
    void skipIfNeedsStorageBuffersInFragmentStageAndHaveNone(std::string_view /*type*/,
                                                            std::string_view /*encoderType*/) {}

    static WGPUShaderStage visibilityForEncoderType(std::string_view encoderType) {
        return encoderType == "compute pass" ? WGPUShaderStage_Compute : WGPUShaderStage_Fragment;
    }

    WGPUBindGroupLayout getBindGroupLayout(WGPUBufferBindingType type, WGPUShaderStage visibility) {
        auto& perDevice = deviceScoped<ProgrammableStateCache>().byDevice[device()];
        const std::string id = std::to_string(static_cast<uint32_t>(type)) + ":" +
                               std::to_string(static_cast<uint64_t>(visibility));
        auto it = perDevice.bindGroupLayouts.find(id);
        if (it != perDevice.bindGroupLayouts.end()) {
            return it->second;
        }
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = visibility;
        entry.buffer.type = type;
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;
        WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device(), &desc);
        perDevice.bindGroupLayouts.emplace(id, layout);
        return layout;
    }

    // Holes (an unset slot) are null entries, as upstream's sparse JS array.
    std::vector<WGPUBindGroupLayout> getBindGroupLayouts(const BindGroupIndices& indices,
                                                         std::string_view type,
                                                         std::string_view encoderType) {
        std::vector<WGPUBindGroupLayout> layouts;
        auto setAt = [&](uint32_t index, WGPUBindGroupLayout layout) {
            if (layouts.size() <= index) {
                layouts.resize(index + 1, nullptr);
            }
            layouts[index] = layout;
        };
        const WGPUBufferBindingType inputType =
            type == "storage" ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Uniform;
        const WGPUShaderStage visibility = visibilityForEncoderType(encoderType);
        setAt(indices.a, getBindGroupLayout(inputType, visibility));
        setAt(indices.b, getBindGroupLayout(inputType, visibility));
        if (type == "storage" || encoderType == "compute pass") {
            setAt(indices.out, getBindGroupLayout(WGPUBufferBindingType_Storage, visibility));
        }
        return layouts;
    }

    WGPUBindGroup createBindGroup(WGPUBuffer buffer, WGPUBufferBindingType type, std::string_view encoderType) {
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = buffer;
        entry.offset = 0;
        entry.size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout = getBindGroupLayout(type, visibilityForEncoderType(encoderType));
        desc.entryCount = 1;
        desc.entries = &entry;
        return createBindGroupTracked(desc);
    }

    // Create a pipeline that performs an operation on data from two bind groups, then writes the
    // result to a third bind group (or, for uniform render pipelines, to the color target).
    StatePipeline createBindingStatePipeline(std::string_view encoderType,
                                             const BindGroupIndices& groups,
                                             std::string_view type,
                                             std::string_view algorithm) {
        auto& perDevice = deviceScoped<ProgrammableStateCache>().byDevice[device()];
        const std::string key = std::string(encoderType) + "|" + std::to_string(groups.a) + "," +
                                std::to_string(groups.b) + "," + std::to_string(groups.out) + "|" +
                                std::string(type) + "|" + std::string(algorithm);
        auto it = perDevice.pipelines.find(key);
        if (it != perDevice.pipelines.end()) {
            return it->second;
        }

        const std::vector<WGPUBindGroupLayout> layouts = getBindGroupLayouts(groups, type, encoderType);
        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = layouts.size();
        layoutDesc.bindGroupLayouts = layouts.data();
        WGPUPipelineLayout layout = createPipelineLayoutTracked(layoutDesc);

        // WGSL address-space spelling of the input type: 'storage' or 'uniform'.
        const std::string varType(type);
        const std::string decls =
            "struct Data {\n  value : i32\n};\n"
            "@group(" + std::to_string(groups.a) + ") @binding(0) var<" + varType + "> a : Data;\n"
            "@group(" + std::to_string(groups.b) + ") @binding(0) var<" + varType + "> b : Data;\n"
            "@group(" + std::to_string(groups.out) + ") @binding(0) var<storage, read_write> out : Data;\n";

        StatePipeline pipeline;
        if (encoderType == "compute pass") {
            const std::string wgsl = decls +
                "@compute @workgroup_size(1) fn main() {\n"
                "  out.value = " + std::string(algorithm) + ";\n"
                "  return;\n"
                "}\n";
            WGPUShaderModule module = createShaderModuleTracked(wgsl);
            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.layout = layout;
            desc.compute.module = module;
            desc.compute.entryPoint = stringView("main");
            pipeline.compute = wgpuDeviceCreateComputePipeline(device(), &desc);
        } else {
            const std::string vertexWgsl =
                "@vertex fn vert_main() -> @builtin(position) vec4<f32> {\n"
                "  return vec4<f32>(0, 0, 0, 1);\n"
                "}\n";
            const std::string fragmentWgsl = decls +
                "@fragment fn frag_main_storage() -> @location(0) vec4<i32> {\n"
                "  out.value = " + std::string(algorithm) + ";\n"
                "  return vec4<i32>(1, 0, 0, 1);\n"
                "}\n"
                "@fragment fn frag_main_uniform() -> @location(0) vec4<i32> {\n"
                "  return vec4<i32>(" + std::string(algorithm) + ");\n"
                "}\n";
            WGPUShaderModule vertexModule = createShaderModuleTracked(vertexWgsl);
            WGPUShaderModule fragmentModule = createShaderModuleTracked(fragmentWgsl);

            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            target.format = WGPUTextureFormat_R32Sint;
            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            fragment.module = fragmentModule;
            fragment.entryPoint = stringView(type == "uniform" ? "frag_main_uniform" : "frag_main_storage");
            fragment.targetCount = 1;
            fragment.targets = &target;

            WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            desc.layout = layout;
            desc.vertex.module = vertexModule;
            desc.vertex.entryPoint = stringView("vert_main");
            desc.fragment = &fragment;
            desc.primitive.topology = WGPUPrimitiveTopology_PointList;
            pipeline.render = wgpuDeviceCreateRenderPipeline(device(), &desc);
        }
        perDevice.pipelines.emplace(key, pipeline);
        return pipeline;
    }
};

// Upstream createEncoder(encoderType, { attachmentInfo: { colorFormats: ['r32sint'] }, targets })
// plus the createEncoderForStateTest readback for uniform render encoders.
class StateEncoder {
  public:
    // `target` null means upstream's createEncoder without `targets`: a fresh 16x16 attachment.
    StateEncoder(ProgrammableStateTest& t, std::string encoderType, WGPUTextureView target)
        : t_(t), encoderType_(std::move(encoderType)) {
        commandEncoder_ = t_.createCommandEncoderTracked();
        if (encoderType_ == "compute pass") {
            WGPUComputePassDescriptor desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            compute_ = t_.beginComputePassTracked(commandEncoder_, &desc);
            return;
        }

        if (target == nullptr) {
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.size = WGPUExtent3D{16, 16, 1};
            desc.format = WGPUTextureFormat_R32Sint;
            desc.usage = WGPUTextureUsage_RenderAttachment;
            desc.sampleCount = 1;
            WGPUTexture texture = t_.createTextureTracked(desc);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            target = t_.createViewTracked(texture, viewDesc);
        }
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = target;
        color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;
        render_ = t_.beginRenderPassTracked(commandEncoder_, &passDesc);

        if (encoderType_ == "render bundle") {
            const WGPUTextureFormat format = WGPUTextureFormat_R32Sint;
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = 1;
            desc.colorFormats = &format;
            desc.sampleCount = 1;
            bundle_ = t_.createRenderBundleEncoderTracked(desc);
        }
    }

    void setPipeline(const StatePipeline& pipeline) {
        if (compute_ != nullptr) {
            wgpuComputePassEncoderSetPipeline(compute_, pipeline.compute);
        } else if (bundle_ != nullptr) {
            wgpuRenderBundleEncoderSetPipeline(bundle_, pipeline.render);
        } else {
            wgpuRenderPassEncoderSetPipeline(render_, pipeline.render);
        }
    }

    // `group` may be null (upstream passes null for the unused uniform-render `out` slot).
    void setBindGroup(uint32_t index, WGPUBindGroup group) {
        if (compute_ != nullptr) {
            wgpuComputePassEncoderSetBindGroup(compute_, index, group, 0, nullptr);
        } else if (bundle_ != nullptr) {
            wgpuRenderBundleEncoderSetBindGroup(bundle_, index, group, 0, nullptr);
        } else {
            wgpuRenderPassEncoderSetBindGroup(render_, index, group, 0, nullptr);
        }
    }

    void dispatchOrDraw() {
        if (compute_ != nullptr) {
            wgpuComputePassEncoderDispatchWorkgroups(compute_, 1, 1, 1);
        } else if (bundle_ != nullptr) {
            wgpuRenderBundleEncoderDraw(bundle_, 1, 1, 0, 0);
        } else {
            wgpuRenderPassEncoderDraw(render_, 1, 1, 0, 0);
        }
    }

    // CommandBufferMaker.validateFinishAndSubmit(true, true): finishing raises no validation error
    // and the command buffer is submitted.
    void validateFinishAndSubmit() {
        WGPUCommandBuffer commandBuffer = nullptr;
        t_.expectValidationError([&] {
            if (compute_ != nullptr) {
                wgpuComputePassEncoderEnd(compute_);
            } else {
                if (bundle_ != nullptr) {
                    WGPURenderBundle bundle = t_.finishRenderBundleTracked(bundle_);
                    wgpuRenderPassEncoderExecuteBundles(render_, 1, &bundle);
                }
                wgpuRenderPassEncoderEnd(render_);
            }
            commandBuffer = t_.finishTracked(commandEncoder_);
        }, false);
        wgpuQueueSubmit(t_.queue(), 1, &commandBuffer);
    }

  private:
    ProgrammableStateTest& t_;
    std::string encoderType_;
    WGPUCommandEncoder commandEncoder_ = nullptr;
    WGPUComputePassEncoder compute_ = nullptr;
    WGPURenderPassEncoder render_ = nullptr;
    WGPURenderBundleEncoder bundle_ = nullptr;
};

// Upstream createEncoderForStateTest: a 1x1 r32sint render target; for uniform render encoders the
// fragment output lands there and is copied into `out` after submission.
class StateTestEncoder {
  public:
    StateTestEncoder(ProgrammableStateTest& t, std::string type, WGPUBuffer out, const std::string& encoderType)
        : t_(t), type_(std::move(type)), out_(out), encoderType_(encoderType) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = WGPUExtent3D{1, 1, 1};
        desc.format = WGPUTextureFormat_R32Sint;
        desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        desc.sampleCount = 1;
        renderTarget_ = t_.createTextureTracked(desc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        encoder_ = std::make_unique<StateEncoder>(t_, encoderType, t_.createViewTracked(renderTarget_, viewDesc));
    }

    StateEncoder& encoder() { return *encoder_; }

    void validateFinishAndSubmit() {
        encoder_->validateFinishAndSubmit();
        if (type_ == "uniform" && (encoderType_ == "render pass" || encoderType_ == "render bundle")) {
            WGPUCommandEncoder encoder = t_.createCommandEncoderTracked();
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = renderTarget_;
            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer = out_;
            WGPUExtent3D size = {1, 1, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &size);
            WGPUCommandBuffer commandBuffer = t_.finishTracked(encoder);
            wgpuQueueSubmit(t_.queue(), 1, &commandBuffer);
        }
    }

  private:
    ProgrammableStateTest& t_;
    std::string type_;
    WGPUBuffer out_;
    std::string encoderType_;
    WGPUTexture renderTarget_ = nullptr;
    std::unique_ptr<StateEncoder> encoder_;
};

WGPUBuffer makeI32Buffer(ProgrammableStateTest& t, int32_t value) {
    return t.makeBufferWithContents(&value, sizeof(value), kBufferUsage);
}

WGPUBufferBindingType inputBindingType(std::string_view type) {
    return type == "storage" ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Uniform;
}

// The `out` bind group, or null for a uniform render encoder (upstream:
// `encoderType === 'compute pass' || type === 'storage' ? createBindGroup(...) : null`).
WGPUBindGroup outBindGroup(ProgrammableStateTest& t,
                           WGPUBuffer out,
                           std::string_view type,
                           std::string_view encoderType) {
    if (encoderType == "compute pass" || type == "storage") {
        return t.createBindGroup(out, WGPUBufferBindingType_Storage, encoderType);
    }
    return nullptr;
}

void expectI32(ProgrammableStateTest& t, WGPUBuffer buffer, int32_t expected) {
    t.expectGPUBufferValuesEqual(buffer, &expected, sizeof(expected));
}

// Bind group names used by bind_group_order / bind_group_before_pipeline.
struct NamedBindGroups {
    WGPUBindGroup a = nullptr;
    WGPUBindGroup b = nullptr;
    WGPUBindGroup out = nullptr;

    WGPUBindGroup get(char name) const {
        return name == 'a' ? a : (name == 'b' ? b : out);
    }
};

uint32_t indexFor(const BindGroupIndices& indices, char name) {
    return name == 'a' ? indices.a : (name == 'b' ? indices.b : indices.out);
}

// Parses a comma-separated list of binding names ("a,b,out") into first letters ('a', 'b', 'o').
std::vector<char> parseNames(const std::string& list) {
    std::vector<char> names;
    size_t start = 0;
    while (start < list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        if (end > start) {
            names.push_back(list[start]);
        }
        start = end + 1;
    }
    return names;
}

TestGroup<ProgrammableStateTest> g = MakeTestGroup<ProgrammableStateTest>(
    "api,operation,command_buffer,programmable,state_tracking",
    R"(
Ensure state is set correctly. Tries to stress state caching (setting different states multiple
times in different orders) for setBindGroup and setPipeline.
)");

CTS_TEST(g, "bind_group_indices")
    .desc(R"(
    Test that bind group indices can be declared in any order, regardless of their order in the shader.
    - Test places the value of buffer a - buffer b into the out buffer, then reads the result.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .beginSubcases()
            .combine("type", {"storage", "uniform"})
            .combine("groupIndices", {"0,1,2", "1,2,0", "2,0,1", "0,2,1", "2,1,0", "1,0,2"});
    })
    .fn([](ProgrammableStateTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string type = t.param<std::string>("type");
        const std::string indicesStr = t.param<std::string>("groupIndices");
        BindGroupIndices groupIndices;
        groupIndices.a = static_cast<uint32_t>(indicesStr[0] - '0');
        groupIndices.b = static_cast<uint32_t>(indicesStr[2] - '0');
        groupIndices.out = static_cast<uint32_t>(indicesStr[4] - '0');
        t.skipIfNeedsStorageBuffersInFragmentStageAndHaveNone(type, encoderType);

        const StatePipeline pipeline =
            t.createBindingStatePipeline(encoderType, groupIndices, type, "a.value - b.value");

        const WGPUBufferBindingType inputType = inputBindingType(type);
        WGPUBuffer out = makeI32Buffer(t, 0);
        NamedBindGroups bindGroups;
        bindGroups.a = t.createBindGroup(makeI32Buffer(t, 3), inputType, encoderType);
        bindGroups.b = t.createBindGroup(makeI32Buffer(t, 2), inputType, encoderType);
        bindGroups.out = outBindGroup(t, out, type, encoderType);

        StateTestEncoder enc(t, type, out, encoderType);
        enc.encoder().setPipeline(pipeline);
        enc.encoder().setBindGroup(groupIndices.a, bindGroups.a);
        enc.encoder().setBindGroup(groupIndices.b, bindGroups.b);
        enc.encoder().setBindGroup(groupIndices.out, bindGroups.out);
        enc.encoder().dispatchOrDraw();
        enc.validateFinishAndSubmit();

        expectI32(t, out, 1);
    });

CTS_TEST(g, "bind_group_order")
    .desc(R"(
    Test that the order in which you set the bind groups doesn't matter.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .beginSubcases()
            .combine("type", {"storage", "uniform"})
            .combine("setOrder", {"a,b,out", "b,out,a", "out,a,b", "b,a,out", "a,out,b", "out,b,a"});
    })
    .fn([](ProgrammableStateTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string type = t.param<std::string>("type");
        const std::vector<char> setOrder = parseNames(t.param<std::string>("setOrder"));
        t.skipIfNeedsStorageBuffersInFragmentStageAndHaveNone(type, encoderType);

        const BindGroupIndices groupIndices{0, 1, 2};
        const StatePipeline pipeline =
            t.createBindingStatePipeline(encoderType, groupIndices, type, "a.value - b.value");

        WGPUBuffer out = makeI32Buffer(t, 0);
        const WGPUBufferBindingType inputType = inputBindingType(type);
        NamedBindGroups bindGroups;
        bindGroups.a = t.createBindGroup(makeI32Buffer(t, 3), inputType, encoderType);
        bindGroups.b = t.createBindGroup(makeI32Buffer(t, 2), inputType, encoderType);
        bindGroups.out = outBindGroup(t, out, type, encoderType);

        StateTestEncoder enc(t, type, out, encoderType);
        enc.encoder().setPipeline(pipeline);
        for (char name : setOrder) {
            enc.encoder().setBindGroup(indexFor(groupIndices, name), bindGroups.get(name));
        }
        enc.encoder().dispatchOrDraw();
        enc.validateFinishAndSubmit();

        expectI32(t, out, 1);
    });

CTS_TEST(g, "bind_group_before_pipeline")
    .desc(R"(
    Test that setting bind groups prior to setting the pipeline is still valid.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .beginSubcases()
            .combine("type", {"storage", "uniform"})
            .combineWithParams({
                ParamRecord{{"setBefore", "a,b"}, {"setAfter", "out"}},
                ParamRecord{{"setBefore", "a"}, {"setAfter", "b,out"}},
                ParamRecord{{"setBefore", "out,b"}, {"setAfter", "a"}},
                ParamRecord{{"setBefore", "a,b,out"}, {"setAfter", ""}},
            });
    })
    .fn([](ProgrammableStateTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string type = t.param<std::string>("type");
        const std::vector<char> setBefore = parseNames(t.param<std::string>("setBefore"));
        const std::vector<char> setAfter = parseNames(t.param<std::string>("setAfter"));
        t.skipIfNeedsStorageBuffersInFragmentStageAndHaveNone(type, encoderType);

        const BindGroupIndices groupIndices{0, 1, 2};
        const StatePipeline pipeline =
            t.createBindingStatePipeline(encoderType, groupIndices, type, "a.value - b.value");

        WGPUBuffer out = makeI32Buffer(t, 0);
        const WGPUBufferBindingType inputType = inputBindingType(type);
        NamedBindGroups bindGroups;
        bindGroups.a = t.createBindGroup(makeI32Buffer(t, 3), inputType, encoderType);
        bindGroups.b = t.createBindGroup(makeI32Buffer(t, 2), inputType, encoderType);
        bindGroups.out = outBindGroup(t, out, type, encoderType);

        StateTestEncoder enc(t, type, out, encoderType);
        for (char name : setBefore) {
            enc.encoder().setBindGroup(indexFor(groupIndices, name), bindGroups.get(name));
        }
        enc.encoder().setPipeline(pipeline);
        for (char name : setAfter) {
            enc.encoder().setBindGroup(indexFor(groupIndices, name), bindGroups.get(name));
        }
        enc.encoder().dispatchOrDraw();
        enc.validateFinishAndSubmit();

        expectI32(t, out, 1);
    });

CTS_TEST(g, "one_bind_group_multiple_slots")
    .desc(R"(
    Test that a single bind group may be bound to more than one slot.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .beginSubcases()
            .combine("type", {"storage", "uniform"});
    })
    .fn([](ProgrammableStateTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string type = t.param<std::string>("type");
        t.skipIfNeedsStorageBuffersInFragmentStageAndHaveNone(type, encoderType);

        const StatePipeline pipeline =
            t.createBindingStatePipeline(encoderType, BindGroupIndices{0, 1, 2}, type, "a.value - b.value");

        WGPUBuffer out = makeI32Buffer(t, 1);
        const WGPUBufferBindingType inputType = inputBindingType(type);
        WGPUBindGroup ab = t.createBindGroup(makeI32Buffer(t, 3), inputType, encoderType);
        WGPUBindGroup outGroup = outBindGroup(t, out, type, encoderType);

        StateTestEncoder enc(t, type, out, encoderType);
        enc.encoder().setPipeline(pipeline);
        enc.encoder().setBindGroup(0, ab);
        enc.encoder().setBindGroup(1, ab);
        enc.encoder().setBindGroup(2, outGroup);
        enc.encoder().dispatchOrDraw();
        enc.validateFinishAndSubmit();

        expectI32(t, out, 0);
    });

CTS_TEST(g, "bind_group_multiple_sets")
    .desc(R"(
    Test that the last bind group set to a given slot is used when dispatching.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .beginSubcases()
            .combine("type", {"storage", "uniform"});
    })
    .fn([](ProgrammableStateTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string type = t.param<std::string>("type");
        t.skipIfNeedsStorageBuffersInFragmentStageAndHaveNone(type, encoderType);

        const StatePipeline pipeline =
            t.createBindingStatePipeline(encoderType, BindGroupIndices{0, 1, 2}, type, "a.value - b.value");

        WGPUBuffer badOut = makeI32Buffer(t, -1);
        WGPUBuffer out = makeI32Buffer(t, 0);
        const WGPUBufferBindingType inputType = inputBindingType(type);
        WGPUBindGroup a = t.createBindGroup(makeI32Buffer(t, 3), inputType, encoderType);
        WGPUBindGroup b = t.createBindGroup(makeI32Buffer(t, 2), inputType, encoderType);
        WGPUBindGroup c = t.createBindGroup(makeI32Buffer(t, 5), inputType, encoderType);
        WGPUBindGroup badOutGroup = outBindGroup(t, badOut, type, encoderType);
        WGPUBindGroup outGroup = outBindGroup(t, out, type, encoderType);

        StateTestEncoder enc(t, type, out, encoderType);
        enc.encoder().setBindGroup(1, c);

        enc.encoder().setPipeline(pipeline);

        enc.encoder().setBindGroup(0, c);
        enc.encoder().setBindGroup(0, a);

        enc.encoder().setBindGroup(2, badOutGroup);

        enc.encoder().setBindGroup(1, b);
        enc.encoder().setBindGroup(2, outGroup);

        enc.encoder().dispatchOrDraw();
        enc.validateFinishAndSubmit();

        expectI32(t, out, 1);
        expectI32(t, badOut, -1);
    });

CTS_TEST(g, "compatible_pipelines")
    .desc("Test that bind groups can be shared between compatible pipelines.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes());
    })
    .fn([](ProgrammableStateTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        t.skipIfNeedsStorageBuffersInFragmentStageAndHaveNone("storage", encoderType);

        const StatePipeline pipelineA =
            t.createBindingStatePipeline(encoderType, BindGroupIndices{0, 1, 2}, "storage", "a.value - b.value");
        const StatePipeline pipelineB =
            t.createBindingStatePipeline(encoderType, BindGroupIndices{0, 1, 2}, "storage", "a.value + b.value");

        WGPUBuffer outA = makeI32Buffer(t, 0);
        WGPUBuffer outB = makeI32Buffer(t, 0);
        WGPUBindGroup a =
            t.createBindGroup(makeI32Buffer(t, 3), WGPUBufferBindingType_ReadOnlyStorage, encoderType);
        WGPUBindGroup b =
            t.createBindGroup(makeI32Buffer(t, 2), WGPUBufferBindingType_ReadOnlyStorage, encoderType);
        WGPUBindGroup outAGroup = t.createBindGroup(outA, WGPUBufferBindingType_Storage, encoderType);
        WGPUBindGroup outBGroup = t.createBindGroup(outB, WGPUBufferBindingType_Storage, encoderType);

        StateEncoder enc(t, encoderType, nullptr);
        enc.setBindGroup(0, a);
        enc.setBindGroup(1, b);

        enc.setPipeline(pipelineA);
        enc.setBindGroup(2, outAGroup);
        enc.dispatchOrDraw();

        enc.setPipeline(pipelineB);
        enc.setBindGroup(2, outBGroup);
        enc.dispatchOrDraw();

        enc.validateFinishAndSubmit();

        expectI32(t, outA, 1);
        expectI32(t, outB, 5);
    });

} // namespace
