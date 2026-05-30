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
# Build wgpu-native first (from ../wgpu-native): `cargo build --release` (or `make`),
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
# In the yawgpu checkout (macOS → metal; Linux → vulkan):
cargo build --release --features metal        # produces target/release/libyawgpu.{a,dylib}
# Point CTS at the checkout root:
cmake -S . -B build -DCTS_BACKEND=yawgpu -DCTS_YAWGPU_DIR=/abs/path/to/yawgpu
```

`cts::createInstance()` **must** chain `YaWGPUInstanceBackendSelect` to get a real GPU backend — an
unchained yawgpu instance returns a Noop. Phase 2 pins Metal at the chain and builds yawgpu with
`--features metal`; a runtime `--yawgpu-backend metal|vulkan` option is deferred until more than one
backend is compiled in.

For **Dawn** (added after yawgpu): it exposes a `webgpu_dawn` / `dawn::webgpu_dawn` CMake target
and `include/webgpu/webgpu.h`; the `find_library`/header paths differ accordingly and are captured
when Dawn is wired.

---

## 3. Building

```bash
# Configure (pick a backend + how to find it; see §2 for the expected dir layout)
cmake -S . -B build \
      -DCTS_BACKEND=wgpu-native \
      -DCTS_WGPU_NATIVE_DIR=/abs/path/to/wgpu-native/dist

# Build everything
cmake --build build -j

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
| `--verbose` / `--quiet` | log level |
| `--power-preference {low,high}` | adapter selection |
| `--force-fallback-adapter` | request the fallback adapter |
| `--expectations <file>` | known-failure list (case-query lines); matching fails → `xfail`, matching passes → `xpass`; run fails only on an unexpected fail |
| `--yawgpu-backend {metal,vulkan}` | (deferred — planned) select yawgpu's GPU backend at runtime once multiple are compiled in |
| `--adapter-name <substr>` | pick an adapter by name substring |
| `--enable-feature <name>` | request an optional feature for device creation |
| `--future-timeout-ms <n>` | timeout for async-wait wrappers (default 5000) |
| `--format {text,json}` | report format (json = one result object per line) |
| `--seed <n>` | seed for any randomized helpers (kept deterministic by default) |

Exit code: non-zero if any non-expected case fails. Skips/warns do not fail the run by default.

---

## 5. Regenerating the listing

```bash
cmake --build build --target gen_listings
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
  Linux).
- Build, run `cts_unittests`, then run the suite with an `--expectations` file capturing known
  failures/skips per backend.
- `--format json` output merged across shards (the result model is merge-able).
- A listing-freshness check and a coverage-diff against the pinned upstream CTS revision
  (see [05-porting-guide §7](05-porting-guide.md)).

Headless GPU in CI: depends on available adapters (software/fallback such as Vulkan SwiftShader
or a Metal device on macOS runners). The runner's `--force-fallback-adapter` and adapter
selection options exist to make CI deterministic where a software adapter is available.

---

## 8. Platform notes

- **macOS** (this workspace): Metal backend on any of the three implementations (yawgpu via its
  `metal` feature); link against the backend's `.dylib`/`.a`. The async wrappers use
  `wgpuInstanceWaitAny`/`ProcessEvents`; verify timeout support on Metal for each backend (see
  [03 §8](03-webgpu-c-abstraction.md)).
- **Linux/Windows**: same CMake flow; backend libraries differ. MSVC is a supported compiler;
  compiling the `.spec.cpp` files directly into the executable keeps their static initializers on
  every compiler without `--whole-archive`/`/WHOLEARCHIVE` tricks (see
  [02-harness §1](02-harness.md)).
