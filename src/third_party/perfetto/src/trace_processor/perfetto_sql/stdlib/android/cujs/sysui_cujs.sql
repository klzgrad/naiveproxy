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

INCLUDE PERFETTO MODULE android.frames.timeline;

INCLUDE PERFETTO MODULE android.cujs.base;

-- Table tracking all jank CUJs information.
CREATE PERFETTO TABLE android_sysui_jank_cujs (
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- process name.
  process_name STRING,
  -- Name of the CUJ slice.
  cuj_slice_name STRING,
  -- Name of the CUJ without the 'J<' prefix.
  cuj_name STRING,
  -- Id of the CUJ slice in perfetto. Keeping the slice id column as part of this table
  -- as provision to lookup the actual CUJ slice ts and dur. The ts and dur in this table
  -- might differ from the slice duration, as they are associated with start and end frame
  -- corresponding to the CUJ.
  slice_id JOINID(slice.id),
  -- Start timestamp of the CUJ. Start of the CUJ as defined by the start of the first overlapping
  -- expected frame.
  ts TIMESTAMP,
  -- End timestamp of the CUJ. Calculated as the end timestamp of the last actual frame
  -- overlapping with the CUJ.
  ts_end TIMESTAMP,
  -- Duration of the CUJ calculated based on the ts and ts_end values.
  dur DURATION,
  -- State of the CUJ. One of "completed", "cancelled" or NULL. NULL in cases where the FT#cancel or
  -- FT#end instant event is not present for the CUJ.
  state STRING,
  -- thread id of the UI thread.
  ui_thread JOINID(thread.id),
  -- layer id associated with the actual frame.
  layer_id LONG,
  -- vysnc id of the first frame that falls within the CUJ boundary.
  begin_vsync LONG,
  -- vysnc id of the last frame that falls within the CUJ boundary.
  end_vsync LONG
) AS
WITH
  -- select the first and last frame.
  cuj_frame_boundary AS (
    SELECT
      *,
      -- To keep the new table consistent with the current jank CUJ boundary
      -- logic, the start ts of the CUJ will be the start of the first
      -- expected frame, and the end ts will be the end of the last Choreographer
      -- doFrame.
      (
        SELECT
          ts
        FROM expected_frame_timeline_slice
        WHERE
          id = expected_frame_timeline_id
      ) AS start_frame_ts,
      (
        SELECT
          (
            ts + dur
          ) AS ts_end
        FROM slice
        WHERE
          id = do_frame_id
      ) AS end_frame_ts_end
    FROM _android_distinct_frames_in_cuj
    WHERE
      frame_idx = 1 OR frame_idx = frame_cnt
  )
SELECT
  cuj.cuj_id,
  cuj.upid,
  cuj.process_name,
  cuj.cuj_slice_name,
  -- Extracts "CUJ_NAME" from "J<CUJ_NAME>"
  _extract_cuj_name_from_slice(cuj.cuj_slice_name) AS cuj_name,
  cuj.slice_id,
  min(start_frame_ts) AS ts,
  max(end_frame_ts_end) AS ts_end,
  (
    max(end_frame_ts_end) - min(start_frame_ts)
  ) AS dur,
  CASE
    WHEN EXISTS(
      SELECT
        1
      FROM _cuj_state_markers AS csm
      WHERE
        csm.cuj_id = cuj.cuj_id AND csm.marker_type = 'cancel'
    )
    THEN 'canceled'
    WHEN EXISTS(
      SELECT
        1
      FROM _cuj_state_markers AS csm
      WHERE
        csm.cuj_id = cuj.cuj_id AND csm.marker_type = 'end'
    )
    THEN 'completed'
    ELSE NULL
  END AS state,
  cuj_events.ui_thread,
  cuj_events.layer_id,
  cuj_events.begin_vsync,
  cuj_events.end_vsync
FROM _jank_cujs_slices AS cuj
JOIN _cuj_instant_events AS cuj_events
  USING (cuj_id)
JOIN cuj_frame_boundary AS boundary
  USING (cuj_id)
JOIN android_frames_choreographer_do_frame AS do_frame
  ON do_frame_id = do_frame.id
WHERE
  -- Filter only jank CUJs.
  cuj.cuj_slice_name GLOB 'J<*>'
  AND (
    state != 'canceled'
    -- Older builds don't have the state markers so we allow NULL but filter out
    -- CUJs that are <4ms long - assuming CUJ was canceled in that case.
    OR (
      state IS NULL AND cuj.dur > 4e6
    )
  )
GROUP BY
  cuj_id
ORDER BY
  ts ASC;

-- Track IDs for all tracks named after Latency CUJs.
CREATE PERFETTO TABLE _latency_cuj_tracks AS
SELECT
  id,
  name
FROM track
WHERE
  name GLOB 'L<*>';

-- Markers (cancel, timeout) related to Latency CUJ slices.
CREATE PERFETTO TABLE _latency_cuj_markers AS
SELECT
  cuj_slice.id AS cuj_slice_id,
  CASE WHEN s.name = 'cancel' THEN 'cancel' ELSE 'timeout' END AS marker_type
FROM slice AS cuj_slice
JOIN _latency_cuj_tracks AS t
  ON t.name = cuj_slice.name
JOIN slice AS s
  ON s.track_id = t.id
