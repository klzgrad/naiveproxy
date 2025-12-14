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

-- All slices related to one frame for max SDK 28. Aggregates
-- "Choreographer#doFrame" and "DrawFrame". Tries to guess the `ts` and `dur`
-- of the frame by first guessing which "DrawFrame" slices are related to which
-- "Choreographer#doSlice".
CREATE PERFETTO TABLE _frames_maxsdk_28 (
  -- Frame id. Created manually starting from 0.
  frame_id LONG,
  -- Timestamp of the frame. Start of "Choreographer#doFrame" slice.
  ts TIMESTAMP,
  -- Duration of the frame, defined as the duration until the last
  -- "DrawFrame" of this frame finishes.
  dur DURATION,
  -- Slice with name "Choreographer#doFrame" corresponding to this frame.
  do_frame_id JOINID(slice.id),
  -- Slice with name "DrawFrame" corresponding to this frame. Fetched as one
  -- of the "DrawFrame" slices that happen for the same process as
  -- "Choreographer#doFrame" slice and start after it started and before the
  -- next "doFrame" started.
  draw_frame_id JOINID(slice.id),
  -- `utid` of the render thread.
  render_thread_utid JOINID(thread.id),
  -- `utid` of the UI thread.
  ui_thread_utid JOINID(thread.id),
  -- "maxsdk28"
  sdk STRING,
  -- process id.
  upid JOINID(process.id),
  -- process name.
  process_name STRING
) AS
WITH
  do_frames AS (
    SELECT
      id,
      ts,
      lead(ts, 1, trace_end()) OVER (PARTITION BY upid ORDER BY ts) AS next_do_frame,
      utid,
      upid
    FROM thread_slice
    WHERE
      is_main_thread = 1 AND name = 'Choreographer#doFrame'
    ORDER BY
      ts
  ),
  draw_frames AS (
    SELECT
      id,
      ts,
      dur,
      ts + dur AS ts_end,
      utid,
      upid
    FROM thread_slice
    WHERE
      name = 'DrawFrame'
  )
SELECT
  row_number() OVER () AS frame_id,
  do.ts,
  max(draw.ts_end) OVER (PARTITION BY do.id) - do.ts AS dur,
  do.id AS do_frame_id,
  draw.id AS draw_frame_id,
  draw.utid AS render_thread_utid,
  do.utid AS ui_thread_utid,
  do.upid AS upid,
  process.name AS process_name,
  "maxsdk28" AS sdk
FROM do_frames AS do
JOIN draw_frames AS draw
  ON (
    do.upid = draw.upid AND draw.ts >= do.ts AND draw.ts < next_do_frame
  )
JOIN process
  USING (upid)
ORDER BY
  do.ts;
