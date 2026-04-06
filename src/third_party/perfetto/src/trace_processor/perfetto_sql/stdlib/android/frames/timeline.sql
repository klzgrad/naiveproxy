--
-- Copyright 2024 The Android Open Source Project
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

INCLUDE PERFETTO MODULE slices.with_context;

INCLUDE PERFETTO MODULE android.frames.timeline_maxsdk28;

-- Parses the slice name to fetch `frame_id` from `slice` table.
-- Use with caution. Slice names are a flaky source of ids and the resulting
-- table might require some further operations.
CREATE PERFETTO FUNCTION _get_frame_table_with_id(
    -- String just before id.
    glob_str STRING
)
RETURNS TABLE (
  -- Frame slice.
  id ID(slice.id),
  -- Parsed frame id.
  frame_id LONG,
  -- Utid.
  utid JOINID(thread.id),
  -- Upid.
  upid JOINID(process.id),
  -- Timestamp of the frame slice.
  ts TIMESTAMP
) AS
WITH
  all_found AS (
    SELECT
      id,
      cast_int!(STR_SPLIT(name, ' ', 1)) AS frame_id,
      utid,
      upid,
      ts
    FROM thread_slice
    WHERE
      name GLOB $glob_str
  )
SELECT
  *
FROM all_found
-- Casting string to int returns 0 if the string can't be cast. This eliminates the Choreographer resynced slices
-- with the format "Choreographer#doFrame - resynced to 1234 in 20.0ms". The stdlib table is to only list the top
-- level Choreographer#doFrame slices, hence the 'resynced' slices are ignored.
-- frame_id is -1 indicating an invalid vsync id.
WHERE
  frame_id != 0 AND frame_id != -1;

-- All of the `Choreographer#doFrame` slices with their frame id.
CREATE PERFETTO TABLE android_frames_choreographer_do_frame (
  -- Choreographer#doFrame slice. Slice with the name "Choreographer#doFrame
  -- {frame id}".
  id ID(slice.id),
  -- Frame id. Taken as the value behind "Choreographer#doFrame" in slice
  -- name.
  frame_id LONG,
  -- Utid of the UI thread
  ui_thread_utid JOINID(thread.id),
  -- Upid of application process
  upid JOINID(process.id),
  -- Timestamp of the slice.
  ts TIMESTAMP
) AS
SELECT
  id,
  frame_id,
  utid AS ui_thread_utid,
  upid,
  ts
-- Some OEMs have customized `doFrame` to add more information, but we've only
-- observed it added after the frame ID (b/303823815).
FROM _get_frame_table_with_id('Choreographer#doFrame*');

-- All of the `DrawFrame` slices with their frame id and render thread.
-- There might be multiple DrawFrames slices for a single vsync (frame id).
-- This happens when we are drawing multiple layers (e.g. status bar and
-- notifications).
CREATE PERFETTO TABLE android_frames_draw_frame (
  -- DrawFrame slice. Slice with the name "DrawFrame {frame id}".
  id ID(slice.id),
  -- Frame id. Taken as the value behind "DrawFrame" in slice
  -- name.
  frame_id LONG,
  -- Utid of the render thread
  render_thread_utid JOINID(thread.id),
  -- Upid of application process
  upid JOINID(process.id)
) AS
SELECT
  id,
  frame_id,
  utid AS render_thread_utid,
  upid
FROM _get_frame_table_with_id('DrawFrame*');

-- Fetch distinct actual frames per layer per process.
CREATE PERFETTO TABLE _distinct_layer_actual_timeline_slice_per_process AS
SELECT
  cast_int!(name) AS frame_id,
  CAST(str_split(layer_name, '#', 1) AS INTEGER) AS layer_id,
  layer_name,
  id AS slice_id,
  ts,
  dur,
  upid
FROM actual_frame_timeline_slice;

-- Fetch distinct expected frames per process.
CREATE PERFETTO TABLE _distinct_from_expected_timeline_slice_per_process AS
SELECT
  cast_int!(name) AS frame_id,
  upid,
  id
FROM expected_frame_timeline_slice;

-- Contains frames with missed SF or HWUI callbacks.
CREATE PERFETTO TABLE _vsync_missed_callback AS
SELECT
  CAST(str_split(name, 'Callback#', 1) AS INTEGER) AS vsync,
  max(name GLOB '*SF*') AS sf_callback_missed,
  max(name GLOB '*HWUI*') AS hwui_callback_missed
