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

-- Macro defining the filtering conditions for a jank or latency CUJ slice in relevant processes.
CREATE PERFETTO MACRO _is_jank_slice(
    slice TableOrSubquery,
    process TableOrSubquery
)
RETURNS Expr AS
$slice.name GLOB 'J<*>'
AND (
  $process.name GLOB 'com.google.android*' OR $process.name GLOB 'com.android.*'
);

-- List of CUJ slices emitted. Note that this is not the final list of CUJs with the correct
-- boundary information. The proper CUJs and their boundaries are computed after taking into
-- account instant events and frame boundaries in the following tables.
CREATE PERFETTO TABLE _jank_cujs_slices AS
SELECT
  row_number() OVER (ORDER BY ts) AS cuj_id,
  process.upid AS upid,
  process.name AS process_name,
  slice.id AS slice_id,
  slice.name AS cuj_slice_name,
  ts,
  dur,
  ts + dur AS ts_end
FROM slice
JOIN process_track
  ON slice.track_id = process_track.id
JOIN process
  USING (upid)
WHERE
  _is_jank_slice!(slice, process) AND dur > 0;

-- Slices logged from FrameTracker#markEvent that describe when
-- the instrumentation was started and the reason the CUJ ended.
CREATE PERFETTO TABLE _cuj_state_markers AS
SELECT
  cuj.cuj_id,
  upid,
  CASE
    WHEN cuj_state_marker.name GLOB '*FT#begin*'
    THEN 'begin'
    WHEN cuj_state_marker.name GLOB '*FT#deferMonitoring*'
    THEN 'deferMonitoring'
    WHEN cuj_state_marker.name GLOB '*FT#end*'
    THEN 'end'
    WHEN cuj_state_marker.name GLOB '*FT#cancel*'
    THEN 'cancel'
    WHEN cuj_state_marker.name GLOB '*FT#layerId*'
    THEN 'layerId'
    WHEN cuj_state_marker.name GLOB '*#UIThread'
    THEN 'UIThread'
    ELSE 'other'
  END AS marker_type,
  cuj_state_marker.name AS marker_name,
  thread_track.utid AS utid
FROM _jank_cujs_slices AS cuj
LEFT JOIN slice AS cuj_state_marker
  ON cuj_state_marker.ts >= cuj.ts AND cuj_state_marker.ts < cuj.ts_end
LEFT JOIN track AS marker_track
  ON marker_track.id = cuj_state_marker.track_id
LEFT JOIN thread_track
  ON cuj_state_marker.track_id = thread_track.id
WHERE
  -- e.g. J<CUJ_NAME>#FT#end#0 this for backward compatibility
  cuj_state_marker.name GLOB (
    cuj.cuj_slice_name || "#FT#*"
  )
  OR (
    marker_track.name = cuj_slice_name AND cuj_state_marker.name GLOB 'FT#*'
  )
  OR cuj_state_marker.name = (
    cuj.cuj_slice_name || "#UIThread"
  );

-- CUJ instant event values.
CREATE PERFETTO TABLE _cuj_instant_events AS
SELECT
  cuj_id,
  cuj.upid,
  max(
    CASE
      WHEN csm.marker_name GLOB '*layerId#*'
      THEN CAST(str_split(csm.marker_name, 'layerId#', 1) AS INTEGER)
      ELSE NULL
    END
  ) AS layer_id,
  -- Extract begin VSync ID from 'beginVsync#<ID>' marker.
  max(
    CASE
      WHEN csm.marker_name GLOB '*beginVsync#*'
      THEN CAST(str_split(csm.marker_name, 'beginVsync#', 1) AS INTEGER)
      ELSE NULL
    END
  ) AS begin_vsync,
  -- Extract end VSync ID from 'endVsync#<ID>' marker.
  max(
    CASE
      WHEN csm.marker_name GLOB '*endVsync#*'
      THEN CAST(str_split(csm.marker_name, 'endVsync#', 1) AS INTEGER)
      ELSE NULL
    END
  ) AS end_vsync,
  -- Extract UI thread UTID from 'UIThread' marker.
  max(CASE WHEN csm.marker_type = 'UIThread' THEN csm.utid ELSE NULL END) AS ui_thread
FROM _jank_cujs_slices AS cuj
LEFT JOIN _cuj_state_markers AS csm
  USING (cuj_id)
GROUP BY
  cuj_id;

CREATE PERFETTO FUNCTION _extract_cuj_name_from_slice(
    cuj_slice_name STRING
)
RETURNS STRING AS
SELECT
  substr($cuj_slice_name, 3, length($cuj_slice_name) - 3);

-- Information about all frames in a process that overlap with a CUJ from the same process.
-- This can include multiple frames for the same frame_id (for eg. frames with different layers).
CREATE PERFETTO VIEW _all_frames_in_cuj AS
SELECT
  _extract_cuj_name_from_slice(cuj.cuj_slice_name) AS cuj_name,
  cuj.upid,
  cuj.process_name,
  cie.layer_id AS cuj_layer_id,
  frame.layer_id,
  frame.layer_name,
  frame.frame_id,
  frame.do_frame_id,
  frame.expected_frame_timeline_id,
  cuj.cuj_id,
  frame.ts AS frame_ts,
  frame.dur AS dur,
  (
    frame.ts + frame.dur
  ) AS ts_end,
  ui_thread_utid
