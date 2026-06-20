# F-126 — texture-copy GPU DMA out-of-bounds write (whole-machine freeze, cross-OS)

> Investigation + finding-documentation task. The root-cause **fix is in yawgpu's HAL copy
> path** (the separate yawgpu repo); this CTS-side task pinpoints the exact triggering cases,
> records the finding, and decides how the suite carries them so a full sweep no longer hard-hangs.
> (Renumbered F-122 → **F-126**: F-122 was already taken in `docs/FINDINGS.md` by the shift-left
> const-eval finding; earlier commits 5499821/d7c08ac used the colliding "F-122" label.)

## Goal

Pinpoint the exact `copyTextureToTexture` / `image_copy` case(s) whose GPU work writes outside the
destination allocation (caught on Linux/VT-d as an IOMMU DMA-write fault; manifests on Windows/NVIDIA
as a TDR/whole-machine freeze), document it as finding F-126, and make the full sweep survivable.

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

## Code review outcome (2026-06-20) — yawgpu copy path is validation-clean; NOT yet convicted

A static review of the yawgpu repo (`yawgpu-hal/src/vulkan/encode.rs`, `vulkan/texture.rs`;
`yawgpu-core/src/command_encoder.rs`, `copy.rs`) plus safe dynamic validation found **no
validation-detectable defect** in the copy path:

- **Allocation is correct.** Texture memory is sized from the driver's
  `get_image_memory_requirements().size` (full mips/layers/depth) — `texture.rs:182-208`. Not
  undersized, so a valid copy cannot write past the image's real backing on that account.
- **Core bounds-checks are mip-aware.** `validate_texture_copy_subresource`
  (`command_encoder.rs:1961`) validates origin+extent against `texture.subresource_size(mip_level)`;
  `validate_texture_to_texture_copy` (`:1862`) checks `copy_size` against each side's
  `subresource_size(mip)`. Valid copies are correctly bounded before reaching the HAL.
- **Emitted Vulkan commands are valid.** llvmpipe + `VK_LAYER_KHRONOS_validation`: **0 VUID** for
  both `copyTextureToTexture` and `image_copy`.
- **No synchronization hazards.** Same files under sync validation
  (`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`): **0 sync-hazards**.
- **The large llvmpipe correctness fails (~21k / ~20k) are lavapipe artifacts**, not yawgpu bugs —
  these files pass on native Intel.

**Two latent code nitpicks (NOT proven causes; worth fixing regardless):**
1. `encode_texture_to_texture` builds the copy extent from `source.dimension` only
   (`encode.rs` ~824) — asymmetric; safe only under the src==dst dimension rule.
2. HAL `validate_origin_extent` (`texture.rs:55-76`) is **mip-ignorant** (checks base dims); core
   already guards this, so it is a defense-in-depth gap only.

**Conclusion:** attribution is still **open** — either a real-HW-execution-only issue that
validation cannot see, or an i915/ANV-Haswell + VT-d driver bug (the cross-OS NVIDIA freeze leans
toward the former, but the Windows fault location is unconfirmed). The VT-d DMAR fault does **not**
reproduce in cold per-case isolation (`copyTextureToTexture` 247 cases + `createView` 1367 cases ran
clean cold), so it is load/heap-layout dependent. May still relate to README's **F-104
copyTextureToTexture** (logged as a MoltenVK-only artifact) — re-evaluate. The only way to convict
or exonerate yawgpu is to **capture the exact faulting command on hardware** (below).

## Scope

**In:**
- Capture the **exact faulting command** on hardware (HW capture is now the only viable oracle —
  validation is clean and cold per-case does not reproduce). Then inspect that specific command's
  Vulkan parameters to convict or exonerate yawgpu.
- Add a finding **F-126** to `docs/FINDINGS.md` recording the symptom, cross-OS evidence, the
  review outcome (copy path validation-clean), and — once captured — the exact triggering case.
