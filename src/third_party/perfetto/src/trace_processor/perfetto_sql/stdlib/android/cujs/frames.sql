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
--

INCLUDE PERFETTO MODULE android.frames.jank_type;

INCLUDE PERFETTO MODULE android.frames.timeline;

INCLUDE PERFETTO MODULE android.cujs.base;

INCLUDE PERFETTO MODULE android.cujs.boundaries;

INCLUDE PERFETTO MODULE android.cujs.relevant_slices;

INCLUDE PERFETTO MODULE android.cujs.threads;

INCLUDE PERFETTO MODULE android.surfaceflinger;

-- Matches actual frame timeline slices within CUJ vsync boundaries
-- to aggregate frame stats like app/SF missed status, callback delays, and layer names.
CREATE PERFETTO TABLE _android_jank_cuj_frame_timeline(
  -- CUJ id.
  cuj_id LONG,
  -- Vsync ID of this frame.
  vsync LONG,
  -- Whether the app missed the frame deadline.
  app_missed LONG,
  -- Whether SF missed the frame deadline.
  sf_missed LONG,
  -- Jank score of the frame (severity of jank).
  jank_score LONG,
  -- Whether SF callback was missed.
  sf_callback_missed LONG,
  -- Whether HWUI callback was missed.
  hwui_callback_missed LONG,
  -- Whether the frame finished on time.
  on_time_finish LONG,
  -- Actual end timestamp of the frame.
  ts_end_actual TIMESTAMP,
  -- Actual duration of the frame.
  dur DURATION,
  -- Expected duration of the frame.
  dur_expected DURATION,
  -- Number of layers drawn for this frame.
  number_of_layers_for_frame LONG,
  -- Name of the layer drawn for this frame.
  frame_layer_name STRING
)
AS
WITH
  actual_timeline_with_vsync AS (
    SELECT *, CAST(name AS INTEGER) AS vsync
    FROM actual_frame_timeline_slice
    WHERE
      dur > 0
  )
SELECT
  cuj_id,
  vsync,
  -- We use MAX to check if at least one of the layers jank_type matches the pattern
  MAX(android_is_app_jank_type(jank_type)) AS app_missed,
  -- We use MAX to check if at least one of the layers jank_type matches the pattern
  MAX(android_is_sf_jank_type(jank_type)) AS sf_missed,
  IFNULL(MAX(ABS(jank_score)), 0) AS jank_score,
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
  COUNT(DISTINCT timeline.layer_name) AS number_of_layers_for_frame,
  -- we use MAX to get at least one of the frame's layer names
  MAX(timeline.layer_name) AS frame_layer_name
FROM _android_jank_cuj_vsync_boundary AS boundary
JOIN actual_timeline_with_vsync AS timeline
  ON vsync >= vsync_min
  AND vsync <= vsync_max
LEFT JOIN expected_frame_timeline_slice AS expected
  ON expected.upid = timeline.upid
  AND expected.name = timeline.name
LEFT JOIN _vsync_missed_callback AS missed_callback USING (vsync)
LEFT JOIN android_jank_cuj_layer_name AS cuj_layer USING (cuj_id)
WHERE
  cuj_layer.layer_name IS NULL
  OR timeline.layer_name = cuj_layer.layer_name
GROUP BY
  cuj_id,
  vsync;

