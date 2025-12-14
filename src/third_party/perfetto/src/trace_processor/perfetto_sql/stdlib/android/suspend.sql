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
--

-- Table of suspended and awake slices.
--
-- Selects either the minimal or full ftrace source depending on what's
-- available, marks suspended periods, and complements them to give awake
-- periods.
CREATE PERFETTO TABLE android_suspend_state (
  -- Timestamp
  ts TIMESTAMP,
  -- Duration
  dur DURATION,
  -- 'awake' or 'suspended'
  power_state STRING
) AS
WITH
  suspend_slice_from_minimal AS (
    SELECT
      ts,
      dur,
      coalesce(lead(ts) OVER (ORDER BY ts), trace_end()) - ts - dur AS duration_gap
    FROM track AS t
    JOIN slice AS s
      ON s.track_id = t.id
    WHERE
      t.name = 'Suspend/Resume Minimal'
  ),
  suspend_slice_latency AS (
    SELECT
      ts,
      dur,
      coalesce(lead(ts) OVER (ORDER BY ts), trace_end()) - ts - dur AS duration_gap
    FROM slice
    JOIN track
      ON slice.track_id = track.id
    WHERE
      track.name = 'Suspend/Resume Latency'
      AND (
        slice.name = 'syscore_resume(0)' OR slice.name = 'timekeeping_freeze(0)'
      )
      AND dur != -1
      AND NOT EXISTS(
        SELECT
          *
        FROM suspend_slice_from_minimal
      )
  ),
  suspend_slice_pre_filter AS (
    SELECT
      ts,
      dur,
      duration_gap
    FROM suspend_slice_from_minimal
    UNION ALL
    SELECT
      ts,
      dur,
      duration_gap
    FROM suspend_slice_latency
  ),
  suspend_slice AS (
    -- Filter out all the slices that overlapped with the following slices.
    -- This happens with data loss where we lose start and end slices for suspends.
    SELECT
      ts,
      dur
    FROM suspend_slice_pre_filter
    WHERE
      duration_gap >= 0
  ),
  awake_slice AS (
    -- If we don't have any rows, use the trace bounds if bounds are defined.
    SELECT
      trace_start() AS ts,
      trace_dur() AS dur
    WHERE
      (
        SELECT
          count(*)
        FROM suspend_slice
      ) = 0 AND dur > 0
    UNION ALL
    -- If we do have rows, create one slice from the trace start to the first suspend.
    SELECT
      trace_start() AS ts,
      (
        SELECT
          min(ts)
        FROM suspend_slice
      ) - trace_start() AS dur
    WHERE
      (
        SELECT
          count(*)
        FROM suspend_slice
      ) != 0
    UNION ALL
    -- And then one slice for each suspend, from the end of the suspend to the
    -- start of the next one (or the end of the trace if there is no next one).
    SELECT
      ts + dur AS ts,
      coalesce(lead(ts) OVER (ORDER BY ts), trace_end()) - ts - dur AS dur
    FROM suspend_slice
  )
SELECT
  ts,
  dur,
  'awake' AS power_state
FROM awake_slice
UNION ALL
SELECT
  ts,
  dur,
  'suspended' AS power_state
FROM suspend_slice
ORDER BY
  ts;

-- Order by will cause Perfetto table to index by ts.;

-- Extracts the duration without counting CPU suspended time from an event.
-- This is the same as converting an event duration from wall clock to monotonic clock.
-- If there was no CPU suspend, the result is same as |dur|.
CREATE PERFETTO FUNCTION _extract_duration_without_suspend(
    -- Timestamp of event.
    ts TIMESTAMP,
    -- Duration of event.
    dur DURATION
)
RETURNS LONG AS
SELECT
  to_monotonic($ts + $dur) - to_monotonic($ts);