FROM android_frames_layers AS frame
JOIN _cuj_instant_events AS cie
  ON frame.ui_thread_utid = cie.ui_thread AND frame.layer_id IS NOT NULL
JOIN _jank_cujs_slices AS cuj
  ON cie.cuj_id = cuj.cuj_id
-- Check whether the frame_id falls within the begin and end vsync of the cuj.
-- Also check if the frame start or end timestamp falls within the cuj boundary.
WHERE
  frame_id >= begin_vsync
  AND frame_id <= end_vsync
  AND (
    -- frame start within cuj
    (
      frame.ts >= cuj.ts AND frame.ts <= cuj.ts_end
    )
    -- frame end within cuj
    OR (
      (
        frame.ts + frame.dur
      ) >= cuj.ts AND (
        frame.ts + frame.dur
      ) <= cuj.ts_end
    )
  );

-- Track all distinct frames that overlap with the CUJ slice. In this table two frames are considered
-- distinct if they have different frame_id/vsync.
CREATE PERFETTO VIEW _android_distinct_frames_in_cuj AS
-- Captures all frames in the CUJ boundary. In cases where there are multiple actual frames, there
-- can be multiple rows with the same frame_id.
SELECT
  row_number() OVER (PARTITION BY cuj_id ORDER BY min(frame_ts)) AS frame_idx,
  count(*) OVER (PARTITION BY cuj_id) AS frame_cnt,
  -- With a 'GROUP BY' clause for this table, there is no aggregations function used for the
  -- selected columns. This is because these columns values are expected to remain identical. For eg.
  -- a cuj_name, upid will be the same for a given cuj_id. do_frame_id or expected_frame_timeline_id
  -- will be the same for a given frame_id.
  cuj_name,
  upid,
  cuj_layer_id,
  layer_id,
  layer_name,
  process_name,
  frame_id,
  do_frame_id,
  expected_frame_timeline_id,
  cuj_id,
  ui_thread_utid,
  -- In case of multiple frames for a frame_id, consider the min start timestamp.
  min(frame_ts) AS frame_ts,
  -- In case of multiple frames for a frame_id, consider the max end timestamp.
  max(ts_end) AS ts_end,
  (
    max(ts_end) - min(frame_ts)
  ) AS dur
FROM _all_frames_in_cuj
GROUP BY
  frame_id,
  cuj_id;

-- Track all distinct frames with layer_id consideration that overlap with the CUJ slice.
CREATE PERFETTO VIEW _android_distinct_frames_layers_cuj AS
-- Captures all frames in the CUJ boundary. In cases where there are multiple actual frames, there
-- can be multiple rows with the same frame_id.
SELECT
  -- With a 'GROUP BY' clause for this table, there is no aggregations function used for the
  -- selected columns. This is because these columns values are expected to remain identical. For eg.
  -- a cuj_name, upid will be the same for a given cuj_id. do_frame_id or expected_frame_timeline_id
  -- will be the same for a given frame_id and layer_id.
  cuj_name,
  upid,
  cuj_layer_id,
  layer_id,
  layer_name,
  process_name,
  frame_id,
  do_frame_id,
  expected_frame_timeline_id,
  cuj_id,
  ui_thread_utid,
  frame_ts,
  ts_end,
  dur
FROM _all_frames_in_cuj
GROUP BY
  frame_id,
  cuj_id,
  layer_id;

-- Table captures all Choreographer#doFrame within a CUJ boundary.
CREATE PERFETTO TABLE _android_jank_cuj_do_frames AS
WITH
  do_frame_slice_with_end_ts AS (
    SELECT
      slice.id,
      frame_id AS vsync,
      do_frame.ts,
      upid,
      slice.ts + slice.dur AS ts_end,
      slice.track_id
    FROM android_frames_choreographer_do_frame AS do_frame
    JOIN slice
      USING (id)
  )
SELECT
  cuj.cuj_id,
  cie.ui_thread,
  thread.utid,
  do_frame.*
FROM thread
JOIN _jank_cujs_slices AS cuj
  USING (upid)
LEFT JOIN _cuj_instant_events AS cie
  USING (cuj_id, upid)
JOIN thread_track
  USING (utid)
JOIN do_frame_slice_with_end_ts AS do_frame
  ON do_frame.ts_end >= cuj.ts
  AND do_frame.ts <= cuj.ts_end
  AND thread_track.id = do_frame.track_id
WHERE
  (
    (
      cie.ui_thread IS NULL AND thread.is_main_thread
    )
    -- Some CUJs use a dedicated thread for Choreographer callbacks
    OR (
      cie.ui_thread = thread.utid
    )
  )
  AND vsync > 0
  AND (
    vsync >= begin_vsync OR begin_vsync IS NULL
  )
  AND (
    vsync <= end_vsync OR end_vsync IS NULL
  );

-- Table tracking all jank CUJs information.
CREATE PERFETTO TABLE android_jank_cuj (
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
SELECT
  cuj.*,
  _extract_cuj_name_from_slice(cuj.cuj_slice_name) AS cuj_name,
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
WHERE
  state != 'canceled'
  -- Older builds don't have the state markers so we allow NULL but filter out
  -- CUJs that are <4ms long - assuming CUJ was canceled in that case.
  OR (
    state IS NULL AND cuj.dur > 4e6
  )
ORDER BY
  ts ASC;
