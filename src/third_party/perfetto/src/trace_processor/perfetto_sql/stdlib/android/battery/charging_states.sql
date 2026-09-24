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

INCLUDE PERFETTO MODULE intervals.fill_gaps;

-- Device charging states.
CREATE PERFETTO TABLE android_charging_states(
  -- A unique id for each row.
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
)
AS
-- Either the first statement is populated or the select statement after the
-- union is populated but not both.
WITH
  _counter AS (
    SELECT counter.id, ts, 0 AS track_id, value
    FROM counter
    JOIN counter_track
      ON counter_track.id = counter.track_id
    WHERE
      counter_track.name = 'BatteryStatus'
  ),
  _intervals AS (
    SELECT
      id,
      ts,
      dur,
      CASE value
        WHEN 2 THEN 'charging'
        WHEN 3 THEN 'discharging'
        WHEN 4 THEN 'not_charging'
        WHEN 5 THEN 'full'
      END AS short_charging_state,
      CASE value
        -- 0 and 1 are both 'Unknown'
        WHEN 2 THEN 'Charging'
        WHEN 3 THEN 'Discharging'
        -- special case when charger is present but battery isn't charging
        WHEN 4 THEN 'Not charging'
        WHEN 5 THEN 'Full'
      END AS charging_state
    FROM counter_leading_intervals!(_counter)
    WHERE
      dur > 0
  )
-- When the trace does not have a slice in the charging state track or when
-- the charging state value is not a valid enum value, assume the charging
-- state for the entire trace is unknown. The use of _intervals_fill_gaps
-- ensures we still have data for the entirety of the trace.
SELECT
  ROW_NUMBER() OVER () AS id,
  ts,
  dur,
  COALESCE(short_charging_state, 'unknown') AS short_charging_state,
  COALESCE(charging_state, 'Unknown') AS charging_state
FROM _intervals_fill_gaps!((NULL), (short_charging_state, charging_state), _intervals);
