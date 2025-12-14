--
-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.cujs.sysui_cujs;

CREATE PERFETTO TABLE _android_sf_process AS
SELECT
  *
FROM process
WHERE
  process.name = '/system/bin/surfaceflinger'
LIMIT 1;

CREATE PERFETTO TABLE _android_sf_main_thread AS
SELECT
  upid,
  utid,
  thread.name,
  thread_track.id AS track_id
FROM thread
JOIN _android_sf_process AS sf_process
  USING (upid)
JOIN thread_track
  USING (utid)
WHERE
  thread.is_main_thread;

-- Extract thread and track information for a given thread in the surfaceflinger process.
CREATE PERFETTO FUNCTION _android_sf_thread(
    -- thread_name to fetch information for.
    thread_name STRING
)
RETURNS TABLE (
  -- upid of the process.
  upid JOINID(process.id),
  -- utid of the process.
  utid JOINID(thread.id),
  -- name of the thread.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
) AS
SELECT
  upid,
  utid,
  thread.name,
  thread_track.id AS track_id
FROM thread
JOIN _android_sf_process AS sf_process
  USING (upid)
JOIN thread_track
  USING (utid)
WHERE
  thread.name = $thread_name;

-- Match the frame timeline on the app side with the frame timeline on the SF side.
-- In cases where there are multiple layers drawn, there would be separate frame timeline
-- slice for each of the layers. GROUP BY is used to deduplicate these rows.
CREATE PERFETTO TABLE android_app_to_sf_frame_timeline_match (
  -- upid of the app.
  app_upid JOINID(process.upid),
  -- vsync id of the app.
  app_vsync LONG,
  -- upid of surfaceflinger process.
  sf_upid JOINID(process.upid),
  -- vsync id for surfaceflinger.
  sf_vsync LONG
) AS
SELECT
  app_timeline.upid AS app_upid,
  CAST(app_timeline.name AS INTEGER) AS app_vsync,
  sf_process.upid AS sf_upid,
  CAST(sf_timeline.name AS INTEGER) AS sf_vsync
FROM actual_frame_timeline_slice AS app_timeline
JOIN flow
  ON flow.slice_out = app_timeline.id
JOIN actual_frame_timeline_slice AS sf_timeline
  ON flow.slice_in = sf_timeline.id
JOIN _android_sf_process AS sf_process
  ON sf_timeline.upid = sf_process.upid
GROUP BY
  app_upid,
  app_vsync,
  sf_upid,
  sf_vsync;

-- Extract app and SF frame vsync scoped to CUJs.
CREATE PERFETTO TABLE _android_jank_cuj_app_sf_frame_timeline_match AS
SELECT
  cuj_id,
  do_frame.upid AS app_upid,
  app_vsync,
  app_sf_match.sf_upid,
  app_sf_match.sf_vsync
FROM _android_jank_cuj_do_frames AS do_frame
JOIN android_app_to_sf_frame_timeline_match AS app_sf_match
  ON do_frame.vsync = app_sf_match.app_vsync AND do_frame.upid = app_sf_match.app_upid;

-- Extract ts and dur for a given slice name from the SF process main thread track.
CREATE PERFETTO FUNCTION _find_android_jank_cuj_sf_main_thread_slice(
    slice_name_glob STRING
)
RETURNS TABLE (
  cuj_id LONG,
  utid JOINID(thread.id),
  vsync LONG,
  id ID(slice.id),
  name STRING,
  ts TIMESTAMP,
  dur LONG,
  ts_end TIMESTAMP
) AS
WITH
  sf_vsync AS (
    SELECT DISTINCT
      cuj_id,
      sf_vsync AS vsync
    FROM _android_jank_cuj_app_sf_frame_timeline_match
  )
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
JOIN _android_sf_main_thread AS main_thread
  USING (track_id)
JOIN sf_vsync
  ON CAST(str_split(slice.name, " ", 1) AS INTEGER) = sf_vsync.vsync
WHERE
  slice.name GLOB $slice_name_glob AND slice.dur > 0
ORDER BY
  cuj_id,
  vsync;

CREATE PERFETTO TABLE _android_jank_cuj_sf_commit_slice AS
SELECT
  *
FROM _find_android_jank_cuj_sf_main_thread_slice('commit *');

CREATE PERFETTO TABLE _android_jank_cuj_sf_composite_slice AS
SELECT
  *
FROM _find_android_jank_cuj_sf_main_thread_slice('composite *');

CREATE PERFETTO TABLE _android_jank_cuj_sf_on_message_invalidate_slice AS
SELECT
  *
FROM _find_android_jank_cuj_sf_main_thread_slice('onMessageInvalidate *');

-- Calculates the frame boundaries based on when we *expected* the work to
-- start and we use the end of the `composite` slice as the end of the work
-- on the frame.
CREATE PERFETTO TABLE _android_jank_cuj_sf_main_thread_frame_boundary AS
-- Join `commit` and `composite` slices using vsync IDs.
-- We treat the two slices as a single "fake slice" that starts when `commit` starts, and ends
-- when `composite` ends.
WITH
  combined_commit_composite_slice AS (
    SELECT
      cuj_id,
      commit_slice.utid,
      vsync,
      commit_slice.ts,
      composite_slice.ts_end,
      composite_slice.ts_end - commit_slice.ts AS dur
    FROM _android_jank_cuj_sf_commit_slice AS commit_slice
    JOIN _android_jank_cuj_sf_composite_slice AS composite_slice
      USING (cuj_id, vsync)
  ),
  -- As older builds will not have separate commit/composite slices for each frame, but instead
  -- a single `onMessageInvalidate`, we UNION ALL the two tables. Exactly one of them should
  -- have data.
  main_thread_slice AS (
    SELECT
      utid,
      cuj_id,
      vsync,
      ts,
      dur,
      ts_end
    FROM combined_commit_composite_slice
    UNION ALL
    SELECT
      utid,
      cuj_id,
      vsync,
      ts,
      dur,
      ts_end
    FROM _android_jank_cuj_sf_on_message_invalidate_slice
  )
SELECT
  cuj_id,
  utid,
  vsync,
  expected_timeline.ts,
  main_thread_slice.ts AS ts_main_thread_start,
  main_thread_slice.ts_end,
  main_thread_slice.ts_end - expected_timeline.ts AS dur
FROM expected_frame_timeline_slice AS expected_timeline
JOIN _android_sf_process
  USING (upid)
JOIN main_thread_slice
  ON main_thread_slice.vsync = CAST(expected_timeline.name AS INTEGER);
