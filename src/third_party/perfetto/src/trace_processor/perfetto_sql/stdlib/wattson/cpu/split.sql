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

INCLUDE PERFETTO MODULE time.conversion;

INCLUDE PERFETTO MODULE wattson.cpu.arm_dsu;

INCLUDE PERFETTO MODULE wattson.cpu.freq_idle;

INCLUDE PERFETTO MODULE wattson.curves.utils;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.utils;

-- Helper macro to do pivot function without policy information
CREATE PERFETTO MACRO _stats_wo_policy_subquery(
    cpu Expr,
    curve_col ColumnName,
    freq_col ColumnName,
    idle_col ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT
    ts,
    dur,
    curve_value AS $curve_col,
    freq AS $freq_col,
    idle AS $idle_col
  FROM _idle_freq_materialized
  WHERE
    cpu = $cpu
);

-- Helper macro to do pivot function with policy information
CREATE PERFETTO MACRO _stats_w_policy_subquery(
    cpu Expr,
    policy_col ColumnName,
    curve_col ColumnName,
    freq_col ColumnName,
    idle_col ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT
    ts,
    dur,
    policy AS $policy_col,
    curve_value AS $curve_col,
    freq AS $freq_col,
    idle AS $idle_col
  FROM _idle_freq_materialized
  WHERE
    cpu = $cpu
);

CREATE PERFETTO TABLE _stats_cpu0 AS
SELECT
  *
FROM _stats_wo_policy_subquery!(0, cpu0_curve, freq_0, idle_0);

CREATE PERFETTO TABLE _stats_cpu1 AS
SELECT
  *
FROM _stats_wo_policy_subquery!(1, cpu1_curve, freq_1, idle_1);

CREATE PERFETTO TABLE _stats_cpu2 AS
SELECT
  *
FROM _stats_wo_policy_subquery!(2, cpu2_curve, freq_2, idle_2);

CREATE PERFETTO TABLE _stats_cpu3 AS
SELECT
  *
FROM _stats_wo_policy_subquery!(3, cpu3_curve, freq_3, idle_3);

CREATE PERFETTO TABLE _stats_cpu4 AS
SELECT
  *
FROM _stats_w_policy_subquery!(4, policy_4, cpu4_curve, freq_4, idle_4);

CREATE PERFETTO TABLE _stats_cpu5 AS
SELECT
  *
FROM _stats_w_policy_subquery!(5, policy_5, cpu5_curve, freq_5, idle_5);

CREATE PERFETTO TABLE _stats_cpu6 AS
SELECT
  *
FROM _stats_w_policy_subquery!(6, policy_6, cpu6_curve, freq_6, idle_6);

CREATE PERFETTO TABLE _stats_cpu7 AS
SELECT
  *
FROM _stats_w_policy_subquery!(7, policy_7, cpu7_curve, freq_7, idle_7);

CREATE PERFETTO TABLE _stats_cpu0123 AS
SELECT
  ii.ts,
  ii.dur,
  id_0 AS cpu0_id,
  id_1 AS cpu1_id,
  id_2 AS cpu2_id,
  id_3 AS cpu3_id
FROM _interval_intersect!(
  (
    _ii_subquery!(_stats_cpu0),
    _ii_subquery!(_stats_cpu1),
    _ii_subquery!(_stats_cpu2),
    _ii_subquery!(_stats_cpu3)
  ),
  ()
) AS ii;

CREATE PERFETTO TABLE _stats_cpu4567 AS
SELECT
  ii.ts,
  ii.dur,
  id_0 AS cpu4_id,
  id_1 AS cpu5_id,
  id_2 AS cpu6_id,
  id_3 AS cpu7_id
FROM _interval_intersect!(
  (
    _ii_subquery!(_stats_cpu4),
    _ii_subquery!(_stats_cpu5),
    _ii_subquery!(_stats_cpu6),
    _ii_subquery!(_stats_cpu7)
  ),
  ()
) AS ii;

-- SPAN OUTER JOIN because sometimes CPU4/5/6/7 are empty tables
CREATE VIRTUAL TABLE _stats_cpu01234567 USING SPAN_OUTER_JOIN (_stats_cpu0123, _stats_cpu4567);

