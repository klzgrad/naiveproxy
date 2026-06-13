--
-- Copyright 2022 The Android Open Source Project
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
INCLUDE PERFETTO MODULE android.surfaceflinger;

SELECT RUN_METRIC('android/jank/relevant_threads.sql');

CREATE OR REPLACE PERFETTO FUNCTION vsync_from_name(slice_name STRING)
RETURNS STRING AS
SELECT CAST(STR_SPLIT($slice_name, " ", 1) AS INTEGER);

CREATE OR REPLACE PERFETTO FUNCTION gpu_completion_fence_id_from_name(slice_name STRING)
RETURNS STRING AS
SELECT
  CASE
    WHEN
      $slice_name GLOB "GPU completion fence *"
    THEN
      CAST(STR_SPLIT($slice_name, " ", 3) AS INTEGER)
    WHEN
      $slice_name GLOB "Trace GPU completion fence *"
    THEN
      CAST(STR_SPLIT($slice_name, " ", 4) AS INTEGER)
    WHEN
      $slice_name GLOB "waiting for GPU completion *"
    THEN
      CAST(STR_SPLIT($slice_name, " ", 4) AS INTEGER)
    WHEN
      $slice_name GLOB "Trace HWC release fence *"
    THEN
      CAST(STR_SPLIT($slice_name, " ", 4) AS INTEGER)
    WHEN
      $slice_name GLOB "waiting for HWC release *"
    THEN
      CAST(STR_SPLIT($slice_name, " ", 4) AS INTEGER)
    ELSE NULL
  END;

-- Find Choreographer#doFrame slices that are between the CUJ markers.
-- We extract vsync IDs from doFrame slice names and use these as the source
-- of truth that allow us to get correct slices on the other threads.
DROP TABLE IF EXISTS android_jank_cuj_do_frame_slice;
CREATE PERFETTO TABLE android_jank_cuj_do_frame_slice AS
SELECT
  cuj.cuj_id,
  main_thread.upid,
  main_thread.utid,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  vsync_from_name(slice.name) AS vsync
FROM android_jank_cuj cuj
JOIN slice
  ON slice.ts + slice.dur >= cuj.ts AND slice.ts <= cuj.ts_end
JOIN android_jank_cuj_main_thread main_thread
  ON cuj.cuj_id = main_thread.cuj_id
    AND main_thread.track_id = slice.track_id
WHERE
  slice.name GLOB 'Choreographer#doFrame*'
-- Ignore child slice e.g. "Choreographer#doFrame - resynced to 1234 in 20.0ms"
  AND slice.name not GLOB '*resynced*'
  AND slice.dur > 0
  AND vsync > 0
  AND (vsync >= begin_vsync OR begin_vsync is NULL)
  AND (vsync <= end_vsync OR end_vsync is NULL)
  -- In some malformed traces we see nested doFrame slices.
  -- If that is the case, we ignore all parent doFrames and only keep the one
  -- the lowest in the hierarchy.
  AND NOT EXISTS (
    SELECT 1 FROM descendant_slice(slice.id) child
    WHERE child.name GLOB 'Choreographer#doFrame*'
    AND child.name NOT GLOB '*resynced*'
  );


-- Store render thread DrawFrames by matching in the vsync IDs extracted from
-- doFrame slices. In case of multiple layers being drawn, there might be
-- multiple DrawFrames for a single vsync.
DROP TABLE IF EXISTS android_jank_cuj_draw_frame_slice;
CREATE PERFETTO TABLE android_jank_cuj_draw_frame_slice AS
SELECT
  cuj_id,
  render_thread.upid,
  render_thread.utid,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  vsync_from_name(slice.name) AS vsync
FROM _android_jank_cuj_do_frames do_frame
JOIN android_jank_cuj_render_thread render_thread USING (cuj_id)
JOIN slice
  ON slice.track_id = render_thread.track_id
