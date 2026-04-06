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

-- This module contains helpers for computing the thread-level parallelism counters,
-- including how many threads were runnable at a given time and how many threads
-- where running at a given point in time.

INCLUDE PERFETTO MODULE intervals.overlap;

-- The count of runnable threads over time.
CREATE PERFETTO TABLE sched_runnable_thread_count (
  -- Timestamp when the runnable thread count changed to the current value.
  ts TIMESTAMP,
  -- Number of runnable threads, covering the range from this timestamp to the
  -- next row's timestamp.
  runnable_thread_count LONG
) AS
WITH
  runnable AS (
    SELECT
      ts,
      dur
    FROM thread_state
    WHERE
      state = 'R'
  )
SELECT
  ts,
  value AS runnable_thread_count
FROM intervals_overlap_count!(runnable, ts, dur)
ORDER BY
  ts;

-- The count of threads in uninterruptible sleep over time.
CREATE PERFETTO TABLE sched_uninterruptible_sleep_thread_count (
  -- Timestamp when the thread count changed to the current value.
  ts TIMESTAMP,
  -- Number of threads in uninterrutible sleep, covering the range from this timestamp to the
  -- next row's timestamp.
  uninterruptible_sleep_thread_count LONG
) AS
WITH
  uninterruptible_sleep AS (
    SELECT
      ts,
      dur
    FROM thread_state
    WHERE
      state = 'D'
  )
SELECT
  ts,
  value AS uninterruptible_sleep_thread_count
FROM intervals_overlap_count!(uninterruptible_sleep, ts, dur)
ORDER BY
  ts;

-- The count of active CPUs over time.
CREATE PERFETTO TABLE sched_active_cpu_count (
  -- Timestamp when the number of active CPU changed.
  ts TIMESTAMP,
  -- Number of active CPUs, covering the range from this timestamp to the next
  -- row's timestamp.
  active_cpu_count LONG
) AS
WITH
  -- Filter sched events corresponding to running tasks.
  -- thread(s) with is_idle = 1 are the swapper threads / idle tasks.
  tasks AS (
    SELECT
      ts,
      dur
    FROM sched
    WHERE
      NOT utid IN (
        SELECT
          utid
        FROM thread
        WHERE
          is_idle
      )
  )
SELECT
  ts,
  value AS active_cpu_count
FROM intervals_overlap_count!(tasks, ts, dur)
ORDER BY
  ts;
