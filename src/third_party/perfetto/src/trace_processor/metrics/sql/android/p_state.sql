--
-- Copyright 2022 The Android Open Source Project
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

SELECT RUN_METRIC("android/android_cpu_agg.sql");

DROP VIEW IF EXISTS p_state_cpu_idle_counter;
CREATE PERFETTO VIEW p_state_cpu_idle_counter AS
SELECT
  ts,
  ts - LAG(ts) OVER (
    PARTITION BY track_id
    ORDER BY
      ts
  ) AS dur,
  cpu,
  iif(value = 4294967295, -1, cast(value AS int)) AS idle_value
FROM
  counter c
JOIN cpu_counter_track t ON c.track_id = t.id
WHERE
  t.name = "cpuidle";

DROP TABLE IF EXISTS p_state_sched_freq_idle;
CREATE VIRTUAL TABLE p_state_sched_freq_idle USING span_join(
  cpu_freq_sched_per_thread PARTITIONED cpu,
  p_state_cpu_idle_counter PARTITIONED cpu
);

CREATE OR REPLACE PERFETTO FUNCTION p_state_over_interval(
  start_ns LONG, end_ns LONG)
RETURNS TABLE(cpu INT, freq_khz INT, idle_value INT, dur_ns INT)
AS
WITH sched_freq_idle_windowed AS (
  SELECT
    freq_khz,
    idle_value,
    cpu,
    IIF(ts + dur <= $end_ns, ts + dur, $end_ns) - IIF(ts >= $start_ns, ts, $start_ns) AS dur
  FROM
    p_state_sched_freq_idle
  WHERE
    ts + dur > $start_ns
    AND ts < $end_ns
)
SELECT
  cast(cpu AS int) AS cpu,
  cast(freq_khz AS int) AS freq_khz,
  cast(idle_value AS int) AS idle_value,
  cast(sum(dur) AS int) AS dur_ns
FROM
  sched_freq_idle_windowed
WHERE
  freq_khz > 0
GROUP BY
  cpu,
  freq_khz,
  idle_value;
