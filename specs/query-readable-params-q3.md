# Q3 — readable query params: the remaining named enums (binding-layout + resourceState)

> Final phase of the readable-query-params work (Q1 = operation specs; Q2 = validation
> format/dimension/viewDimension/aspect). Migrate the **last** integer-encoded **named** enums to
> upstream string identifiers. After Q3, every named string enum matches upstream CTS; only genuine
> bitflags/numerics (usage, visibility, mapMode, counts, sizes, indices) remain integer — as upstream
> also encodes them. Implementation by the coding agent; **Claude regenerates the 4 affected
> `expectations/wgpu-native.txt` lines** after the code migration (see below).

## Already done — do NOT touch
`pipelineType` (`"Render"`/`"Compute"`), `bindingEntryKey` (`maxedEntry`/`entry`), and `maybeNullBGLType`
are **already strings**. Bitflags/numerics stay integer: `usage`/`bufferUsage`/`visibility`/`shaderStage`,
`mapMode`, `sampleCount`, `*Count`, `*Index`, `size`/`*Variant`, `lod*Clamp`, internal `feature`.

## Scope — four enums

### A. WebGPU binding-layout enums (new `enum_strings.h` helpers; expectations-safe)
- **`bufferBindingType`** — key `type`. `createBindGroupLayout` (`bufferBindingTypeValues`, l.62),
  `createPipelineLayout` (l.78). `WGPUBufferBindingType` → `"uniform"` / `"storage"` /
  `"read-only-storage"`.
- **`storageTextureAccess`** — key `access`. `createBindGroupLayout`
  (`storageTextureAccessValues` + `…WithUndefined`, l.84/94). `WGPUStorageTextureAccess` →
  `"write-only"` / `"read-only"` / `"read-write"`.
- **`textureSampleType`** — key `sampleType`. `createBindGroupLayout`
  (`textureSampleTypeValuesWithUndefined`, l.103). `WGPUTextureSampleType` → `"float"` /
  `"unfilterable-float"` / `"depth"` / `"sint"` / `"uint"`.

These are expectations-safe (not present in `expectations/`). The `…WithUndefined` producers already use
`Value::undef()` for the "not used" entry — **keep it** (serializes `_undef_`); only the enum values
become strings; reads guard with the existing undefined check (`paramIsUndefined` / the current sentinel
test) **before** calling `parse*`.

### B. `resourceState` (test-local enum; **touches expectations**)
- Keys `state` (`createView`), `bufferState` (`encoding/cmds/clearBuffer`), `srcBufferState` /
  `dstBufferState` (`encoding/cmds/copyBufferToBuffer`). Backed by the per-spec `ResourceState` enum +
  `kResourceStates` + `resourceStateValues()`.
- Map `ResourceState` → the upstream strings **`"valid"` / `"invalid"` / `"destroyed"`** (match each enum
  value to its upstream identifier). Producer emits strings; reads `parseResourceState(t.param<std::string>
  (key))` (a small local or shared mapping — `ResourceState` is test-local, not a WebGPU enum, so it does
  **not** belong in `enum_strings.h`; a tiny per-spec or shared `resource_state.h` helper is fine).
- **Expectations:** `expectations/wgpu-native.txt` has **4** lines that will go stale (the cases now
  serialize the string form):
  ```
  …clearBuffer:buffer_state:bufferState=2
  …copyBufferToBuffer:buffer_state:srcBufferState=0;dstBufferState=2
  …copyBufferToBuffer:buffer_state:srcBufferState=2;dstBufferState=0
  …copyBufferToBuffer:buffer_state:srcBufferState=2;dstBufferState=2
  ```
  **The coding agent does NOT edit `expectations/` (Claude regenerates these 4 lines on real GPU after
  the code migration).** Codex's own acceptance keeps `git status --porcelain expectations/` empty.

## Interfaces

- `src/webgpu/util/enum_strings.h` — add name/parse for `WGPUBufferBindingType`,
  `WGPUStorageTextureAccess`, `WGPUTextureSampleType` (mirror the Q1 helpers; `std::abort()`/assert on
  unknown). No format/registrar change (Q1's hook is unaffected — these keys aren't format keys).
- Producers → string values (reuse the helpers; `…WithUndefined` keeps `Value::undef()`).
- Reads → `parse*(t.param<std::string>(key))` / `parse*(valueAs<std::string>(*findParam(params, key)))`,
  guarding undef first where the producer included it.
- `resourceState` → a small `parseResourceState`/`resourceStateIdentifier` (test-local; `"valid"`/
  `"invalid"`/`"destroyed"`); de-dupe the three `resourceStateValues()` copies into the shared helper if
  practical.

## Acceptance criteria

GPU-free (coding agent — **no GPU runs; do NOT edit `expectations/`**):
- [ ] `cmake --build build-yawgpu --target cts cts_unittests gen_listings` succeeds (+ wgpu/dawn);
      `cts_unittests` exit 0 (add round-trip tests for the three WebGPU enums).
- [ ] `cts --list-cases` prints string params for `type`/`access`/`sampleType` (e.g.
      `type="uniform"`, `access="write-only"`, `sampleType="unfilterable-float"`, `_undef_` preserved)
      and for `state`/`bufferState`/`srcBufferState`/`dstBufferState` (`="valid"|"invalid"|"destroyed"`).
      **No bare integers** remain for these keys; the bitflag/numeric keys are untouched.
- [ ] **Counts unchanged** for the affected tests; round-trip (a `--list-cases` line selects exactly 1).
- [ ] `git status --porcelain expectations/` empty (codex doesn't touch it); `listing.json` unchanged.

Claude (real GPU, sandbox off):
- yawgpu Metal: the migrated specs run with the **same** `summary:` as before.
- **Regenerate the 4 `wgpu-native` resourceState expectation lines** to the new string form (re-run
  `…clearBuffer:buffer_state:*` and `…copyBufferToBuffer:buffer_state:*` on wgpu-native, capture the
  failing-case queries now in `bufferState="…"` form, update the lines), then confirm the wgpu-native
  `--isolate --expectations` run is green again.

## References

- `src/webgpu/util/enum_strings.h` (Q1/Q2) — add the three WebGPU enum helpers here.
- `createBindGroupLayout.spec.cpp` — `bufferBindingTypeValues` (62), `storageTextureAccessValues`/
  `…WithUndefined` (84/94), `textureSampleTypeValuesWithUndefined` (103); keys `type`/`access`/`sampleType`.
- `createPipelineLayout.spec.cpp` — `bufferBindingTypeValues` (78), key `type`. (`pipelineType` already
  string — leave.)
- `createView.spec.cpp` (`state`), `encoding/cmds/clearBuffer.spec.cpp` (`bufferState`),
  `encoding/cmds/copyBufferToBuffer.spec.cpp` (`srcBufferState`/`dstBufferState`) — `ResourceState` /
  `kResourceStates` / `resourceStateValues()`.
- `expectations/wgpu-native.txt` — the 4 `buffer_state` lines (Claude-regenerated).
- Q1/Q2 specs for the pattern (`paramIsUndefined` undef-guard, string producers, no query-engine change).
