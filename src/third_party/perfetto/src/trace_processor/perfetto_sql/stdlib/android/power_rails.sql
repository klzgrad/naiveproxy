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

INCLUDE PERFETTO MODULE counters.intervals;

-- Android power rails counters data.
-- For details see: https://perfetto.dev/docs/data-sources/battery-counters#odpm
-- NOTE: Requires dedicated hardware - table is only populated on Pixels.
CREATE PERFETTO TABLE android_power_rails_counters (
  -- `counter.id`
  id ID(counter.id),
  -- Timestamp of the energy measurement.
  ts TIMESTAMP,
  -- Time until the next energy measurement.
  dur DURATION,
  -- Power rail name. Alias of `counter_track.name`.
  power_rail_name STRING,
  -- Raw power rail name.
  raw_power_rail_name STRING,
  -- Energy accumulated by this rail since boot in microwatt-seconds
  -- (uWs) (AKA micro-joules). Alias of `counter.value`.
  energy_since_boot DOUBLE,
  -- Energy accumulated by this rail at next energy measurement in
  -- microwatt-seconds (uWs) (AKA micro-joules). Alias of `counter.value` of
  -- the next meaningful (with value change) counter value.
  energy_since_boot_at_end DOUBLE,
  -- Average power in mW (milliwatts) over between ts and the next energy
  -- measurement.
  average_power DOUBLE,
  -- The change of energy accumulated by this rails since the last
  -- measurement in microwatt-seconds (uWs) (AKA micro-joules).
  energy_delta DOUBLE,
  -- Power rail track id. Alias of `counter_track.id`.
  track_id JOINID(track.id),
  -- DEPRECATED. Use `energy_since_boot` instead.
  value DOUBLE
) AS
WITH
  counter_table AS (
    SELECT
      c.*
    FROM counter AS c
    JOIN counter_track AS t
      ON c.track_id = t.id
    WHERE
      type = 'power_rails'
  )
SELECT
  c.id,
  c.ts,
  c.dur,
  t.name AS power_rail_name,
  extract_arg(source_arg_set_id, 'raw_name') AS raw_power_rail_name,
  c.value AS energy_since_boot,
  c.next_value AS energy_since_boot_at_end,
  1e6 * (
    (
      c.next_value - c.value
    ) / c.dur
  ) AS average_power,
  c.delta_value AS energy_delta,
  c.track_id,
  c.value
FROM counter_leading_intervals!(counter_table) AS c
JOIN counter_track AS t
  ON c.track_id = t.id;

-- High level metadata about each of the power rails.
CREATE PERFETTO TABLE android_power_rails_metadata (
  -- Power rail name. Alias of `counter_track.name`.
  power_rail_name STRING,
  -- Raw power rail name from the hardware.
  raw_power_rail_name STRING,
  -- User-friendly name for the power rail.
  friendly_name STRING,
  -- Power rail track id. Alias of `counter_track.id`.
  track_id JOINID(track.id),
  -- Subsystem name that this power rail belongs to.
  subsystem_name STRING,
  -- The device the power rail is associated with.
  machine_id JOINID(machine.id)
) AS
SELECT
  t.name AS power_rail_name,
  extract_arg(t.source_arg_set_id, 'raw_name') AS raw_power_rail_name,
  CASE WHEN t.name GLOB 'power.rails.*' THEN substr(t.name, 13) ELSE NULL END AS friendly_name,
  t.id AS track_id,
  extract_arg(t.source_arg_set_id, 'subsystem_name') AS subsystem_name,
  t.machine_id AS machine_id
FROM counter_track AS t
WHERE
  t.type = 'power_rails';
