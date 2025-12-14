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

INCLUDE PERFETTO MODULE wattson.cpu.arm_dsu;

INCLUDE PERFETTO MODULE wattson.cpu.freq_idle;

INCLUDE PERFETTO MODULE wattson.curves.utils;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.utils;

-- Helper macro to do pivot function
CREATE PERFETTO MACRO _cpu_stats_subquery(
    cpu Expr,
    curve_col ColumnName,
    static_col ColumnName,
    freq_col ColumnName,
    idle_col ColumnName,
    default_dep_policy ColumnName,
    default_dep_freq ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT
    t1.ts,
    t1.dur,
    t1.curve_value AS $curve_col,
    iif($cpu IN _device_policies, coalesce(t1.static, 0), 0) AS $static_col,
    t1.freq AS $freq_col,
    coalesce(t1.idle, deepest.idle) AS $idle_col,
    t2.dep_policy AS $default_dep_policy,
    t2.dep_freq AS $default_dep_freq
  FROM _idle_freq_materialized AS t1
  CROSS JOIN _deepest_idle AS deepest
  LEFT JOIN _cpu_w_dependency_default_vote AS t2
    USING (cpu)
  WHERE
    cpu = $cpu
  UNION ALL
  SELECT
    trace_start(),
    trace_dur(),
    0,
    0,
    NULL,
    idle,
    NULL,
    NULL
  FROM _deepest_idle()
  WHERE
    NOT EXISTS(
      SELECT
        1
      FROM _dev_cpu_policy_map
      WHERE
        cpu = $cpu
    )
);

CREATE PERFETTO TABLE _stats_cpu0 AS
SELECT
  *
FROM _cpu_stats_subquery!(0, cpu0_curve, cpu0_static, freq_0, idle_0, default_dep_policy_0, default_dep_freq_0);

CREATE PERFETTO TABLE _stats_cpu1 AS
SELECT
  *
FROM _cpu_stats_subquery!(1, cpu1_curve, cpu1_static, freq_1, idle_1, default_dep_policy_1, default_dep_freq_1);

CREATE PERFETTO TABLE _stats_cpu2 AS
SELECT
  *
FROM _cpu_stats_subquery!(2, cpu2_curve, cpu2_static, freq_2, idle_2, default_dep_policy_2, default_dep_freq_2);

CREATE PERFETTO TABLE _stats_cpu3 AS
SELECT
  *
FROM _cpu_stats_subquery!(3, cpu3_curve, cpu3_static, freq_3, idle_3, default_dep_policy_3, default_dep_freq_3);

CREATE PERFETTO TABLE _stats_cpu4 AS
SELECT
  *
FROM _cpu_stats_subquery!(4, cpu4_curve, cpu4_static, freq_4, idle_4, default_dep_policy_4, default_dep_freq_4);

CREATE PERFETTO TABLE _stats_cpu5 AS
SELECT
  *
FROM _cpu_stats_subquery!(5, cpu5_curve, cpu5_static, freq_5, idle_5, default_dep_policy_5, default_dep_freq_5);

CREATE PERFETTO TABLE _stats_cpu6 AS
SELECT
  *
FROM _cpu_stats_subquery!(6, cpu6_curve, cpu6_static, freq_6, idle_6, default_dep_policy_6, default_dep_freq_6);

CREATE PERFETTO TABLE _stats_cpu7 AS
SELECT
  *
FROM _cpu_stats_subquery!(7, cpu7_curve, cpu7_static, freq_7, idle_7, default_dep_policy_7, default_dep_freq_7);

CREATE PERFETTO TABLE _stats_cpu0123 AS
SELECT
  ii.ts,
  ii.dur,
  id_0 AS cpu0_id,
  id_1 AS cpu1_id,
  id_2 AS cpu2_id,
  id_3 AS cpu3_id,
  id_4 AS dsu_id
FROM _interval_intersect!(
  (
    _ii_subquery!(_stats_cpu0),
    _ii_subquery!(_stats_cpu1),
    _ii_subquery!(_stats_cpu2),
    _ii_subquery!(_stats_cpu3),
    _ii_subquery!(_wattson_dsu_frequency)
  ),
  ()
) AS ii;

CREATE PERFETTO TABLE _stats_cpu01234567 AS
SELECT
  ii.ts,
  ii.dur,
  cpu0123.dsu_id,
  cpu0123.cpu0_id,
  cpu0123.cpu1_id,
  cpu0123.cpu2_id,
  cpu0123.cpu3_id,
  id_1 AS cpu4_id,
  id_2 AS cpu5_id,
  id_3 AS cpu6_id,
  id_4 AS cpu7_id
FROM _interval_intersect!(
  (
    _ii_subquery!(_stats_cpu0123),
    _ii_subquery!(_stats_cpu4),
    _ii_subquery!(_stats_cpu5),
    _ii_subquery!(_stats_cpu6),
    _ii_subquery!(_stats_cpu7)
  ),
  ()
) AS ii
JOIN _stats_cpu0123 AS cpu0123
  ON cpu0123._auto_id = id_0;

