# Q1 — readable query params: enum params as upstream-identical strings (infra + operation specs)

> Harness ergonomics + faithfulness. Replaces the **integer encoding** of named-enum case params
> (`format=22;copyMethod=2;aspect=0`) with the **upstream/WebGPU-spec string identifiers**
> (`format="rgba8unorm";copyMethod="CopyB2T";aspect="depth-only"`), so queries are readable,
> constructible, stable across `webgpu.h` enum renumbering, and **identical to upstream CTS**
> (copy-paste a query from upstream and it runs here). Implementation by the coding agent.

## Why this is low-risk

The harness already supports it end-to-end — **no query-engine change is needed**:
- `Value` is `std::variant<int64_t, bool, double, Undefined, std::string>` (`include/cts/test.h`);
  `stringifyValue` already JSON-quotes strings (`src/common/params.cpp:54` → `"rgba8unorm"`).
- Case matching is pure **string equality** on the serialized params:
  `query.params == stringifyParams(params)` (`src/common/query.cpp:64`), and `--list-cases` emits
  `caseQuery(...)` from the same `stringifyParams`. So if a test stores **string** Values, the listing
  prints readable params and feeding that exact string back matches — round-trips with zero engine work.

Today's tests instead do `static_cast<int64_t>(format)` and read `t.param<int64_t>(...)`, producing the
opaque integers. This task swaps that convention to strings for **named enums only**.

## Scope

**In (Phase Q1):**
- Centralized string↔enum helpers for the named enums upstream represents as strings:
  `WGPUTextureFormat`, `WGPUTextureDimension`, `WGPUTextureViewDimension`, `WGPUTextureAspect`.
- Migrate the **operation** specs that carry the worst pain — `api/operation/command_buffer/image_copy`
  and `.../copyTextureToTexture` — including their per-test string enums (`copyMethod`, `initMethod`,
  `checkMethod`, `aspect`).
- Update the `--sample-formats` registrar to read **string** format identifiers (it currently reads the
  integer format values to decide thinning — see `src/webgpu/format_sample_registrar.cpp`).
- Unit tests for the new name/parse round-trips + a `stringifyParams` test showing the quoted form.

**Out (do NOT change here):**
- **Numeric / index / bitflag params** stay as-is (upstream uses numbers for them too): `mipLevel`,
  `offset`, `size`, `sampleCount`, counts, `*Index`, and `usage` bitflags (`usage1`/`usage2`/`usage`,
  `maxedEntry`, …). These already match upstream and appear in `expectations/wgpu-native.txt` — leaving
  them untouched keeps expectations stable.
- **The validation specs** (`createView`, `createTexture`, `createBindGroupLayout`, `createSampler`,
  `buffer/*`, `encoding/*`) — a follow-up (Q2) that reuses the same helpers. (`viewDimension` there only
  appears in expectations as `_undef_`, which is representation-independent.)
- `mapMode` — leave numeric for now (upstream uses the `GPUMapMode` flag value); revisit in Q2 if needed.
- The non-hierarchical query model and partial-param matching (the "B"/"C" items) — separate tasks.

## Upstream string identifiers (authoritative — use these EXACT strings)

Queries must match upstream CTS, i.e. the **WebGPU-spec format identifiers** and CTS enum strings:

- **`WGPUTextureFormat`** → the spec identifier: `r8unorm`, `rg8unorm`, `rgba8unorm`, `rgba8unorm-srgb`,
  `bgra8unorm`, `bgra8unorm-srgb`, `rgb10a2unorm`, `rgb10a2uint`, `rg11b10ufloat`, `rgb9e5ufloat`,
  `*8snorm/uint/sint`, `*16uint/sint/float`, `*32uint/sint/float`, depth/stencil
  (`depth16unorm`, `depth24plus`, `depth24plus-stencil8`, `depth32float`, `depth32float-stencil8`,
  `stencil8`), and compressed (`bc1-rgba-unorm`, `bc1-rgba-unorm-srgb`, …, `etc2-*`, `eac-*`,
  `astc-4x4-unorm`, `astc-4x4-unorm-srgb`, …). Source of truth: the upstream CTS `kTextureFormatInfo`
  keys / the WebGPU spec `GPUTextureFormat` enum (pinned `b507bd1`).
- **`WGPUTextureDimension`** → `"1d"`, `"2d"`, `"3d"`.
- **`WGPUTextureViewDimension`** → `"1d"`, `"2d"`, `"2d-array"`, `"cube"`, `"cube-array"`, `"3d"`;
  the undefined/auto value stays `Value::undef()` (serializes to `_undef_`, unchanged).
- **`WGPUTextureAspect`** → `"all"`, `"depth-only"`, `"stencil-only"`.
- **Per-test string enums** (exact upstream casing): `copyMethod` ∈ `{WriteTexture, CopyB2T, CopyT2B}`,
  `initMethod` ∈ `{WriteTexture, CopyB2T}`, `checkMethod` ∈ `{FullCopyT2B, PartialCopyT2B}`.

## Interfaces

### `src/webgpu/texture_format.h` — format identifier

Add an `identifier` (the upstream string) to each entry of the existing `TextureFormatInfo` table and:
```cpp
std::string_view textureFormatIdentifier(WGPUTextureFormat);   // RGBA8Unorm -> "rgba8unorm"
WGPUTextureFormat parseTextureFormat(std::string_view);        // "rgba8unorm" -> RGBA8Unorm (assert on unknown)
```

### Small enum-string helpers (new `src/webgpu/util/enum_strings.{h,cpp}` or extend an existing util)

