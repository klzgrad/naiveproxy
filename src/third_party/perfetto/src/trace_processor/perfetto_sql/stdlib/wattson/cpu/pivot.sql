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
    idle_col ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT
    t1.ts,
    t1.dur,
    t1.curve_value AS $curve_col,
    iif($cpu IN _device_policies, coalesce(t1.static, 0), 0) AS $static_col,
    coalesce(t1.freq, 0) AS $freq_col,
    coalesce(t1.idle, deepest.idle) AS $idle_col
  FROM _idle_freq_materialized AS t1
  CROSS JOIN _deepest_idle AS deepest
  WHERE
    cpu = $cpu
  UNION ALL
  SELECT
    trace_start(),
    trace_dur(),
    0,
    0,
    0,
    idle
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
FROM _cpu_stats_subquery!(0, cpu0_curve, cpu0_static, freq_0, idle_0);

CREATE PERFETTO TABLE _stats_cpu1 AS
SELECT
  *
FROM _cpu_stats_subquery!(1, cpu1_curve, cpu1_static, freq_1, idle_1);

CREATE PERFETTO TABLE _stats_cpu2 AS
SELECT
  *
FROM _cpu_stats_subquery!(2, cpu2_curve, cpu2_static, freq_2, idle_2);

CREATE PERFETTO TABLE _stats_cpu3 AS
SELECT
  *
FROM _cpu_stats_subquery!(3, cpu3_curve, cpu3_static, freq_3, idle_3);

CREATE PERFETTO TABLE _stats_cpu4 AS
SELECT
  *
FROM _cpu_stats_subquery!(4, cpu4_curve, cpu4_static, freq_4, idle_4);

CREATE PERFETTO TABLE _stats_cpu5 AS
SELECT
  *
FROM _cpu_stats_subquery!(5, cpu5_curve, cpu5_static, freq_5, idle_5);

CREATE PERFETTO TABLE _stats_cpu6 AS
SELECT
  *
FROM _cpu_stats_subquery!(6, cpu6_curve, cpu6_static, freq_6, idle_6);

CREATE PERFETTO TABLE _stats_cpu7 AS
SELECT
  *
FROM _cpu_stats_subquery!(7, cpu7_curve, cpu7_static, freq_7, idle_7);

-- Does calculations for CPUs that are independent of other CPUs or frequencies
-- This is the last generic table before going to device specific table calcs
CREATE PERFETTO TABLE _w_independent_cpus_calc AS
SELECT
  base.ts,
  base.dur,
  cast_int!(l3_hit_rate * base.dur) AS l3_hit_count,
  cast_int!(l3_miss_rate * base.dur) AS l3_miss_count,
  hash(
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
    dsu_freq
  ) AS config_hash,
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
  _wattson_dsu_frequency.dsu_freq,
  cpu0_static + cpu1_static + cpu2_static + cpu3_static + cpu4_static + cpu5_static + cpu6_static + cpu7_static AS static_1d
FROM _interval_intersect!(
  (
    _ii_subquery!(_stats_cpu0),
    _ii_subquery!(_stats_cpu1),
    _ii_subquery!(_stats_cpu2),
    _ii_subquery!(_stats_cpu3),
    _ii_subquery!(_stats_cpu4),
    _ii_subquery!(_stats_cpu5),
    _ii_subquery!(_stats_cpu6),
    _ii_subquery!(_stats_cpu7),
    _ii_subquery!(_wattson_dsu_frequency),
    _ii_subquery!(_arm_l3_rates)
  ),
  ()
) AS base
JOIN _stats_cpu0
  ON _stats_cpu0._auto_id = base.id_0
JOIN _stats_cpu1
  ON _stats_cpu1._auto_id = base.id_1
JOIN _stats_cpu2
  ON _stats_cpu2._auto_id = base.id_2
JOIN _stats_cpu3
  ON _stats_cpu3._auto_id = base.id_3
JOIN _stats_cpu4
  ON _stats_cpu4._auto_id = base.id_4
JOIN _stats_cpu5
  ON _stats_cpu5._auto_id = base.id_5
JOIN _stats_cpu6
  ON _stats_cpu6._auto_id = base.id_6
JOIN _stats_cpu7
  ON _stats_cpu7._auto_id = base.id_7
JOIN _wattson_dsu_frequency
  ON _wattson_dsu_frequency._auto_id = base.id_8
JOIN _arm_l3_rates
  ON _arm_l3_rates._auto_id = base.id_9;