- Address the two latent nitpicks above if confirmed harmful (or at minimum note them).
- Make a full sweep survivable: quarantine confirmed-hanging case queries the runner can skip (see
  `expectations/yawgpu.crash.txt` convention / `quarantine.txt`), cross-referenced from the README.

**Out (non-goals):**
- The yawgpu HAL copy fix itself (lands in the yawgpu repo; this spec produces the repro that drives it).
- Re-running the whole 345-file Intel sweep to green (blocked until the yawgpu fix lands).
- Any change to `--case-timeout-ms` semantics (it cannot catch a hard GPU hang by design).

## HW-capture methodology (validation is clean; cold per-case does not reproduce)

The fault is load/heap-layout dependent, so it must be caught **in context on real hardware**.

**Option A — Intel/Linux, VT-d as the oracle (keep VT-d ON), from a TTY (Ctrl+Alt+F3) or SSH:**
1. Reproduce the original *in-context* condition (cold per-case did NOT fault): run the sweep prefix
   up to and through the copy files, or run `copyTextureToTexture` + `image_copy` back-to-back
   in-process (one device, sustained), each case pre-written to a progress file **with `sync` before
   launch** (pattern `build-yawgpu-release/run-linux-vulkan/narrow-copy.sh`).
2. Concurrently `journalctl -k -f | grep -E 'DMA Write|PTE Write access is not set'`. Map each
   fault's timestamp to the most-recently-STARTed case. Faults are survivable; on freeze, reboot and
   resume.
3. Reduce each triggering case to its params and dump the exact `VkImageCopy`/`VkBufferImageCopy`
   yawgpu emits for it (extent, offset, subresource, bufferRowLength/bufferImageHeight) vs the image
   subresource size — that comparison convicts or clears yawgpu.

**Option B — NVIDIA/Windows (best for a real-HW-execution OOB):** run the suspect cases under
**Nsight Aftermath** or Vulkan **GPU-Assisted Validation (GPU-AV)**, which name the faulting
draw/copy and the offending access directly — no IOMMU/heap-layout dependence.

## Acceptance criteria

- [ ] `docs/FINDINGS.md` has an **F-126** entry: symptom, cross-OS evidence, and the review outcome
      (copy path validation-clean; allocation/region/mip-validation correct; two latent nitpicks).
- [ ] The exact faulting case(s) are captured on hardware and recorded (file + `test:params`), with
      the emitted `VkImageCopy`/`VkBufferImageCopy` parameters vs the image subresource size, so the
      entry states whether yawgpu is convicted or exonerated.
- [ ] Each captured case is confirmed clean on llvmpipe
      (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`) and emits no VUID / sync
      hazard — establishing it is a HW-execution issue, not a malformed command or harness/leak.
- [ ] A quarantine/known-hang list contains those queries so `sweep.sh` (or the equivalent runner
      path) skips them and a resumed sweep does not re-freeze.
- [ ] README "Test results" references F-126 (re: F-104 copyTextureToTexture re-evaluation).

## Verification

- Per-case run + `journalctl -k` correlation as in the methodology; record the fault→case mapping.
- llvmpipe control run of the same case(s): `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
  VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation build-yawgpu-release/cts --run-case '<case>'` →
  expect `pass`/`fail` but **no** DMA fault and **no** object-tracker leak (proves it is the
  hardware-DMA copy path, not harness/leak).
- After quarantining, `sweep.sh` completes the remaining files without a freeze (run from TTY/SSH).

## References

- `docs/06-build-and-run.md` §4 (`--isolate`, `--case-timeout-ms`, `--emit-crash-list`), §8 (Linux).
- `docs/FINDINGS.md` — F-104 copyTextureToTexture (re-evaluate); add F-126.
- `expectations/yawgpu.crash.txt` — known-hang/crash list convention.
- `build-yawgpu-release/run-linux-vulkan/` (git-ignored) — `sweep.sh`, `narrow-createView.sh`,
  `master.log`, per-file `jsonl/` — the run artifacts this finding is derived from.
- Memory: `yawgpu-copy-dma-oob-freeze`, `yawgpu-linux-sweep-runbook`.
