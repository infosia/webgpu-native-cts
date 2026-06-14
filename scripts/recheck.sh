#!/usr/bin/env bash
# Regression guard: isolate-verify the yawgpu/Vulkan tests that have been findings, after a yawgpu
# change. Edit WATCH below as findings open/close (add a query when one surfaces; keep it after the
# fix lands so a regression is caught). Delegates to scripts/isolate.sh — run from the repo root.
#
# Expected current state (yawgpu >= e7db246): every query below should report fail=0.
set -u
HERE=$(dirname "$0")
WATCH=(
  # F-103 — Vulkan-HAL 3D / multi-slice copy slice-stride (fixed e7db246; the 4th is stencil8 stencil-only)
  'webgpu:api,operation,command_buffer,image_copy:offsets_and_sizes:*'
  'webgpu:api,operation,command_buffer,image_copy:origins_and_extents:*'
  'webgpu:api,operation,command_buffer,image_copy:rowsPerImage_and_bytesPerRow:*'
  'webgpu:api,operation,command_buffer,image_copy:rowsPerImage_and_bytesPerRow_depth_stencil:*'
  # F-096 — texture subresource usage-scope hazards (fixed 5ed5ada)
  'webgpu:api,validation,resource_usages,texture,in_render_common:subresources,multiple_bind_groups:*'
  # F-095 — buffer usage-scope hazards (fixed c0e5ba7)
  'webgpu:api,validation,resource_usages,buffer,in_pass_encoder:subresources,buffer_usage_in_one_render_pass_with_one_draw:*'
  # F-093b — vertex-buffer OOB indirect draws aborting the queue (fixed c0e5ba7)
  'webgpu:api,validation,encoding,cmds,render,draw:vertex_buffer_OOB:*'
)
exec bash "$HERE/isolate.sh" "${WATCH[@]}"
