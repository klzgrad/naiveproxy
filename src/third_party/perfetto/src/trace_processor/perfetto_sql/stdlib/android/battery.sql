--
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

-- Battery charge at timestamp.
CREATE PERFETTO VIEW android_battery_charge (
  -- Timestamp.
  ts TIMESTAMP,
  -- Current average micro ampers.
  current_avg_ua DOUBLE,
  -- Current capacity percentage.
  capacity_percent DOUBLE,
  -- Current charge in micro ampers.
  charge_uah DOUBLE,
  -- Current micro ampers.
  current_ua DOUBLE,
  -- Current voltage in micro volts.
  voltage_uv DOUBLE,
  -- Current energy counter in microwatt-hours(µWh).
  energy_counter_uwh DOUBLE,
  -- Current power in milliwatts.
  power_mw DOUBLE
) AS
SELECT
  all_ts.ts,
  current_avg_ua,
  capacity_percent,
  charge_uah,
  current_ua,
  voltage_uv,
  energy_counter_uwh,
  power_mw
FROM (
  SELECT DISTINCT
    (
      ts
    ) AS ts
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name GLOB 'batt.*'
) AS all_ts
LEFT JOIN (
  SELECT
    ts,
    value AS current_avg_ua
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.current.avg_ua'
)
  USING (ts)
LEFT JOIN (
  SELECT
    ts,
    value AS capacity_percent
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.capacity_pct'
)
  USING (ts)
LEFT JOIN (
  SELECT
    ts,
    value AS charge_uah
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.charge_uah'
)
  USING (ts)
LEFT JOIN (
  SELECT
    ts,
    value AS current_ua
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.current_ua'
)
  USING (ts)
LEFT JOIN (
  SELECT
    ts,
    value AS voltage_uv
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.voltage_uv'
)
  USING (ts)
LEFT JOIN (
  SELECT
    ts,
    value AS energy_counter_uwh
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.energy_counter_uwh'
)
  USING (ts)
LEFT JOIN (
  SELECT
    ts,
    value AS power_mw
  FROM counter AS c
  JOIN counter_track AS t
    ON c.track_id = t.id
  WHERE
    name = 'batt.power_mw'
)
  USING (ts)
ORDER BY
  ts;
