--
-- Copyright 2026 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE android.cujs.base;

INCLUDE PERFETTO MODULE android.cujs.threads;

INCLUDE PERFETTO MODULE android.surfaceflinger;

-- Extracts the vsync ID from a slice name like "Choreographer#doFrame 1234".
CREATE PERFETTO FUNCTION _vsync_from_name(slice_name STRING)
RETURNS STRING
AS
SELECT CAST(STR_SPLIT($slice_name, " ", 1) AS INTEGER);

-- Extracts the GPU completion fence ID from fence-related slice names.
CREATE PERFETTO FUNCTION _gpu_completion_fence_id_from_name(slice_name STRING)
RETURNS STRING
AS
SELECT
  CASE
    WHEN $slice_name GLOB "GPU completion fence *" THEN CAST(STR_SPLIT(
      $slice_name,
      " ",
      3
    ) AS INTEGER)
    WHEN $slice_name GLOB "Trace GPU completion fence *" THEN CAST(STR_SPLIT(
      $slice_name,
      " ",
      4
    ) AS INTEGER)
    WHEN $slice_name GLOB "waiting for GPU completion *" THEN CAST(STR_SPLIT(
      $slice_name,
      " ",
      4
    ) AS INTEGER)
    WHEN $slice_name GLOB "Trace HWC release fence *" THEN CAST(STR_SPLIT(
      $slice_name,
      " ",
      4
    ) AS INTEGER)
    WHEN $slice_name GLOB "waiting for HWC release *" THEN CAST(STR_SPLIT(
      $slice_name,
      " ",
      4
    ) AS INTEGER)
    ELSE NULL
  END;

-- Choreographer#doFrame slices between the CUJ markers.
-- We extract vsync IDs from doFrame slice names and use these as the source
-- of truth that allow us to get correct slices on the other threads.
CREATE PERFETTO TABLE _android_jank_cuj_do_frame_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process id.
  upid JOINID(process.id),
  -- Thread id.
  utid JOINID(thread.id),
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the doFrame slice.
  ts TIMESTAMP,
  -- Duration of the doFrame slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the doFrame slice.
  ts_end TIMESTAMP,
  -- Vsync ID extracted from the doFrame slice name.
  vsync LONG
)
AS
SELECT
  cuj.cuj_id,
  main_thread.upid,
  main_thread.utid,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  _vsync_from_name(slice.name) AS vsync
FROM android_jank_cuj AS cuj
JOIN slice
  ON slice.ts + slice.dur >= cuj.ts
  AND slice.ts <= cuj.ts_end
JOIN _android_jank_cuj_main_thread AS main_thread
  ON cuj.cuj_id = main_thread.cuj_id
  AND main_thread.track_id = slice.track_id
WHERE
  slice.name GLOB 'Choreographer#doFrame*'
  -- Ignore child slice e.g. "Choreographer#doFrame - resynced to 1234 in 20.0ms"
  AND slice.name NOT GLOB '*resynced*'
  AND slice.dur > 0
  AND vsync > 0
  AND (vsync >= begin_vsync OR begin_vsync IS NULL)
  AND (vsync <= end_vsync OR end_vsync IS NULL)
  -- In some malformed traces we see nested doFrame slices.
  -- If that is the case, we ignore all parent doFrames and only keep the one
  -- the lowest in the hierarchy.
  AND NOT EXISTS (
    SELECT 1
    FROM descendant_slice(slice.id) AS child
    WHERE
      child.name GLOB 'Choreographer#doFrame*'
      AND child.name NOT GLOB '*resynced*'
  );

-- Render thread DrawFrame slices matched by vsync IDs extracted from
-- doFrame slices. In case of multiple layers being drawn, there might be
-- multiple DrawFrames for a single vsync.
CREATE PERFETTO TABLE _android_jank_cuj_draw_frame_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process id.
  upid JOINID(process.id),
  -- Thread id.
  utid JOINID(thread.id),
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the DrawFrame slice.
  ts TIMESTAMP,
  -- Duration of the DrawFrame slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the DrawFrame slice.
  ts_end TIMESTAMP,
  -- Vsync ID extracted from the DrawFrame slice name.
  vsync LONG
)
AS
SELECT
  cuj_id,
  render_thread.upid,
  render_thread.utid,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  _vsync_from_name(slice.name) AS vsync