WHERE
  s.dur = 0
  AND s.ts >= cuj_slice.ts
  AND s.ts <= cuj_slice.ts + cuj_slice.dur
  AND cuj_slice.name GLOB 'L<*>'
  AND (
    s.name = 'cancel' OR s.name = 'timeout'
  );

-- Table tracking all latency CUJs information.
CREATE PERFETTO TABLE android_sysui_latency_cujs (
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- process name.
  process_name STRING,
  -- Name of the CUJ slice.
  cuj_slice_name STRING,
  -- Name of the CUJ without the 'L<' prefix.
  cuj_name STRING,
  -- Id of the CUJ slice in perfetto. Keeping the slice id column as part of this table
  -- as provision to lookup the actual CUJ slice ts and dur. The ts and dur in this table
  -- might differ from the slice duration, as they are associated with start and end frame
  -- corresponding to the CUJ.
  slice_id JOINID(slice.id),
  -- Start timestamp of the CUJ calculated as the start of the CUJ slice in trace.
  ts TIMESTAMP,
  -- End timestamp of the CUJ calculated as the end timestamp of the CUJ slice.
  ts_end TIMESTAMP,
  -- Duration of the CUJ calculated based on the ts and ts_end values.
  dur DURATION,
  -- State of the CUJ whether it was completed/cancelled.
  state STRING
) AS
SELECT
  row_number() OVER (ORDER BY ts) AS cuj_id,
  process.upid AS upid,
  process.name AS process_name,
  slice.name AS cuj_slice_name,
  -- Extracts "CUJ_NAME" from "L<CUJ_NAME>"
  _extract_cuj_name_from_slice(slice.name) AS cuj_name,
  slice.id AS slice_id,
  ts,
  ts + dur AS ts_end,
  dur,
  CASE
    WHEN EXISTS(
      SELECT
        1
      FROM _latency_cuj_markers AS m
      WHERE
        m.cuj_slice_id = slice.id AND m.marker_type = 'cancel'
    )
    THEN 'canceled'
    WHEN EXISTS(
      SELECT
        1
      FROM _latency_cuj_markers AS m
      WHERE
        m.cuj_slice_id = slice.id AND m.marker_type = 'timeout'
    )
    THEN 'timeout'
    ELSE 'completed'
  END AS state
FROM slice
JOIN process_track
  ON slice.track_id = process_track.id
JOIN process
  USING (upid)
WHERE
  slice.name GLOB 'L<*>' AND dur > 0;

-- Table tracking all jank/latency CUJs information.
CREATE PERFETTO TABLE android_jank_latency_cujs (
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- An alias for cuj_id for compatibility purposes.
  id LONG,
  -- process id.
  upid JOINID(process.id),
  -- process name.
  process_name STRING,
  -- Name of the CUJ slice.
  cuj_slice_name STRING,
  -- Name of the CUJ without the 'J<' prefix.
  cuj_name STRING,
  -- Id of the CUJ slice in perfetto. Keeping the slice id column as part of this table
  -- as provision to lookup the actual CUJ slice ts and dur. The ts and dur in this table
  -- might differ from the slice duration, as they are associated with start and end frame
  -- corresponding to the CUJ.
  slice_id JOINID(slice.id),
  -- Start timestamp of the CUJ. Start of the CUJ as defined by the start of the first overlapping
  -- expected frame.
  ts TIMESTAMP,
  -- End timestamp of the CUJ. Calculated as the end timestamp of the last actual frame
  -- overlapping with the CUJ.
  ts_end TIMESTAMP,
  -- Duration of the CUJ calculated based on the ts and ts_end values.
  dur DURATION,
  -- State of the CUJ. One of "completed", "cancelled" or NULL. NULL in cases where the FT#cancel or
  -- FT#end instant event is not present for the CUJ.
  state STRING,
  -- thread id of the UI thread. In case of latency CUJs, this will always be the main thread of
  -- the process.
  ui_thread JOINID(thread.id),
  -- layer id associated with the actual frame.
  layer_id LONG,
  -- vysnc id of the first frame that falls within the CUJ boundary.
  begin_vsync LONG,
  -- vysnc id of the last frame that falls within the CUJ boundary.
  end_vsync LONG,
  -- Type of CUJ, i.e. jank or latency.
  cuj_type STRING
) AS
WITH
  combined_cujs AS (
    SELECT
      *,
      "jank" AS cuj_type,
      cuj_id AS original_cuj_id
    FROM android_sysui_jank_cujs
    UNION ALL
    SELECT
      *,
      -- upid is used as the ui_thread as it's the tid of the main thread.
      upid AS ui_thread,
      NULL AS layer_id,
      NULL AS begin_vsync,
      NULL AS end_vsync,
      "latency" AS cuj_type,
      cuj_id AS original_cuj_id
    FROM android_sysui_latency_cujs
  )
SELECT
  row_number() OVER (ORDER BY cuj_type, original_cuj_id) AS cuj_id,
  row_number() OVER (ORDER BY cuj_type, original_cuj_id) AS id,
  upid,
  process_name,
  cuj_slice_name,
  cuj_name,
  slice_id,
  ts,
  ts_end,
  dur,
  state,
  ui_thread,
  layer_id,
  begin_vsync,
  end_vsync,
  cuj_type
FROM combined_cujs;
