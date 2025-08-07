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

INCLUDE PERFETTO MODULE android.device;

-- Device specific info for deep idle time offsets
CREATE PERFETTO TABLE _device_cpu_deep_idle_offsets AS
WITH
  data(device, cpu, offset_ns) AS (
    SELECT
      *
    FROM (VALUES
      ("Tensor", 0, 0),
      ("Tensor", 1, 0),
      ("Tensor", 2, 0),
      ("Tensor", 3, 0),
      ("Tensor", 4, 0),
      ("Tensor", 5, 0),
      ("Tensor", 6, 200000),
      ("Tensor", 7, 200000),
      ("monaco", 0, 450000),
      ("monaco", 1, 450000),
      ("monaco", 2, 450000),
      ("monaco", 3, 450000),
      ("Tensor G4", 0, 0),
      ("Tensor G4", 1, 0),
      ("Tensor G4", 2, 0),
      ("Tensor G4", 3, 0),
      ("Tensor G4", 4, 110000),
      ("Tensor G4", 5, 110000),
      ("Tensor G4", 6, 110000),
      ("Tensor G4", 7, 400000),
      ("neo", 0, 100000),
      ("neo", 1, 100000),
      ("neo", 2, 100000),
      ("neo", 3, 100000)) AS _values
  )
SELECT
  *
FROM data;

CREATE PERFETTO TABLE _wattson_device_map AS
WITH
  data(device, wattson_device) AS (
    SELECT
      *
    FROM (VALUES
      ("oriole", "Tensor"),
      ("raven", "Tensor"),
      ("bluejay", "Tensor"),
      ("eos", "monaco"),
      ("aurora", "monaco")) AS _values
  )
SELECT
  *
FROM data;

CREATE PERFETTO TABLE _wattson_device AS
WITH
  soc_model AS (
    SELECT
      coalesce(
        -- Get guest model from metadata, which takes precedence if set
        (
          SELECT
            str_value
          FROM metadata
          WHERE
            name = 'android_guest_soc_model'
        ),
        -- Get model from metadata
        (
          SELECT
            str_value
          FROM metadata
          WHERE
            name = 'android_soc_model'
        ),
        -- Get device name from metadata and map it to model
        (
          SELECT
            wattson_device
          FROM _wattson_device_map AS map
          JOIN android_device_name AS ad
            ON ad.name = map.device
        )
      ) AS name
  )
-- Once model is obtained, check to see if the model is supported by Wattson
-- via checking if model is within a key-value pair mapping
SELECT DISTINCT
  name
FROM soc_model
JOIN _device_cpu_deep_idle_offsets AS map
  ON map.device = name;

-- Device specific mapping from CPU to policy
CREATE PERFETTO TABLE _cpu_to_policy_map AS
WITH
  data(device, cpu, policy) AS (
    SELECT
      *
    FROM (VALUES
      ("monaco", 0, 0),
      ("monaco", 1, 0),
      ("monaco", 2, 0),
      ("monaco", 3, 0),
      ("Tensor", 0, 0),
      ("Tensor", 1, 0),
      ("Tensor", 2, 0),
      ("Tensor", 3, 0),
      ("Tensor", 4, 4),
      ("Tensor", 5, 4),
      ("Tensor", 6, 6),
      ("Tensor", 7, 6),
      ("Tensor G4", 0, 0),
      ("Tensor G4", 1, 0),
      ("Tensor G4", 2, 0),
      ("Tensor G4", 3, 0),
      ("Tensor G4", 4, 4),
      ("Tensor G4", 5, 4),
      ("Tensor G4", 6, 4),
      ("Tensor G4", 7, 7),
      ("Tensor G4", 255, 255),
      ("neo", 0, 0),
      ("neo", 1, 0),
      ("neo", 2, 0),
      ("neo", 3, 0)) AS _values
  )
SELECT
  *
FROM data;

-- Prefilter table based on device
CREATE PERFETTO TABLE _dev_cpu_policy_map AS
SELECT
  cpu,
  policy
FROM _cpu_to_policy_map AS cp_map
JOIN _wattson_device AS device
  ON cp_map.device = device.name
ORDER BY
  cpu;

-- Policy and freq that will give minimum volt vote
CREATE PERFETTO TABLE _device_min_volt_vote AS
WITH
  data(device, policy, freq) AS (
    SELECT
      *
    FROM (VALUES
      ("monaco", 0, 614400),
      ("Tensor", 4, 400000),
      ("Tensor G4", 0, 700000),
      ("neo", 0, 691200)) AS _values
  )
SELECT
  *
FROM data;

-- Get policy corresponding to minimum volt vote
CREATE PERFETTO FUNCTION _get_min_policy_vote()
RETURNS LONG AS
SELECT
  vote_tbl.policy
FROM _device_min_volt_vote AS vote_tbl, _wattson_device AS device
WHERE
  vote_tbl.device = device.name;

-- Get frequency corresponding to minimum volt vote
CREATE PERFETTO FUNCTION _get_min_freq_vote()
RETURNS LONG AS
SELECT
  vote_tbl.freq
FROM _device_min_volt_vote AS vote_tbl, _wattson_device AS device
WHERE
  vote_tbl.device = device.name;

-- Devices that require using devfreq
CREATE PERFETTO TABLE _use_devfreq AS
WITH
  data(device) AS (
    SELECT
      *
    FROM (VALUES
      ("Tensor G4")) AS _values
  )
SELECT
  *
FROM data;

-- Creates non-empty table if device needs devfreq
CREATE PERFETTO TABLE _use_devfreq_for_calc AS
SELECT
  TRUE AS devfreq_necessary
FROM _use_devfreq AS d
JOIN _wattson_device AS device
  ON d.device = device.name;

-- Creates empty table if device needs devfreq; inverse of _use_devfreq_for_calc
CREATE PERFETTO TABLE _skip_devfreq_for_calc AS
SELECT
  FALSE AS devfreq_necessary
FROM _use_devfreq AS d
JOIN _wattson_device AS device
  ON d.device != device.name;

-- Devices that require idle state mapping
CREATE PERFETTO TABLE _idle_state_map AS
WITH
  data(device, nominal_idle, override_idle) AS (
    SELECT
      *
    FROM (VALUES
      ("neo", 4294967295, -1),
      ("neo", 0, 0),
      ("neo", 1, 1),
      ("neo", 2, 1)) AS _values
  )
SELECT
  *
FROM data;

-- idle_mapping override filtered for device
CREATE PERFETTO TABLE _idle_state_map_override AS
SELECT
  nominal_idle,
  override_idle
FROM _idle_state_map AS idle_map
JOIN _wattson_device AS device
  ON idle_map.device = device.name;

-- Get the device specific deepest idle state if defined, otherwise use 1 as the
-- deepest idle state
CREATE PERFETTO TABLE _deepest_idle AS
SELECT
  coalesce((
    SELECT
      max(override_idle)
    FROM _idle_state_map_override
  ), 1) AS idle;
