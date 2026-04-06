--
-- Copyright 2019 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.power_rails;

-- View of Power Rail counters with ts converted from ns to ms.
DROP VIEW IF EXISTS power_rails_counters;
CREATE PERFETTO VIEW power_rails_counters AS
SELECT value, ts / 1000000 AS ts, power_rail_name AS name
FROM android_power_rails_counters;

DROP VIEW IF EXISTS avg_used_powers;
CREATE PERFETTO VIEW avg_used_powers AS
SELECT
  name,
  avg_used_power,
  tot_used_power,
  powrail_start_ts,
  powrail_end_ts
FROM (
  SELECT
    name,
    (LEAD(value) OVER (PARTITION BY name ORDER BY ts) - value)
    / (LEAD(ts) OVER (PARTITION BY name ORDER BY ts) - ts) AS avg_used_power,
    (LEAD(value) OVER (PARTITION BY name ORDER BY ts) - value) AS tot_used_power,
    ts AS powrail_start_ts,
    (LEAD(ts) OVER (PARTITION BY name ORDER BY ts)) AS powrail_end_ts
  FROM (
    SELECT name, MIN(ts) AS ts, value
    FROM power_rails_counters
    GROUP BY name
    UNION
    SELECT name, MAX(ts) AS ts, value
    FROM power_rails_counters
    GROUP BY name
  )
  ORDER BY name, ts
) WHERE avg_used_power IS NOT NULL;

DROP VIEW IF EXISTS power_rails_view;
CREATE PERFETTO VIEW power_rails_view AS
WITH RECURSIVE name AS (SELECT DISTINCT name FROM power_rails_counters)
SELECT
  name,
  ts,
  AndroidPowerRails_PowerRails(
    'name', name,
    'energy_data', RepeatedField(
      AndroidPowerRails_EnergyData(
        'timestamp_ms', ts,
        'energy_uws', value
      )
    ),
    'avg_used_power_mw', (SELECT avg_used_power FROM avg_used_powers
      WHERE avg_used_powers.name = power_rails_counters.name)
  ) AS power_rails_proto
FROM power_rails_counters
GROUP BY name
ORDER BY ts ASC;

DROP VIEW IF EXISTS android_powrails_output;
CREATE PERFETTO VIEW android_powrails_output AS
SELECT AndroidPowerRails(
  'power_rails', (
    SELECT RepeatedField(power_rails_proto)
    FROM power_rails_view
  ),
  'avg_total_used_power_mw', (
    SELECT SUM(tot_used_power) / (MAX(powrail_end_ts) - MIN(powrail_start_ts)) FROM avg_used_powers
  )
);
