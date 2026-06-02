# H1 — `--sample-formats` representative-format fast-iteration mode

> Harness feature (not a port). Speeds up the yawgpu find/fix loop by running only one
> representative texture format per decode/encoding family when a test sweeps many formats.
> See [`reference/workflow.md`](reference/workflow.md). Implementation by the coding agent.

## Goal

Add an opt-in `--sample-formats` CLI flag that, for tests sweeping a large set of texture formats,
runs only a curated **representative** subset (one per *major* decode family — an aggressive,
speed-tuned set) and skips the rest — cutting wall-clock for the iterate-on-yawgpu loop. Full
coverage remains the default (flag off).

## Scope

**In:**
- A pure, testable sampling function in the **common** layer that thins a test's expanded cases to
  representative format values, driven by an injected hook (no suite knowledge in common).
- A **webgpu**-layer representative-format table + predicate, registered into the common hook at
  static-init.
- Wiring `--sample-formats` through `RunOptions` → the in-process run path, the isolated/selective
  run paths (forwarded to child processes), and the `--list` / `--list-cases` preview.
- A clearly-visible stderr notice when sampling is active and a recap of how many runs were dropped
  (the project's no-silent-caps rule — sampling must never look like full coverage).
- Unit tests (GPU-free) and docs/`--help` updates.

**Out (non-goals):**
- Sampling any param other than the format-like keys (`dimension`, `usage`, sizes, etc. stay full).
- Changing `writeListingJson` / the committed `src/webgpu/listing.json` (the catalog stays the full
  suite — sampling is a run-time/preview concern only).
- Changing the **stdout** result-line or `summary:` format (expectations/crash-list/CI parsing must
  be byte-identical to a non-sampled run; dropped cases simply don't appear, exactly as a narrower
  query already behaves).
- Any new GPU behavior; no expectations files touched.

## Interfaces

### 1. Common layer — `include/cts/format_sample.h` (new)

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>
#include "cts/test.h" // ParamsBuilder::ExpandedCase, ParamRecord, Value

namespace cts {

// Per (param-key, int64 value) verdict from the suite:
//   nullopt -> key is NOT a sampled (format-like) param; never affects sampling for this key
//   true    -> value is a representative (keep)
//   false   -> value is non-representative (droppable)
using FormatSampleHook = std::function<std::optional<bool>(std::string_view key, int64_t value)>;

void setFormatSampleHook(FormatSampleHook hook);
const FormatSampleHook& getFormatSampleHook(); // empty target() when unset

// A test is thinned only if some recognized format-like key sweeps MORE than this many distinct
// int64 values (protects intentional small format sets, e.g. a hand-picked 4-format test).
inline constexpr std::size_t kFormatSampleThreshold = 6;

struct FormatSampleStats {
    std::size_t testsSampled = 0; // tests where at least one run was dropped
    std::size_t runsKept = 0;     // subcase-runs kept (across sampled tests only)
    std::size_t runsDropped = 0;  // subcase-runs dropped
};

// Returns `cases` with every run (a subcase merged onto its case params, or a case with no
// subcases) DROPPED when any recognized format-like param in that run holds a non-representative
// int64 value — but ONLY for tests where some recognized key sweeps > `threshold` distinct values,
// and NEVER reducing a test to zero runs (if the kept set would be empty, that test is returned
// unchanged). No-op when `hook` is empty. Pure (no globals) for unit testing; accumulates into
// `*stats` when non-null.
std::vector<ParamsBuilder::ExpandedCase> sampleFormatsInCases(
    std::vector<ParamsBuilder::ExpandedCase> cases,
    const FormatSampleHook& hook,
    std::size_t threshold = kFormatSampleThreshold,
    FormatSampleStats* stats = nullptr);

} // namespace cts
```

**Algorithm for `sampleFormatsInCases` (per `ExpandedCase` list = one test):**

1. If `hook` is empty → return `cases` unchanged.
2. Enumerate the test's **runs**. A run = each subcase merged onto its case `params`
   (`merged = case.params + subcase`), or just `case.params` when `case.subcases` is empty. For each
   run, look at every `(key, value)` pair whose value `std::holds_alternative<int64_t>` and call
   `hook(key, intValue)`. (Non-int values — strings, `Undefined`, bools — are never passed to the
   hook and never cause a drop, so e.g. `viewFormat=_undef_` is always kept.)
3. A run is **representative** iff *every* hook call that returned non-`nullopt` returned `true`.
4. For each recognized key, count its **distinct** int64 values across all runs. If no recognized
   key has `> threshold` distinct values → return `cases` unchanged (intentional small set).
5. Compute the kept runs (representative ones). If **zero** kept → return `cases` unchanged (safety).
6. Otherwise rebuild the `ExpandedCase` list from kept runs:
   - case with no subcases → include the case iff its single run is kept;
   - case with subcases → keep only the subcases whose merged run is kept; drop the case entirely if
     none remain.
   Preserve original order. Update `*stats` (`testsSampled += (anyDropped ? 1 : 0)`, plus
   `runsKept` / `runsDropped`).

Use `findParam` / iterate `ParamRecord` directly; `valueAs<int64_t>` or `std::get<int64_t>(v.data())`
to read the int.

### 2. webgpu layer — `src/webgpu/texture_format.h` (extend)

Add a curated representative set + predicate. **An aggressive subset tuned for loop speed** — one
format per *major* decode family, accepting that some narrower paths (e.g. snorm-16, uint-16/32,
sint, packed-float, single-channel) will not run in this mode. This array is the single tuning knob;
widen it later if a missed path needs routine fast-mode coverage.

```cpp
// Representative formats for the --sample-formats fast-iteration mode (see specs/format-sampling-mode.md).
inline constexpr std::array<WGPUTextureFormat, 12> kRepresentativeTextureFormats = {
    // color: the major decode families only (aggressive — favors loop speed over exhaustiveness)
    WGPUTextureFormat_RGBA8Unorm,     WGPUTextureFormat_RGBA8Snorm,    WGPUTextureFormat_RGBA8Uint,
    WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_BGRA8Unorm,    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_RGBA32Float,    WGPUTextureFormat_RGB10A2Unorm,
    // depth / stencil
    WGPUTextureFormat_Depth32Float,   WGPUTextureFormat_Depth24PlusStencil8,
    // compressed: one BC + one ASTC
    WGPUTextureFormat_BC1RGBAUnorm,   WGPUTextureFormat_ASTC4x4Unorm,
};

inline bool isRepresentativeTextureFormat(WGPUTextureFormat format) {
    for (WGPUTextureFormat f : kRepresentativeTextureFormats) {
        if (f == format) return true;
    }
    return false;
}
```

Exactly **8** of these 12 are color formats (members of `kColorTextureFormats`): RGBA8Unorm,
RGBA8Snorm, RGBA8Uint, RGBA8UnormSrgb, BGRA8Unorm, RGBA16Float, RGBA32Float, RGB10A2Unorm.

### 3. webgpu layer — `src/webgpu/format_sample_registrar.cpp` (new)

Static-init registrar mapping the format-like param keys to the predicate. Recognize the keys the
specs actually use: `format`, `textureFormat`, `viewFormat`.

```cpp
#include <optional>
#include <string_view>
#include "cts/format_sample.h"
#include "webgpu/texture_format.h"
namespace {
const bool kRegistered = [] {
    cts::setFormatSampleHook([](std::string_view key, int64_t value) -> std::optional<bool> {
        if (key == "format" || key == "textureFormat" || key == "viewFormat") {
            return cts::isRepresentativeTextureFormat(static_cast<WGPUTextureFormat>(value));
        }
        return std::nullopt;
    });
    return true;
}();
} // namespace
```

**Linker pitfall:** this TU is referenced by nothing, so it can be dropped from a static lib (same
problem the `CTS_TEST` self-registration solves). Build it into the same target / with the same
whole-archive (or OBJECT-library) treatment that keeps the test registrars alive, so its static
initializer runs in the `cts` binary.

### 4. `include/cts/test.h` — `RunOptions`

Add one field:

```cpp
struct RunOptions {
    ...
    bool sampleFormats = false; // --sample-formats
    ...
};
```

### 5. `src/common/runtime/main.cpp`

- Parse `--sample-formats` → `options.sampleFormats = true;` **and** append it to
  `options.forwardedArgs` so isolated child processes (`--run-case`) sample identically (mirror the
  `--yawgpu-backend` forwarding, but it is a value-less flag — just push the one token).
- Add `[--sample-formats]` to `printUsage()` (line ~66).

### 6. `src/common/runner.cpp`

- Add a shared helper used by every **run** and **preview** path (but NOT `writeListingJson`):

```cpp
std::vector<ParamsBuilder::ExpandedCase> sampledExpandedCases(
    const TestSpec& test, bool sampleFormats, FormatSampleStats* stats) {
    auto cases = expandedCases(test);
    if (sampleFormats) {
        cases = sampleFormatsInCases(std::move(cases), getFormatSampleHook(),
                                     kFormatSampleThreshold, stats);
    }
    return cases;
}
```

- `collectRuns`, `collectCases`: take the sample flag (thread `options.sampleFormats`; simplest is
  to pass `const RunOptions&` or a `bool` + `FormatSampleStats*`) and call `sampledExpandedCases`
  instead of `expandedCases`.
- `runSingleCase` (the `--run-case` child path): when sampling, expand via `sampledExpandedCases`
  before locating the queried case, so the child filters its subcases consistently with the parent.
  (The parent only dispatches kept cases, so the queried case is always present; if for any reason it
  is not, fall back to the unsampled case so the child never mis-reports.)
- `--list` / `--list-cases` in `runQueries` and `printTestListLine`: use
  `sampledExpandedCases(test, options.sampleFormats, nullptr)` so the preview reflects what would
  run. **`writeListingJson` keeps using raw `expandedCases`** (the committed catalog stays full).
- **Reporting (stderr only; parent process only):** in `runQueries`, when `options.sampleFormats`
  and not in `--run-case` child mode, before running print:
  `format-sample: representative-formats mode ON — non-representative format cases are SKIPPED; this is NOT full conformance coverage.`
  After collecting, print a recap from the accumulated `FormatSampleStats`, e.g.:
  `format-sample: thinned <testsSampled> tests, dropped <runsDropped> of <runsKept+runsDropped> format-swept subcases.`
  Do not print anything when `runsDropped == 0`. **stdout (result lines + `summary:`) is unchanged.**

### 7. CMake

Add `src/common/format_sample.cpp` to the harness/common target and
`src/webgpu/format_sample_registrar.cpp` to the webgpu suite target — the same targets that already
build `src/common/runner.cpp` and the `src/webgpu/**` spec/registrar sources, so both link into
`cts` and `cts_unittests`.

## Acceptance criteria

All criteria are **GPU-free** (build + `cts_unittests` + `--list`/`--list-cases`, which enumerate
params without creating a device):

- [ ] `cmake --build build-yawgpu --target cts cts_unittests` succeeds (and likewise for the wgpu/dawn
      build dirs the repo uses).
- [ ] `build-yawgpu/cts_unittests` exits 0, including new tests that:
  - assert `isRepresentativeTextureFormat(WGPUTextureFormat_RGBA8Unorm) == true` and
    `isRepresentativeTextureFormat(WGPUTextureFormat_RG16Snorm) == false`;
  - call `sampleFormatsInCases` on a synthetic case list with a `format` param sweeping all 43
    `kColorTextureFormats` (case-level) plus a non-format param, using a local hook equal to the
    registrar's, and assert exactly the 8 representative color formats survive (and the count of
    surviving cases = 8 × the non-format multiplicity);
  - assert a synthetic test whose `format` sweeps only **4** formats (≤ threshold) is returned
    unchanged;
  - assert a synthetic test whose swept formats contain **no** representative is returned unchanged
    (never zero);
  - assert a synthetic test with **no** format-like param is returned unchanged;
  - assert subcase-level format sampling: a single case with subcases sweeping the 43 color formats
    keeps only the 8 representative subcases.
- [ ] Default (no flag) is unchanged:
      `build-yawgpu/cts --list 'webgpu:api,operation,command_buffer,image_copy:undefined_params:*'`
      still prints `... cases=516 subcases=2064`.
- [ ] With the flag:
      `build-yawgpu/cts --list --sample-formats 'webgpu:api,operation,command_buffer,image_copy:undefined_params:*'`
      prints `... cases=96 subcases=384` (8 of 43 color formats; 516×8/43=96, 2064×8/43=384).
- [ ] `build-yawgpu/cts --list-cases --sample-formats 'webgpu:api,operation,command_buffer,image_copy:undefined_params:*'`
      prints 96 case-query lines, each with a `format=` value in the representative-color set, and
      **none** of the other 35 color formats appear (verify by grepping the format ids).
- [ ] `build-yawgpu/cts --list-cases 'webgpu:api,operation,command_buffer,image_copy:undefined_params:*'`
      (no flag) still prints 516 lines.
- [ ] The committed `src/webgpu/listing.json` is unchanged after `cmake --build … --target gen_listings`
      (sampling does not touch the catalog).
- [ ] `build-yawgpu/cts --help` lists `--sample-formats`; `docs/06-build-and-run.md` §4 "Common
      options" has a row for it.
- [ ] No file under `expectations/` is modified.

## Verification

1. `cmake --build build-yawgpu --target cts cts_unittests gen_listings` (codex: build + unittests +
   listing only — **no GPU runs**).
2. `build-yawgpu/cts_unittests` → exit 0.
3. The four `--list` / `--list-cases` commands above; diff the counts against the stated numbers.
4. `git status --porcelain src/webgpu/listing.json expectations/` → empty.
5. Claude later does the real-GPU timing/behaviour check (sandbox off): confirm a sampled
   `image_copy:*` run reproduces F-025/F-026-class results far faster, with the stderr notice shown
   and the `summary:` line in the unchanged format.

## References

- `docs/02-harness.md` — `ParamsBuilder`, case/subcase model, query format.
- `docs/06-build-and-run.md` §4 "Common options" — CLI flag table to extend; §6 — `cts_unittests`.
- `src/common/runner.cpp` — `expandedCases` (94), `collectRuns` (163), `collectCases` (192),
  `runSingleCase`/`--run-case`, `printTestListLine` (98), `writeListingJson` (748), `runQueries` (671),
  isolated/selective paths (574/582) and `runIsolatedChild` arg-forwarding (~509).
- `src/common/runtime/main.cpp` — flag parsing (131), `printUsage` (65), `forwardedArgs`.
- `include/cts/test.h` — `Value`/`ParamRecord`/`findParam` (21–47), `ParamsBuilder::ExpandedCase`
  (61), `RunOptions` (252).
- `src/webgpu/texture_format.h` — `kColorTextureFormats` (211), the format arrays to draw reps from.
- `src/webgpu/api/operation/command_buffer/image_copy.spec.cpp` — `colorFormatValues` (57),
  `baseParams` (74) — the canonical `combine("format", …)` case-level sweep, encoded as
  `Value(static_cast<int64_t>(WGPUTextureFormat_…))`.
- `src/unittests/main.cpp` — existing unit-test harness (`require`, includes `webgpu/texture_format.h`).
