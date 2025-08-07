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

-- The purpose of this module is to provide high level aggregates of system
-- utilization, akin to /proc/stat results.

-- Returns a table of system utilization per given period.
-- Utilization is calculated as sum of average utilization of each CPU in each
-- period, which is defined as a multiply of |interval|. For this reason
-- first and last period might have lower then real utilization.
CREATE PERFETTO FUNCTION cpu_utilization_per_period(
    -- Length of the period on which utilization should be averaged.
    interval LONG
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
FROM _cpu_avg_utilization_per_period!(
  $interval,
  (SELECT * FROM sched WHERE NOT utid IN
    (
      SELECT utid FROM thread WHERE is_idle
    )
  )
);

-- Table with system utilization per second.
-- Utilization is calculated by sum of average utilization of each CPU every
-- second. For this reason first and last second might have lower then real
-- utilization.
CREATE PERFETTO TABLE cpu_utilization_per_second (
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
  ts,
  utilization,
  unnormalized_utilization
FROM cpu_utilization_per_period(time_from_s(1));

-- Aggregated CPU statistics for whole trace. Results in only one row.
CREATE PERFETTO TABLE cpu_cycles (
  -- Sum of CPU millicycles.
  millicycles LONG,
  -- Sum of CPU megacycles.
  megacycles LONG,
  -- Total runtime of all threads running on all CPUs.
  runtime LONG,
  -- Minimum CPU frequency in kHz.
  min_freq LONG,
  -- Maximum CPU frequency in kHz.
  max_freq LONG,
  -- Average CPU frequency in kHz.
  avg_freq LONG
) AS
SELECT
  cast_int!(SUM(dur * freq / 1000)) AS millicycles,
  cast_int!(SUM(dur * freq / 1000) / 1e9) AS megacycles,
  sum(dur) AS runtime,
  min(freq) AS min_freq,
  max(freq) AS max_freq,
  cast_int!(SUM((dur * freq / 1000)) / SUM(dur / 1000)) AS avg_freq
FROM _cpu_freq_per_thread;

-- Aggregated CPU statistics in a provided interval. Results in one row.
CREATE PERFETTO FUNCTION cpu_cycles_in_interval(
    -- Start of the interval.
    ts TIMESTAMP,
    -- Duration of the interval.
    dur LONG
)
RETURNS TABLE (
  -- Sum of CPU millicycles.
  millicycles LONG,
  -- Sum of CPU megacycles.
  megacycles LONG,
  -- Total runtime of all threads running on all CPUs.
  runtime LONG,
  -- Minimum CPU frequency in kHz.
  min_freq LONG,
  -- Maximum CPU frequency in kHz.
  max_freq LONG,
  -- Average CPU frequency in kHz.
  avg_freq LONG
) AS
SELECT
  cast_int!(SUM(ii.dur * freq / 1000)) AS millicycles,
  cast_int!(SUM(ii.dur * freq / 1000) / 1e9) AS megacycles,
  sum(ii.dur) AS runtime,
  min(freq) AS min_freq,
  max(freq) AS max_freq,
  cast_int!(SUM((ii.dur * freq / 1000)) / SUM(ii.dur / 1000)) AS avg_freq
FROM _interval_intersect_single!($ts, $dur, _cpu_freq_per_thread) AS ii
JOIN _cpu_freq_per_thread
  USING (id);

-- Aggregated CPU statistics for each CPU.
CREATE PERFETTO TABLE cpu_cycles_per_cpu (
  -- Unique CPU id. Joinable with `cpu.id`.
  ucpu LONG,
  -- The number of the CPU. Might not be the same as ucpu in multi machine cases.
  cpu LONG,
  -- Sum of CPU millicycles.
  millicycles LONG,
  -- Sum of CPU megacycles.
  megacycles LONG,
  -- Total runtime of all threads running on CPU.
  runtime LONG,
  -- Minimum CPU frequency in kHz.
  min_freq LONG,
  -- Maximum CPU frequency in kHz.
  max_freq LONG,
  -- Average CPU frequency in kHz.
  avg_freq LONG
) AS
SELECT
  ucpu,
  cpu,
  cast_int!(SUM(dur * freq / 1000)) AS millicycles,
  cast_int!(SUM(dur * freq / 1000) / 1e9) AS megacycles,
  sum(dur) AS runtime,
  min(freq) AS min_freq,
  max(freq) AS max_freq,
  cast_int!(SUM((dur * freq / 1000)) / SUM(dur / 1000)) AS avg_freq
FROM _cpu_freq_per_thread
GROUP BY
  ucpu;

-- Aggregated CPU statistics for each CPU in a provided interval.
CREATE PERFETTO FUNCTION cpu_cycles_per_cpu_in_interval(
    -- Start of the interval.
    ts TIMESTAMP,
    -- Duration of the interval.
    dur LONG
)
RETURNS TABLE (
  -- Unique CPU id. Joinable with `cpu.id`.
  ucpu LONG,
  -- CPU number.
  cpu LONG,
  -- Sum of CPU millicycles.
  millicycles LONG,
  -- Sum of CPU megacycles.
  megacycles LONG,
  -- Total runtime of all threads running on CPU.
  runtime LONG,
  -- Minimum CPU frequency in kHz.
  min_freq LONG,
  -- Maximum CPU frequency in kHz.
  max_freq LONG,
  -- Average CPU frequency in kHz.
  avg_freq LONG
) AS
SELECT
  ucpu,
  cpu,
  cast_int!(SUM(ii.dur * freq / 1000)) AS millicycles,
  cast_int!(SUM(ii.dur * freq / 1000) / 1e9) AS megacycles,
  sum(ii.dur) AS runtime,
  min(freq) AS min_freq,
  max(freq) AS max_freq,
  cast_int!(SUM((ii.dur * freq / 1000)) / SUM(ii.dur / 1000)) AS avg_freq
FROM _interval_intersect_single!($ts, $dur, _cpu_freq_per_thread) AS ii
JOIN _cpu_freq_per_thread
  USING (id)
GROUP BY
  ucpu;