-- Matches slices and boundaries to compute estimated frame boundaries across
-- all threads. Joins with the actual timeline to figure out which frames missed
-- the deadline and whether the app process or SF are at fault.
CREATE PERFETTO TABLE _android_jank_cuj_frame(
  -- CUJ id.
  cuj_id LONG,
  -- Incremental frame number within this CUJ.
  frame_number LONG,
  -- Vsync ID of this frame.
  vsync LONG,
  -- Estimated start timestamp of the frame.
  ts TIMESTAMP,
  -- Expected start timestamp of the frame.
  ts_expected TIMESTAMP,
  -- Start timestamp of the Choreographer#doFrame slice.
  ts_do_frame_start TIMESTAMP,
  -- Number of GPU completion fences for this frame.
  gpu_fence_count LONG,
  -- Whether the frame drew anything.
  drew_anything LONG,
  -- Whether the app missed the frame deadline.
  app_missed LONG,
  -- Whether SF missed the frame deadline.
  sf_missed LONG,
  -- Jank score of the frame.
  jank_score LONG,
  -- Whether SF callback was missed.
  sf_callback_missed LONG,
  -- Whether HWUI callback was missed.
  hwui_callback_missed LONG,
  -- Whether the frame finished on time.
  on_time_finish LONG,
  -- Frame duration.
  dur DURATION,
  -- Unadjusted frame duration.
  dur_unadjusted DURATION,
  -- Expected frame duration.
  dur_expected DURATION,
  -- End timestamp of the frame.
  ts_end TIMESTAMP
)
AS
WITH
  frame_base AS (
    SELECT
      cuj_id,
      ROW_NUMBER() OVER (PARTITION BY cuj_id ORDER BY do_frame.vsync) AS frame_number,
      vsync,
      boundary.ts,
      boundary.ts_expected,
      boundary.ts_do_frame_start,
      COUNT(fence_idx) AS gpu_fence_count,
      COUNT(fence_idx) > 0 AS drew_anything
    FROM _android_jank_cuj_do_frames AS do_frame
    JOIN _android_jank_cuj_main_thread_frame_boundary AS boundary USING (
      cuj_id,
      vsync
    )
    JOIN _android_jank_cuj_draw_frame_slice AS draw_frame USING (cuj_id, vsync)
    LEFT JOIN _android_jank_cuj_gpu_completion_fence AS fence USING (
      cuj_id,
      vsync
    )
    WHERE
      draw_frame.id = fence.draw_frame_slice_id
    GROUP BY
      cuj_id,
      vsync,
      boundary.ts,
      boundary.ts_do_frame_start
  )
SELECT
  frame_base.*,
  app_missed,
  sf_missed,
  jank_score,
  sf_callback_missed,
  hwui_callback_missed,
  on_time_finish,
  ts_end_actual - ts AS dur,
  ts_end_actual - ts_do_frame_start AS dur_unadjusted,
  dur_expected,
  ts_end_actual AS ts_end
FROM frame_base
JOIN _android_jank_cuj_frame_timeline USING (cuj_id, vsync);

-- Similar to `_android_jank_cuj_frame` computes overall SF frame boundaries.
-- The computation is somewhat simpler as most of SF work happens within the duration of
-- the commit/composite slices on the main thread.
CREATE PERFETTO TABLE _android_jank_cuj_sf_frame(
  -- CUJ id.
  cuj_id LONG,
  -- Vsync ID of this frame.
  vsync LONG,
  -- Estimated start timestamp of the SF frame.
  ts TIMESTAMP,
  -- Start timestamp of the SF main thread work.
  ts_main_thread_start TIMESTAMP,
  -- End timestamp of the SF frame.
  ts_end TIMESTAMP,
  -- Frame duration.
  dur DURATION,
  -- Whether SF missed the frame deadline.
  sf_missed LONG,
  -- Whether the app missed the frame deadline (always NULL).
  app_missed LONG,
  -- Jank score of the frame.
  jank_score LONG,
  -- Jank tag of the frame.
  jank_tag STRING,
  -- Jank type of the frame.
  jank_type STRING,
  -- Prediction type of the frame.
  prediction_type STRING,
  -- Present type of the frame.
  present_type STRING,
  -- Whether GPU composition was used.
  gpu_composition LONG,
  -- Expected frame duration.
  dur_expected DURATION,
  -- Incremental frame number within this CUJ for SF.
  frame_number LONG
)
AS
WITH
  android_jank_cuj_timeline_sf_frame AS (
    SELECT DISTINCT
      cuj_id,
      CAST(timeline.name AS INTEGER) AS vsync,
      timeline.display_frame_token
    FROM _android_jank_cuj_vsync_boundary AS boundary
    JOIN actual_frame_timeline_slice AS timeline
      ON boundary.upid = timeline.upid
      AND CAST(timeline.name AS INTEGER) >= vsync_min
      AND CAST(timeline.name AS INTEGER) <= vsync_max
    LEFT JOIN android_jank_cuj_layer_name AS cuj_layer USING (cuj_id)
    WHERE
      cuj_layer.layer_name IS NULL
      OR timeline.layer_name = cuj_layer.layer_name
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
      jank_score,
      jank_tag,
      jank_type,
      prediction_type,
      present_type,
      gpu_composition,
      -- In case expected timeline is missing, as a fallback we use the typical frame deadline
      -- for 60Hz.
      -- See similar expression in android_jank_cuj_frame_timeline.
      COALESCE(expected_timeline.dur, 16600000) AS dur_expected
    FROM _android_jank_cuj_sf_main_thread_frame_boundary AS boundary
    JOIN _android_jank_cuj_sf_process AS sf_process
    JOIN actual_frame_timeline_slice AS actual_timeline
      ON actual_timeline.upid = sf_process.upid
      AND boundary.vsync = CAST(actual_timeline.name AS INTEGER)
    JOIN android_jank_cuj_timeline_sf_frame AS ft
      ON CAST(actual_timeline.name AS INTEGER) = ft.display_frame_token
      AND boundary.cuj_id = ft.cuj_id
    LEFT JOIN expected_frame_timeline_slice AS expected_timeline
      ON expected_timeline.upid = actual_timeline.upid
      AND expected_timeline.name = actual_timeline.name
  )