-- Combine system state so that it has idle, freq, and L3 hit info.
CREATE VIRTUAL TABLE _idle_freq_l3_hit_l3_miss_slice USING SPAN_OUTER_JOIN (_stats_cpu01234567, _arm_l3_rates);

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
  _stats_cpu0.cpu0_curve,
  _stats_cpu1.cpu1_curve,
  _stats_cpu2.cpu2_curve,
  _stats_cpu3.cpu3_curve,
  _stats_cpu4.cpu4_curve,
  _stats_cpu5.cpu5_curve,
  _stats_cpu6.cpu6_curve,
  _stats_cpu7.cpu7_curve,
  _stats_cpu0.default_dep_policy_0,
  _stats_cpu1.default_dep_policy_1,
  _stats_cpu2.default_dep_policy_2,
  _stats_cpu3.default_dep_policy_3,
  _stats_cpu4.default_dep_policy_4,
  _stats_cpu5.default_dep_policy_5,
  _stats_cpu6.default_dep_policy_6,
  _stats_cpu7.default_dep_policy_7,
  _stats_cpu0.default_dep_freq_0,
  _stats_cpu1.default_dep_freq_1,
  _stats_cpu2.default_dep_freq_2,
  _stats_cpu3.default_dep_freq_3,
  _stats_cpu4.default_dep_freq_4,
  _stats_cpu5.default_dep_freq_5,
  _stats_cpu6.default_dep_freq_6,
  _stats_cpu7.default_dep_freq_7,
  _wattson_dsu_frequency.dsu_freq,
  cpu0_static + cpu1_static + cpu2_static + cpu3_static + cpu4_static + cpu5_static + cpu6_static + cpu7_static AS static_1d,
  min(idle_0, idle_1, idle_2, idle_3, idle_4, idle_5, idle_6, idle_7) AS all_cpu_deep_idle,
  min(
    iif(0 IN _cpus_for_static, idle_0, 1),
    iif(1 IN _cpus_for_static, idle_1, 1),
    iif(2 IN _cpus_for_static, idle_2, 1),
    iif(3 IN _cpus_for_static, idle_3, 1),
    iif(4 IN _cpus_for_static, idle_4, 1),
    iif(5 IN _cpus_for_static, idle_5, 1),
    iif(6 IN _cpus_for_static, idle_6, 1),
    iif(7 IN _cpus_for_static, idle_7, 1)
  ) AS no_static
FROM _idle_freq_l3_hit_l3_miss_slice AS base
JOIN _wattson_dsu_frequency
  ON _wattson_dsu_frequency._auto_id = base.dsu_id
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
  ON _stats_cpu7._auto_id = base.cpu7_id;

