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

CREATE PERFETTO FUNCTION _extract_freezer_pid(
    name STRING
)
RETURNS LONG AS
SELECT
  cast_int!(reverse(str_split(reverse(str_split($name, ' ', 1)), ':', 0)));

-- Converts a pid to a upid using the timestamp of occurrence of an event from
-- |pid| to disambiguate duplicate pids.
--
-- This is still best effort because it relies on having information about
-- process start and end in the trace. In the edge case that we are missing this,
-- it best effort returns the last upid.
CREATE PERFETTO FUNCTION _pid_to_upid(
    -- Pid to convert from.
    pid LONG,
    -- Timestamp of an event from the |pid|.
    event_ts TIMESTAMP
)
-- Returns the converted upid.
RETURNS LONG AS
WITH
  process_lifetime AS (
    SELECT
      pid,
      upid,
      coalesce(start_ts, trace_start()) AS start_ts,
      coalesce(end_ts, trace_end()) AS end_ts
    FROM process
  )
SELECT
  upid
FROM process_lifetime
WHERE
  pid = $pid AND $event_ts BETWEEN start_ts AND end_ts
ORDER BY
  upid DESC
LIMIT 1;

-- Translate unfreeze reason from INT to STRING.
-- See: frameworks/proto_logging/stats/atoms.proto
CREATE PERFETTO FUNCTION _translate_unfreeze_reason(
    reason LONG
)
RETURNS STRING AS
SELECT
  CASE
    WHEN $reason = 0
    THEN 'none'
    WHEN $reason = 1
    THEN 'activity'
    WHEN $reason = 2
    THEN 'finish_receiver'
    WHEN $reason = 3
    THEN 'start_receiver'
    WHEN $reason = 4
    THEN 'bind_service'
    WHEN $reason = 5
    THEN 'unbind_service'
    WHEN $reason = 6
    THEN 'start_service'
    WHEN $reason = 7
    THEN 'get_provider'
    WHEN $reason = 8
    THEN 'remove_provider'
    WHEN $reason = 9
    THEN 'ui_visibility'
    WHEN $reason = 10
    THEN 'allowlist'
    WHEN $reason = 11
    THEN 'process_begin'
    WHEN $reason = 12
    THEN 'process_end'
    WHEN $reason = 13
    THEN 'trim_memory'
    WHEN $reason = 15
    THEN 'ping'
    WHEN $reason = 16
    THEN 'file_locks'
    WHEN $reason = 17
    THEN 'file_lock_check_failure'
    WHEN $reason = 18
    THEN 'binder_txns'
    WHEN $reason = 19
    THEN 'feature_flags'
    WHEN $reason = 20
    THEN 'short_fgs_timeout'
    WHEN $reason = 21
    THEN 'system_init'
    WHEN $reason = 22
    THEN 'backup'
    WHEN $reason = 23
    THEN 'shell'
    WHEN $reason = 24
    THEN 'remove_task'
    WHEN $reason = 25
    THEN 'uid_idle'
    WHEN $reason = 26
    THEN 'stop_service'
    WHEN $reason = 27
    THEN 'executing_service'
    WHEN $reason = 28
    THEN 'restriction_change'
    WHEN $reason = 29
    THEN 'component_disabled'
    ELSE NULL
  END;

-- All frozen processes and their frozen duration.
CREATE PERFETTO TABLE android_freezer_events (
  -- Upid of frozen process
  upid JOINID(process.id),
  -- Pid of frozen process
  pid LONG,
  -- Timestamp process was frozen.
  ts TIMESTAMP,
  -- Duration process was frozen for.
  dur DURATION,
  -- Unfreeze reason Integer.
  unfreeze_reason_int LONG,
  -- Unfreeze reason String.
  unfreeze_reason_str STRING
) AS
WITH
  freeze AS (
    SELECT
      ts,
      _extract_freezer_pid(name) AS pid,
      _pid_to_upid(_extract_freezer_pid(name), ts) AS upid,
      'freeze' AS type,
      NULL AS unfreeze_reason
    FROM slice
    WHERE
      name GLOB 'Freeze *:*'
  ),
  unfreeze AS (
    SELECT
      ts,
      _extract_freezer_pid(name) AS pid,
      _pid_to_upid(_extract_freezer_pid(name), ts) AS upid,
      'unfreeze' AS type,
      str_split(name, ' ', 2) AS unfreeze_reason
    FROM slice
    WHERE
      name GLOB 'Unfreeze *:*'
  ),
  merged AS (
    SELECT
      *
    FROM freeze
    UNION ALL
    SELECT
      *
    FROM unfreeze
  ),
  starts AS (
    SELECT
      type,
      upid,
      pid,
      ts,
      coalesce(lead(ts) OVER (PARTITION BY upid ORDER BY ts), trace_end()) - ts AS dur,
      cast_int!(lead(unfreeze_reason) OVER (PARTITION BY upid ORDER BY ts)) AS unfreeze_reason
    FROM merged
  )
SELECT
  upid,
  pid,
  ts,
  dur,
  unfreeze_reason AS unfreeze_reason_int,
  _translate_unfreeze_reason(unfreeze_reason) AS unfreeze_reason_str
FROM starts
WHERE
  starts.type = 'freeze' AND upid IS NOT NULL;
