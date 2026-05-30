# cleanup-async-wait — remove the dead WaitAny flag + placeholder; standardize on pumpUntil

Follows [`reference/workflow.md`](reference/workflow.md). A code-honesty cleanup raised by the
[Phase 2b](../docs/07-roadmap.md) review: the harness already uses `pumpUntil` (ProcessEvents
polling) for **every** backend, but the codebase still carries an unwired WaitAny path —
`backendSupportsTimeoutWaitAny()` (no callers; Dawn even returns `true`) and `waitFuture` (a
placeholder that ignores its future and always times out). Make the code reflect reality.

## Goal

Remove the dead `backendSupportsTimeoutWaitAny()` and the placeholder `waitFuture`; keep `pumpUntil`
as the single async-wait primitive. No behavior change.

## Scope

**In:**

- Remove `bool backendSupportsTimeoutWaitAny()` from `src/common/webgpu/backend.h` and its three
  definitions in `backend_wgpu.cpp`, `backend_yawgpu.cpp`, `backend_dawn.cpp`. (Confirmed: it has
  **no callers**.)
- Remove the placeholder `WGPUWaitStatus waitFuture(WGPUInstance, WGPUFuture, uint64_t)` from
  `src/common/webgpu/sync.h` and `sync.cpp`. (Confirmed: no callers; the typed wrappers use
  `pumpUntil`.)
- Keep `pumpUntil` and the typed sync wrappers (`requestAdapterSync`, `requestDeviceSync`,
  `popErrorScopeSync`, …) exactly as they are — they already poll via the callback's `completed`
  flag. **No functional change.**
- If `pumpUntil` was internal (anonymous namespace) it can stay internal; if it was declared in
  `sync.h` it may stay (it is the real primitive now).

**Out:** wiring an actual WaitAny path; any behavior change; new tests; touching other backends'
logic; Dawn rebuild.

## Rationale (why drop, not wire)

`wgpuInstanceWaitAny` is unimplemented on the current wgpu-native build (it panics — see
[FINDINGS F-001](../docs/FINDINGS.md) context), so that backend must poll regardless; and
ProcessEvents polling works uniformly on wgpu-native, yawgpu, and Dawn (all ported files pass on all
three). A single poll path is simpler and honest. If a future backend ever requires WaitAny, the
capability-gated design in `docs/03 §2` can be implemented then.

## Acceptance criteria

- [ ] All three backends build clean (C++20 -Werror), no warnings:
      `CTS_BACKEND=wgpu-native`, `=yawgpu`, `=dawn` (reuse `build-wgpu`/`build-yawgpu`/`build-dawn`).
- [ ] `cts_unittests` exits 0 on each.
- [ ] `git grep backendSupportsTimeoutWaitAny` and `git grep -w waitFuture` return **nothing** (the
      symbols are gone).
- [ ] No behavior change — spot-check on real GPU (outside sandbox): each backend still runs a
      ported file as before, e.g. `…:buffer,create:limit:*` → 3 pass on wgpu-native and Dawn,
      `…:buffer,create:usage:*` → 156 on yawgpu; `--isolate` runs unchanged.
- [ ] No edits under `docs/`/`specs/`; no `git` actions.

## Verification

```bash
for b in build-wgpu build-yawgpu build-dawn; do
  cmake --build "$b" -j 2>&1 | grep -iE 'warning:|error:' && echo "$b: ISSUES" || echo "$b: clean"
  "$b/cts_unittests"; echo "$b unittests=$?"
done
git grep -n 'backendSupportsTimeoutWaitAny' || echo "flag gone"
git grep -nw 'waitFuture' || echo "waitFuture gone"
# real GPU spot-check (outside sandbox):
build-wgpu/cts 'webgpu:api,validation,buffer,create:limit:*' | tail -1
build-yawgpu/cts 'webgpu:api,validation,buffer,create:usage:*' | tail -1
build-dawn/cts 'webgpu:api,validation,buffer,create:limit:*' | tail -1
```

## References

- [docs/03-webgpu-c-abstraction.md §2](../docs/03-webgpu-c-abstraction.md) (Claude will update it to
  describe the single `pumpUntil` path on acceptance), [docs/07-roadmap.md Phase 2b follow-up](../docs/07-roadmap.md).
- Files: `src/common/webgpu/{backend.h,backend_wgpu.cpp,backend_yawgpu.cpp,backend_dawn.cpp,sync.h,sync.cpp}`.
