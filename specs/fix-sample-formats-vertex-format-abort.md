# P-6 — `--sample-formats` must not abort on non-texture `format` params

> Bug fix found while verifying P-5. See [`format-sampling-mode.md`](format-sampling-mode.md) and
> [`reference/workflow.md`](reference/workflow.md).

## Goal

The `--sample-formats` hook treats a string `format`-like param that is not a texture format
identifier as "not recognized" instead of aborting the process, so fast-mode enumeration of
`webgpu:*` (and `webgpu:api,*`) completes.

## Background

`src/webgpu/format_sample_registrar.cpp` classifies params named `format`, `textureFormat`,
`viewFormat`, `srcFormat`, `dstFormat` by calling `cts::parseTextureFormat()` on string values;
`parseTextureFormat()` (`src/webgpu/texture_format.h`) calls `std::abort()` for unknown identifiers.
Four tests use `format` for **vertex** formats (`"uint8x2"`, …), so any `--sample-formats` run whose
query includes them dies silently (exit 134, no output) during case enumeration:

- `webgpu:api,operation,vertex_state,correctness:vertex_format_to_shader_format_conversion:*`
- `webgpu:api,validation,render_pipeline,vertex_state:vertex_shader_type_matches_attribute_format:*`
- `webgpu:api,validation,render_pipeline,vertex_state:vertex_attribute_offset_alignment:*`
- `webgpu:api,validation,render_pipeline,vertex_state:vertex_attribute_contained_in_stride:*`

## Scope

**In:**
- `src/webgpu/texture_format.h`: add
  `inline std::optional<WGPUTextureFormat> tryParseTextureFormat(std::string_view identifier);`
  (nullopt for unknown identifiers) and implement `parseTextureFormat()` on top of it (still aborts
  on unknown — existing callers unchanged).
- `src/webgpu/format_sample_registrar.cpp`: for string values use `tryParseTextureFormat()`; if it
  returns nullopt, return `std::nullopt` from the hook (param not recognized → no sampling effect).
  Numeric values: unchanged.
- GPU-free unit test (`src/unittests/main.cpp`, which already includes `webgpu/texture_format.h`):
  `tryParseTextureFormat("rgba8unorm") == WGPUTextureFormat_RGBA8Unorm`,
  `tryParseTextureFormat("uint8x2") == std::nullopt`, `tryParseTextureFormat("") == std::nullopt`.

**Out:** changing which texture formats are representative; renaming test params; the numeric path.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene.
- [ ] `build-yawgpu/cts_unittests` exits 0 with the new test.
- [ ] `cts --list-cases --sample-formats 'webgpu:*'` exits 0.
- [ ] For each of the four tests above, `--list-cases --sample-formats '<test>:*'` output equals its
      `--list-cases '<test>:*'` output (vertex formats are never thinned).
- [ ] `--list-cases --sample-formats` output for `webgpu:shader,*`, `webgpu:web_platform,*`,
      `webgpu:util,*`, `webgpu:idl,*`, `webgpu:compat,*` and every other `webgpu:api,*` test is
      byte-identical to the pre-change binary.
- [ ] Changes confined to `src/webgpu/texture_format.h`, `src/webgpu/format_sample_registrar.cpp`,
      `src/unittests/main.cpp`.

## References

- [`format-sampling-mode.md`](format-sampling-mode.md) — hook semantics (nullopt = not recognized).
