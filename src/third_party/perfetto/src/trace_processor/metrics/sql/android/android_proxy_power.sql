--
-- Copyright 2020 The Android Open Source Project
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

-- This metric assigns a power drain rate in milliampers to each sched slice.
-- The power_profile table should be populated with device power profile data
-- before running the metric. It has the following scheme:
-- (device STRING, cpu INT, cluster INT, freq INT, power DOUBLE).
--
-- Metric usage examples:
-- 1) Compute the power cost of every thread in milliamper-seconds:
--     SELECT
--         utid,
--         SUM(dur * COALESCE(power_ma, 0) / 1e9) AS power_mas
--     FROM power_per_thread
--     GROUP BY utid;
-- 2) Compute the total power cost of all slices from a table 'my_slice':
--     CREATE PERFETTO VIEW my_slice_utid AS
--     SELECT ts, dur, utid
--     FROM my_slice
--     JOIN thread_track ON track_id = thread_track.id;
--
--     CREATE VIRTUAL TABLE my_slice_power
--     USING SPAN_JOIN(power_per_thread PARTITIONED utid,
--                     my_slice_utid PARTITIONED utid);
--
--     SELECT
--         SUM(dur * COALESCE(power_ma, 0) / 1e9) as power_mas
--     FROM my_slice_power;

SELECT RUN_METRIC('android/android_cpu_agg.sql');
SELECT RUN_METRIC('android/power_profile_data.sql');

DROP VIEW IF EXISTS device;
CREATE PERFETTO VIEW device AS
WITH
after_first_slash(str) AS (
  SELECT SUBSTR(str_value, INSTR(str_value, '/') + 1)
  FROM metadata
  WHERE name = 'android_build_fingerprint'
),
before_second_slash(str) AS (
  SELECT SUBSTR(str, 0, INSTR(str, '/'))
  FROM after_first_slash
)
SELECT str AS name FROM before_second_slash;

DROP VIEW IF EXISTS power_view;
CREATE PERFETTO VIEW power_view AS
SELECT
  cpu_freq_view.cpu AS cpu,
  ts,
  dur,
  power AS power_ma
FROM cpu_freq_view
JOIN power_profile ON (
  power_profile.device = (SELECT name FROM device)
  AND power_profile.cpu = cpu_freq_view.cpu
  AND power_profile.freq = cpu_freq_view.freq_khz
  );

-- threads with is_idle = 1 are used to mark sched slices where CPU was idle. It
-- doesn't correspond to any real thread.
DROP VIEW IF EXISTS sched_real_threads;
CREATE PERFETTO VIEW sched_real_threads AS
SELECT *
FROM sched
WHERE NOT utid IN (
  SELECT utid FROM thread WHERE is_idle
);

DROP TABLE IF EXISTS power_per_thread;
CREATE VIRTUAL TABLE power_per_thread
USING SPAN_LEFT_JOIN(sched_real_threads PARTITIONED cpu, power_view PARTITIONED cpu);
