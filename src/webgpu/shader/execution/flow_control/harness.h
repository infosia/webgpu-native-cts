// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/harness.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
//
// Header-only port of the flow-control test harness.
//
// runFlowControlTest() builds a WGSL compute shader from a templated body
// containing expect_order()/expect_not_reached() checkpoints, runs a single
// dispatch, and validates the recorded event order against the expectation
// chain, producing rich diagnostics on mismatch.
//
// Port notes (simplifications vs upstream):
// - Upstream captures a JS stack trace per expectation for diagnostics; here
//   each expectation records its 1-based id and its textual form (e.g.
//   "expect_order(2, 6)") instead.
// - Upstream colorizes diagnostics; here the offending event value is marked
//   with >>>value<<< in plain text.
// - JS template literals are replaced by either plain std::string
//   concatenation or the wgslTemplate() "%%" placeholder helper below.

#ifndef CTS_WEBGPU_SHADER_EXECUTION_FLOW_CONTROL_HARNESS_H_
#define CTS_WEBGPU_SHADER_EXECUTION_FLOW_CONTROL_HARNESS_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

namespace cts {

/**
 * Expands the "%%" placeholders in `tmpl`, in order, with the given
 * substitutions. This replaces upstream's JS template-literal interpolation:
 *
 *   runFlowControlTest(t, [](FlowControlTestBuilder& f) {
 *       return wgslTemplate(R"(
 *   %%
 *   if (%%) {
 *     %%
 *   } else {
 *     %%
 *   }
 *   %%
 * )",
 *           {f.expect_order(0), f.value(true), f.expect_order(1),
 *            f.expect_not_reached(), f.expect_order(2)});
 *   });
 *
 * Initializer-list elements are evaluated left-to-right, so builder calls
 * register in source order, matching upstream. Note: each placeholder must be
 * written exactly as two consecutive '%' characters; a lone '%' (e.g. the WGSL
 * modulo operator) is left untouched.
 *
 * Throws TestFailed if the number of placeholders does not match the number of
 * substitutions.
 */
inline std::string wgslTemplate(std::string_view tmpl, const std::vector<std::string>& substitutions) {
    std::string out;
    out.reserve(tmpl.size());
    size_t cursor = 0;
    size_t used = 0;
    for (;;) {
        size_t pos = tmpl.find("%%", cursor);
        if (pos == std::string_view::npos) {
            break;
        }
        out.append(tmpl.substr(cursor, pos - cursor));
        if (used >= substitutions.size()) {
            throw TestFailed(
                "wgslTemplate: template has more %% placeholders than substitutions (" +
                std::to_string(substitutions.size()) + ")");
        }
        out.append(substitutions[used]);
        used++;
        cursor = pos + 2;
    }
    out.append(tmpl.substr(cursor));
    if (used != substitutions.size()) {
        throw TestFailed(
            "wgslTemplate: template has " + std::to_string(used) + " %% placeholders but " +
            std::to_string(substitutions.size()) + " substitutions were provided");
    }
    return out;
}

inline std::string wgslTemplate(std::string_view tmpl, std::initializer_list<std::string> substitutions) {
    return wgslTemplate(tmpl, std::vector<std::string>(substitutions));
}

/**
 * The result of a runFlowControlTest() build callback: WGSL code placed inside
 * the entrypoint function, plus optional extra module-scope WGSL code.
 * Mirrors upstream's `{ entrypoint: string; extra: string }` return shape.
 */
struct FlowControlWgsl {
    std::string entrypoint;
    std::string extra;
};

/**
 * The builder interface for the runFlowControlTest() callback.
 * This interface is intended to be used to inject WGSL logic into the test
 * shader.
 * @see runFlowControlTest
 */
class FlowControlTestBuilder {
  public:
    /** One recorded expectation (used internally by runFlowControlTest). */
    struct Expectation {
        enum class Kind {
            Events,
            NotReached,
        };
        Kind kind = Kind::Events;
        // Textual form for diagnostics, e.g. "expect_order(2, 6)".
        std::string description;
        // Kind::Events only: the expected chronological events, and how many
        // have been matched so far during validation.
        std::vector<uint32_t> values;
        size_t counter = 0;
    };

