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

INCLUDE PERFETTO MODULE wattson.cpu.pivot;

INCLUDE PERFETTO MODULE wattson.curves.utils;

INCLUDE PERFETTO MODULE wattson.device_infos;

-- Get estimates per unique configuration, establishing the 1-to-1 map from CPU
-- configuration to the config_hash
CREATE PERFETTO TABLE _unique_estimates_mw AS
SELECT
  base.config_hash,
  coalesce(base.cpu0_curve, lut0.curve_value) AS cpu0_mw,
  coalesce(base.cpu1_curve, lut1.curve_value) AS cpu1_mw,
  coalesce(base.cpu2_curve, lut2.curve_value) AS cpu2_mw,
  coalesce(base.cpu3_curve, lut3.curve_value) AS cpu3_mw,
  coalesce(base.cpu4_curve, lut4.curve_value) AS cpu4_mw,
  coalesce(base.cpu5_curve, lut5.curve_value) AS cpu5_mw,
  coalesce(base.cpu6_curve, lut6.curve_value) AS cpu6_mw,
  coalesce(base.cpu7_curve, lut7.curve_value) AS cpu7_mw,
  iif(
    no_static = 1,
    0.0,
    iif(0 IN _device_policies, coalesce(lut0.static, 0), 0) + iif(1 IN _device_policies, coalesce(lut1.static, 0), 0) + iif(2 IN _device_policies, coalesce(lut2.static, 0), 0) + iif(3 IN _device_policies, coalesce(lut3.static, 0), 0) + iif(4 IN _device_policies, coalesce(lut4.static, 0), 0) + iif(5 IN _device_policies, coalesce(lut5.static, 0), 0) + iif(6 IN _device_policies, coalesce(lut6.static, 0), 0) + iif(7 IN _device_policies, coalesce(lut7.static, 0), 0) + static_1d
  ) AS static_mw,
  l3_lut.l3_hit,
  l3_lut.l3_miss
FROM _w_dependent_cpus_unique AS base
-- LUT for 2D dependencies
LEFT JOIN _filtered_curves_2d AS lut0
  ON lut0.freq_khz = base.freq_0
  AND lut0.dep_policy = base.dep_policy_0
  AND lut0.dep_freq = base.dep_freq_0
  AND lut0.idle = base.idle_0
LEFT JOIN _filtered_curves_2d AS lut1
  ON lut1.freq_khz = base.freq_1
  AND lut1.dep_policy = base.dep_policy_1
  AND lut1.dep_freq = base.dep_freq_1
  AND lut1.idle = base.idle_1
LEFT JOIN _filtered_curves_2d AS lut2
  ON lut2.freq_khz = base.freq_2
  AND lut2.dep_policy = base.dep_policy_2
  AND lut2.dep_freq = base.dep_freq_2
  AND lut2.idle = base.idle_2
LEFT JOIN _filtered_curves_2d AS lut3
  ON lut3.freq_khz = base.freq_3
  AND lut3.dep_policy = base.dep_policy_3
  AND lut3.dep_freq = base.dep_freq_3
  AND lut3.idle = base.idle_3
LEFT JOIN _filtered_curves_2d AS lut4
  ON lut4.freq_khz = base.freq_4
  AND lut4.dep_policy = base.dep_policy_4
  AND lut4.dep_freq = base.dep_freq_4
  AND lut4.idle = base.idle_4
LEFT JOIN _filtered_curves_2d AS lut5
  ON lut5.freq_khz = base.freq_5
  AND lut5.dep_policy = base.dep_policy_5
  AND lut5.dep_freq = base.dep_freq_5
  AND lut5.idle = base.idle_5
LEFT JOIN _filtered_curves_2d AS lut6
  ON lut6.freq_khz = base.freq_6
  AND lut6.dep_policy = base.dep_policy_6
  AND lut6.dep_freq = base.dep_freq_6
  AND lut6.idle = base.idle_6
LEFT JOIN _filtered_curves_2d AS lut7
  ON lut7.freq_khz = base.freq_7
  AND lut7.dep_policy = base.dep_policy_7
  AND lut7.dep_freq = base.dep_freq_7
  AND lut7.idle = base.idle_7
LEFT JOIN _filtered_curves_l3 AS l3_lut
  ON l3_lut.freq_khz = base.freq_0
  AND l3_lut.dep_policy = base.dep_policy_0
  AND l3_lut.dep_freq = base.dep_freq_0;

-- The most basic components of Wattson, all normalized to be in mW on a per
-- system state basis
CREATE PERFETTO TABLE _cpu_estimates_mw AS
SELECT
  slices.ts,
  slices.dur,
  base.cpu0_mw,
  base.cpu1_mw,
  base.cpu2_mw,
  base.cpu3_mw,
  base.cpu4_mw,
  base.cpu5_mw,
  base.cpu6_mw,
  base.cpu7_mw,
  base.static_mw + (
    coalesce(slices.l3_hit_count * base.l3_hit, 0) + coalesce(slices.l3_miss_count * base.l3_miss, 0)
  ) * 1000 / slices.dur AS dsu_scu_mw
FROM _w_independent_cpus_calc AS slices
JOIN _unique_estimates_mw AS base
  USING (config_hash)
WHERE
  slices.dur > 0;
