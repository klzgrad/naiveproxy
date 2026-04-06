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

INCLUDE PERFETTO MODULE android.freezer;

CREATE PERFETTO FUNCTION _extract_broadcast_process_name(
    name STRING
)
RETURNS LONG AS
WITH
  pid_and_name AS (
    SELECT
      str_split(str_split($name, '/', 0), ' ', 1) AS value
  ),
  start AS (
    SELECT
      cast_int!(INSTR(value, ':')) + 1 AS value
    FROM pid_and_name
  )
SELECT
  substr(pid_and_name.value, start.value)
FROM pid_and_name, start;

-- Provides a list of broadcast names and processes they were sent to by the
-- system_server process on U+ devices.
CREATE PERFETTO TABLE _android_broadcasts_minsdk_u (
  -- Broadcast record id.
  record_id STRING,
  -- Intent action of the broadcast.
  intent_action STRING,
  -- Name of the process the broadcast was sent to.
  process_name STRING,
  -- Pid of the process the broadcast was sent to.
  pid LONG,
  -- Upid of the process the broadcast was sent to.
  upid JOINID(process.id),
  -- Id of the broacast process queue the broadcast was dispatched from.
  process_queue_id STRING,
  -- Id of the broacast queue the broadcast was dispatched from.
  queue_id LONG,
  -- Broadcast dispatch slice.
  id JOINID(slice.id),
  -- Timestamp the broadcast was dispatched.
  ts TIMESTAMP,
  -- Duration to dispatch the broadcast.
  dur DURATION,
  -- Track id the broadcast was dispatched from.
  track_id JOINID(track.id)
) AS
WITH
  broadcast_queues AS (
    SELECT
      process_track.id,
      cast_int!(replace(str_split(process_track.name, '[', 1), ']', '')) AS queue_id
    FROM process_track
    JOIN process
      USING (upid)
    WHERE
      process_track.name GLOB 'BroadcastQueue.mRunning*'
      AND process.name = 'system_server'
  ),
  broadcast_process_running AS (
    SELECT
      slice.id AS id,
      slice.ts,
      slice.dur,
      str_split(slice.name, ' ', 0) AS process_queue_id,
      broadcast_queues.queue_id,
      _extract_broadcast_process_name(slice.name) AS process_name,
      cast_int!(str_split(str_split(str_split(slice.name, '/', 0), ' ', 1), ':', 0)) AS pid,
      queue_id
    FROM slice
    JOIN broadcast_queues
      ON broadcast_queues.id = slice.track_id
    WHERE
      slice.name GLOB '* running'
  ),
  broadcast_intent_action AS (
    SELECT
      str_split(str_split(slice.name, '/', 0), ' ', 1) AS intent_action,
      str_split(slice.name, ' ', 0) AS record_id,
      slice.parent_id,
      slice.id AS intent_id,
      slice.ts AS intent_ts,
      slice.track_id AS track_id,
      slice.dur AS intent_dur
    FROM slice
    WHERE
      slice.name GLOB '* scheduled'
  )
SELECT
  broadcast_intent_action.record_id,
  broadcast_intent_action.intent_action,
  broadcast_process_running.process_name,
  broadcast_process_running.pid,
  _pid_to_upid(broadcast_process_running.pid, broadcast_intent_action.intent_ts) AS upid,
  broadcast_process_running.process_queue_id,
  broadcast_process_running.queue_id,
  broadcast_intent_action.intent_id AS id,
  broadcast_intent_action.intent_ts AS ts,
  broadcast_intent_action.intent_dur AS dur,
  broadcast_intent_action.track_id
FROM broadcast_intent_action
JOIN broadcast_process_running
  ON parent_id = id;