FROM slice
WHERE
  name GLOB '*FT#Missed*Callback*'
GROUP BY
  vsync;

-- TODO(b/384322064) Match actual timeline slice with correct draw frame using layer name.
-- All slices related to one frame. Aggregates `Choreographer#doFrame`,
-- `actual_frame_timeline_slice` and `expected_frame_timeline_slice` slices.
-- This table differs slightly from the android_frames table, as it
-- captures the layer_id for each actual timeline slice too.
CREATE PERFETTO TABLE android_frames_layers (
  -- Frame id.
  frame_id LONG,
  -- Timestamp of the frame. Start of the frame as defined by the start of
  -- "Choreographer#doFrame" slice and the same as the start of the frame in
  -- `actual_frame_timeline_slice if present.
  ts TIMESTAMP,
  -- Duration of the frame, as defined by the duration of the corresponding
  -- `actual_frame_timeline_slice` or, if not present the time between the
  -- `ts` and the end of the final `DrawFrame`.
  dur DURATION,
  -- End timestamp of the frame. End of the frame as defined by the sum of start timestamp and
  -- duration of the frame.
  ts_end TIMESTAMP,
  -- `slice.id` of "Choreographer#doFrame" slice.
  do_frame_id JOINID(slice.id),
  -- `slice.id` of "DrawFrame" slice. For now, we only support the first
  -- DrawFrame slice (due to b/384322064).
  draw_frame_id JOINID(slice.id),
  -- `slice.id` from `actual_frame_timeline_slice`
  actual_frame_timeline_id JOINID(slice.id),
  -- `slice.id` from `expected_frame_timeline_slice`
  expected_frame_timeline_id JOINID(slice.id),
  -- `utid` of the render thread.
  render_thread_utid JOINID(thread.id),
  -- thread id of the UI thread.
  ui_thread_utid JOINID(thread.id),
  -- layer id associated with the actual frame.
  layer_id LONG,
  -- layer name associated with the actual frame.
  layer_name STRING,
  -- process id.
  upid JOINID(process.id),
  -- process name.
  process_name STRING
) AS
WITH
  fallback AS MATERIALIZED (
    SELECT
      frame_id,
      do_frame_slice.ts AS ts,
      (
        draw_frame_slice.ts + draw_frame_slice.dur
      ) - do_frame_slice.ts AS dur
    FROM android_frames_choreographer_do_frame AS do_frame
    JOIN android_frames_draw_frame AS draw_frame
      USING (frame_id, upid)
    JOIN slice AS do_frame_slice
      ON (
        do_frame.id = do_frame_slice.id
      )
    JOIN slice AS draw_frame_slice
      ON (
        draw_frame.id = draw_frame_slice.id
      )
  ),
  frames_sdk_after_28 AS (
    SELECT
      frame_id,
      coalesce(act.ts, fallback.ts) AS ts,
      coalesce(act.dur, fallback.dur) AS dur,
      do_frame.id AS do_frame_id,
      draw_frame.id AS draw_frame_id,
      draw_frame.render_thread_utid,
      do_frame.ui_thread_utid AS ui_thread_utid,
      exp.upid AS upid,
      process.name AS process_name,
      "after_28" AS sdk,
      act.slice_id AS actual_frame_timeline_id,
      exp.id AS expected_frame_timeline_id,
      act.layer_id AS layer_id,
      act.layer_name AS layer_name
    FROM android_frames_choreographer_do_frame AS do_frame
    JOIN android_frames_draw_frame AS draw_frame
      USING (frame_id, upid)
    JOIN fallback
      USING (frame_id)
    JOIN process
      USING (upid)
    LEFT JOIN _distinct_layer_actual_timeline_slice_per_process AS act
      USING (frame_id, upid)
    LEFT JOIN _distinct_from_expected_timeline_slice_per_process AS exp
      USING (frame_id, upid)
    ORDER BY
      frame_id
  ),
  all_frames AS (
    SELECT
      *
    FROM frames_sdk_after_28
    UNION
    SELECT
      *,
      NULL AS actual_frame_timeline_id,
      NULL AS expected_frame_timeline_id,
      NULL AS layer_id,
      NULL AS layer_name
    FROM _frames_maxsdk_28
  )
SELECT
  frame_id,
  ts,
  dur,
  (
    ts + dur
  ) AS ts_end,
  do_frame_id,
  draw_frame_id,
  actual_frame_timeline_id,
  expected_frame_timeline_id,
  render_thread_utid,
  ui_thread_utid,
  layer_id,
  layer_name,
  upid,
  process_name
FROM all_frames
WHERE
  sdk = iif(
    (
      SELECT
        count(1)
      FROM actual_frame_timeline_slice
    ) > 0,
    "after_28",
    "maxsdk28"
  );

-- Table based on the android_frames_layers table. It aggregates time, duration and counts
-- information across different layers for a given frame_id in a given process.
CREATE PERFETTO TABLE android_frames (
  -- Frame id.
  frame_id LONG,
  -- Timestamp of the frame. Start of the frame as defined by the start of
  -- "Choreographer#doFrame" slice and the same as the start of the frame in
  -- `actual_frame_timeline_slice if present.
  ts TIMESTAMP,
  -- Duration of the frame, as defined by the duration of the corresponding
  -- `actual_frame_timeline_slice` or, if not present the time between the
  -- `ts` and the end of the final `DrawFrame`.
  dur DURATION,
  -- `slice.id` of "Choreographer#doFrame" slice.
  do_frame_id JOINID(slice.id),
  -- `slice.id` of "DrawFrame" slice. For now, we only support the first
  -- DrawFrame slice (due to b/384322064).
  draw_frame_id JOINID(slice.id),
  -- `slice.id` from `actual_frame_timeline_slice`
  actual_frame_timeline_id JOINID(slice.id),
  -- `slice.id` from `expected_frame_timeline_slice`
  expected_frame_timeline_id JOINID(slice.id),
  -- `utid` of the render thread.
  render_thread_utid JOINID(thread.id),
  -- thread id of the UI thread.
  ui_thread_utid JOINID(thread.id),
  -- Count of slices in `actual_frame_timeline_slice` related to this frame.
  actual_frame_timeline_count LONG,
  -- Count of slices in `expected_frame_timeline_slice` related to this frame.
  expected_frame_timeline_count LONG,
  -- Count of draw_frame associated to this frame.
  draw_frame_count LONG,
  -- process id.
  upid JOINID(process.id),
  -- process name.
  process_name STRING
) AS
SELECT
  frame_id,
  min(frames_layers.ts) AS ts,
  max(frames_layers.dur) AS dur,
  min(do_frame_id) AS do_frame_id,
  min(draw_frame_id) AS draw_frame_id,
  min(actual_frame_timeline_id) AS actual_frame_timeline_id,
  expected_frame_timeline_id,
  render_thread_utid,
  ui_thread_utid,
  count(DISTINCT actual_frame_timeline_id) AS actual_frame_timeline_count,
  -- Expected frame count will always be 1 for a given frame_id.
  1 AS expected_frame_timeline_count,
  count(DISTINCT draw_frame_id) AS draw_frame_count,
  upid,
  process_name
