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

Language: `CXX` (C++17) only. Tests and harness are all C++; tests call the WebGPU **C** API.

---

## 2. Backend selection

A single cache variable selects the implementation under test:

```bash
cmake -S . -B build -DCTS_BACKEND=wgpu-native   # or: -DCTS_BACKEND=dawn
```

`CTS_BACKEND` controls:

- which canonical `webgpu.h` include path is used,
- which backend shim TU is compiled (`backend_wgpu.cpp` vs `backend_dawn.cpp`,
  see [03-webgpu-c-abstraction §6](03-webgpu-c-abstraction.md)),
- a `CTS_BACKEND_WGPU` / `CTS_BACKEND_DAWN` compile definition (for the rare backend-specific
  test), and
- which library the runner links.

### Locating the backend library

Two supported modes, chosen by additional cache vars:

| Mode | Vars | Behavior |
|------|------|----------|
| **Prebuilt** | `CTS_WGPU_NATIVE_DIR` / `CTS_DAWN_DIR` pointing at an install/build dir | Use `find_library` + a known header dir; link the prebuilt `.a`/`.so`/`.dylib` |
| **FetchContent / submodule** | default | Add the backend as a subproject and build it (Dawn via its CMake; wgpu-native via its Makefile/meson invoked from CMake, or a prebuilt release) |

For wgpu-native (built from `../wgpu-native`): it produces `libwgpu_native` and ships
`ffi/webgpu.h` + `ffi/webgpu-headers/webgpu.h`. The prebuilt mode points `CTS_WGPU_NATIVE_DIR`
at its build output. For Dawn (`../../C/dawn`): it exposes a `webgpu_dawn` (or
`dawn::webgpu_dawn`) target and `include/webgpu/webgpu.h`.

The slice will wire **one** backend first (wgpu-native, since it lives in this workspace and is
simplest to link as a C library), then add Dawn.

---

## 3. Building

```bash
# Configure (pick a backend + how to find it)
cmake -S . -B build \
      -DCTS_BACKEND=wgpu-native \
      -DCTS_WGPU_NATIVE_DIR=/path/to/wgpu-native/target/release

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
| `--adapter-name <substr>` | pick an adapter by name substring |
| `--enable-feature <name>` | request an optional feature for device creation |
| `--future-timeout-ms <n>` | timeout for async-wait wrappers (default 5000) |
| `--expectations <file>` | known-failure list; listed cases may fail without failing the run |
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

- Matrix over `CTS_BACKEND ∈ {wgpu-native, dawn}` and OS.
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

- **macOS** (this workspace): Metal backend via either implementation; link against the
  backend's `.dylib`/`.a`. The async wrappers use `wgpuInstanceWaitAny`/`ProcessEvents`; verify
  timeout support on Metal for both backends (see [03 §8](03-webgpu-c-abstraction.md)).
- **Linux/Windows**: same CMake flow; backend libraries differ. MSVC is a supported compiler;
  compiling the `.spec.cpp` files directly into the executable keeps their static initializers on
  every compiler without `--whole-archive`/`/WHOLEARCHIVE` tricks (see
  [02-harness §1](02-harness.md)).
