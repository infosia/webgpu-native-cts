# yawgpu backend selection — `CTS_YAWGPU_BACKEND` (+ Mac Vulkan via MoltenVK)

> Small harness feature. Lets a yawgpu cts build pick the yawgpu instance backend
> (Metal / Vulkan / GLES) at **runtime**, so the same machine can exercise yawgpu's Vulkan HAL on Mac
> (through MoltenVK) without editing the harness. Implementation by the coding agent.

## Goal

`src/common/webgpu/backend_yawgpu.cpp` currently hardcodes the yawgpu instance backend by platform
(`YAWGPU_INSTANCE_BACKEND_METAL` under `#if defined(__APPLE__)`, else `VULKAN`). Add a **`CTS_YAWGPU_BACKEND`
environment override** so the backend can be chosen at runtime, and document the **Mac → Vulkan via
MoltenVK** recipe (which this enables). This is what let us confirm F-031 is unfixed on yawgpu's Vulkan
HAL (`copy_depth_stencil` `pass=36 fail=180` on Vulkan vs `216/0` on Metal — see `docs/FINDINGS.md`).

## Scope

**In:**
- `CTS_YAWGPU_BACKEND` env override in `createInstance()` (yawgpu shim only).
- A `docs/06-build-and-run.md` subsection documenting the env var **and** the full Mac-Vulkan-via-MoltenVK
  recipe (separate yawgpu vulkan build, `CTS_YAWGPU_LIB`, `DYLD_LIBRARY_PATH`).