WHERE slice.name GLOB 'DrawFrame*'
  AND vsync_from_name(slice.name) = do_frame.vsync
  AND slice.dur > 0;

-- Find descendants of DrawFrames which contain the GPU completion fence ID that
-- is used for signaling that the GPU finished drawing.
DROP TABLE IF EXISTS android_jank_cuj_gpu_completion_fence;
CREATE PERFETTO TABLE android_jank_cuj_gpu_completion_fence AS
SELECT
  cuj_id,
  vsync,
  draw_frame.id AS draw_frame_slice_id,
  gpu_completion_fence_id_from_name(fence.name) AS fence_idx
FROM android_jank_cuj_draw_frame_slice draw_frame
JOIN descendant_slice(draw_frame.id) fence
  ON fence.name GLOB '*GPU completion fence*';

-- Similarly find descendants of DrawFrames which have the HWC release fence ID
DROP TABLE IF EXISTS android_jank_cuj_hwc_release_fence;
CREATE PERFETTO TABLE android_jank_cuj_hwc_release_fence AS
SELECT
  cuj_id,
  vsync,
  draw_frame.id AS draw_frame_slice_id,
  gpu_completion_fence_id_from_name(fence.name) AS fence_idx
FROM android_jank_cuj_draw_frame_slice draw_frame
JOIN descendant_slice(draw_frame.id) fence
  ON fence.name GLOB '*HWC release fence *';

-- Find HWC release slices which indicate when the HWC released the buffer.
DROP TABLE IF EXISTS android_jank_cuj_hwc_release_slice;
CREATE PERFETTO TABLE android_jank_cuj_hwc_release_slice AS
SELECT
  fence.cuj_id,
  vsync,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  fence.fence_idx,
  draw_frame_slice_id
FROM android_jank_cuj_hwc_release_thread hwc_release_thread
JOIN slice USING (track_id)
JOIN android_jank_cuj_hwc_release_fence fence
  ON fence.cuj_id = hwc_release_thread.cuj_id
    AND fence.fence_idx = gpu_completion_fence_id_from_name(slice.name)
WHERE
  slice.name GLOB 'waiting for HWC release *'
  AND slice.dur > 0;

-- Find GPU completion slices which indicate when the GPU finished drawing.
DROP TABLE IF EXISTS android_jank_cuj_gpu_completion_slice;
CREATE PERFETTO TABLE android_jank_cuj_gpu_completion_slice AS
SELECT
  fence.cuj_id,
  vsync,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  hwc_release.ts_end AS hwc_release_ts_end,
  fence.fence_idx
FROM android_jank_cuj_gpu_completion_thread gpu_completion_thread
JOIN slice USING (track_id)
JOIN android_jank_cuj_gpu_completion_fence fence
  ON fence.cuj_id = gpu_completion_thread.cuj_id
  AND fence.fence_idx = gpu_completion_fence_id_from_name(slice.name)
LEFT JOIN android_jank_cuj_hwc_release_slice hwc_release
  USING (cuj_id, vsync, draw_frame_slice_id)
WHERE
  slice.name GLOB 'waiting for GPU completion *'
  AND slice.dur > 0;

CREATE OR REPLACE PERFETTO FUNCTION find_android_jank_cuj_sf_main_thread_slice(
  slice_name_glob STRING)
RETURNS TABLE(
  cuj_id INT, utid INT, vsync INT, id INT,
  name STRING, ts LONG, dur LONG, ts_end LONG)
AS
WITH sf_vsync AS (
  SELECT DISTINCT cuj_id, sf_vsync AS vsync
  FROM _android_jank_cuj_app_sf_frame_timeline_match)
SELECT
  cuj_id,
  utid,
  sf_vsync.vsync,
  slice.id,
  slice.name,
  slice.ts,
  slice.dur,
  slice.ts + slice.dur AS ts_end
FROM slice
JOIN _android_sf_main_thread main_thread USING (track_id)
JOIN sf_vsync
  ON vsync_from_name(slice.name) = sf_vsync.vsync