    explicit FlowControlTestBuilder(bool preventValueOptimizations)
        : preventValueOptimizations_(preventValueOptimizations) {}

    /**
     * Emits a value into the shader.
     * If the test's "preventValueOptimizations" param is enabled, then value()
     * emits an expression to load the given value from a storage buffer,
     * preventing the shader compiler from knowing the value at compile time.
     * This can prevent constant folding, loop unrolling, dead-code
     * optimizations etc, which could all affect the tests.
     */
    std::string value(bool v) {
        if (preventValueOptimizations_) {
            inputData_.push_back(v ? 1 : 0);
            return "inputs[" + std::to_string(inputData_.size() - 1) + "] != 0";
        }
        return v ? "true" : "false";
    }

    /** @copydoc value(bool) */
    std::string value(int v) {
        if (preventValueOptimizations_) {
            inputData_.push_back(static_cast<int32_t>(v));
            return "inputs[" + std::to_string(inputData_.size() - 1) + "]";
        }
        return std::to_string(v);
    }

    /**
     * Emits an expectation that the statement will be executed at the given
     * chronological events.
     * @param events one or more chronological events, the first being 0.
     */
    template <class... Events>
    std::string expect_order(Events... events) {
        static_assert(sizeof...(Events) > 0, "expect_order() requires at least one event");
        return expectOrderImpl(std::vector<uint32_t>{static_cast<uint32_t>(events)...});
    }

    /**
     * Emits an expectation that the statement will not be reached.
     */
    std::string expect_not_reached() {
        Expectation expectation;
        expectation.kind = Expectation::Kind::NotReached;
        expectation.description = "expect_not_reached()";
        expectations_.push_back(std::move(expectation));
        // Expectation id starts from 1 to distinguish from initialization 0.
        return "push_output(" + std::to_string(expectations_.size()) + "); // expect_not_reached()";
    }

    // Accessors used by runFlowControlTest().
    const std::vector<int32_t>& inputData() const {
        return inputData_;
    }
    const std::vector<Expectation>& expectations() const {
        return expectations_;
    }

  private:
    std::string expectOrderImpl(std::vector<uint32_t> values) {
        std::string args;
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) {
                args += ", ";
            }
            args += std::to_string(values[i]);
        }
        Expectation expectation;
        expectation.kind = Expectation::Kind::Events;
        expectation.description = "expect_order(" + args + ")";
        expectation.values = std::move(values);
        expectations_.push_back(std::move(expectation));
        // Expectation id starts from 1 to distinguish from initialization 0.
        return "push_output(" + std::to_string(expectations_.size()) + "); // expect_order(" + args + ")";
    }

    bool preventValueOptimizations_ = false;
    std::vector<int32_t> inputData_;
    std::vector<Expectation> expectations_;
};