-- Slices view with all UNIQUE configs of independent and dependent CPU data
CREATE PERFETTO VIEW _w_dependent_cpus_unique AS
WITH
  -- Gets DSU dependent CPU upfront as a single row, which means this can be
  -- efficiently CROSS JOIN-ed later
  dsu_flags AS (
    SELECT
      max(cpu = 0) AS dsu_0,
      max(cpu = 1) AS dsu_1,
      max(cpu = 2) AS dsu_2,
      max(cpu = 3) AS dsu_3,
      max(cpu = 4) AS dsu_4,
      max(cpu = 5) AS dsu_5,
      max(cpu = 6) AS dsu_6,
      max(cpu = 7) AS dsu_7
    FROM _cpu_w_dsu_dependency
  ),
  _static_checks AS (
    SELECT
      0 IN _cpus_for_static AS c0,
      1 IN _cpus_for_static AS c1,
      2 IN _cpus_for_static AS c2,
      3 IN _cpus_for_static AS c3,
      4 IN _cpus_for_static AS c4,
      5 IN _cpus_for_static AS c5,
      6 IN _cpus_for_static AS c6,
      7 IN _cpus_for_static AS c7
  ),
  _w_unique_configs AS (
    SELECT
      config_hash,
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
      cpu0_curve,
      cpu1_curve,
      cpu2_curve,
      cpu3_curve,
      cpu4_curve,
      cpu5_curve,
      cpu6_curve,
      cpu7_curve,
      dsu_freq,
      static_1d,
      min(idle_0, idle_1, idle_2, idle_3, idle_4, idle_5, idle_6, idle_7) AS all_cpu_deep_idle,
      min(
        iif(sc.c0, idle_0, 1),
        iif(sc.c1, idle_1, 1),
        iif(sc.c2, idle_2, 1),
        iif(sc.c3, idle_3, 1),
        iif(sc.c4, idle_4, 1),
        iif(sc.c5, idle_5, 1),
        iif(sc.c6, idle_6, 1),
        iif(sc.c7, idle_7, 1)
      ) AS no_static
    FROM _w_independent_cpus_calc
    CROSS JOIN _static_checks AS sc
    GROUP BY
      config_hash
  ),
  -- Only unpivot the necessary columns for dependency calculation.
  -- Additionally, only unpivot the necessary rows for dependency calculation
  -- based off of _cpu_lut_dependencies. The superset of the CROSS JOIN will be
  -- CPU (x0, y0), ..., (x0, yN), ..., (xN, yN). The _cpu_lut_dependencies will
  -- eliminate any possible CPU-pairing that are not possible dependencies.
  unpivoted_deps AS (
    SELECT
      i.config_hash,
      d.cpu,
      -- Determine the scoring value (Frequency or Curve) based on device
      CASE v.vote_by_freq
        WHEN 1
        THEN CASE d.dep_cpu
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
        END
        ELSE CASE d.dep_cpu
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
        END
      END AS vote_score,
      -- Calculate the Actual Frequency (to be used in the result)
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
      p.policy
    FROM _w_unique_configs AS i
    CROSS JOIN _cpu_lut_dependencies AS d
    JOIN _dev_vote_by_freq AS v
      ON d.cpu = v.cpu
    JOIN _dev_cpu_policy_map AS p
      ON d.dep_cpu = p.cpu
    WHERE
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
      END = -1
  ),
  max_voters AS (
    SELECT
      config_hash,
      cpu,
      freq,
      policy,
      max(vote_score)
    FROM unpivoted_deps
    GROUP BY
      config_hash,
      cpu
  ),
  -- Pivot the results back into new columns.
  pivoted_results AS (
    SELECT
      config_hash,
      max(CASE WHEN cpu = 0 THEN freq END) AS dep_freq_0,
      max(CASE WHEN cpu = 0 THEN policy END) AS dep_policy_0,
      max(CASE WHEN cpu = 1 THEN freq END) AS dep_freq_1,
      max(CASE WHEN cpu = 1 THEN policy END) AS dep_policy_1,
      max(CASE WHEN cpu = 2 THEN freq END) AS dep_freq_2,
      max(CASE WHEN cpu = 2 THEN policy END) AS dep_policy_2,
      max(CASE WHEN cpu = 3 THEN freq END) AS dep_freq_3,
      max(CASE WHEN cpu = 3 THEN policy END) AS dep_policy_3,
      max(CASE WHEN cpu = 4 THEN freq END) AS dep_freq_4,
      max(CASE WHEN cpu = 4 THEN policy END) AS dep_policy_4,
      max(CASE WHEN cpu = 5 THEN freq END) AS dep_freq_5,
      max(CASE WHEN cpu = 5 THEN policy END) AS dep_policy_5,
      max(CASE WHEN cpu = 6 THEN freq END) AS dep_freq_6,
      max(CASE WHEN cpu = 6 THEN policy END) AS dep_policy_6,
      max(CASE WHEN cpu = 7 THEN freq END) AS dep_freq_7,
      max(CASE WHEN cpu = 7 THEN policy END) AS dep_policy_7
    FROM max_voters
    GROUP BY
      config_hash
  ),
  default_votes AS (
    SELECT
      max(iif(cpu = 0, dep_policy, NULL)) AS default_dep_policy_0,
      max(iif(cpu = 0, dep_freq, NULL)) AS default_dep_freq_0,
      max(iif(cpu = 1, dep_policy, NULL)) AS default_dep_policy_1,
      max(iif(cpu = 1, dep_freq, NULL)) AS default_dep_freq_1,
      max(iif(cpu = 2, dep_policy, NULL)) AS default_dep_policy_2,
      max(iif(cpu = 2, dep_freq, NULL)) AS default_dep_freq_2,
      max(iif(cpu = 3, dep_policy, NULL)) AS default_dep_policy_3,
      max(iif(cpu = 3, dep_freq, NULL)) AS default_dep_freq_3,
      max(iif(cpu = 4, dep_policy, NULL)) AS default_dep_policy_4,
      max(iif(cpu = 4, dep_freq, NULL)) AS default_dep_freq_4,
      max(iif(cpu = 5, dep_policy, NULL)) AS default_dep_policy_5,
      max(iif(cpu = 5, dep_freq, NULL)) AS default_dep_freq_5,
      max(iif(cpu = 6, dep_policy, NULL)) AS default_dep_policy_6,
      max(iif(cpu = 6, dep_freq, NULL)) AS default_dep_freq_6,
      max(iif(cpu = 7, dep_policy, NULL)) AS default_dep_policy_7,
      max(iif(cpu = 7, dep_freq, NULL)) AS default_dep_freq_7
    FROM _cpu_w_dependency_default_vote
  )
