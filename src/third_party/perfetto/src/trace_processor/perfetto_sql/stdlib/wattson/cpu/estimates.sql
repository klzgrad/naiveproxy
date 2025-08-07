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

INCLUDE PERFETTO MODULE wattson.cpu.split;

INCLUDE PERFETTO MODULE wattson.cpu.w_cpu_dependence;

INCLUDE PERFETTO MODULE wattson.cpu.w_dsu_dependence;

INCLUDE PERFETTO MODULE wattson.curves.utils;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.utils;

-- One of the two tables will be empty, depending on whether the device is
-- dependent on devfreq or a different CPU's frequency
CREATE PERFETTO VIEW _curves_w_dependencies (
  ts TIMESTAMP,
  dur DURATION,
  freq_0 LONG,
  idle_0 LONG,
  freq_1 LONG,
  idle_1 LONG,
  freq_2 LONG,
  idle_2 LONG,
  freq_3 LONG,
  idle_3 LONG,
  cpu0_curve DOUBLE,
  cpu1_curve DOUBLE,
  cpu2_curve DOUBLE,
  cpu3_curve DOUBLE,
  cpu4_curve DOUBLE,
  cpu5_curve DOUBLE,
  cpu6_curve DOUBLE,
  cpu7_curve DOUBLE,
  l3_hit_count LONG,
  l3_miss_count LONG,
  no_static LONG,
  all_cpu_deep_idle LONG,
  dependent_freq LONG,
  dependent_policy LONG
) AS
-- Table that is dependent on differet CPU's frequency
SELECT
  *
FROM _w_cpu_dependence
UNION ALL
-- Table that is dependent of devfreq frequency
SELECT
  *
FROM _w_dsu_dependence;

-- Final table showing the curves per CPU per slice
CREATE PERFETTO TABLE _system_state_curves AS
SELECT
  base.ts,
  base.dur,
  -- base.cpu[0-3]_curve will be non-zero if CPU has 1D dependency
  -- base.cpu[0-3]_curve will be zero if device is suspended or deep idle
  -- base.cpu[0-3]_curve will be NULL if 2D dependency required
  coalesce(base.cpu0_curve, lut0.curve_value) AS cpu0_curve,
  coalesce(base.cpu1_curve, lut1.curve_value) AS cpu1_curve,
  coalesce(base.cpu2_curve, lut2.curve_value) AS cpu2_curve,
  coalesce(base.cpu3_curve, lut3.curve_value) AS cpu3_curve,
  -- base.cpu[4-7]_curve will be non-zero if CPU has 1D dependency
  -- base.cpu[4-7]_curve will be zero if device is suspended or deep idle
  -- base.cpu[4-7]_curve will be NULL if CPU doesn't exist on device
  coalesce(base.cpu4_curve, 0.0) AS cpu4_curve,
  coalesce(base.cpu5_curve, 0.0) AS cpu5_curve,
  coalesce(base.cpu6_curve, 0.0) AS cpu6_curve,
  coalesce(base.cpu7_curve, 0.0) AS cpu7_curve,
  iif(no_static = 1, 0.0, coalesce(static_1d.curve_value, static_2d.curve_value)) AS static_curve,
  iif(all_cpu_deep_idle = 1, 0, base.l3_hit_count * l3_hit_lut.curve_value) AS l3_hit_value,
  iif(all_cpu_deep_idle = 1, 0, base.l3_miss_count * l3_miss_lut.curve_value) AS l3_miss_value
FROM _curves_w_dependencies AS base
-- LUT for 2D dependencies
LEFT JOIN _filtered_curves_2d AS lut0
  ON lut0.freq_khz = base.freq_0
  AND lut0.other_policy = base.dependent_policy
  AND lut0.other_freq_khz = base.dependent_freq
  AND lut0.idle = base.idle_0
LEFT JOIN _filtered_curves_2d AS lut1
  ON lut1.freq_khz = base.freq_1
  AND lut1.other_policy = base.dependent_policy
  AND lut1.other_freq_khz = base.dependent_freq
  AND lut1.idle = base.idle_1
LEFT JOIN _filtered_curves_2d AS lut2
  ON lut2.freq_khz = base.freq_2
  AND lut2.other_policy = base.dependent_policy
  AND lut2.other_freq_khz = base.dependent_freq
  AND lut2.idle = base.idle_2
LEFT JOIN _filtered_curves_2d AS lut3
  ON lut3.freq_khz = base.freq_3
  AND lut3.other_policy = base.dependent_policy
  AND lut3.other_freq_khz = base.dependent_freq
  AND lut3.idle = base.idle_3
-- LUT for static curve lookup
LEFT JOIN _filtered_curves_2d AS static_2d
  ON static_2d.freq_khz = base.freq_0
  AND static_2d.other_policy = base.dependent_policy
  AND static_2d.other_freq_khz = base.dependent_freq
  AND static_2d.idle = 255
LEFT JOIN _filtered_curves_1d AS static_1d
  ON static_1d.policy = 0 AND static_1d.freq_khz = base.freq_0 AND static_1d.idle = 255
-- LUT joins for L3 cache
LEFT JOIN _filtered_curves_l3 AS l3_hit_lut
  ON l3_hit_lut.freq_khz = base.freq_0
  AND l3_hit_lut.other_policy = base.dependent_policy
  AND l3_hit_lut.other_freq_khz = base.dependent_freq
  AND l3_hit_lut.action = 'hit'
LEFT JOIN _filtered_curves_l3 AS l3_miss_lut
  ON l3_miss_lut.freq_khz = base.freq_0
  AND l3_miss_lut.other_policy = base.dependent_policy
  AND l3_miss_lut.other_freq_khz = base.dependent_freq
  AND l3_miss_lut.action = 'miss';

-- The most basic components of Wattson, all normalized to be in mW on a per
-- system state basis
CREATE PERFETTO TABLE _cpu_estimates_mw AS
SELECT
  ts,
  dur,
  cpu0_curve AS cpu0_mw,
  cpu1_curve AS cpu1_mw,
  cpu2_curve AS cpu2_mw,
  cpu3_curve AS cpu3_mw,
  cpu4_curve AS cpu4_mw,
  cpu5_curve AS cpu5_mw,
  cpu6_curve AS cpu6_mw,
  cpu7_curve AS cpu7_mw,
  -- LUT for l3 is scaled by 10^6 to save resolution and in units of kWs. Scale
  -- this by 10^3 so when divided by ns, result is in units of mW
  (
    (
      coalesce(l3_hit_value, 0) + coalesce(l3_miss_value, 0)
    ) * 1000 / dur
  ) + static_curve AS dsu_scu_mw
FROM _system_state_curves;