namespace flow_control_detail {

inline constexpr uint32_t kMaxOutputValues = 1000;

inline WGPUStringView toStringView(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

// Returns a string of the expect_order() call, marking the event value at
// err_idx (the one that caused an error) with >>> <<<.
// Plain-text port of upstream's colorized expect_order_err().
inline std::string expectOrderErr(const FlowControlTestBuilder::Expectation& expectation, size_t errIdx) {
    std::string out = "expect_order(";
    for (size_t i = 0; i < expectation.values.size(); i++) {
        if (i > 0) {
            out += ", ";
        }
        if (i == errIdx) {
            out += ">>>" + std::to_string(expectation.values[i]) + "<<<";
        } else {
            out += std::to_string(expectation.values[i]);
        }
    }
    out += ")";
    return out;
}

// Validates the readback words ([0] = count, [1..] = event values) against the
// expectations. Returns an error message on mismatch, std::nullopt on success.
inline std::optional<std::string> checkOutputs(
    std::vector<FlowControlTestBuilder::Expectation> expectations,
    const std::vector<uint32_t>& words,
    const std::string& wgsl) {
    // words[0]     is the number of outputted values
    // words[1..N]  holds the outputted values
    const uint32_t outputCount = words[0];
    if (outputCount > kMaxOutputValues) {
        return "output data count (" + std::to_string(outputCount) + ") exceeds limit of " +
               std::to_string(kMaxOutputValues);
    }

    // Returns an error message with the WGSL source appended.
    auto failMsg = [&wgsl](const std::string& err) -> std::optional<std::string> {
        return err + "\nWGSL:\n" + wgsl;
    };

    // Returns a string that shows the outputted values to help understand the whole trace.
    auto printOutputValue = [&words, outputCount]() {
        std::string out = "Output values (length: " + std::to_string(outputCount) + "): ";
        for (uint32_t i = 0; i < outputCount; i++) {
            if (i > 0) {
                out += ", ";
            }
            out += std::to_string(words[1 + i]);
        }
        return out;
    };

    // Identifies an expectation in diagnostics (stands in for upstream's JS stack trace).
    auto origin = [](size_t expectationIndex, const FlowControlTestBuilder::Expectation& expectation) {
        return "expectation #" + std::to_string(expectationIndex + 1) + ": " + expectation.description;
    };

    // Each of the outputted values represents an event.
    // Check that each event is as expected.
    for (uint32_t event = 0; event < outputCount; event++) {
        const uint32_t eventValue = words[1 + event]; // words[0] is count
        // Expectation id starts from 1, and 0 is invalid value.
        if (eventValue == 0) {
            return failMsg(
                "outputs.data[" + std::to_string(event) +
                "] is initial value 0, doesn't refer to any valid expectations\n" + printOutputValue());
        }
        const size_t expectationIndex = static_cast<size_t>(eventValue) - 1;
        if (expectationIndex >= expectations.size()) {
            return failMsg(
                "outputs.data[" + std::to_string(event) + "] value (" + std::to_string(expectationIndex) +
                ") exceeds number of expectations (" + std::to_string(expectations.size()) + ")\n" +
                printOutputValue());
        }
        FlowControlTestBuilder::Expectation& expectation = expectations[expectationIndex];
        switch (expectation.kind) {
            case FlowControlTestBuilder::Expectation::Kind::NotReached:
                return failMsg(
                    "expect_not_reached() reached at event " + std::to_string(event) + "\n" +
                    printOutputValue() + "\n" + origin(expectationIndex, expectation));
            case FlowControlTestBuilder::Expectation::Kind::Events:
                if (expectation.counter >= expectation.values.size()) {
                    return failMsg(
                        expectOrderErr(expectation, expectation.counter) +
                        " unexpectedly reached at event >>>" + std::to_string(event) + "<<<\n" +
                        printOutputValue() + "\n" + origin(expectationIndex, expectation));
                }
                if (event != expectation.values[expectation.counter]) {
                    return failMsg(
                        expectOrderErr(expectation, expectation.counter) + " expected event " +
                        std::to_string(expectation.values[expectation.counter]) + ", got " +
                        std::to_string(event) + "\n" + printOutputValue() + "\n" +
                        origin(expectationIndex, expectation));
                }
                expectation.counter++;
                break;
        }
    }

    // Finally check that all expect_order() calls were reached.
    for (size_t i = 0; i < expectations.size(); i++) {
        const FlowControlTestBuilder::Expectation& expectation = expectations[i];
        if (expectation.kind == FlowControlTestBuilder::Expectation::Kind::Events &&
            expectation.counter != expectation.values.size()) {
            return failMsg(
                expectOrderErr(expectation, expectation.counter) + " event " +
                std::to_string(expectation.values[expectation.counter]) + " was not reached\n" +
                origin(i, expectation) + "\n" + printOutputValue());
        }
    }
    return std::nullopt;
}

} // namespace flow_control_detail

/**
 * Builds, runs then checks the output of a flow control shader test.
 *
 * `build_wgsl` is a function that's called to build the WGSL shader.
 * This function takes a FlowControlTestBuilder& as the single argument, and
 * returns either a std::string which is embedded into the WGSL entrypoint
 * function, or a FlowControlWgsl which contains the entrypoint code along with
 * additional module-scope code.
 *
 * The FlowControlTestBuilder should be used to insert expectations into WGSL
 * to validate control flow. FlowControlTestBuilder also can be used to add
 * values to the shader which cannot be optimized away.
 *
 * Example, testing that an if-statement behaves as expected:
 *
 *   runFlowControlTest(t, [](FlowControlTestBuilder& f) {
 *       return wgslTemplate(R"(
 *   %%
 *   if (%%) {
 *     %%
 *   } else {
 *     %%
 *   }
 *   %%
 * )",
 *           {f.expect_order(0), f.value(true), f.expect_order(1),
 *            f.expect_not_reached(), f.expect_order(2)});
 *   });
 *
 * If the test's "preventValueOptimizations" param is present and true, the
 * value() calls are routed through a storage buffer (see FlowControlTestBuilder).
 *
 * @param t The test fixture (GpuTest or a subclass such as AllFeaturesMaxLimitsGpuTest)
 * @param build_wgsl The shader builder function.
 */
inline void runFlowControlTest(
    GpuTest& t,
    const std::function<FlowControlWgsl(FlowControlTestBuilder&)>& build_wgsl) {
    const bool preventValueOptimizations =
        t.hasParam("preventValueOptimizations") && t.param<bool>("preventValueOptimizations");

    FlowControlTestBuilder builder(preventValueOptimizations);
    const FlowControlWgsl built = build_wgsl(builder);

    const std::string wgsl = std::string(R"(
struct Outputs {
  count : u32,
  data  : array<u32>,
};
@group(0) @binding(0) var<storage, read>       inputs  : array<i32>;
@group(0) @binding(1) var<storage, read_write> outputs : Outputs;

fn push_output(value : u32) {
  outputs.data[outputs.count] = value;
  outputs.count++;
}

@compute @workgroup_size(1)
fn main() {
  _ = &inputs;
  _ = &outputs;
  )") + built.entrypoint +
                             "\n}\n" + built.extra + "\n";

    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // 'auto' layout
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = flow_control_detail::toStringView("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    // If there are no inputs, just put a single value in the buffer to keep
    // makeBufferWithContents() happy.
    std::vector<int32_t> inputData = builder.inputData();
    if (inputData.empty()) {
        inputData.push_back(0);
    }
    const uint64_t inputBufferSize = static_cast<uint64_t>(inputData.size()) * sizeof(int32_t);
    WGPUBuffer inputBuffer = t.makeBufferWithContents(
        inputData.data(), static_cast<size_t>(inputBufferSize), WGPUBufferUsage_Storage);

    // Output buffer: count word + up to kMaxOutputValues event words.
    // Zero-initialized by WebGPU buffer creation (never pre-filled with
    // expected values); 0 is the sentinel "no event recorded" value.
    const uint64_t outputBufferSize = 4ull * (1 + flow_control_detail::kMaxOutputValues);
    WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputBufferDesc.size = outputBufferSize;
    outputBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputBufferDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    std::array<WGPUBindGroupEntry, 2> entries;
    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].buffer = inputBuffer;
    entries[0].offset = 0;
    entries[0].size = inputBufferSize;
    entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].buffer = outputBuffer;
    entries[1].offset = 0;
    entries[1].size = outputBufferSize;

    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = bgl;
    bindGroupDesc.entryCount = entries.size();
    bindGroupDesc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Run the shader.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    // Read back and validate the recorded events.
    const std::vector<FlowControlTestBuilder::Expectation> expectations = builder.expectations();
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [expectations, wgsl](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                return std::string("readback too small: ") + std::to_string(len) + " bytes";
            }
            std::vector<uint32_t> words(len / 4, 0u);
            std::memcpy(words.data(), actual, words.size() * 4);
            return flow_control_detail::checkOutputs(expectations, words, wgsl);
        },
        0,
        static_cast<size_t>(outputBufferSize));
}

/**
 * Overload of runFlowControlTest() for build callbacks that return only
 * entrypoint code (no extra module-scope WGSL), mirroring upstream's
 * string-returning callback form.
 */
inline void runFlowControlTest(
    GpuTest& t,
    const std::function<std::string(FlowControlTestBuilder&)>& build_wgsl) {
    runFlowControlTest(
        t,
        std::function<FlowControlWgsl(FlowControlTestBuilder&)>(
            [&build_wgsl](FlowControlTestBuilder& fcBuilder) {
                return FlowControlWgsl{build_wgsl(fcBuilder), std::string()};
            }));
}

} // namespace cts

#endif // CTS_WEBGPU_SHADER_EXECUTION_FLOW_CONTROL_HARNESS_H_