-- Join the calculated dependencies back to the original data.
SELECT
  base.*,
  iif(dsu.dsu_0, dsu_freq, coalesce(dep_freq_0, defaults.default_dep_freq_0)) AS dep_freq_0,
  iif(dsu.dsu_0, 255, coalesce(dep_policy_0, defaults.default_dep_policy_0)) AS dep_policy_0,
  iif(dsu.dsu_1, dsu_freq, coalesce(dep_freq_1, defaults.default_dep_freq_1)) AS dep_freq_1,
  iif(dsu.dsu_1, 255, coalesce(dep_policy_1, defaults.default_dep_policy_1)) AS dep_policy_1,
  iif(dsu.dsu_2, dsu_freq, coalesce(dep_freq_2, defaults.default_dep_freq_2)) AS dep_freq_2,
  iif(dsu.dsu_2, 255, coalesce(dep_policy_2, defaults.default_dep_policy_2)) AS dep_policy_2,
  iif(dsu.dsu_3, dsu_freq, coalesce(dep_freq_3, defaults.default_dep_freq_3)) AS dep_freq_3,
  iif(dsu.dsu_3, 255, coalesce(dep_policy_3, defaults.default_dep_policy_3)) AS dep_policy_3,
  iif(dsu.dsu_4, dsu_freq, coalesce(dep_freq_4, defaults.default_dep_freq_4)) AS dep_freq_4,
  iif(dsu.dsu_4, 255, coalesce(dep_policy_4, defaults.default_dep_policy_4)) AS dep_policy_4,
  iif(dsu.dsu_5, dsu_freq, coalesce(dep_freq_5, defaults.default_dep_freq_5)) AS dep_freq_5,
  iif(dsu.dsu_5, 255, coalesce(dep_policy_5, defaults.default_dep_policy_5)) AS dep_policy_5,
  iif(dsu.dsu_6, dsu_freq, coalesce(dep_freq_6, defaults.default_dep_freq_6)) AS dep_freq_6,
  iif(dsu.dsu_6, 255, coalesce(dep_policy_6, defaults.default_dep_policy_6)) AS dep_policy_6,
  iif(dsu.dsu_7, dsu_freq, coalesce(dep_freq_7, defaults.default_dep_freq_7)) AS dep_freq_7,
  iif(dsu.dsu_7, 255, coalesce(dep_policy_7, defaults.default_dep_policy_7)) AS dep_policy_7
FROM _w_unique_configs AS base
CROSS JOIN dsu_flags AS dsu
CROSS JOIN default_votes AS defaults
LEFT JOIN pivoted_results AS pivoted
  USING (config_hash);
