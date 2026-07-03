# 06 — Build and Run

This document specifies the build system, how a backend is selected and linked, how the suite
is run and filtered, and how the listing is regenerated.

> Exact target names / link details are finalized during the vertical slice; this captures the
> intended shape and the decisions already made.

---

## 1. Build system: CMake

CMake (3.20+) is the build system — it is the common denominator for the C/C++ ecosystem and is
what Dawn itself uses. The top-level `CMakeLists.txt` defines:

- `cts` — the runner executable (harness + all `src/webgpu/**/*.spec.cpp` compiled directly in).
- `cts_unittests` — harness self-tests (no GPU; doctest-based).
- `gen_listings` — the listing generator tool (links the same tests, runs registry iteration).
- `cts_harness` — the harness as a library (so tools share it; the `.spec.cpp` files are compiled
  into the executables that need them, not into this library — see §3).

Language: `CXX` (C++20) only. Tests and harness are all C++; tests call the WebGPU **C** API.
(C++20 is used for designated initializers, `std::span`, and `std::source_location`.)

---

## 2. Backend selection

A single cache variable selects the implementation under test:

```bash
cmake -S . -B build -DCTS_BACKEND=wgpu-native   # or: yawgpu | dawn
```

`CTS_BACKEND` ∈ `{wgpu-native, yawgpu, dawn}` controls:

- which canonical `webgpu.h` include path is used,
- which backend shim TU is compiled (`backend_wgpu.cpp` / `backend_yawgpu.cpp` / `backend_dawn.cpp`,
  see [03-webgpu-c-abstraction §6](03-webgpu-c-abstraction.md)),
- a `CTS_BACKEND_WGPU` / `CTS_BACKEND_YAWGPU` / `CTS_BACKEND_DAWN` compile definition (for the rare
  backend-specific test), and
- which library the runner links.

### Locating the backend library

Two supported modes, chosen by additional cache vars:

| Mode | Vars | Behavior |
|------|------|----------|
| **Prebuilt** | `CTS_WGPU_NATIVE_DIR` / `CTS_DAWN_DIR` pointing at an install/build dir | Use `find_library` + a known header dir; link the prebuilt `.a`/`.so`/`.dylib` |
| **FetchContent / submodule** | default | Add the backend as a subproject and build it (Dawn via its CMake; wgpu-native via its Makefile/meson invoked from CMake, or a prebuilt release) |

**Vertical-slice path — wgpu-native, concretely.** Phase 0/1 wires exactly one backend:
wgpu-native (it lives in this workspace and links as a plain C library). The expected layout and a
known-good configure command:

```
$CTS_WGPU_NATIVE_DIR/
  include/
    webgpu.h                     # wgpu-native extensions
    webgpu-headers/webgpu.h      # canonical C API (the header tests compile against)
  lib/
    libwgpu_native.a             # static lib (preferred for the slice)
    libwgpu_native.dylib         # or the shared lib (then handle rpath at runtime)
```

```bash
# Build wgpu-native first (in your wgpu-native checkout): `cargo build --release -j 1` (or `make`),
# then point CTS at a dir laid out as above (header dir + lib dir).
cmake -S . -B build \
      -DCTS_BACKEND=wgpu-native \
      -DCTS_WGPU_NATIVE_DIR=/abs/path/to/wgpu-native/dist
```

The CMake glue resolves the canonical header as
`${CTS_WGPU_NATIVE_DIR}/include/webgpu-headers/webgpu.h`, finds the library with
`find_library(WGPU_NATIVE_LIB wgpu_native PATHS ${CTS_WGPU_NATIVE_DIR}/lib)`, links it, and — for a
shared lib — sets an rpath / copies the dylib next to `build/cts`. (wgpu-native's own build output
dir layout differs slightly between `make` and `cargo`; the exact `CTS_WGPU_NATIVE_DIR` to pass for
the local checkout is pinned in the Phase 0 task spec, `specs/phase0-build-skeleton.md`.)

