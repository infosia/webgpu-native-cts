// Ported from gpuweb/cts src/webgpu/api/validation/shader_module/overrides.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,shader_module,overrides",
    "This tests overrides numeric identifiers should not conflict.");

// ---------------------------------------------------------------------------
// test: id_conflict
// ---------------------------------------------------------------------------
CTS_TEST(g, "id_conflict")
    .desc(R"(
Tests that overrides' explicit numeric identifier should not conflict.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Two overrides with different @id — no conflict: valid
        t.expectValidationError([&] {
            t.createShaderModuleTracked(R"(
@id(1234) override c0: u32;
@id(4321) override c1: u32;

@compute @workgroup_size(1) fn main() {
  // make sure the overridable constants are not optimized out
  _ = c0;
  _ = c1;
}
          )");
        }, false);

        // Two overrides with the same @id — conflict: invalid
        t.expectValidationError([&] {
            t.createShaderModuleTracked(R"(
@id(1234) override c0: u32;
@id(1234) override c1: u32;

@compute @workgroup_size(1) fn main() {
  // make sure the overridable constants are not optimized out
  _ = c0;
  _ = c1;
}
          )");
        }, true);
    });

// ---------------------------------------------------------------------------
// test: name_conflict
// ---------------------------------------------------------------------------
CTS_TEST(g, "name_conflict")
    .desc(R"(
Tests that overrides' variable name should not conflict, regardless of their numeric identifiers.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Two overrides with the same name and no @id — name conflict: invalid
        t.expectValidationError([&] {
            t.createShaderModuleTracked(R"(
override c0: u32;
override c0: u32;

@compute @workgroup_size(1) fn main() {
  // make sure the overridable constants are not optimized out
  _ = c0;
}
          )");
        }, true);

        // One with @id(1), one without — same name: still a name conflict: invalid
        t.expectValidationError([&] {
            t.createShaderModuleTracked(R"(
@id(1) override c0: u32;
override c0: u32;

@compute @workgroup_size(1) fn main() {
  // make sure the overridable constants are not optimized out
  _ = c0;
}
          )");
        }, true);

        // Two with different @id but same name — name conflict: invalid
        t.expectValidationError([&] {
            t.createShaderModuleTracked(R"(
@id(1) override c0: u32;
@id(2) override c0: u32;

@compute @workgroup_size(1) fn main() {
  // make sure the overridable constants are not optimized out
  _ = c0;
}
          )");
        }, true);
    });

} // namespace
