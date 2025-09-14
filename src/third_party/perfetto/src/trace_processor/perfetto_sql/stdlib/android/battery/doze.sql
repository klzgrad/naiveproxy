-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE counters.intervals;

-- Light idle states. This is the state machine that quickly detects the
-- device is unused and restricts background activity.
-- See https://developer.android.com/training/monitoring-device-state/doze-standby
CREATE PERFETTO TABLE android_light_idle_state (
  -- ID
  id LONG,
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration.
  dur DURATION,
  -- Description of the light idle state.
  light_idle_state STRING
) AS
WITH
  _counter AS (
    SELECT
      counter.id,
      ts,
      0 AS track_id,
      value
    FROM counter
    JOIN counter_track
      ON counter_track.id = counter.track_id
    WHERE
      name = 'DozeLightState'
  )
SELECT
  id,
  ts,
  dur,
  CASE value
    -- device is used or on power
    WHEN 0
    THEN 'active'
    -- device is waiting to go idle
    WHEN 1
    THEN 'inactive'
    -- device is idle
    WHEN 4
    THEN 'idle'
    -- waiting for connectivity before maintenance
    WHEN 5
    THEN 'waiting_for_network'
    -- maintenance running
    WHEN 6
    THEN 'idle_maintenance'
    -- device has gone deep idle, light idle state irrelevant
    WHEN 7
    THEN 'override'
    ELSE 'unmapped'
  END AS light_idle_state
FROM counter_leading_intervals!(_counter);

-- Deep idle states. This is the state machine that more slowly detects deeper
-- levels of device unuse and restricts background activity further.
-- See https://developer.android.com/training/monitoring-device-state/doze-standby
CREATE PERFETTO TABLE android_deep_idle_state (
  -- ID
  id LONG,
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration.
  dur DURATION,
  -- Description of the deep idle state.
  deep_idle_state STRING
) AS
WITH
  _counter AS (
    SELECT
      counter.id,
      ts,
      0 AS track_id,
      value
    FROM counter
    JOIN counter_track
      ON counter_track.id = counter.track_id
    WHERE
      name = 'DozeDeepState'
  )
SELECT
  id,
  ts,
  dur,
  CASE value
    WHEN 0
    THEN 'active'
    WHEN 1
    THEN 'inactive'
    -- waiting for next idle period
    WHEN 2
    THEN 'idle_pending'
    -- device is sensing motion
    WHEN 3
    THEN 'sensing'
    -- device is finding location
    WHEN 4
    THEN 'locating'
    WHEN 5
    THEN 'idle'
    WHEN 6
    THEN 'idle_maintenance'
    -- inactive, should go straight to idle without motion / location
    -- sensing.
    WHEN 7
    THEN 'quick_doze_delay'
    ELSE 'unmapped'
  END AS deep_idle_state
FROM counter_leading_intervals!(_counter);
