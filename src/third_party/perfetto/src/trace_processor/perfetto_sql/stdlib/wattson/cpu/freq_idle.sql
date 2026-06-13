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

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.cpu.freq;

INCLUDE PERFETTO MODULE wattson.cpu.hotplug;

INCLUDE PERFETTO MODULE wattson.cpu.idle;

INCLUDE PERFETTO MODULE wattson.curves.utils;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.utils;

-- Wattson estimation is valid from when first CPU0 frequency appears
CREATE PERFETTO TABLE _valid_window AS
WITH
  window_start AS (
    SELECT
      ts AS start_ts
    FROM _adjusted_cpu_freq
    WHERE
      cpu = 0 AND freq IS NOT NULL
    ORDER BY
      ts ASC
    LIMIT 1
  )
SELECT
  start_ts AS ts,
  trace_end() - start_ts AS dur,
  cpu
FROM window_start
CROSS JOIN _dev_cpu_policy_map;

-- Start matching CPUs with 1D curves based on combination of freq and idle
CREATE PERFETTO TABLE _idle_freq_materialized AS
SELECT
  ii.ts,
  ii.dur,
  ii.cpu,
  freq.policy,
  freq.freq,
  -- Set idle since subsequent calculations are based on number of idle/active
  -- CPUs. If offline/suspended, set the CPU to the device specific deepest idle
  -- state.
  iif(
    suspend.suspended OR hotplug.offline,
    (
      SELECT
        idle
      FROM _deepest_idle
    ),
    idle.idle
  ) AS idle,
  -- If CPU is suspended or offline, set power estimate to 0
  iif(suspend.suspended OR hotplug.offline, 0, lut.curve_value) AS curve_value,
  iif(suspend.suspended OR hotplug.offline, 0, lut.static) AS static
FROM _interval_intersect!(
  (
    _ii_subquery!(_valid_window),
    _ii_subquery!(_adjusted_cpu_freq),
    _ii_subquery!(_adjusted_deep_idle),
    _ii_subquery!(_gapless_hotplug_slices),
    _ii_subquery!(_gapless_suspend_slices)
  ),
  (cpu)
) AS ii
JOIN _adjusted_cpu_freq AS freq
  ON freq._auto_id = id_1
JOIN _adjusted_deep_idle AS idle
  ON idle._auto_id = id_2
JOIN _gapless_hotplug_slices AS hotplug
  ON hotplug._auto_id = id_3
JOIN _gapless_suspend_slices AS suspend
  ON suspend._auto_id = id_4
-- Left join since some CPUs may only match the 2D LUT
LEFT JOIN _filtered_curves_1d AS lut
  ON freq.policy = lut.policy AND freq.freq = lut.freq_khz AND idle.idle = lut.idle;