**yawgpu** (added right after the slice; the primary conformance subject). yawgpu
([github.com/infosia/yawgpu](https://github.com/infosia/yawgpu)) is a Rust crate
(`crate-type = ["cdylib", "staticlib", "rlib"]`) that, like wgpu-native, exposes the canonical
`webgpu.h` plus a vendor header `yawgpu.h`. Build it with cargo, selecting a GPU backend feature:

`CTS_YAWGPU_DIR` points at the yawgpu checkout root; CMake resolves the header and library inside
it:

```
$CTS_YAWGPU_DIR/                       # yawgpu checkout root
  yawgpu/ffi/webgpu-headers/webgpu.h   # canonical C API (the header tests compile against)
  yawgpu/ffi/webgpu-headers/yawgpu.h   # vendor extensions
  target/release/libyawgpu.a           # from cargo build (also target-metal/release, etc.)
```

```bash
# In the yawgpu checkout (macOS → metal; Windows/Linux → vulkan):
cargo build --release --features metal -j 1   # produces target/release/libyawgpu.{a,dylib}
#   (Windows/Linux: --features vulkan; e.g. --target-dir target-vulkan → target-vulkan/release/)
# Point CTS at the checkout root:
cmake -S . -B build -DCTS_BACKEND=yawgpu -DCTS_YAWGPU_DIR=<yawgpu>
```

> **Tint frontend (link the dylib, not the static `.a`).** Since yawgpu's WGSL frontend
> is **Tint** (Dawn's compiler, vendored via the `third_party/dawn` submodule), `libyawgpu.a`
> carries unresolved references into the C++ shim library `libtint_shim.dylib` and **does not
> link standalone** — a static-`.a` CTS build fails with
> `Undefined symbols ... yawgpu_tint::imp::take_error`. Pass the **dylib** explicitly (it links
> the shim via `@rpath`) and put the shim build dir on the loader path at runtime:
>
> ```bash
> cargo build --release -p yawgpu --features metal -j 1   # also builds out/build/libtint_shim.dylib
> SHIM_DIR=<yawgpu>/target/release/build/yawgpu-tint-*/out/build
> cmake -S . -B build-yawgpu -DCTS_BACKEND=yawgpu \
>       -DCTS_YAWGPU_DIR=<yawgpu> \
>       -DCTS_YAWGPU_LIB=<yawgpu>/target/release/libyawgpu.dylib
> cmake --build build-yawgpu --target cts -j 1
> DYLD_LIBRARY_PATH=$SHIM_DIR build-yawgpu/cts --isolate --workers 6 \
>       --expectations expectations/yawgpu.txt '<query>'
> ```
>
> Building yawgpu requires the Dawn submodule initialized + its deps fetched (see the yawgpu
> repo's `specs/reference/dependencies.md`). The static-`.a` flow below predates Tint.

`cts::createInstance()` **must** chain `YaWGPUInstanceBackendSelect` to get a real GPU backend — an
unchained yawgpu instance returns a Noop. The shim selects a platform default — Metal on Apple,
Vulkan elsewhere (Windows/Linux) — and yawgpu must be built with the matching cargo feature
(`--features metal` / `--features vulkan`). `CTS_YAWGPU_BACKEND={metal,vulkan,gles}` overrides the
yawgpu instance backend at runtime; unset, empty, or unknown values keep the platform default. The
selected backend must be compiled into the linked `libyawgpu.a`.

To run yawgpu's Vulkan HAL on macOS through MoltenVK, build yawgpu's Vulkan library in a separate
target dir and point CTS at that exact archive:

```bash
CARGO_TARGET_DIR=<yawgpu>/target-vulkan \
  cargo build --release -p yawgpu --features vulkan -j 1

cmake -S . -B build-yawgpu-vulkan \
      -DCTS_BACKEND=yawgpu \
      -DCTS_YAWGPU_DIR=<yawgpu> \
      -DCTS_YAWGPU_LIB=<yawgpu>/target-vulkan/release/libyawgpu.a
cmake --build build-yawgpu-vulkan --target cts -j 1
```

Run with the Vulkan backend override and MoltenVK loader environment:

```bash
CTS_YAWGPU_BACKEND=vulkan \
DYLD_LIBRARY_PATH=$VULKAN_SDK/lib \
VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json \
  build-yawgpu-vulkan/cts '<query>'
```

`DYLD_LIBRARY_PATH=$VULKAN_SDK/lib` is required so `ash` can load the Vulkan loader; without it yawgpu
can report `BackendUnavailable`.

Expectation files are split per backend **and per API**: Metal runs use
`expectations/<backend>.txt`; Vulkan-backend runs (native Windows or MoltenVK) use
`expectations/<backend>-vulkan.txt`, which carries the Vulkan-only xfails (currently the F-085
spec-in-flux set). Applying the `-vulkan` file on Metal would surface its entries as `xpass`. This is Vulkan API coverage via MoltenVK, not native Vulkan. It is
useful for yawgpu Vulkan HAL triage, but authoritative Vulkan conformance still requires native
Vulkan hardware/OS coverage.

**Dawn** (the oracle backend; `backend_dawn.cpp` just calls `wgpuCreateInstance` — no backend
selection chaining). Dawn is built from its own checkout as a **monolithic CMake library** — no
`depot_tools`/`gn` required, only CMake + a C++20 compiler + Python (the `third_party/` deps must
already be populated, e.g. by a prior `gclient sync`, or fetched via `DAWN_FETCH_DEPENDENCIES=ON`):

```bash
# In your Dawn checkout (<dawn>): build the monolithic SHARED library.
cmake -S <dawn> -B <dawn>/out/Release -G "Visual Studio 17 2022" -A x64 \
      -DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED -DBUILD_SHARED_LIBS=OFF \
      -DDAWN_ENABLE_INSTALL=ON -DDAWN_FETCH_DEPENDENCIES=ON \
      -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF \
      -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF
cmake --build <dawn>/out/Release --config Release --target webgpu_dawn -j 1
#   produces: out/Release/Release/webgpu_dawn.dll
#             out/Release/src/dawn/native/Release/webgpu_dawn.lib
```

Point CTS at the Dawn checkout (headers) and build dir (library). The canonical `webgpu/webgpu.h`
is a stub that includes the generated `dawn/webgpu.h`, so two header roots are needed —
`<dawn>/include` and `<dawn>/out/Release/gen/include` — both supplied automatically from
`CTS_DAWN_DIR` + `CTS_DAWN_BUILD_DIR`. With a multi-config Visual Studio generator the import lib
lands in a `Release/` subdir that `find_library` does not search, so pass `CTS_DAWN_LIB` explicitly:

```bash
cmake -S . -B build-dawn -G "Visual Studio 17 2022" -A x64 \
      -DCTS_BACKEND=dawn \
      -DCTS_DAWN_DIR=<dawn> \
      -DCTS_DAWN_BUILD_DIR=<dawn>/out/Release \
      -DCTS_DAWN_LIB=<dawn>/out/Release/src/dawn/native/Release/webgpu_dawn.lib
cmake --build build-dawn --config Release --target cts -j 1
```

**Backend selection.** Like yawgpu, the Dawn shim requests a specific adapter backend rather than
Dawn's platform default: **Vulkan** on non-Apple (Metal on Apple), overridable at runtime with
`CTS_DAWN_BACKEND=vulkan|d3d12|d3d11|metal|opengl|opengles|null` (the shim sets
`WGPURequestAdapterOptions.backendType`, a Dawn-only field). Unknown/unset keeps the platform
default. This keeps Dawn on the same Vulkan path the yawgpu suite exercises.

**Runtime: the required DLLs must sit next to `cts.exe`** (in `build-dawn/Release/`):

1. `webgpu_dawn.dll` (from `<dawn>/out/Release/Release/`),
2. `d3dcompiler_47.dll` (the **x64** copy from `<Windows SDK>/bin/<ver>/x64/`), and
3. `vulkan-1.dll` (the Vulkan loader, e.g. the x64 copy from `System32` or the Vulkan SDK) —
   needed because Dawn defaults to the Vulkan backend.

Dawn loads these from the **executable directory**, not from `System32` — a `System32` copy is not
used and yields `DynamicLib.Open: ... Windows Error: 87`. Without `d3dcompiler_47.dll` **every** case
fails with `failed to request all-features/max-limits device: DynamicLib.Open: d3dcompiler_47.dll
Windows Error: 87 at EnsureFXC` (Dawn loads FXC for device creation); without `vulkan-1.dll` the
default Vulkan adapter request fails. A bare `cts.exe` (no query) prints the selected adapter
(`backendType: vulkan`) and is a good smoke test. Verified on Windows / Vulkan / NVIDIA (2026-06-24):
smoke cases green (`adapter_info`, `requestAdapter`, `buffers,map:mapAsync,read` all `fail=0 crash=0`).

---

## 3. Building

```bash
# Configure (pick a backend + how to find it; see §2 for the expected dir layout)
cmake -S . -B build \
      -DCTS_BACKEND=wgpu-native \
      -DCTS_WGPU_NATIVE_DIR=/abs/path/to/wgpu-native/dist

# Build everything.
# Convention: build SERIALLY (-j 1) — parallel compiles overload the dev machine
# (CPU/memory); this applies to every cmake/cargo build command in this repo.
cmake --build build -j 1

# Targets:
#   build/cts              the runner
#   build/cts_unittests    harness self-tests
#   build/gen_listings     listing generator
```

The `.spec.cpp` files are **compiled directly into** the `cts` (and `gen_listings`) executables —
not bundled into an intermediate static library — so their static initializers (which register
the tests) are never dropped by the linker. CMake globs `src/webgpu/**/*.spec.cpp` with
`CONFIGURE_DEPENDS` so the source set updates when files are added/removed. (If the tests ever
need to live in a static library, switch to a generated index TU or `--whole-archive`; see
[02-harness §1](02-harness.md).)

---

## 4. Running

```bash
# Run a whole file
build/cts 'webgpu:api,validation,createBuffer:*'

# Run one test (all cases)
build/cts 'webgpu:api,validation,createBuffer:size_alignment:*'

# Run one specific case
build/cts 'webgpu:api,validation,createBuffer:size_alignment:usage=4'

# Run several queries
build/cts 'webgpu:api,validation,createBuffer:*' 'webgpu:api,validation,createTexture:*'

# List without running
build/cts --list       'webgpu:api,validation,*'
build/cts --list-cases 'webgpu:api,validation,createBuffer:*'
```

### Common options

| Option | Effect |
|--------|--------|
| `--list` / `--list-cases` | print matching paths/cases; do not run |
| `--sample-formats` | opt-in fast-iteration mode for large texture-format sweeps; keeps only representative formats, prints a stderr notice/recap, and is not full conformance coverage |
| `--workers N` \| `auto` | parallel runner: spawn `N` shard workers on one machine, merge their machine-readable results back into the same ordered text summary as a sequential run. POSIX uses `fork`+`exec`; Windows uses `CreateProcess`. Workers load the parent's ordered case plan instead of re-enumerating cases. `N=1` is sequential; `auto` (or `0`) resolves to `min(hardware_concurrency, 8)` — capped low because GPU/VRAM pressure (not CPU) is the limiter and high counts can wedge the OS. **Composes with `--isolate`** (parallel per-case isolation pool — see that row); still incompatible with `--crash-list` |
| `--shard I/N` | run or list only the deterministic round-robin shard where case index `idx % N == I`; works with `--list-cases` for partition checks and can be used directly in CI on any platform |
| `--expectations <file>` | known-failure list (case-query lines, `#` comments); matching fails/crashes → `xfail`, matching passes → `xpass`; run fails only on an unexpected fail/crash. A line ending in `:*` is a **prefix** match — it covers every case of a test in one line (used when a whole test crashes a backend, e.g. wgpu-native on `texture_usage`); a pass under such a prefix shows as `xpass`, flagging that the wildcard can be tightened |
| `--isolate` | **wgpu-native triage only.** yawgpu and Dawn are crash-free suite-wide, so they never need it; and it reports **per-case** (not per-subcase) counts, which must not be mixed into the per-subcase result tables. Runs **every** case in a child process (`--run-case`); a backend abort becomes a contained `crash` result instead of killing the run. POSIX uses `fork`+`exec`; Windows uses `CreateProcess` (a Rust abort shows as a non-zero child exit, e.g. `0xC0000409`). Much slower than plain `--workers` — use it only for **uninflated per-case crash classification** on wgpu-native (in plain worker mode an abort contaminates later cases in the same worker process, inflating `crash` ~4×). Sequential by default; **add `--workers N` to run the per-case isolation as a bounded concurrent pool** (≤`N` children at once, results merged back into sequential order — same per-case classification as serial `--isolate` for any `N`, just faster) |
| `--crash-list <file>` | **selective isolation** (wgpu-native only, like `--isolate`): fork only the cases the file lists (same line format as `--expectations`: exact or `:*` prefix), run all others **in-process**. Produces the **same per-case classification** as full `--isolate` (in-process cases are aggregated per case), so summaries are interchangeable. A case that aborts but is **not** on the list takes down the in-process run — refresh the list with `--isolate --emit-crash-list`. Composable with `--expectations`. On a clean backend (no list match) nothing forks |
| `--emit-crash-list <file>` | with `--isolate`, write every case that actually **crashed** (raw, before `--expectations` reclassification), sorted+unique, one query per line — directly reusable as a `--crash-list`. Workflow: run `--isolate --emit-crash-list expectations/<backend>.crash.txt` once per backend revision, then iterate with `--crash-list expectations/<backend>.crash.txt` |
| `--run-case <case-query>` | (internal, used by `--isolate`/`--crash-list`) run exactly one case in-process and print `RESULT\t<status>\t<message>` |
| `--shard-results` / `--shard-from K` / `--case-plan <file>` | (internal, the `--workers` worker protocol) a spawned shard worker emits machine-readable `RESULT` lines (`--shard-results`), resumes after a crash from case index `K` (`--shard-from`), and loads the parent's ordered case plan instead of re-enumerating (`--case-plan`). Never pass these by hand |
| `--case-timeout-ms M` | per-case wall-clock watchdog (only meaningful with `--isolate`): a child exceeding `M` ms is killed and recorded as a `crash` ("case timed out after M ms"), so a single wedged case cannot hang the whole run. POSIX kills with `SIGKILL`+`waitpid`; Windows uses `TerminateProcess`. `0` (default) disables it |
| `--output <file>` | also write machine-readable **JSONL** results: one `{"query","status","message","expected","effective"}` object per case (messages JSON-escaped), then a trailing `{"summary":true,…}` line with the counts. Works in every run mode; the human text + summary on stdout are unchanged. Makes cross-shard aggregation deterministic (no stdout re-parsing) |
| `--baseline <file>` | after the run, diff the current results' *effective* statuses against a prior `--output` JSONL and print `Regressed`/`Fixed`/`New`/`Removed` query sets. With `--baseline` set, the **exit code reflects regressions only** (0 iff nothing got worse vs the baseline), independent of the absolute fail count — the CI-useful "did this change make anything worse" gate |

Exit code: non-zero if any non-expected case fails. Skips/warns do not fail the run by default.
With `--baseline` the exit code reflects regressions only (see that row).

The table above is the **complete** flag set — anything else is rejected as `unknown option`.
yawgpu's Metal/Vulkan HAL selection is not a flag: it is the `CTS_YAWGPU_BACKEND` env var plus a
per-backend build dir (see §3).

### Large-suite runs & finding triage

At the current suite size (**642 ported files / ~2.1M subcases**) the default full-suite mode on every
robust backend (yawgpu, Dawn) is a single plain **`--workers 6`** run per area (or one `webgpu:*`) — the
2026-07-03 README tables were produced exactly that way, `fail=0 crash=0` modulo the 2 known port-oracle
cases, in well under an hour per backend. Since the F-146 fix (workers spawn via `fork`+`exec` and load
the parent's case plan) parallel numbers on macOS are authoritative; no isolation, sharding dance, or
serial re-run is needed for a clean backend.

Two situations still need different tools:

> - **Windows/Vulkan worker count.** On Vulkan a shard process runs many cases on **one process-global
>   device**, and with **too few workers** GPU state can degrade until later cases fail en masse with
>   `HAL queue submission failed: vulkan`. That is a **fake-fail artifact, not a backend regression** —
>   the fix is to *raise* the worker count (8 was clean where 4 showed ~6.3k fake fails), never to
>   switch isolation mode. Very high concurrency (>~10 simultaneous Vulkan devices) can freeze the OS;
>   stay in the 6–8 band.
> - **wgpu-native (panic-heavy) findings triage:** use `--isolate --workers N` (below). It reports
>   **per-case** counts (different units — do not mix them into the per-subcase tables) and is far
>   slower; use it to *classify* crashes accurately, not to produce the result tables. In plain worker
>   mode an abort contaminates later cases in the same worker process (`crash` inflated ~4×), which is
>   acceptable for the coarse wgpu-native table but not for per-defect counting.

**The per-case isolation pool: `--isolate --workers N`.** Each case runs in its own child process — so
in-process degradation cannot accrue — and up to `N` run at once with a capped default (`min(cores, 8)`;
GPU/VRAM pressure, not CPU, is the limiter), which stays under the OS-freeze ceiling. Results are
**identical to serial `--isolate` for any `N`**:

```bash
# wgpu-native per-case crash classification (the one remaining --isolate consumer):
build/cts --isolate --workers 8 \
    --expectations expectations/wgpu-native.txt --output run.jsonl 'webgpu:*'
```

(Historical validation: a full-suite isolate-pool run on yawgpu/Vulkan — Windows, RTX 5060 Ti, when the
suite was 234 queries — completed with **no OS freeze, `crash=0`, ~41 min**; the only residual fails
were a tiny probabilistic tail, e.g. `memory_model`. On today's crash-free yawgpu/Dawn, plain
`--workers` replaces this entirely.)

**Triage flow (all native, no helper scripts):**
- **Screen + record:** an `--isolate --workers 8 --output run.jsonl` run is itself the
  authoritative per-case pass — its `summary:` line and the JSONL are the verdict, not just a candidate list.
- **Confirm one suspect at finer grain:** a *single* case with a very large subcase count can still
  degrade in-process (its subcases share one process); re-run that test alone, or split it narrower,
  with `cts --isolate 'webgpu:<file>:<test>:*'` (optionally `--workers N`). `fail=0` ⇒ the suite-run
  "fail" was within-case flakiness; a stable `fail>0` with no `HAL queue submission failed` noise ⇒
  real finding.
- **Regression guard:** keep a known-clean `run.jsonl` as a baseline and diff a later run with
  `--baseline run.jsonl` — it reports `Regressed`/`Fixed`/`New`/`Removed` and exits non-zero only on a
  regression. This replaces the old hand-maintained recheck list.

Note `--shard I/N` is **0-indexed** (`0/N … (N-1)/N`; `N/N` errors) — still the primitive for fanning
a run across CI machines (each machine runs and judges its own shard).

---

## 5. Regenerating the listing

```bash
cmake --build build --target gen_listings -j 1
# emits src/webgpu/listing.json (and/or listing.inc consumed by the runner)
```

The listing is checked in (like upstream's generated `listing.js`) so tools and CI can read the
catalog without a GPU. A CI check verifies the committed listing matches a freshly generated
one (fails if a test was added/removed without regenerating).

---

## 6. Testing the harness itself

```bash
build/cts_unittests           # runs params/query/tree/loading self-tests, no GPU (doctest)
ctest --test-dir build        # if registered with CTest
```

These must pass before GPU results are trusted (see [02-harness §9](02-harness.md)). They use
**doctest** (single-header, fast compile) — the one place we lean on an existing test framework,
since the self-tests are internal and unconstrained by the CTS query/subcase model. The query
self-tests include the **param-stringification parity** table against upstream-derived strings.

---

## 7. CI shape (later)

- Matrix over `CTS_BACKEND ∈ {wgpu-native, yawgpu, dawn}` and OS (yawgpu: Metal on macOS, Vulkan on
  Windows/Linux).
- Build, run `cts_unittests`, then run the suite with an `--expectations` file capturing known
  failures/skips per backend.
- `--output` JSONL merged across shards (the result model is merge-able).
- A listing-freshness check and a coverage-diff against the pinned upstream CTS revision
  (see [05-porting-guide §7](05-porting-guide.md)).

Headless GPU in CI: depends on available adapters (software/fallback such as Vulkan SwiftShader
or a Metal device on macOS runners). Adapter selection is not exposed as CLI flags (the designed
`--force-fallback-adapter`/`--adapter-name` options were never needed); the runner takes the
backend's default adapter, so CI determinism comes from the runner environment.

---

## 8. Platform notes

- **macOS**: Metal backend on any of the three implementations (yawgpu via its `metal` feature);
  link against the backend's `.dylib`/`.a`. The async wrappers use
  `wgpuInstanceWaitAny`/`ProcessEvents`; verify timeout support on Metal for each backend (see
  [03 §8](03-webgpu-c-abstraction.md)).
- **Windows** (verified — MSVC + Vulkan, for wgpu-native and yawgpu): same CMake flow with the
  `Visual Studio 17 2022` generator. MSVC is a supported compiler; compiling the `.spec.cpp` files
  directly into the executable keeps their static initializers on every compiler without
  `--whole-archive`/`/WHOLEARCHIVE` tricks (see [02-harness §1](02-harness.md)). Practical notes:
  - Build the backend as a Rust library and pass the **import lib** (`*.dll.lib`) via
    `CTS_WGPU_NATIVE_LIB` / `CTS_YAWGPU_LIB`; `find_library` otherwise picks the large static `.lib`,
    whose Rust→MSVC link pulls in many unlisted system libraries. Copy the backend `.dll` next to
    `build/<config>/cts.exe` (the build does not auto-copy).
  - yawgpu must be built with `--features vulkan`; the shim selects Vulkan on non-Apple platforms
    automatically (see [03 §6](03-webgpu-c-abstraction.md)).
  - The build compiles with `/utf-8` so MSVC accepts the UTF-8 backend headers under `/WX` regardless
    of the system code page (otherwise C4819).
  - `--isolate` uses `CreateProcess` on Windows (see §4), so per-case crash isolation works here too.
- **Linux**: same CMake flow; backend libraries differ (yawgpu via its `vulkan` feature).