FROM _android_jank_cuj_do_frames AS do_frame
JOIN android_jank_cuj_render_thread AS render_thread USING (cuj_id)
JOIN slice
  ON slice.track_id = render_thread.track_id
WHERE
  slice.name GLOB 'DrawFrame*'
  AND _vsync_from_name(slice.name) = do_frame.vsync
  AND slice.dur > 0;

-- Descendants of DrawFrames containing the GPU completion fence ID used for
-- signaling that the GPU finished drawing.
CREATE PERFETTO TABLE _android_jank_cuj_gpu_completion_fence(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Vsync ID of the frame.
  vsync LONG,
  -- Slice ID of the parent DrawFrame.
  draw_frame_slice_id ID(slice.id),
  -- Fence index extracted from the fence slice name.
  fence_idx LONG
)
AS
SELECT
  cuj_id,
  vsync,
  draw_frame.id AS draw_frame_slice_id,
  _gpu_completion_fence_id_from_name(fence.name) AS fence_idx
FROM _android_jank_cuj_draw_frame_slice AS draw_frame
JOIN descendant_slice(draw_frame.id) AS fence
  ON fence.name GLOB '*GPU completion fence*';

-- Descendants of DrawFrames containing the HWC release fence ID.
CREATE PERFETTO TABLE _android_jank_cuj_hwc_release_fence(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Vsync ID of the frame.
  vsync LONG,
  -- Slice ID of the parent DrawFrame.
  draw_frame_slice_id ID(slice.id),
  -- Fence index extracted from the fence slice name.
  fence_idx LONG
)
AS
SELECT
  cuj_id,
  vsync,
  draw_frame.id AS draw_frame_slice_id,
  _gpu_completion_fence_id_from_name(fence.name) AS fence_idx
FROM _android_jank_cuj_draw_frame_slice AS draw_frame
JOIN descendant_slice(draw_frame.id) AS fence
  ON fence.name GLOB '*HWC release fence *';

-- HWC release slices indicating when the HWC released the buffer.
CREATE PERFETTO TABLE _android_jank_cuj_hwc_release_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Vsync ID of the frame.
  vsync LONG,
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the HWC release slice.
  ts TIMESTAMP,
  -- Duration of the HWC release slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the HWC release slice.
  ts_end TIMESTAMP,
  -- Fence index used for matching.
  fence_idx LONG,
  -- Slice ID of the parent DrawFrame.
  draw_frame_slice_id ID(slice.id)
)
AS
SELECT
  fence.cuj_id,
  vsync,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  fence.fence_idx,
  draw_frame_slice_id
FROM _android_jank_cuj_hwc_release_thread AS hwc_release_thread
JOIN slice USING (track_id)
JOIN _android_jank_cuj_hwc_release_fence AS fence
  ON fence.cuj_id = hwc_release_thread.cuj_id
  AND fence.fence_idx = _gpu_completion_fence_id_from_name(slice.name)
WHERE
  slice.name GLOB 'waiting for HWC release *'
  AND slice.dur > 0;

-- GPU completion slices indicating when the GPU finished drawing.
CREATE PERFETTO TABLE _android_jank_cuj_gpu_completion_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Vsync ID of the frame.
  vsync LONG,
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the GPU completion slice.
  ts TIMESTAMP,
  -- Duration of the GPU completion slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the GPU completion slice.
  ts_end TIMESTAMP,
  -- End timestamp of the corresponding HWC release slice (NULL if not found).
  hwc_release_ts_end TIMESTAMP,
  -- Fence index used for matching.
  fence_idx LONG
)
AS
SELECT
  fence.cuj_id,
  vsync,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  hwc_release.ts_end AS hwc_release_ts_end,
  fence.fence_idx
FROM _android_jank_cuj_gpu_completion_thread AS gpu_completion_thread
JOIN slice USING (track_id)
JOIN _android_jank_cuj_gpu_completion_fence AS fence
  ON fence.cuj_id = gpu_completion_thread.cuj_id
  AND fence.fence_idx = _gpu_completion_fence_id_from_name(slice.name)
LEFT JOIN _android_jank_cuj_hwc_release_slice AS hwc_release USING (
  cuj_id,
  vsync,
  draw_frame_slice_id
)
WHERE
  slice.name GLOB 'waiting for GPU completion *'
  AND slice.dur > 0;

