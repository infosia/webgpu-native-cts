# Q2 — readable query params: migrate the validation specs' format/dimension/viewDimension/aspect

> Phase 2 of the readable-query-params work (Q1 did the operation specs). Migrate the
> **`format` / `dimension` / `viewDimension` / `aspect`** named-enum params in the **validation** specs
> from the integer encoding to the upstream string identifiers, **reusing the Q1 infrastructure**
> (`parseTextureFormat` / `formatIdentifierValues` / `parseTextureDimension` /
> `parseTextureViewDimension` / `parseTextureAspect` in `texture_format.h` + `util/enum_strings.h`).
> Implementation by the coding agent. **No new helpers, no query-engine change.**

## Scope

**In — these four enums only, across the specs that use them:**
- **`format`** (and `viewFormat`, `feature`-gated format lists): `createView`, `createTexture`,
  `createBindGroupLayout`. Producers: `allTextureFormatValues`, `textureFormatValues`,
  `regularTextureFormatValues`, `compressedTextureFormatValues`, `uncompressedTextureFormatValues`,
  `textureFormatsForFeatureParam`, `viewFormatsForFeatureParam`. → `formatIdentifierValues(...)`.
- **`dimension`**: `createView` (`textureDimensionValues`), `createTexture`
  (`textureDimensionValuesWithUndefined`). → string identifiers, `parseTextureDimension` on read.
- **`viewDimension`**: `createView` (`textureViewDimensionValuesWithUndefined`,
  `textureViewCubeDimensionValues`), `createBindGroupLayout` (`textureViewDimensionValuesWithUndefined`).
  → string identifiers, `parseTextureViewDimension` on read.
- **`aspect`**: `createView` (`textureAspectValues`). → string identifiers, `parseTextureAspect`.

**Out (deferred to Q3 — do NOT touch here):**
- `resourceState` (the `state` / `bufferState` / `srcBufferState` / `dstBufferState` params) — these
  appear in `expectations/wgpu-native.txt`, so migrating them needs an expectations regeneration
  (Claude-owned). Leave them integer-encoded.
- The binding-layout enums `bufferBindingType`, `storageTextureAccess`, `textureSampleType`,
  `pipelineType`, and the test-local enums (`maybeNullBGLType`, `bindingEntryKey`,
  `emptyBindGroupLayoutType`) — new helpers + per-enum upstream-string verification; a separate task.
- **Bitflags / numerics** stay integer (upstream too): `usage`/`bufferUsage` bitflags, `shaderStage`/
  `visibility`, `sampleCount`, `*Count`, `*Index`, `size`/`sizeVariant`, `lod*Clamp`, `mapMode`.

## Implementation notes (reuse Q1)

- Replace each format producer body with `return formatIdentifierValues(<the span/list it iterates>);`
  (it already iterates a `WGPUTextureFormat` list). For the **feature-param** producers
  (`textureFormatsForFeatureParam` / `viewFormatsForFeatureParam`), keep their filtering logic and emit
  the surviving formats via `textureFormatIdentifier(...)` / `formatIdentifierValues`.
- Reads: `parseTextureFormat(t.param<std::string>("format"))`, `parseTextureDimension(...)`,
  `parseTextureViewDimension(...)`, `parseTextureAspect(...)`; in `filter`/`expand` predicates use
  `parse*(valueAs<std::string>(*findParam(params, key)))`.
- **`*WithUndefined` producers** mix `Value::undef()` with the enum values. Keep the `undef()` entry
  (serializes to `_undef_`, unchanged); only the **non-undef** entries become strings. On read, check
  `std::holds_alternative<Value::Undefined>` (or the existing undef test) **before** calling `parse*`,
  exactly as the current code checks for the undefined sentinel before the int cast.
- `include "webgpu/util/enum_strings.h"` where needed; the `--sample-formats` registrar already decodes
  string format identifiers (Q1) and keeps an int64 fallback, so no registrar change is needed.

## Acceptance criteria

GPU-free (coding agent — **no GPU runs**):
- [ ] `cmake --build build-yawgpu --target cts cts_unittests gen_listings` succeeds (+ wgpu/dawn);
      `cts_unittests` exit 0.
- [ ] `cts --list-cases` for a migrated test prints **string** format/dimension/viewDimension/aspect
      params (e.g. `createView:…:format="rgba8unorm";dimension="2d";…`, `…;viewDimension="2d-array"`,
      `…;aspect="depth-only"`), with `_undef_` preserved where the producer used `Value::undef()`.
      **No bare `format=…`/`dimension=…`/`viewDimension=…`/`aspect=…` integers** remain in those specs.
- [ ] **Case/subcase counts unchanged** for the migrated tests (compare a few `cts --list` totals
      before/after — only the representation changes).
- [ ] Round-trip: a `--list-cases` line, single-quoted, selects exactly that one case.
- [ ] `git status --porcelain expectations/` **empty** (the migrated keys don't appear in expectations
      except `viewDimension=_undef_`, which is representation-independent). `listing.json` unchanged.
- [ ] The deferred params (`resourceState`/`bufferState`/`srcBufferState`, `usage`, `bufferBindingType`,
      …) are **untouched** — they still serialize as integers.

Claude verifies on real GPU (sandbox off, Metal): the migrated validation specs run with the **same**
`summary:` (pass/skip/fail/crash) as before — including the `--isolate --expectations` run for
`api,validation` (the wgpu-native expectations still match, since only out-of-scope keys appear there).

## Verification

1. Build `cts cts_unittests gen_listings`; `cts_unittests` → 0.
2. The `--list-cases` readability + count-parity + round-trip checks; `git status --porcelain
   expectations/` empty; `listing.json` unchanged.

## References

- `src/webgpu/texture_format.h` — `textureFormatIdentifier` / `parseTextureFormat` (Q1).
- `src/webgpu/util/enum_strings.h` — `parseTextureDimension` / `parseTextureViewDimension` /
  `parseTextureAspect` / `formatIdentifierValues` (Q1).
- Specs to migrate: `src/webgpu/api/validation/createView.spec.cpp` (most: format/dimension/
  viewDimension/aspect + the feature-param format lists; note `state`=resourceState is **out**),
  `…/createTexture.spec.cpp` (format/dimension), `…/createBindGroupLayout.spec.cpp` (format +
  viewDimension; the binding-type enums are **out**).
- Q1 spec `specs/query-readable-params.md` (the pattern + the `--sample-formats` registrar fallback).
- Follow-up **Q3** (separate spec): `resourceState` (with expectations regen) + the binding-layout /
  sampler / pipeline-type enums.