WHERE slice.name GLOB $slice_name_glob AND slice.dur > 0
ORDER BY cuj_id, vsync;

DROP TABLE IF EXISTS android_jank_cuj_sf_commit_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_commit_slice AS
SELECT * FROM FIND_ANDROID_JANK_CUJ_SF_MAIN_THREAD_SLICE('commit *');

DROP TABLE IF EXISTS android_jank_cuj_sf_composite_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_composite_slice AS
SELECT * FROM FIND_ANDROID_JANK_CUJ_SF_MAIN_THREAD_SLICE('composite *');

-- Older builds do not have the commit/composite but onMessageInvalidate instead
DROP TABLE IF EXISTS android_jank_cuj_sf_on_message_invalidate_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_on_message_invalidate_slice AS
SELECT * FROM FIND_ANDROID_JANK_CUJ_SF_MAIN_THREAD_SLICE('onMessageInvalidate *');

DROP VIEW IF EXISTS android_jank_cuj_sf_root_slice;
CREATE PERFETTO VIEW android_jank_cuj_sf_root_slice AS
SELECT * FROM android_jank_cuj_sf_commit_slice
UNION ALL
SELECT * FROM android_jank_cuj_sf_composite_slice
UNION ALL
SELECT * FROM android_jank_cuj_sf_on_message_invalidate_slice;

-- Find descendants of SF main thread slices which contain the GPU completion fence ID that
-- is used for signaling that the GPU finished drawing.
DROP TABLE IF EXISTS android_jank_cuj_sf_gpu_completion_fence;
CREATE PERFETTO TABLE android_jank_cuj_sf_gpu_completion_fence AS
SELECT
  cuj_id,
  vsync,
  sf_root_slice.id AS sf_root_slice_id,
  gpu_completion_fence_id_from_name(fence.name) AS fence_idx
FROM android_jank_cuj_sf_root_slice sf_root_slice
JOIN descendant_slice(sf_root_slice.id) fence
  ON fence.name GLOB '*GPU completion fence*';

-- Find GPU completion slices which indicate when the GPU finished drawing.
DROP TABLE IF EXISTS android_jank_cuj_sf_gpu_completion_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_gpu_completion_slice AS
SELECT
  fence.cuj_id,
  vsync,
  slice.*,
  slice.ts + slice.dur AS ts_end,
  fence.fence_idx
FROM android_jank_cuj_sf_gpu_completion_fence fence
JOIN android_jank_cuj_sf_gpu_completion_thread gpu_completion_thread
JOIN slice
  ON slice.track_id = gpu_completion_thread.track_id
    AND fence.fence_idx = gpu_completion_fence_id_from_name(slice.name)
WHERE
  slice.name GLOB 'waiting for GPU completion *'
  AND slice.dur > 0;


-- Find REThreaded::drawLayers on RenderEngine thread.
-- These will be only relevant if SF is doing client composition so we check if
-- the drawLayers slice is completely within the bounds of composeSurfaces on SF
-- main thread.
DROP TABLE IF EXISTS android_jank_cuj_sf_draw_layers_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_draw_layers_slice AS
WITH compose_surfaces AS (
  SELECT
    cuj_id,
    vsync,
    sf_root_slice.id AS sf_root_slice_id,
    compose_surfaces.ts,
    compose_surfaces.ts + compose_surfaces.dur AS ts_end
  FROM android_jank_cuj_sf_root_slice sf_root_slice
  JOIN descendant_slice(sf_root_slice.id) compose_surfaces
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
JOIN android_jank_cuj_sf_render_engine_thread re_thread
JOIN slice draw_layers
  ON draw_layers.track_id = re_thread.track_id
    AND draw_layers.ts >= compose_surfaces.ts
    AND draw_layers.ts + draw_layers.dur <= compose_surfaces.ts_end
WHERE
  draw_layers.name = 'REThreaded::drawLayers'
  AND draw_layers.dur > 0;
