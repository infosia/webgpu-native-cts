# F-122 — texture-copy GPU DMA out-of-bounds write (whole-machine freeze, cross-OS)

> Investigation + finding-documentation task. The root-cause **fix is in yawgpu's HAL copy
> path** (separate repo `../yawgpu`); this CTS-side task pinpoints the exact triggering cases,
> records the finding, and decides how the suite carries them so a full sweep no longer hard-hangs.

## Goal

Pinpoint the exact `copyTextureToTexture` / `image_copy` case(s) whose GPU work writes outside the
destination allocation (caught on Linux/VT-d as an IOMMU DMA-write fault; manifests on Windows/NVIDIA
as a TDR/whole-machine freeze), document it as finding F-122, and make the full sweep survivable.

## Background — evidence gathered 2026-06-20 (Linux, Intel Iris 5100 / Haswell, Mesa ANV, Wayland)

A full yawgpu/Vulkan sweep hard-froze the desktop. Triage established it is **not a resource leak**
and identified the culprit operation family:

1. **No userspace Vulkan-object leak.** `api,validation,createView` ran **77,398 subcases in one
   process / one device** under `VK_LAYER_KHRONOS_validation` → object-tracker reported **0 leaks**,
   peak RSS 132 MB. A dozen heavy `api,operation,*` files: 0 leaks each. `GpuTest::finalize()`
   (`src/common/harness.cpp`) releases per-test resources correctly. A userspace leak would
   reproduce identically on llvmpipe — it does not.
2. **No kernel GPU-memory leak.** Intel per-case `--isolate` batch of real-buffer cases →
   `/proc/meminfo` `MemAvailable`/`Shmem` flat (reclaimed on each process exit).
3. **Root cause — GPU DMA out-of-bounds write.** The previous boot's kernel journal
   (`journalctl -k -b -1`) shows repeated VT-d faults from the GPU (`device [00:02.0]`):
   `DMAR: [DMA Write NO_PASID] ... PTE Write access is not set` at 15:37:27, 15:50:04, 15:50:05,
   then the machine froze (last log line 16:02:05). VT-d is active for gfx
   (`i915 ... [drm] VT-d active for gfx access`).
4. **Culprit operations (fault timestamps × per-file sweep log `master.log`):**
   - 15:37:27 → during `webgpu:api,operation,command_buffer,copyTextureToTexture:*` (15:35:48–15:37:38, 242 cases)
   - 15:50:04 / 15:50:05 → during `webgpu:api,operation,command_buffer,image_copy:*` (15:37:38–15:50:07, 2604 cases)
5. **Cross-OS ⇒ driver-agnostic.** The same freeze occurs on Windows/NVIDIA (RTX). Two different
   vendors/drivers/OSes ⇒ the common layer is **the GPU work yawgpu submits**, not an i915/VT-d or
   Mesa-specific bug. Linux/VT-d turns the illegal copy into a clean, attributable DMA-write fault;
   NVIDIA (no gfx-IOMMU layer) turns it into a TDR/hang.
6. Individual DMA faults are **survivable** (the run continued ~13 min and ~110 more files after the
   first fault); they corrupt/accumulate until an eventual hard freeze. `--case-timeout-ms` does
   **not** catch this (a hard GPU hang ignores SIGKILL); only the per-file `master.log` START/END +
   reboot pinpoints the file.

**Suspected defect (in yawgpu HAL copy, fixed-function — NOT naga/shader):** destination
addressing/size math for certain param combos — `bytesPerRow` / `rowsPerImage`, array-layer or
3D-slice offset+extent, block-compressed or depth/stencil copy sizing. May re-open / subsume
README's **F-104 copyTextureToTexture** (previously logged as a MoltenVK-only artifact,
"native-Vulkan-green"); native-Vulkan is **not** green here.

## Scope

**In:**
- Pinpoint the minimal set of triggering cases in `command_buffer,copyTextureToTexture` and
  `command_buffer,image_copy` (and check the `copyBufferToBuffer` / `api,validation,image_copy,*`
  neighbours) using VT-d as the OOB oracle.
- Add a finding **F-122** to `docs/FINDINGS.md` with the evidence above, the minimal repro
  query/queries, and the suspected addressing math.
- Make a full sweep survivable: add the confirmed-hanging case queries to a quarantine/known-hang
  list the runner can skip (see `expectations/yawgpu.crash.txt` convention / `quarantine.txt`), and
  cross-reference from the README "Test results" notes.

**Out (non-goals):**
- The yawgpu HAL copy fix itself (lands in `../yawgpu`; this spec produces the repro that drives it).
- Re-running the whole 345-file Intel sweep to green (blocked until the yawgpu fix lands).
- Any change to `--case-timeout-ms` semantics (it cannot catch a hard GPU hang by design).

## Pinpoint methodology (VT-d as a free OOB-write detector — keep VT-d ON)

Run from a **TTY (Ctrl+Alt+F3) or SSH** so an eventual freeze does not lock out the desktop.

1. For each suspect file, run per-case isolation writing each case query to a progress file **with
   `sync` before launch** (pattern: `build-yawgpu-release/run-linux-vulkan/narrow-createView.sh`):
   `cts --isolate --workers 1 'webgpu:api,operation,command_buffer,image_copy:*'` driven case-by-case.
2. Concurrently `journalctl -k -f | grep -E 'DMAR.*DMA Write|PTE Write access is not set'`. Each
   fault's timestamp maps to the most-recently-STARTed case = a triggering case. Faults are
   survivable, so collect several per pass; on freeze, reboot and resume (skip cases already OK,
   quarantine the dangling RUN).
3. Reduce each triggering case to its distinguishing params (format, dimension, size, mip/layer,
   bytesPerRow/rowsPerImage) for the FINDINGS repro.

## Acceptance criteria

- [ ] `docs/FINDINGS.md` has an **F-122** entry: symptom, cross-OS evidence, exact triggering case
      query/queries, and the suspected yawgpu HAL addressing math.
- [ ] The exact triggering case queries are listed (file + `test:params`), each confirmed to emit a
      `DMAR ... DMA Write ... PTE Write access is not set` for `device [00:02.0]` in `journalctl -k`
      when run alone, and confirmed clean on llvmpipe (`VK_ICD_FILENAMES=.../lvp_icd...`).
- [ ] A quarantine/known-hang list contains those queries so `sweep.sh` (or the equivalent runner
      path) skips them and a resumed sweep does not re-freeze.
- [ ] README "Test results" references F-122 and notes native-Vulkan copy is not green (re: F-104).

## Verification

- Per-case run + `journalctl -k` correlation as in the methodology; record the fault→case mapping.
- llvmpipe control run of the same case(s): `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
  VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation build-yawgpu-release/cts --run-case '<case>'` →
  expect `pass`/`fail` but **no** DMA fault and **no** object-tracker leak (proves it is the
  hardware-DMA copy path, not harness/leak).
- After quarantining, `sweep.sh` completes the remaining files without a freeze (run from TTY/SSH).

## References

- `docs/06-build-and-run.md` §4 (`--isolate`, `--case-timeout-ms`, `--emit-crash-list`), §8 (Linux).
- `docs/FINDINGS.md` — F-104 copyTextureToTexture (re-evaluate); add F-122.
- `expectations/yawgpu.crash.txt` — known-hang/crash list convention.
- `build-yawgpu-release/run-linux-vulkan/` (git-ignored) — `sweep.sh`, `narrow-createView.sh`,
  `master.log`, per-file `jsonl/` — the run artifacts this finding is derived from.
- Memory: `yawgpu-copy-dma-oob-freeze`, `yawgpu-linux-sweep-runbook`.
