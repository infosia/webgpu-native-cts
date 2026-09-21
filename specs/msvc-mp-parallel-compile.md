# msvc-mp-parallel-compile — compile `cts` in parallel under the Visual Studio generator

## Goal

Make a Windows/MSVC build of `cts` actually use the machine's cores by enabling MSVC's per-project
multi-process compilation (`/MP`).

## Background

`CLAUDE.md` mandates `cmake --build ... -j 8`. Under the `Visual Studio 17 2022` generator `-j 8`
becomes MSBuild `/m:8`, which parallelizes **across projects** only. `cts` is a single project
holding all 642 `.spec.cpp` TUs, so they compile through **one** `cl.exe` — observed 2026-09-21:
one `cl.exe` process, an idle CPU, and a ~15 minute full rebuild where the Linux (Ninja/Make) build
takes ~3 minutes. `/MP` is the MSVC switch that fans one project's TUs out over multiple `cl.exe`
processes.

## Scope

**In:**
- Add `/MP` to the MSVC compile options in `cts_configure_target()` (`CMakeLists.txt`), so every
  target configured through it (`cts_harness`, `cts`, `gen_listings`, `cts_unittests`) gets it.
- Update `docs/06-build-and-run.md` §8 *Platform notes → Windows* with one bullet explaining that
  the build passes `/MP` because `-j` alone does not parallelize a single VS project.

**Out (non-goals):**
- No change for non-MSVC compilers (the generator expression must stay MSVC-only).
- No switch to Ninja, no precompiled headers, no unity builds.
- No cap/tuning of the `/MP` process count (bare `/MP` = one process per logical core; build-time
  CPU/memory is not the constraint on this machine — see `CLAUDE.md` *Tooling — builds*).
- No edits to `CLAUDE.md` or `specs/`.

## Interfaces

```cmake
function(cts_configure_target target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8 /MP>
    )
endfunction()
```

## Acceptance criteria

- [ ] `CMakeLists.txt` passes `/MP` for MSVC only, via `cts_configure_target()`; no other compile
      option changes.
- [ ] A clean `cmake --build build-yawgpu --config Release --target cts -j 8` succeeds with
      `/W4 /WX` still clean (no new warnings, in particular no D9030 `/MP`-incompatibility warning).
- [ ] During that build more than one `cl.exe` runs concurrently.
- [ ] `build-yawgpu/Release/cts.exe --list 'webgpu:*'` prints the same test list as before the change.
- [ ] `docs/06-build-and-run.md` §8 carries the `/MP` note.

## Verification

```bash
cmake -S . -B build-yawgpu            # regenerate
cmake --build build-yawgpu --config Release --target cts -j 8 --clean-first
tasklist | grep -ci '^cl.exe'         # while building: > 1
build-yawgpu/Release/cts.exe --list 'webgpu:*' | wc -l
```

## References

- `docs/06-build-and-run.md` §3 (build convention), §8 (Windows platform notes)
- `CLAUDE.md` *Tooling — builds*