-- Combine system state so that it has idle, freq, and L3 hit info.
CREATE VIRTUAL TABLE _idle_freq_l3_hit_slice USING SPAN_OUTER_JOIN (_stats_cpu01234567, _arm_l3_hit_rate);

-- Combine system state so that it has idle, freq, L3 hit, and L3 miss info.
CREATE VIRTUAL TABLE _idle_freq_l3_hit_l3_miss_slice USING SPAN_OUTER_JOIN (_idle_freq_l3_hit_slice, _arm_l3_miss_rate);

-- Does calculations for CPUs that are independent of other CPUs or frequencies
-- This is the last generic table before going to device specific table calcs
CREATE PERFETTO TABLE _w_independent_cpus_calc AS
SELECT
  base.ts,
  base.dur,
  cast_int!(l3_hit_rate * base.dur) AS l3_hit_count,
  cast_int!(l3_miss_rate * base.dur) AS l3_miss_count,
  freq_0,
  idle_0,
  freq_1,
  idle_1,
  freq_2,
  idle_2,
  freq_3,
  idle_3,
  freq_4,
  idle_4,
  freq_5,
  idle_5,
  freq_6,
  idle_6,
  freq_7,
  idle_7,
  policy_4,
  policy_5,
  policy_6,
  policy_7,
  min(coalesce(idle_0, 1), coalesce(idle_1, 1), coalesce(idle_2, 1), coalesce(idle_3, 1)) AS no_static,
  cpu0_curve,
  cpu1_curve,
  cpu2_curve,
  cpu3_curve,
  cpu4_curve,
  cpu5_curve,
  cpu6_curve,
  cpu7_curve,
  -- If dependency CPUs are active, then that CPU could contribute static power
  iif(idle_4 = -1, lut4.curve_value, -1) AS static_4,
  iif(idle_5 = -1, lut5.curve_value, -1) AS static_5,
  iif(idle_6 = -1, lut6.curve_value, -1) AS static_6,
  iif(idle_7 = -1, lut7.curve_value, -1) AS static_7
FROM _idle_freq_l3_hit_l3_miss_slice AS base
JOIN _stats_cpu0
  ON _stats_cpu0._auto_id = base.cpu0_id
JOIN _stats_cpu1
  ON _stats_cpu1._auto_id = base.cpu1_id
JOIN _stats_cpu2
  ON _stats_cpu2._auto_id = base.cpu2_id
JOIN _stats_cpu3
  ON _stats_cpu3._auto_id = base.cpu3_id
-- Get CPU power curves for CPUs that aren't always present
LEFT JOIN _stats_cpu4
  ON _stats_cpu4._auto_id = base.cpu4_id
LEFT JOIN _stats_cpu5
  ON _stats_cpu5._auto_id = base.cpu5_id
LEFT JOIN _stats_cpu6
  ON _stats_cpu6._auto_id = base.cpu6_id
LEFT JOIN _stats_cpu7
  ON _stats_cpu7._auto_id = base.cpu7_id
-- Match power curves if possible on CPUs that decide 2D dependence
LEFT JOIN _filtered_curves_2d AS lut4
  ON _stats_cpu0.freq_0 = lut4.freq_khz
  AND _stats_cpu4.policy_4 = lut4.other_policy
  AND _stats_cpu4.freq_4 = lut4.other_freq_khz
  AND lut4.idle = 255
LEFT JOIN _filtered_curves_2d AS lut5
  ON _stats_cpu0.freq_0 = lut5.freq_khz
  AND _stats_cpu5.policy_5 = lut5.other_policy
  AND _stats_cpu5.freq_5 = lut5.other_freq_khz
  AND lut5.idle = 255
LEFT JOIN _filtered_curves_2d AS lut6
  ON _stats_cpu0.freq_0 = lut6.freq_khz
  AND _stats_cpu6.policy_6 = lut6.other_policy
  AND _stats_cpu6.freq_6 = lut6.other_freq_khz
  AND lut6.idle = 255
LEFT JOIN _filtered_curves_2d AS lut7
  ON _stats_cpu0.freq_0 = lut7.freq_khz
  AND _stats_cpu7.policy_7 = lut7.other_policy
  AND _stats_cpu7.freq_7 = lut7.other_freq_khz
  AND lut7.idle = 255
-- Needs to be at least 1us to reduce inconsequential rows.
WHERE
  base.dur > time_from_us(1);
