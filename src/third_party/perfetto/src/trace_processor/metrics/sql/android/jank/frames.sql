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

INCLUDE PERFETTO MODULE android.frames.jank_type;
INCLUDE PERFETTO MODULE android.frames.timeline;

DROP TABLE IF EXISTS android_jank_cuj_frame_timeline;
CREATE PERFETTO TABLE android_jank_cuj_frame_timeline AS
WITH actual_timeline_with_vsync AS (
  SELECT
    *,
    CAST(name AS INTEGER) AS vsync
  FROM actual_frame_timeline_slice
  WHERE dur > 0
)
SELECT
  cuj_id,
  vsync,
  -- We use MAX to check if at least one of the layers jank_type matches the pattern
  MAX(android_is_app_jank_type(jank_type)) AS app_missed,
  -- We use MAX to check if at least one of the layers jank_type matches the pattern
  MAX(android_is_sf_jank_type(jank_type)) AS sf_missed,
  IFNULL(MAX(sf_callback_missed), 0) AS sf_callback_missed,
  IFNULL(MAX(hwui_callback_missed), 0) AS hwui_callback_missed,
  -- We use MIN to check if ALL layers finished on time
  MIN(on_time_finish) AS on_time_finish,
  MAX(timeline.ts + timeline.dur) AS ts_end_actual,
  MAX(timeline.dur) AS dur,
  -- At the moment of writing we expect to see at most one expected_frame_timeline_slice
  -- for a given vsync but using MAX here in case this changes in the future.
  -- In case expected timeline is missing, as a fallback we use the typical frame deadline
  -- for 60Hz.
  COALESCE(MAX(expected.dur), 16600000) AS dur_expected,
  COUNT(DISTINCT timeline.layer_name) as number_of_layers_for_frame,
  -- we use MAX to get at least one of the frame's layer names
  MAX(timeline.layer_name) as frame_layer_name
FROM android_jank_cuj_vsync_boundary boundary
JOIN actual_timeline_with_vsync timeline
  ON vsync >= vsync_min
     AND vsync <= vsync_max
LEFT JOIN expected_frame_timeline_slice expected
  ON expected.upid = timeline.upid AND expected.name = timeline.name
LEFT JOIN _vsync_missed_callback missed_callback USING(vsync)
WHERE
  boundary.layer_id IS NULL
  OR (
    timeline.layer_name GLOB '*#*'
    AND boundary.layer_id
      = CAST(STR_SPLIT(timeline.layer_name, '#', 1) AS INTEGER))
GROUP BY cuj_id, vsync;

DROP TABLE IF EXISTS android_jank_cuj_layer_name;
CREATE PERFETTO TABLE android_jank_cuj_layer_name AS
SELECT
    cuj_id,
    MAX(frame_layer_name) as layer_name
FROM android_jank_cuj_frame_timeline timeline
GROUP BY cuj_id
-- Return only cujs where the max number of layers for all frames in the whole cuj equals 1,
-- this is to infer the layer name if the cuj marker for layer id is not present
HAVING MAX(number_of_layers_for_frame) = 1;

-- Matches slices and boundaries to compute estimated frame boundaries across
-- all threads. Joins with the actual timeline to figure out which frames missed
-- the deadline and whether the app process or SF are at fault.
DROP TABLE IF EXISTS android_jank_cuj_frame;
CREATE PERFETTO TABLE android_jank_cuj_frame AS
WITH frame_base AS (
  SELECT
    cuj_id,
    ROW_NUMBER() OVER (PARTITION BY cuj_id ORDER BY do_frame.vsync ASC) AS frame_number,
    vsync,
    boundary.ts,
    boundary.ts_expected,
    boundary.ts_do_frame_start,
    COUNT(fence_idx) AS gpu_fence_count,
    COUNT(fence_idx) > 0 AS drew_anything
  FROM _android_jank_cuj_do_frames do_frame
  JOIN android_jank_cuj_main_thread_frame_boundary boundary USING (cuj_id, vsync)
  JOIN android_jank_cuj_draw_frame_slice draw_frame USING (cuj_id, vsync)
  LEFT JOIN android_jank_cuj_gpu_completion_fence fence USING (cuj_id, vsync)
  WHERE draw_frame.id = fence.draw_frame_slice_id
  GROUP BY cuj_id, vsync, boundary.ts, boundary.ts_do_frame_start
)
SELECT
  frame_base.*,
  app_missed,
  sf_missed,
  sf_callback_missed,
  hwui_callback_missed,
  on_time_finish,
  ts_end_actual - ts AS dur,
  ts_end_actual - ts_do_frame_start AS dur_unadjusted,
  dur_expected,
  ts_end_actual AS ts_end
FROM frame_base
JOIN android_jank_cuj_frame_timeline USING (cuj_id, vsync);

-- Similar to `android_jank_cuj_frame` computes overall SF frame boundaries.
-- The computation is somewhat simpler as most of SF work happens within the duration of
-- the commit/composite slices on the main thread.
DROP TABLE IF EXISTS android_jank_cuj_sf_frame;
CREATE PERFETTO TABLE android_jank_cuj_sf_frame AS
WITH android_jank_cuj_timeline_sf_frame AS (
    SELECT DISTINCT
      cuj_id,
      CAST(timeline.name AS INTEGER) AS vsync,
      timeline.display_frame_token
    FROM android_jank_cuj_vsync_boundary boundary
    JOIN actual_frame_timeline_slice timeline
      ON
        boundary.upid = timeline.upid
        AND CAST(timeline.name AS INTEGER) >= vsync_min
        AND CAST(timeline.name AS INTEGER) <= vsync_max
    WHERE
        boundary.layer_id IS NULL
      OR (
        timeline.layer_name GLOB '*#*'
        AND boundary.layer_id = CAST(STR_SPLIT(timeline.layer_name, '#', 1) AS INTEGER))
),
android_jank_cuj_sf_frame_base AS (
    SELECT DISTINCT
      boundary.cuj_id,
      boundary.vsync,
      boundary.ts,
      boundary.ts_main_thread_start,
      boundary.ts_end,
      boundary.dur,
      actual_timeline.jank_tag = 'Self Jank' AS sf_missed,
      NULL AS app_missed, -- for simplicity align schema with android_jank_cuj_frame
      jank_tag,
      jank_type,
      prediction_type,
      present_type,
      gpu_composition,
      -- In case expected timeline is missing, as a fallback we use the typical frame deadline
      -- for 60Hz.
      -- See similar expression in android_jank_cuj_frame_timeline.
      COALESCE(expected_timeline.dur, 16600000) AS dur_expected
    FROM android_jank_cuj_sf_main_thread_frame_boundary boundary
    JOIN android_jank_cuj_sf_process sf_process
    JOIN actual_frame_timeline_slice actual_timeline
      ON actual_timeline.upid = sf_process.upid
        AND boundary.vsync = CAST(actual_timeline.name AS INTEGER)
    JOIN android_jank_cuj_timeline_sf_frame ft
      ON CAST(actual_timeline.name AS INTEGER) = ft.display_frame_token
        AND boundary.cuj_id = ft.cuj_id
    LEFT JOIN expected_frame_timeline_slice expected_timeline
      ON expected_timeline.upid = actual_timeline.upid
        AND expected_timeline.name = actual_timeline.name
)
SELECT
 *,
 ROW_NUMBER() OVER (PARTITION BY cuj_id ORDER BY vsync ASC) AS frame_number
FROM android_jank_cuj_sf_frame_base;
