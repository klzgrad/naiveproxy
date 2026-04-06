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
--

INCLUDE PERFETTO MODULE android.startup.startups;

-- Create the base CPU span join table.
SELECT RUN_METRIC('android/android_cpu_agg.sql');
SELECT RUN_METRIC('android/cpu_info.sql');

-- Create a span join safe launches view; since both views
-- being span joined have an "id" column, we need to rename
-- the id column for launches to disambiguate the two.
DROP VIEW IF EXISTS android_launches_span_join_safe;
CREATE PERFETTO VIEW android_launches_span_join_safe AS
SELECT ts, dur, startup_id
FROM android_startups;

DROP VIEW IF EXISTS launches_span_join_safe;
CREATE PERFETTO VIEW launches_span_join_safe AS
SELECT startup_id AS launch_id, * FROM android_launches_span_join_safe;

-- Span join the CPU table with the launches table to get the
-- breakdown per-cpu.
DROP TABLE IF EXISTS cpu_freq_sched_per_thread_per_launch;
CREATE VIRTUAL TABLE cpu_freq_sched_per_thread_per_launch
USING SPAN_JOIN(
  android_launches_span_join_safe,
  cpu_freq_sched_per_thread PARTITIONED cpu
);

-- Materialized to avoid span-joining once per core type.
DROP TABLE IF EXISTS mcycles_per_core_type_per_launch;
CREATE PERFETTO TABLE mcycles_per_core_type_per_launch AS
SELECT
  startup_id,
  IFNULL(core_type_per_cpu.core_type, 'unknown') AS core_type,
  CAST(SUM(dur * freq_khz / 1000) / 1e9 AS INT) AS mcycles
FROM cpu_freq_sched_per_thread_per_launch
LEFT JOIN core_type_per_cpu USING (cpu)
WHERE NOT utid IN (
  SELECT utid FROM thread WHERE is_idle
)
GROUP BY 1, 2;

-- Given a launch id and core type, returns the number of mcycles consumed
-- on CPUs of that core type during the launch.
CREATE OR REPLACE PERFETTO FUNCTION mcycles_for_launch_and_core_type(startup_id INT, core_type STRING)
RETURNS INT AS
SELECT mcycles
FROM mcycles_per_core_type_per_launch m
WHERE m.startup_id = $startup_id AND m.core_type = $core_type;

-- Contains the process using the most mcycles during the launch
-- *excluding the process being started*.
-- Materialized to avoid span-joining once per launch.
DROP TABLE IF EXISTS top_mcyles_process_excluding_started_per_launch;
CREATE PERFETTO TABLE top_mcyles_process_excluding_started_per_launch AS
WITH mcycles_per_launch_and_process AS MATERIALIZED (
  SELECT
    startup_id,
    upid,
    CAST(SUM(dur * freq_khz / 1000) / 1e9 AS INT) AS mcycles
  FROM cpu_freq_sched_per_thread_per_launch c
  JOIN thread USING (utid)
  JOIN process USING (upid)
  WHERE
    NOT is_idle
    AND upid NOT IN (
      SELECT upid
      FROM android_startup_processes l
    )
  GROUP BY startup_id, upid
)
SELECT *
FROM (
  SELECT
    *,
    ROW_NUMBER() OVER (PARTITION BY startup_id ORDER BY mcycles DESC) AS mcycles_rank
  FROM mcycles_per_launch_and_process
)
WHERE mcycles_rank <= 5;

-- Given a launch id, returns the name of the processes consuming the most
-- mcycles during the launch excluding the process being started.
CREATE OR REPLACE PERFETTO FUNCTION n_most_active_process_names_for_launch(startup_id INT)
RETURNS STRING AS
SELECT RepeatedField(process_name)
FROM (
  SELECT IFNULL(process.name, "[NULL]") AS process_name
  FROM top_mcyles_process_excluding_started_per_launch
  JOIN process USING (upid)
  WHERE startup_id = $startup_id
  ORDER BY mcycles DESC
);

-- Given a launch id, returns the most active process name.
CREATE OR REPLACE PERFETTO FUNCTION most_active_process_for_launch(startup_id INT)
RETURNS STRING AS
SELECT process.name AS process_name
FROM top_mcyles_process_excluding_started_per_launch
JOIN process USING (upid)
WHERE startup_id = $startup_id
ORDER BY mcycles DESC
LIMIT 1;