FROM android_frames_layers AS frames_layers
GROUP BY
  frame_id,
  upid;

-- Returns first frame after the provided timestamp. The returning table has at
-- most one row.
CREATE PERFETTO FUNCTION android_first_frame_after(
    -- Timestamp.
    ts TIMESTAMP
)
RETURNS TABLE (
  -- Frame id.
  frame_id LONG,
  -- Start of the frame, the timestamp of the "Choreographer#doFrame" slice.
  ts TIMESTAMP,
  -- Duration of the frame.
  dur DURATION,
  -- "Choreographer#doFrame" slice. The slice with name
  -- "Choreographer#doFrame" corresponding to this frame.
  do_frame_id JOINID(slice.id),
  -- "DrawFrame" slice. The slice with name "DrawFrame" corresponding to this
  -- frame.
  draw_frame_id JOINID(slice.id),
  -- actual_frame_timeline_slice` slice related to this frame.
  actual_frame_timeline_id JOINID(slice.id),
  -- `expected_frame_timeline_slice` slice related to this frame.
  expected_frame_timeline_id JOINID(slice.id),
  -- `utid` of the render thread.
  render_thread_utid JOINID(thread.id),
  -- `utid` of the UI thread.
  ui_thread_utid JOINID(thread.id)
) AS
SELECT
  frame_id,
  ts,
  dur,
  do_frame_id,
  draw_frame_id,
  actual_frame_timeline_id,
  expected_frame_timeline_id,
  render_thread_utid,
  ui_thread_utid
FROM android_frames
WHERE
  ts > $ts
ORDER BY
  ts
LIMIT 1;
