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

INCLUDE PERFETTO MODULE android.startup.startups;

-- Time elapsed between the latest user start
-- and the specific end event
-- like package startup(ex carlauncher) or previous user stop.
CREATE PERFETTO TABLE android_auto_multiuser_timing (
  -- Id of the started android user
  event_start_user_id STRING,
  -- Start event time
  event_start_time LONG,
  -- End event time
  event_end_time LONG,
  -- End event name
  event_end_name STRING,
  -- Start event name
  event_start_name STRING,
  -- User switch duration from start event
  -- to end event
  duration LONG
) AS
-- The last ts for user switch event.
WITH
  auto_multiuser_user_start AS (
    SELECT
      slice.name AS event_start_name,
      slice.ts AS user_start_time,
      -- Ex.: UserController.startUser-11-fg-start-mode-1
      -- User is enclosed in dashes and will be at most 2 characters(10, 11, etc.)
      substr(name, instr(name, '-') + 1, 2) AS started_user_id
    FROM slice
    WHERE
      (
        slice.name GLOB "UserController.startUser*"
        AND NOT slice.name GLOB "UserController.startUser-10*"
      )
    ORDER BY
      ts DESC
    LIMIT 1
  )
SELECT
  started_user_id AS event_start_user_id,
  user_start_time AS event_start_time,
  event_end_time,
  event_end_name,
  event_start_name,
  (
    event_end_time - user_start_time
  ) AS duration
FROM (
  SELECT
    ts_end AS event_end_time,
    package AS event_end_name
  FROM android_startups
  UNION
  SELECT
    slice.ts + slice.dur AS event_end_time,
    slice.name AS event_end_name
  FROM slice
  WHERE
    slice.name GLOB "finishUserStopped-10*"
) AS a
JOIN auto_multiuser_user_start AS b
  ON a.event_end_time > b.user_start_time;

-- Previous user(user 10) total CPU time
CREATE PERFETTO VIEW _android_auto_user_10_total_cpu_time AS
SELECT
  sum(dur) AS total_cpu_time,
  (
    uid - android_appid
  ) / 100000 AS user_id,
  event_end_name
FROM sched_slice
JOIN thread
  USING (utid)
JOIN process
  USING (upid), android_auto_multiuser_timing
WHERE
  user_id = 10
  AND ts >= android_auto_multiuser_timing.event_start_time
  AND ts <= android_auto_multiuser_timing.event_end_time
GROUP BY
  event_end_name;

-- Previous user(user 10) total memory usage
CREATE PERFETTO VIEW _android_auto_user_10_total_memory AS
WITH
  filtered_process AS (
    SELECT
      c.ts,
      c.value,
      p.name AS proc_name,
      (
        uid - android_appid
      ) / 100000 AS user_id,
      event_end_name
    FROM counter AS c
    LEFT JOIN process_counter_track AS t
      ON c.track_id = t.id
    LEFT JOIN process AS p
      USING (upid), android_auto_multiuser_timing
    WHERE
      t.name GLOB "mem.rss"
      AND user_id = 10
      AND c.ts >= android_auto_multiuser_timing.event_start_time
      AND c.ts <= android_auto_multiuser_timing.event_end_time
  ),
  process_rss AS (
    SELECT
      *,
      coalesce(lag(value) OVER (PARTITION BY proc_name, event_end_name ORDER BY ts), value) AS prev_value
    FROM filtered_process
  ),
  per_process_allocations AS (
    SELECT
      proc_name,
      sum(value - prev_value) / 1e3 AS alloc_value_kb,
      user_id,
      event_end_name
    FROM process_rss
    WHERE
      value - prev_value > 0
    GROUP BY
      proc_name,
      event_end_name
    ORDER BY
      alloc_value_kb DESC
  )
SELECT
  cast_int!(SUM(alloc_value_kb)) AS total_memory_usage_kb,
  user_id,
  event_end_name
FROM per_process_allocations
GROUP BY
  event_end_name;

-- This table extends `android_auto_multiuser_timing` table with previous user resource usage.
CREATE PERFETTO VIEW android_auto_multiuser_timing_with_previous_user_resource_usage (
  -- Start user id
  event_start_user_id STRING,
  -- Start event time
  event_start_time LONG,
  -- End event time
  event_end_time LONG,
  -- End event name
  event_end_name STRING,
  -- Start event name
  event_start_name STRING,
  -- User switch duration from start event
  -- to end event
  duration LONG,
  -- User id
  user_id LONG,
  -- Total CPU time for a user
  total_cpu_time LONG,
  -- Total memory user for a user
  total_memory_usage_kb LONG
) AS
SELECT
  a.event_start_user_id,
  a.event_start_time,
  a.event_end_time,
  a.event_end_name,
  a.event_start_name,
  a.duration,
  b.user_id,
  b.total_cpu_time,
  c.total_memory_usage_kb
FROM android_auto_multiuser_timing AS a
LEFT JOIN _android_auto_user_10_total_cpu_time AS b
  ON a.event_end_name = b.event_end_name
LEFT JOIN _android_auto_user_10_total_memory AS c
  ON a.event_end_name = c.event_end_name;
