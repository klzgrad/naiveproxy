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
INCLUDE PERFETTO MODULE slices.with_context;

-- All process starts.
CREATE PERFETTO VIEW _proc_start AS
SELECT
  ts,
  dur,
  trim(substr(name, 12)) AS process_name
FROM thread_slice
WHERE
  name GLOB 'Start proc:*' AND process_name = 'system_server';

-- Broadcast, service and activity cold starts.
CREATE PERFETTO TABLE _cold_start AS
WITH
  lifecycle_slice AS (
    SELECT
      id,
      ts AS intent_ts,
      dur AS intent_dur,
      track_id,
      name,
      upid,
      process_name,
      pid,
      utid,
      CASE
        WHEN name GLOB 'performCreate:*'
        THEN 'activity'
        WHEN name GLOB 'serviceCreate:*'
        THEN 'service'
        WHEN name GLOB 'broadcastReceiveComp:*'
        THEN 'broadcast'
      END AS reason,
      CASE
        WHEN name GLOB 'performCreate:*'
        THEN str_split(name, 'performCreate:', 1)
        WHEN name GLOB 'serviceCreate:*'
        THEN str_split(str_split(name, '=', 2), ' ', 0)
        WHEN name GLOB 'broadcastReceive*'
        THEN str_split(name, 'broadcastReceiveComp:', 1)
      END AS intent
    FROM thread_slice AS slice
    WHERE
      name GLOB 'bindApplication'
      OR name GLOB 'performCreate:*'
      OR name GLOB 'serviceCreate:*'
      OR name GLOB 'broadcastReceiveComp:*'
    ORDER BY
      ts
  ),
  cold_start AS (
    SELECT
      *,
      lag(name) OVER (PARTITION BY track_id ORDER BY intent_ts) AS bind_app_name,
      lag(intent_ts) OVER (PARTITION BY track_id ORDER BY intent_ts) AS bind_app_ts,
      lag(intent_dur) OVER (PARTITION BY track_id ORDER BY intent_ts) AS bind_app_dur,
      lag(id) OVER (PARTITION BY track_id ORDER BY intent_ts) AS bind_app_id
    FROM lifecycle_slice
  )
SELECT
  *
FROM cold_start
WHERE
  bind_app_name = 'bindApplication';

-- Join Broadcast, service and activity cold starts with process starts.
CREATE PERFETTO VIEW _cold_proc_start AS
SELECT
  cold_start.*,
  max(proc_start.ts) AS proc_start_ts,
  proc_start.dur AS proc_start_dur,
  cold_start.intent_ts - max(proc_start.ts) + cold_start.intent_dur AS total_dur
FROM _cold_start AS cold_start
JOIN _proc_start AS proc_start
  ON proc_start.process_name = cold_start.process_name
  AND cold_start.intent_ts > proc_start.ts
GROUP BY
  cold_start.upid;

-- Provider cold starts.
CREATE PERFETTO TABLE _provider_start AS
WITH
  provider_start AS (
    SELECT
      id AS bind_app_id
    FROM slice
    WHERE
      name = 'bindApplication'
    EXCEPT
    SELECT
      bind_app_id
    FROM _cold_start
  )
SELECT
  *
FROM provider_start
JOIN thread_slice
  ON id = bind_app_id;

-- Join Provider cold starts with process starts.
CREATE PERFETTO VIEW _provider_proc_start AS
SELECT
  cold_start.*,
  max(proc_start.ts) AS proc_start_ts,
  proc_start.dur AS proc_start_dur,
  cold_start.ts - max(proc_start.ts) + cold_start.dur AS total_dur
FROM _provider_start AS cold_start
JOIN _proc_start AS proc_start
  ON proc_start.process_name = cold_start.process_name
  AND cold_start.ts > proc_start.ts
GROUP BY
  cold_start.upid;

-- All app cold starts with information about their cold start reason:
-- broadcast, service, activity or provider.
CREATE PERFETTO TABLE android_app_process_starts (
  -- Slice id of the bindApplication slice in the app. Uniquely identifies a process start.
  start_id LONG,
  -- Slice id of intent received in the app.
  id LONG,
  -- Track id of the intent received in the app.
  track_id JOINID(track.id),
  -- Name of the process receiving the intent.
  process_name STRING,
  -- Pid of the process receiving the intent.
  pid LONG,
  -- Upid of the process receiving the intent.
  upid JOINID(process.id),
  -- Intent action or component responsible for the cold start.
  intent STRING,
  -- Process start reason: activity, broadcast, service or provider.
  reason STRING,
  -- Timestamp the process start was dispatched from system_server.
  proc_start_ts TIMESTAMP,
  -- Duration to dispatch the process start from system_server.
  proc_start_dur DURATION,
  -- Timestamp the bindApplication started in the app.
  bind_app_ts TIMESTAMP,
  -- Duration to complete bindApplication in the app.
  bind_app_dur DURATION,
  -- Timestamp the Intent was received in the app.
  intent_ts TIMESTAMP,
  -- Duration to handle intent in the app.
  intent_dur DURATION,
  -- Total duration from proc_start dispatched to intent completed.
  total_dur LONG
) AS
SELECT
  bind_app_id AS start_id,
  id,
  track_id,
  process_name,
  pid,
  upid,
  intent,
  reason,
  proc_start_ts,
  proc_start_dur,
  bind_app_ts,
  bind_app_dur,
  intent_ts,
  intent_dur,
  total_dur
FROM _cold_proc_start
UNION ALL
SELECT
  bind_app_id AS start_id,
  NULL AS id,
  NULL AS track_id,
  process_name,
  pid,
  upid,
  NULL AS intent,
  'provider' AS reason,
  proc_start_ts,
  proc_start_dur,
  ts AS bind_app_ts,
  dur AS bind_app_dur,
  NULL AS intent_ts,
  NULL AS intent_dur,
  total_dur
FROM _provider_proc_start;
