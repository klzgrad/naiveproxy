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

INCLUDE PERFETTO MODULE linux.cpu.utilization.general;

INCLUDE PERFETTO MODULE time.conversion;

INCLUDE PERFETTO MODULE intervals.intersect;

-- Returns a table of thread utilization per given period.
-- Utilization is calculated as sum of average utilization of each CPU in each
-- period, which is defined as a multiply of |interval|. For this reason
-- first and last period might have lower then real utilization.
CREATE PERFETTO FUNCTION cpu_thread_utilization_per_period(
    -- Length of the period on which utilization should be averaged.
    interval LONG,
    -- Utid of the thread.
    utid JOINID(thread.id)
)
RETURNS TABLE (
  -- Timestamp of start of a second.
  ts TIMESTAMP,
  -- Sum of average utilization over period.
  -- Note: as the data is normalized, the values will be in the
  -- [0, 1] range.
  utilization DOUBLE,
  -- Sum of average utilization over all CPUs over period.
  -- Note: as the data is unnormalized, the values will be in the
  -- [0, cpu_count] range.
  unnormalized_utilization DOUBLE
) AS
WITH
  sched_for_utid AS (
    SELECT
      ts,
      ts_end,
      utid
    FROM sched
    WHERE
      utid = $utid
  )
SELECT
  *
FROM _cpu_avg_utilization_per_period!($interval, sched_for_utid);

-- Returns a table of thread utilization per second.
-- Utilization is calculated as sum of average utilization of each CPU in each
-- period, which is defined as a multiply of |interval|. For this reason
-- first and last period might have lower then real utilization.
CREATE PERFETTO FUNCTION cpu_thread_utilization_per_second(
    -- Utid of the thread.
    utid JOINID(thread.id)
)
RETURNS TABLE (
  -- Timestamp of start of a second.
  ts TIMESTAMP,
  -- Sum of average utilization over period.
  -- Note: as the data is normalized, the values will be in the
  -- [0, 1] range.
  utilization DOUBLE,
  -- Sum of average utilization over all CPUs over period.
  -- Note: as the data is unnormalized, the values will be in the
  -- [0, cpu_count] range.
  unnormalized_utilization DOUBLE
) AS
SELECT
  *
FROM cpu_thread_utilization_per_period(time_from_s(1), $utid);

-- Aggregated CPU statistics for each thread.
CREATE PERFETTO TABLE cpu_cycles_per_thread (
  -- Thread
  utid JOINID(thread.id),
  -- Sum of CPU millicycles
  millicycles LONG,
  -- Sum of CPU megacycles
  megacycles LONG,
  -- Total runtime duration
  runtime LONG,
  -- Minimum CPU frequency in kHz
  min_freq LONG,
  -- Maximum CPU frequency in kHz
  max_freq LONG,
  -- Average CPU frequency in kHz
  avg_freq LONG
) AS
SELECT
  utid,
  cast_int!(SUM(dur * freq / 1000)) AS millicycles,
  cast_int!(SUM(dur * freq / 1000) / 1e9) AS megacycles,
  sum(dur) AS runtime,
  min(freq) AS min_freq,
  max(freq) AS max_freq,
  cast_int!(SUM((dur * freq / 1000)) / SUM(dur / 1000)) AS avg_freq
FROM _cpu_freq_per_thread
GROUP BY
  utid;

-- Aggregated CPU statistics for each thread in a provided interval.
CREATE PERFETTO FUNCTION cpu_cycles_per_thread_in_interval(
    -- Start of the interval.
    ts TIMESTAMP,
    -- Duration of the interval.
    dur LONG
)
RETURNS TABLE (
  -- Thread with CPU cycles and frequency statistics.
  utid JOINID(thread.id),
  -- Sum of CPU millicycles
  millicycles LONG,
  -- Sum of CPU megacycles
  megacycles LONG,
  -- Total runtime duration
  runtime LONG,
  -- Minimum CPU frequency in kHz
  min_freq LONG,
  -- Maximum CPU frequency in kHz
  max_freq LONG,
  -- Average CPU frequency in kHz
  avg_freq LONG
) AS
SELECT
  utid,
  cast_int!(SUM(ii.dur * freq / 1000)) AS millicycles,
  cast_int!(SUM(ii.dur * freq / 1000 )/ 1e9) AS megacycles,
  sum(ii.dur) AS runtime,
  min(freq) AS min_freq,
  max(freq) AS max_freq,
  cast_int!(SUM((ii.dur * freq / 1000)) / SUM(ii.dur / 1000)) AS avg_freq
FROM _interval_intersect_single!($ts, $dur, _cpu_freq_per_thread) AS ii
JOIN _cpu_freq_per_thread AS c
  USING (id)
GROUP BY
  utid;