SELECT *, ROW_NUMBER() OVER (PARTITION BY cuj_id ORDER BY vsync) AS frame_number
FROM android_jank_cuj_sf_frame_base;

-- Per-CUJ summary used to pin jank CUJs as a track. Contains one row per CUJ
-- slice (including canceled and unknown-state CUJs, unlike `android_jank_cuj`),
-- with aggregated frame stats, resolved layer name and boundary-aware
-- timestamps. This bundles everything needed to display jank CUJs so that
-- consumers (e.g. the UI) can query it directly without additional logic.
CREATE PERFETTO TABLE android_jank_cuj_slice_summary(
  -- CUJ id.
  cuj_id LONG,
  -- Name of the CUJ slice (e.g. 'J<CUJ_NAME>').
  name STRING,
  -- Name of the process the CUJ belongs to.
  process_name STRING,
  -- State of the CUJ. One of 'completed', 'canceled' or NULL.
  state STRING,
  -- Total number of frames in the CUJ.
  total_frames LONG,
  -- Number of frames where the app missed the deadline.
  missed_app_frames LONG,
  -- Number of frames where SurfaceFlinger missed the deadline.
  missed_sf_frames LONG,
  -- Layer name of the CUJ.
  layer_name STRING,
  -- Start timestamp of the CUJ. The main thread boundary start if available,
  -- otherwise the CUJ slice ts (e.g. for canceled CUJs).
  ts TIMESTAMP,
  -- Duration of the CUJ. The main thread boundary duration if available,
  -- otherwise the CUJ slice dur (e.g. for canceled CUJs).
  dur DURATION,
  -- Track id of the CUJ slice.
  track_id JOINID(track.id),
  -- Slice id of the CUJ slice.
  slice_id JOINID(slice.id)
)
AS
WITH
  frame_counts AS (
    SELECT
      cuj_id,
      count(*) AS total_frames,
      sum(app_missed) AS missed_app_frames,
      sum(sf_missed) AS missed_sf_frames
    FROM _android_jank_cuj_frame_timeline
    GROUP BY
      cuj_id
  )
SELECT
  cuj.cuj_id,
  cuj.cuj_slice_name AS name,
  cuj.process_name,
  CASE
    WHEN EXISTS (
      SELECT 1
      FROM _cuj_state_markers AS csm
      WHERE
        csm.cuj_id = cuj.cuj_id
        AND csm.marker_type = 'cancel'
    ) THEN 'canceled'
    WHEN EXISTS (
      SELECT 1
      FROM _cuj_state_markers AS csm
      WHERE
        csm.cuj_id = cuj.cuj_id
        AND csm.marker_type = 'end'
    ) THEN 'completed'
    ELSE NULL
  END AS state,
  fc.total_frames,
  fc.missed_app_frames,
  fc.missed_sf_frames,
  layer.layer_name,
  coalesce(boundary.ts, cuj.ts) AS ts,
  coalesce(boundary.dur, cuj.dur) AS dur,
  s.track_id,
  cuj.slice_id
FROM _jank_cujs_slices AS cuj
JOIN slice AS s
  ON s.id = cuj.slice_id
-- Gate frame stats and boundary on android_jank_cuj membership: canceled/invalid
-- CUJs get NULL stats and fall back to raw slice ts/dur. Layer name covers all
-- CUJs, so it joins on the raw cuj_id.
LEFT JOIN android_jank_cuj AS ajc USING (cuj_id)
LEFT JOIN frame_counts AS fc
  ON fc.cuj_id = ajc.cuj_id
LEFT JOIN android_jank_cuj_layer_name AS layer
  ON layer.cuj_id = cuj.cuj_id
LEFT JOIN _android_jank_cuj_main_thread_cuj_boundary AS boundary
  ON boundary.cuj_id = ajc.cuj_id;
