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
--

-- Android entity state residency samples.
-- For details see: https://perfetto.dev/docs/reference/trace-config-proto#AndroidPowerConfig
CREATE PERFETTO TABLE android_entity_state_residency (
  -- `counter.id`
  id ID(counter.id),
  -- Timestamp of the residency sample.
  ts TIMESTAMP,
  -- Time until the next residency sample.
  dur DURATION,
  -- Entity or subsytem name.
  entity_name STRING,
  -- State name
  state_name STRING,
  -- Raw name (alias of counter.name)
  raw_name STRING,
  -- Time the entity or subsystem spent in the state since boot
  state_time_since_boot DURATION,
  -- Time the entity or subsystem spent in the state since boot on the next
  -- sample
  state_time_since_boot_at_end DURATION,
  -- ratio of the time the entity or subsystem spend in the state out of the
  -- elapsed time of the sample period. A value of 1 typically means the 100%
  -- of time was spent in the state, and a value of 0 means no time was spent.
  state_time_ratio DOUBLE,
  -- entity + state track id. Alias of `counter_track.id`.
  track_id JOINID(track.id)
) AS
WITH
  filtered_track_info AS (
    SELECT
      id,
      name AS raw_name,
      iif(
        name GLOB 'Entity residency: *' AND name GLOB '* is *',
        replace(substr(name, 0, instr(name, ' is ')), 'Entity residency: ', ''),
        NULL
      ) AS entity_name,
      iif(
        name GLOB 'Entity residency: *' AND name GLOB '* is *',
        substr(name, instr(name, ' is ') + length(' is ')),
        NULL
      ) AS state_name
    FROM counter_track
    WHERE
      type = 'entity_state'
  ),
  partial_results AS (
    SELECT
      c.id,
      c.ts,
      lead(c.ts) OVER (PARTITION BY track_id ORDER BY c.ts) - c.ts AS dur,
      t.entity_name,
      t.state_name,
      t.raw_name,
      CAST(c.value * 1e6 AS INTEGER) AS state_time_since_boot,
      CAST(lead(c.value) OVER (PARTITION BY track_id ORDER BY c.ts) * 1e6 AS INTEGER) AS state_time_since_boot_at_end,
      c.track_id
    FROM counter AS c
    JOIN filtered_track_info AS t
      ON c.track_id = t.id
  )
SELECT
  id,
  ts,
  dur,
  entity_name,
  state_name,
  raw_name,
  state_time_since_boot,
  state_time_since_boot_at_end,
  (
    state_time_since_boot_at_end - state_time_since_boot
  ) * 1.0 / dur AS state_time_ratio,
  track_id
FROM partial_results;