-- Slices based table with all independent and dependent CPU data
CREATE PERFETTO TABLE _w_dependent_cpus_calc AS
WITH
  -- Only unpivot the necessary columns for dependency calculation.
  -- Additionally, only unpivot the necessary rows for dependency calculation
  -- based off of _cpu_lut_dependencies. The superset of the CROSS JOIN will be
  -- CPU (x0, y0), ..., (x0, yN), ..., (xN, yN). The _cpu_lut_dependencies will
  -- eliminate any possible CPU-pairing that are not possible dependencies.
  unpivoted_deps AS (
    SELECT
      i.ts,
      d.cpu,
      d.dep_cpu,
      CASE d.dep_cpu
        WHEN 0
        THEN i.cpu0_curve
        WHEN 1
        THEN i.cpu1_curve
        WHEN 2
        THEN i.cpu2_curve
        WHEN 3
        THEN i.cpu3_curve
        WHEN 4
        THEN i.cpu4_curve
        WHEN 5
        THEN i.cpu5_curve
        WHEN 6
        THEN i.cpu6_curve
        WHEN 7
        THEN i.cpu7_curve
      END AS curve,
      CASE d.dep_cpu
        WHEN 0
        THEN i.freq_0
        WHEN 1
        THEN i.freq_1
        WHEN 2
        THEN i.freq_2
        WHEN 3
        THEN i.freq_3
        WHEN 4
        THEN i.freq_4
        WHEN 5
        THEN i.freq_5
        WHEN 6
        THEN i.freq_6
        WHEN 7
        THEN i.freq_7
      END AS freq,
      CASE d.dep_cpu
        WHEN 0
        THEN i.idle_0
        WHEN 1
        THEN i.idle_1
        WHEN 2
        THEN i.idle_2
        WHEN 3
        THEN i.idle_3
        WHEN 4
        THEN i.idle_4
        WHEN 5
        THEN i.idle_5
        WHEN 6
        THEN i.idle_6
        WHEN 7
        THEN i.idle_7
      END AS idle
    FROM _w_independent_cpus_calc AS i
    CROSS JOIN _cpu_lut_dependencies AS d
  ),
  -- For each CPU, find the dependent CPU with the highest "vote"
  ranked_voters AS (
    SELECT
      u.ts,
      u.cpu,
      u.dep_cpu,
      u.freq,
      -- Rank dependencies by curve value or frequency
      row_number() OVER (PARTITION BY u.ts, u.cpu ORDER BY CASE WHEN vote.vote_by_freq = 1 THEN u.freq ELSE NULL END DESC, CASE WHEN vote.vote_by_freq = 0 THEN u.curve ELSE NULL END DESC) AS rn
    FROM unpivoted_deps AS u
    JOIN _dev_vote_by_freq AS vote
      ON u.cpu = vote.cpu
    WHERE
      u.idle = -1
  ),
  max_voters AS (
    SELECT
      ts,
      cpu,
      dep_cpu,
      freq
    FROM ranked_voters
    -- Keep only the top-ranked dependency.
    WHERE
      rn = 1
  ),
  -- Pivot the results back into new columns.
  pivoted_results AS (
    SELECT
      m.ts,
      max(CASE WHEN m.cpu = 0 THEN m.freq END) AS dep_freq_0,
      max(CASE WHEN m.cpu = 0 THEN p.policy END) AS dep_policy_0,
      max(CASE WHEN m.cpu = 1 THEN m.freq END) AS dep_freq_1,
      max(CASE WHEN m.cpu = 1 THEN p.policy END) AS dep_policy_1,
      max(CASE WHEN m.cpu = 2 THEN m.freq END) AS dep_freq_2,
      max(CASE WHEN m.cpu = 2 THEN p.policy END) AS dep_policy_2,
      max(CASE WHEN m.cpu = 3 THEN m.freq END) AS dep_freq_3,
      max(CASE WHEN m.cpu = 3 THEN p.policy END) AS dep_policy_3,
      max(CASE WHEN m.cpu = 4 THEN m.freq END) AS dep_freq_4,
      max(CASE WHEN m.cpu = 4 THEN p.policy END) AS dep_policy_4,
      max(CASE WHEN m.cpu = 5 THEN m.freq END) AS dep_freq_5,
      max(CASE WHEN m.cpu = 5 THEN p.policy END) AS dep_policy_5,
      max(CASE WHEN m.cpu = 6 THEN m.freq END) AS dep_freq_6,
      max(CASE WHEN m.cpu = 6 THEN p.policy END) AS dep_policy_6,
      max(CASE WHEN m.cpu = 7 THEN m.freq END) AS dep_freq_7,
      max(CASE WHEN m.cpu = 7 THEN p.policy END) AS dep_policy_7
    FROM max_voters AS m
    JOIN _dev_cpu_policy_map AS p
      ON m.dep_cpu = p.cpu
    GROUP BY
      m.ts
  )
-- Join the calculated dependencies back to the original data.
SELECT
  base.ts,
  base.dur,
  base.freq_0,
  base.idle_0,
  base.freq_1,
  base.idle_1,
  base.freq_2,
  base.idle_2,
  base.freq_3,
  base.idle_3,
  base.freq_4,
  base.idle_4,
  base.freq_5,
  base.idle_5,
  base.freq_6,
  base.idle_6,
  base.freq_7,
  base.idle_7,
  base.cpu0_curve,
  base.cpu1_curve,
  base.cpu2_curve,
  base.cpu3_curve,
  base.cpu4_curve,
  base.cpu5_curve,
  base.cpu6_curve,
  base.cpu7_curve,
  iif(base.all_cpu_deep_idle = 1, 0, base.l3_hit_count) AS l3_hit_count,
  iif(base.all_cpu_deep_idle = 1, 0, base.l3_miss_count) AS l3_miss_count,
  base.no_static,
  base.static_1d,
  -- Use DSU frequency if required, else use the calculated dependency
  -- frequency, else use the fallback default frequency
  iif(0 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_0, default_dep_freq_0)) AS dep_freq_0,
  iif(0 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_0, default_dep_policy_0)) AS dep_policy_0,
  iif(1 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_1, default_dep_freq_1)) AS dep_freq_1,
  iif(1 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_1, default_dep_policy_1)) AS dep_policy_1,
  iif(2 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_2, default_dep_freq_2)) AS dep_freq_2,
  iif(2 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_2, default_dep_policy_2)) AS dep_policy_2,
  iif(3 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_3, default_dep_freq_3)) AS dep_freq_3,
  iif(3 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_3, default_dep_policy_3)) AS dep_policy_3,
  iif(4 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_4, default_dep_freq_4)) AS dep_freq_4,
  iif(4 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_4, default_dep_policy_4)) AS dep_policy_4,
  iif(5 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_5, default_dep_freq_5)) AS dep_freq_5,
  iif(5 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_5, default_dep_policy_5)) AS dep_policy_5,
  iif(6 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_6, default_dep_freq_6)) AS dep_freq_6,
  iif(6 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_6, default_dep_policy_6)) AS dep_policy_6,
  iif(7 IN _cpu_w_dsu_dependency, dsu_freq, coalesce(dep_freq_7, default_dep_freq_7)) AS dep_freq_7,
  iif(7 IN _cpu_w_dsu_dependency, 255, coalesce(dep_policy_7, default_dep_policy_7)) AS dep_policy_7
FROM _w_independent_cpus_calc AS base
LEFT JOIN pivoted_results AS pivoted
  USING (ts);
