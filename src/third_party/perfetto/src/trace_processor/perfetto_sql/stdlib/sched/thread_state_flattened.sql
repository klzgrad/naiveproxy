--
-- Copyright 2023 The Android Open Source Project
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

INCLUDE PERFETTO MODULE slices.flat_slices;

-- Create a table which joins the thread state across the flattened slices.
CREATE VIRTUAL TABLE __span_joined_thread USING SPAN_JOIN (_slice_flattened PARTITIONED utid, thread_state PARTITIONED utid);

-- Get the thread state breakdown of a flattened slice from its slice id.
-- This table pivoted and summed for better visualization and aggregation.
-- The concept of a "flat slice" is to take the data in the slice table and
-- remove all notion of nesting. For more information, read the description
-- of _slice_flattened.
CREATE PERFETTO FUNCTION _get_flattened_thread_state(
    -- Id of the slice of interest.
    slice_id JOINID(slice.id),
    -- Utid.
    utid JOINID(thread.id)
)
RETURNS TABLE (
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration.
  dur DURATION,
  -- Utid.
  utid JOINID(thread.id),
  -- Depth.
  depth LONG,
  -- Name.
  name STRING,
  -- Slice id.
  slice_id JOINID(slice.id),
  -- Track id.
  track_id JOINID(track.id),
  -- CPU.
  cpu LONG,
  -- State.
  state STRING,
  -- IO wait.
  io_wait LONG,
  -- Thread state's blocked_function.
  blocked_function STRING,
  -- Thread state's waker utid.
  waker_utid JOINID(thread.id),
  -- Thread state's IRQ context.
  irq_context LONG
) AS
WITH
  interesting_slice AS (
    SELECT
      ts,
      dur,
      slice.track_id AS track_id
    FROM slice
    JOIN thread_track
      ON slice.track_id = thread_track.id
    JOIN thread
      USING (utid)
    WHERE
      (
        (
          NOT $slice_id IS NULL AND slice.id = $slice_id
        ) OR (
          $slice_id IS NULL
        )
      )
      AND (
        (
          NOT $utid IS NULL AND utid = $utid
        ) OR (
          $utid IS NULL
        )
      )
  )
SELECT
  ts,
  dur,
  utid,
  depth,
  name,
  slice_id,
  track_id,
  cpu,
  state,
  io_wait,
  blocked_function,
  waker_utid,
  irq_context
FROM __span_joined_thread
WHERE
  track_id = (
    SELECT
      track_id
    FROM interesting_slice
  )
  AND ts >= (
    SELECT
      ts
    FROM interesting_slice
  )
  AND ts < (
    SELECT
      ts + dur
    FROM interesting_slice
  );

-- Get the thread state breakdown of a flattened slice from slice id.
-- This table pivoted and summed for better visualization and aggragation.
-- The concept of a "flat slice" is to take the data in the slice table and
-- remove all notion of nesting. For more information, read the description
-- of _slice_flattened.
CREATE PERFETTO FUNCTION _get_flattened_thread_state_aggregated(
    -- Slice id.
    slice_id JOINID(slice.id),
    -- Utid.
    utid JOINID(thread.id)
)
RETURNS TABLE (
  -- Id of a slice.
  slice_id JOINID(slice.id),
  -- Name of the slice.
  slice_name STRING,
  -- Time (ns) spent in Uninterruptible Sleep (non-IO)
  uninterruptible_sleep_nonio LONG,
  -- Time (ns) spent in Uninterruptible Sleep (IO)
  uninterruptible_sleep_io LONG,
  -- Time (ns) spent in Runnable
  runnable LONG,
  -- Time (ns) spent in Sleeping
  sleeping LONG,
  -- Time (ns) spent in Stopped
  stopped LONG,
  -- Time (ns) spent in Traced
  traced LONG,
  -- Time (ns) spent in Exit (Dead)
  exit_dead LONG,
  -- Time (ns) spent in Exit (Zombie)
  exit_zombie LONG,
  -- Time (ns) spent in Task Dead
  task_dead LONG,
  -- Time (ns) spent in Wake Kill
  wake_kill LONG,
  -- Time (ns) spent in Waking
  waking LONG,
  -- Time (ns) spent in Parked
  parked LONG,
  -- Time (ns) spent in No Load
  no_load LONG,
  -- Time (ns) spent in Runnable (Preempted)
  runnable_preempted LONG,
  -- Time (ns) spent in Running
  running LONG,
  -- Time (ns) spent in Idle
  idle LONG,
  -- Total duration of the slice
  dur DURATION,
  -- Depth of the slice in Perfetto
  depth LONG
) AS
WITH
  final_table AS (
    SELECT
      *
    FROM _get_flattened_thread_state($slice_id, $utid)
  )
SELECT
  fs.slice_id,
  fs.name AS slice_name,
  sum(CASE WHEN fs.state = 'D' AND io_wait = 0 THEN fs.dur END) AS uninterruptible_sleep_nonio,
  sum(CASE WHEN fs.state = 'D' AND io_wait = 1 THEN fs.dur END) AS uninterruptible_sleep_io,
  sum(CASE WHEN fs.state = 'R' THEN fs.dur END) AS runnable,
  sum(CASE WHEN fs.state = 'S' THEN fs.dur END) AS sleeping,
  sum(CASE WHEN fs.state = 'T' THEN fs.dur END) AS stopped,
  sum(CASE WHEN fs.state = 't' THEN fs.dur END) AS traced,
  sum(CASE WHEN fs.state = 'X' THEN fs.dur END) AS exit_dead,
  sum(CASE WHEN fs.state = 'Z' THEN fs.dur END) AS exit_zombie,
  sum(CASE WHEN fs.state = 'x' THEN fs.dur END) AS task_dead,
  sum(CASE WHEN fs.state = 'K' THEN fs.dur END) AS wake_kill,
  sum(CASE WHEN fs.state = 'W' THEN fs.dur END) AS waking,
  sum(CASE WHEN fs.state = 'P' THEN fs.dur END) AS parked,
  sum(CASE WHEN fs.state = 'N' THEN fs.dur END) AS no_load,
  sum(CASE WHEN fs.state = 'R+' THEN fs.dur END) AS runnable_preempted,
  sum(CASE WHEN fs.state = 'Running' THEN fs.dur END) AS running,
  sum(CASE WHEN fs.state = 'I' THEN fs.dur END) AS idle,
  sum(fs.dur) AS dur,
  fs.depth
FROM final_table AS fs
GROUP BY
  fs.slice_id;