**Out:**
- No change to the default (no env var ⇒ exact current platform behavior).
- No change to wgpu-native / Dawn shims, the build system, or CMake. (Selecting Vulkan still requires a
  yawgpu lib built `--features vulkan`; that's a build-time/lib concern, documented, not code.)
- No automatic MoltenVK/`DYLD_LIBRARY_PATH` plumbing in the harness — it stays an env the operator sets.

## Interfaces

### `src/common/webgpu/backend_yawgpu.cpp`

Keep the existing `#if defined(__APPLE__)` default. **After** it sets the platform default, apply an
env override (include `<cstdlib>` and `<cstring>`):

```cpp
// Runtime override of the yawgpu instance backend (default: platform choice above).
// e.g. CTS_YAWGPU_BACKEND=vulkan to drive yawgpu's Vulkan HAL on macOS via MoltenVK.
// The selected backend must be compiled into the linked libyawgpu.a (yawgpu --features <backend>);
// if it isn't, yawgpu returns a NULL instance with a clear message and the run fails fast.
if (const char* sel = std::getenv("CTS_YAWGPU_BACKEND")) {
    if (std::strcmp(sel, "metal") == 0) {
        backendSelect.backend = YAWGPU_INSTANCE_BACKEND_METAL;
    } else if (std::strcmp(sel, "vulkan") == 0) {
        backendSelect.backend = YAWGPU_INSTANCE_BACKEND_VULKAN;
    } else if (std::strcmp(sel, "gles") == 0) {
        backendSelect.backend = YAWGPU_INSTANCE_BACKEND_GLES;
    }
    // unknown / empty → leave the platform default unchanged
}
```

(`yawgpu.h`: `YAWGPU_INSTANCE_BACKEND_{NOOP=0, METAL=1, VULKAN=2, GLES=3}`.) Unknown values must be a
no-op (keep the platform default) — do not error.

### `docs/06-build-and-run.md` — extend §2 "Backend selection"

Add a short subsection (English; **placeholders only, no absolute/user paths**). Cover:

- **`CTS_YAWGPU_BACKEND`** ∈ `{metal, vulkan, gles}` overrides the yawgpu instance backend at runtime
  (default = platform: Metal on macOS, Vulkan elsewhere). The chosen backend must be compiled into the
  linked `libyawgpu.a` (yawgpu built with the matching `--features` flag).
- **Running yawgpu on Vulkan on macOS (via MoltenVK)** — the recipe, with placeholders:
  1. Build yawgpu's Vulkan lib into a **separate** target dir so it doesn't clobber the Metal lib:
     `CARGO_TARGET_DIR=<yawgpu>/target-vulkan cargo build --release -p yawgpu --features vulkan`.
  2. Configure a dedicated build dir pointing at that lib (CMake's `find_library` finds the Metal
     `target/release` first, so set `CTS_YAWGPU_LIB` explicitly):
     `cmake -S . -B build-yawgpu-vulkan -DCTS_BACKEND=yawgpu -DCTS_YAWGPU_DIR=<yawgpu>
     -DCTS_YAWGPU_LIB=<yawgpu>/target-vulkan/release/libyawgpu.a` then
     `cmake --build build-yawgpu-vulkan --target cts`.
  3. Run with `CTS_YAWGPU_BACKEND=vulkan` **and `DYLD_LIBRARY_PATH=$VULKAN_SDK/lib`** (required — `ash`
     can't load the Vulkan loader otherwise and yawgpu reports a silent `BackendUnavailable`), plus the
     usual `VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json`. Real-GPU run ⇒ sandbox off.
  - Note it is **Vulkan-API-via-MoltenVK**, not native Vulkan: it faithfully exercises yawgpu's Vulkan
    HAL (good for HAL bug triage) but a failure *could* be a MoltenVK gap; authoritative Vulkan
    conformance is native (Windows/NVIDIA).

## Acceptance criteria

GPU-free (coding agent — **no GPU runs**):
- [ ] `cmake --build build-yawgpu --target cts cts_unittests` succeeds (+ build-wgpu / build-dawn `cts`).
      (`backend_yawgpu.cpp` only compiles in the yawgpu build; just confirm no breakage.)
- [ ] Code review: with `CTS_YAWGPU_BACKEND` unset the selected backend is byte-for-byte the prior
      platform default; unknown values are a no-op; only `metal`/`vulkan`/`gles` are recognized.
- [ ] `docs/06-build-and-run.md` updated; **no absolute or user-specific paths** (placeholders only —
      `<yawgpu>`, `$VULKAN_SDK`).
- [ ] `git status --porcelain expectations/` empty.

Claude verifies on real GPU (sandbox off):
- `build-yawgpu` (Metal lib): unset ⇒ Metal (`api,validation`/`image_copy` unchanged); `=metal` ⇒ Metal;
  `=vulkan` ⇒ clean NULL-instance failure ("built without feature=vulkan"), no crash.
- `build-yawgpu-vulkan` (Vulkan lib): `CTS_YAWGPU_BACKEND=vulkan DYLD_LIBRARY_PATH=$VULKAN_SDK/lib` ⇒
  `basic:*` passes and `copy_depth_stencil` reproduces F-031 (`pass=36 fail=180`).

## Verification

1. Build `cts cts_unittests` (build-yawgpu) + `cts` (wgpu/dawn); confirm clean.
2. Review the default-unchanged + unknown-no-op behavior; `git status --porcelain expectations/` empty.

## References

- `src/common/webgpu/backend_yawgpu.cpp` — `createInstance()` (the `#if defined(__APPLE__)` backend
  select); `backend.h` interface.
- `yawgpu.h` (`<yawgpu>/yawgpu/ffi/webgpu-headers/yawgpu.h`) — `YaWGPUInstanceBackendSelect`,
  `YAWGPU_INSTANCE_BACKEND_{NOOP,METAL,VULKAN,GLES}`, `YAWGPU_STYPE_INSTANCE_BACKEND_SELECT`.
- `docs/06-build-and-run.md` §2 "Backend selection" and "Locating the backend library"
  (`CTS_YAWGPU_DIR` / `CTS_YAWGPU_LIB`).
- `docs/FINDINGS.md` — F-031 (the Vulkan confirmation this feature enabled).
