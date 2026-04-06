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

INCLUDE PERFETTO MODULE counters.intervals;

-- Device charging states.
CREATE PERFETTO TABLE android_charging_states (
  -- Alias of counter.id if a slice with charging state exists otherwise
  -- there will be a single row where id = 1.
  id LONG,
  -- Timestamp at which the device charging state began.
  ts TIMESTAMP,
  -- Duration of the device charging state.
  dur DURATION,
  -- One of: charging, discharging, not_charging, full, unknown.
  short_charging_state STRING,
  -- Device charging state, one of: Charging, Discharging, Not charging
  -- (when the charger is present but battery is not charging),
  -- Full, Unknown
  charging_state STRING
) AS
-- Either the first statement is populated or the select statement after the
-- union is populated but not both.
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
      counter_track.name = 'BatteryStatus'
  )
SELECT
  id,
  ts,
  dur,
  CASE value
    WHEN 2
    THEN 'charging'
    WHEN 3
    THEN 'discharging'
    WHEN 4
    THEN 'not_charging'
    WHEN 5
    THEN 'full'
    ELSE 'unknown'
  END AS short_charging_state,
  CASE value
    -- 0 and 1 are both 'Unknown'
    WHEN 2
    THEN 'Charging'
    WHEN 3
    THEN 'Discharging'
    -- special case when charger is present but battery isn't charging
    WHEN 4
    THEN 'Not charging'
    WHEN 5
    THEN 'Full'
    ELSE 'Unknown'
  END AS charging_state
FROM counter_leading_intervals !(_counter)
WHERE
  dur > 0
UNION
-- When the trace does not have a slice in the charging state track then
-- we will assume that the charging state for the entire trace is Unknown.
-- This ensures that we still have job data even if the charging state is
-- not known. The following statement will only ever return a single row.
SELECT
  1,
  trace_start(),
  trace_dur(),
  'unknown',
  'Unknown'
WHERE
  NOT EXISTS(
    SELECT
      *
    FROM _counter
  ) AND trace_dur() > 0;