-- NOTE: _find_android_jank_cuj_sf_main_thread_slice, _android_jank_cuj_sf_commit_slice,
-- _android_jank_cuj_sf_composite_slice, and _android_jank_cuj_sf_on_message_invalidate_slice
-- are defined in android.surfaceflinger module (included above).

CREATE PERFETTO VIEW _android_jank_cuj_sf_root_slice AS
SELECT * FROM _android_jank_cuj_sf_commit_slice
UNION ALL
SELECT * FROM _android_jank_cuj_sf_composite_slice
UNION ALL
SELECT * FROM _android_jank_cuj_sf_on_message_invalidate_slice;

-- Descendants of SF main thread slices containing the GPU completion
-- fence ID that is used for signaling that the GPU finished drawing.
CREATE PERFETTO TABLE _android_jank_cuj_sf_gpu_completion_fence(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Vsync ID of the SF frame.
  vsync LONG,
  -- Slice ID of the parent SF root slice.
  sf_root_slice_id ID(slice.id),
  -- Fence index extracted from the fence slice name.
  fence_idx LONG
)
AS
SELECT
  cuj_id,
  vsync,
  sf_root_slice.id AS sf_root_slice_id,
  _gpu_completion_fence_id_from_name(fence.name) AS fence_idx
FROM _android_jank_cuj_sf_root_slice AS sf_root_slice
JOIN descendant_slice(sf_root_slice.id) AS fence
  ON fence.name GLOB '*GPU completion fence*';

-- SF GPU completion slices indicating when the GPU finished drawing.
CREATE PERFETTO TABLE _android_jank_cuj_sf_gpu_completion_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Vsync ID of the SF frame.
  vsync LONG,
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the GPU completion slice.
  ts TIMESTAMP,
  -- Duration of the GPU completion slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the GPU completion slice.
  ts_end TIMESTAMP,
  -- Fence index used for matching.
  fence_idx LONG
)
AS
SELECT
  fence.cuj_id,
  vsync,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  fence.fence_idx
FROM _android_jank_cuj_sf_gpu_completion_fence AS fence
JOIN _android_jank_cuj_sf_gpu_completion_thread AS gpu_completion_thread
JOIN slice
  ON slice.track_id = gpu_completion_thread.track_id
  AND fence.fence_idx = _gpu_completion_fence_id_from_name(slice.name)
WHERE
  slice.name GLOB 'waiting for GPU completion *'
  AND slice.dur > 0;

-- REThreaded::drawLayers on RenderEngine thread.
-- These will be only relevant if SF is doing client composition so we check if
-- the drawLayers slice is completely within the bounds of composeSurfaces on SF
-- main thread.
CREATE PERFETTO TABLE _android_jank_cuj_sf_draw_layers_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Thread id of the RenderEngine thread.
  utid JOINID(thread.id),
  -- Vsync ID of the SF frame.
  vsync LONG,
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the drawLayers slice.
  ts TIMESTAMP,
  -- Duration of the drawLayers slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the drawLayers slice.
  ts_end TIMESTAMP,
  -- Timestamp of the composeSurfaces slice bounding this drawLayers.
  ts_compose_surfaces TIMESTAMP
)
AS
WITH
  compose_surfaces AS (
    SELECT
      cuj_id,
      vsync,
      sf_root_slice.id AS sf_root_slice_id,
      compose_surfaces.ts,
      compose_surfaces.ts + compose_surfaces.dur AS ts_end
    FROM _android_jank_cuj_sf_root_slice AS sf_root_slice
    JOIN descendant_slice(sf_root_slice.id) AS compose_surfaces
      ON compose_surfaces.name = 'composeSurfaces'
  )
SELECT
  cuj_id,
  re_thread.utid,
  vsync,
  draw_layers.*,
  draw_layers.ts + draw_layers.dur AS ts_end,
  -- Store composeSurfaces ts as this will simplify calculating frame boundaries
  compose_surfaces.ts AS ts_compose_surfaces
FROM compose_surfaces
JOIN _android_jank_cuj_sf_render_engine_thread AS re_thread
JOIN slice AS draw_layers
  ON draw_layers.track_id = re_thread.track_id
  AND draw_layers.ts >= compose_surfaces.ts
  AND draw_layers.ts + draw_layers.dur <= compose_surfaces.ts_end
WHERE
  draw_layers.name = 'REThreaded::drawLayers'
  AND draw_layers.dur > 0;