```cpp
std::string_view textureDimensionIdentifier(WGPUTextureDimension);   WGPUTextureDimension parseTextureDimension(std::string_view);
std::string_view textureViewDimensionIdentifier(WGPUTextureViewDimension); WGPUTextureViewDimension parseTextureViewDimension(std::string_view);
std::string_view textureAspectIdentifier(WGPUTextureAspect);         WGPUTextureAspect parseTextureAspect(std::string_view);
```

### A shared string value-producer (replaces the per-spec `*Values()` int casts)

```cpp
// emits string Values for a list of formats (for combine("format", …))
std::vector<Value> formatIdentifierValues(std::span<const WGPUTextureFormat>);
```
Use it for every format-valued key: `format`, `srcFormat`, `dstFormat`, `viewFormat`, `textureFormat`.
The per-spec `colorFormatValues()`, `depthStencilFormatValues()`, `textureDimensionValues()`,
`depthStencilAspectValues()`, `depthStencilCopyMethodValues()` become thin wrappers over the shared
string producers (or are deleted in favor of them).

### Reads

Replace `static_cast<WGPUTextureFormat>(t.param<int64_t>("format"))` with
`parseTextureFormat(t.param<std::string>("format"))`, and likewise for dimension/viewDimension/aspect.
Per-test enums: keep the `enum class`, but map to/from its upstream string with a small local table
(e.g. `copyMethodIdentifier(ImageCopyType)` / `parseCopyMethod(std::string_view)`), and emit string
Values in the params builder.

### `--sample-formats` registrar

`src/webgpu/format_sample_registrar.cpp` thins format-swept keys by inspecting the param **value**.
Update it to parse the **string** identifier (`parseTextureFormat`) instead of reading an `int64`, for
the hooked keys `{format, textureFormat, viewFormat, srcFormat, dstFormat}`. The thinning behavior and
`kRepresentativeTextureFormats` membership test (`isRepresentativeTextureFormat`) are unchanged — only
the value decode changes.

## Migration (this task)

1. Add the helpers + `formatIdentifierValues` + per-format `identifier` table.
2. Migrate `image_copy.spec.cpp` and `copyTextureToTexture.spec.cpp`: value-producers → string,
   reads → `parse*`, per-test enums (`copyMethod`/`initMethod`/`checkMethod`/`aspect`) → string.
3. Update the `--sample-formats` registrar to parse string formats.
4. Regenerate `src/webgpu/listing.json` (`gen_listings`) — catalog is file/test level so it should be
   unchanged; confirm no diff.
5. Add `cts_unittests`: name↔enum round-trip for all four enums (every value), one `parse*` reject/asserts
   path, and a `stringifyParams` test asserting `format="rgba8unorm";dimension="2d"`-style output.

## Acceptance criteria

GPU-free (coding agent — **no GPU runs**):
- [ ] `cmake --build build-yawgpu --target cts cts_unittests gen_listings` succeeds (+ wgpu/dawn);
      `cts_unittests` exits 0 (incl. new round-trip + stringify tests).
- [ ] `cts --list-cases 'webgpu:api,operation,command_buffer,copyTextureToTexture:copy_depth_stencil:*'`
      now prints params like `…:format="depth16unorm";srcCopyLevel=0;…;aspect="…"` (no bare `format=45`);
      same for `image_copy:rowsPerImage_and_bytesPerRow_depth_stencil:*`
      (`copyMethod="CopyB2T";aspect="stencil-only"`). **Case/subcase counts are unchanged**
      (12/864, 12/288, etc. — only the param *representation* changes).
- [ ] Round-trip: pipe one `--list-cases` line back as the query (single-quoted) and it selects exactly
      that one case.
- [ ] `git status --porcelain expectations/` empty (migrated keys don't appear in expectations except
      `viewDimension=_undef_`, which is unchanged). `src/webgpu/listing.json` unchanged.

Claude verifies on real GPU (sandbox off, Metal): the two migrated operation files run **identically**
(same `summary:` pass/skip/fail as before the migration) under the new string params; an upstream CTS
query string for one of these tests selects the matching case here.

## Verification

1. Build `cts cts_unittests gen_listings`; `cts_unittests` → 0.
2. The `--list-cases` readability + count-parity checks; round-trip; `git status --porcelain expectations/`
   empty; `listing.json` unchanged.

## References

- `include/cts/test.h` — `Value` (22), `ParamRecord`, `valueAs<std::string>`, `param<T>`.
- `src/common/params.cpp` — `stringifyValue` (54, JSON-quotes strings), `stringifyParams` (80).
- `src/common/query.cpp` — `queryMatchesCase` (57, string-equality match), `caseQuery` (67).
- `src/webgpu/texture_format.h` — `TextureFormatInfo` (22, add `identifier`), `kColorTextureFormats`,
  `kDepthStencilFormats`, `textureFormatInfo()`.
- `src/webgpu/format_sample_registrar.cpp` / `include/cts/format_sample.h` — the `--sample-formats`
  value decode to update.
- Specs to migrate: `src/webgpu/api/operation/command_buffer/image_copy.spec.cpp`
  (`colorFormatValues`/`depthStencilFormatValues`/`depthStencilAspectValues`/`depthStencilCopyMethodValues`/
  `textureDimensionValues`; `t.param<int64_t>` reads) and `…/copyTextureToTexture.spec.cpp`
  (`regularTextureFormatValues`/`depthStencilFormatValues`/`textureDimensionValues`).
- Upstream (pinned `b507bd1`): the `GPUTextureFormat` identifiers and the `copyMethod`/`initMethod`/
  `checkMethod`/`aspect` strings these tests use.
- Follow-up **Q2** (separate spec): migrate the `api/validation` specs to the same helpers.
