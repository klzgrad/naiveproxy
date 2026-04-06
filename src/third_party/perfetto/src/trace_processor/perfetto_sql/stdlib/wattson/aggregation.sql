--
-- Copyright 2026 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.cpu.idle;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.estimates;

INCLUDE PERFETTO MODULE wattson.tasks.attribution;

INCLUDE PERFETTO MODULE wattson.tasks.idle_transitions_attribution;

INCLUDE PERFETTO MODULE wattson.utils;

-- ========================================================
-- MACRO: wattson_threads_aggregation
--
-- Calculates energy and power attribution per thread/process for the
-- given time windows.
--
-- Input:
--   window_table: A table with columns (ts, dur, period_id).
--
-- Output:
--   Flat table with columns:
--     period_id, period_dur, utid, tid, pid,
--     thread_name, process_name,
--     estimated_mws, estimated_mw, idle_transitions_mws, total_mws
-- ========================================================
CREATE PERFETTO MACRO wattson_threads_aggregation(
    -- Intereseted window table with columns:
    -- (ts, dur, period_id).
    window_table TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  -- Active Power Intersection (Intersection of Task Power & Window)
  WITH
    windowed_active_state AS (
      SELECT
        ii.dur,
        ii.id_1 AS period_id,
        tasks.estimated_mw,
        tasks.thread_name,
        tasks.process_name,
        tasks.tid,
        tasks.pid,
        tasks.utid
      FROM _interval_intersect!(
      (
        _ii_subquery!(_estimates_w_tasks_attribution),
        (SELECT period_id AS id, * FROM $window_table)
      ),
      ()
    ) AS ii
      JOIN _estimates_w_tasks_attribution AS tasks
        ON tasks._auto_id = id_0
    ),
    -- Aggregate Active Power per Thread per Period
    active_summary AS (
      SELECT
        period_id,
        utid,
        -- Metadata (take min/max as they are constant per utid)
        min(thread_name) AS thread_name,
        min(process_name) AS process_name,
        min(tid) AS tid,
        min(pid) AS pid,
        -- Calculations
        sum(estimated_mw * dur) / 1e9 AS active_mws,
        -- Keep for power calc
        sum(estimated_mw * dur) AS total_mw_ns
      FROM windowed_active_state
      GROUP BY
        period_id,
        utid
    ),
    -- Calculate Idle Cost (Join against the specific window constraints)
    idle_summary AS (
      SELECT
        w.period_id,
        cost.utid,
        sum(cost.idle_cost_mws) AS idle_mws
      FROM $window_table AS w, _filter_idle_attribution(w.ts, w.dur) AS cost
      GROUP BY
        w.period_id,
        cost.utid
    )
  -- Final Join & Calculation
  SELECT
    a.period_id,
    w.dur AS period_dur,
    a.utid,
    a.tid,
    a.pid,
    coalesce(a.thread_name, 'Thread ' || a.tid) AS thread_name,
    coalesce(a.process_name, '') AS process_name,
    -- Metrics
    a.active_mws AS estimated_mws,
    -- Power = Energy / Window Duration
    (
      a.total_mw_ns / w.dur
    ) AS estimated_mw,
    coalesce(i.idle_mws, 0) AS idle_transitions_mws,
    (
      a.active_mws + coalesce(i.idle_mws, 0)
    ) AS total_mws
  FROM active_summary AS a
  JOIN $window_table AS w
    ON a.period_id = w.period_id
  LEFT JOIN idle_summary AS i
    ON a.period_id = i.period_id AND a.utid = i.utid
);

-- ========================================================
-- MACRO: _wattson_base_components_avg_mw
--
-- Low-level macro to calculate base power components average mW.
--
-- Input:
--   window_table: A table with columns (ts, dur, period_id).
--
-- Output:
--   Wide table with CPU policy, average power per core, DSU, and GPU.
-- ========================================================
CREATE PERFETTO MACRO _wattson_base_components_avg_mw(
    window_table TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 0
    ) AS cpu0_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 1
    ) AS cpu1_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 2
    ) AS cpu2_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 3
    ) AS cpu3_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 4
    ) AS cpu4_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 5
    ) AS cpu5_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 6
    ) AS cpu6_poli,
    (
      SELECT
        m.policy
      FROM _dev_cpu_policy_map AS m
      WHERE
        m.cpu = 7
    ) AS cpu7_poli,
    sum(ii.dur * ss.cpu0_mw) / nullif(sum(ii.dur), 0) AS cpu0_mw,
    sum(ii.dur * ss.cpu1_mw) / nullif(sum(ii.dur), 0) AS cpu1_mw,
    sum(ii.dur * ss.cpu2_mw) / nullif(sum(ii.dur), 0) AS cpu2_mw,
    sum(ii.dur * ss.cpu3_mw) / nullif(sum(ii.dur), 0) AS cpu3_mw,
    sum(ii.dur * ss.cpu4_mw) / nullif(sum(ii.dur), 0) AS cpu4_mw,
    sum(ii.dur * ss.cpu5_mw) / nullif(sum(ii.dur), 0) AS cpu5_mw,
    sum(ii.dur * ss.cpu6_mw) / nullif(sum(ii.dur), 0) AS cpu6_mw,
    sum(ii.dur * ss.cpu7_mw) / nullif(sum(ii.dur), 0) AS cpu7_mw,
    sum(ii.dur * ss.dsu_scu_mw) / nullif(sum(ii.dur), 0) AS dsu_scu_mw,
    sum(ii.dur * ss.gpu_mw) / nullif(sum(ii.dur), 0) AS gpu_mw,
    sum(ii.dur) AS period_dur,
    ii.id_0 AS period_id
  FROM _interval_intersect!(
    (
      (SELECT period_id AS id, * FROM $window_table),
      _ii_subquery!(_system_state_mw)
    ),
    ()
  ) AS ii
  JOIN _system_state_mw AS ss
    ON ss._auto_id = id_1
  GROUP BY
    period_id
);

-- ========================================================
-- MACRO: wattson_rails_aggregation
--
-- Flattening and unpivoting of rail data into a standard breakdown.
--
-- Input:
--   window_table: A table with columns (ts, dur, period_id).
--
-- Output:
--   Flat breakdown including CORE, POLICY, DSU and SUBSYSTEM TOTAL.
-- ========================================================
CREATE PERFETTO MACRO wattson_rails_aggregation(
    -- Intereseted window table with columns:
    -- (ts, dur, period_id).
    window_table TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  -- 1. Cache base components
  WITH
    base_components AS (
      SELECT
        *
      FROM _wattson_base_components_avg_mw!($window_table)
    ),
    -- 2. Unpivot CPU columns
    cpu_unpivoted AS (
      SELECT
        period_id,
        period_dur,
        0 AS cpu_id,
        cpu0_poli AS policy_id,
        cpu0_mw AS mw
      FROM base_components
      WHERE
        cpu0_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        1,
        cpu1_poli,
        cpu1_mw
      FROM base_components
      WHERE
        cpu1_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        2,
        cpu2_poli,
        cpu2_mw
      FROM base_components
      WHERE
        cpu2_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        3,
        cpu3_poli,
        cpu3_mw
      FROM base_components
      WHERE
        cpu3_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        4,
        cpu4_poli,
        cpu4_mw
      FROM base_components
      WHERE
        cpu4_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        5,
        cpu5_poli,
        cpu5_mw
      FROM base_components
      WHERE
        cpu5_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        6,
        cpu6_poli,
        cpu6_mw
      FROM base_components
      WHERE
        cpu6_mw IS NOT NULL
      UNION ALL
      SELECT
        period_id,
        period_dur,
        7,
        cpu7_poli,
        cpu7_mw
      FROM base_components
      WHERE
        cpu7_mw IS NOT NULL
    ),
    -- 3. Build basic Flat View (encapsulate in CTE to reuse logic for Total calculation)
    flat_view_raw AS (
      -- A. CPU Cores
      SELECT
        c.period_id,
        c.period_dur,
        'CPU' AS subsystem,
        'CORE' AS breakdown_type,
        c.cpu_id AS component_id,
        c.policy_id AS parent_id,
        c.mw AS estimated_mw,
        (
          c.mw * c.period_dur / 1e9
        ) AS estimated_mws
      FROM cpu_unpivoted AS c
      UNION ALL
      -- B. CPU Policies
      SELECT
        c.period_id,
        c.period_dur,
        'CPU' AS subsystem,
        'POLICY' AS breakdown_type,
        c.policy_id AS component_id,
        NULL AS parent_id,
        sum(c.mw) AS estimated_mw,
        sum(c.mw * c.period_dur / 1e9) AS estimated_mws
      FROM cpu_unpivoted AS c
      GROUP BY
        c.period_id,
        c.period_dur,
        c.policy_id
      UNION ALL
      -- C. DSU/SCU
      SELECT
        base.period_id,
        base.period_dur,
        'CPU' AS subsystem,
        'DSU' AS breakdown_type,
        NULL AS component_id,
        NULL AS parent_id,
        base.dsu_scu_mw AS estimated_mw,
        (
          base.dsu_scu_mw * base.period_dur / 1e9
        ) AS estimated_mws
      FROM base_components AS base
      WHERE
        base.dsu_scu_mw IS NOT NULL
      UNION ALL
      -- D. GPU Subsystem
      SELECT
        base.period_id,
        base.period_dur,
        'GPU' AS subsystem,
        'TOTAL' AS breakdown_type,
        NULL AS component_id,
        NULL AS parent_id,
        base.gpu_mw AS estimated_mw,
        (
          base.gpu_mw * base.period_dur / 1e9
        ) AS estimated_mws
      FROM base_components AS base
      WHERE
        base.gpu_mw IS NOT NULL
    )
  -- 4. Final output: Raw Data + Computed CPU Total
  SELECT
    *
  FROM flat_view_raw
  UNION ALL
  -- E. CPU TOTAL (Auto-calculated)
  -- Sum only Policy and DSU (exclude Cores to avoid double counting)
  SELECT
    period_id,
    period_dur,
    subsystem,
    'TOTAL' AS breakdown_type,
    NULL AS component_id,
    NULL AS parent_id,
    sum(estimated_mw) AS estimated_mw,
    sum(estimated_mws) AS estimated_mws
  FROM flat_view_raw
  WHERE
    subsystem = 'CPU' AND breakdown_type IN ('POLICY', 'DSU')
  GROUP BY
    period_id,
    period_dur,
    subsystem
);

-- ========================================================
-- VIEW: _wattson_metric_metadata
--
-- Shared metadata for all Wattson metrics.
-- ========================================================
CREATE PERFETTO VIEW wattson_metric_metadata (
  -- Wattson metric version
  metric_version LONG,
  -- Wattson power curve version
  power_model_version LONG,
  -- Wattson estimation will be crude
  -- if missing cpu/idle counter
  is_crude_estimate BOOL
) AS
SELECT
  4 AS metric_version,
  1 AS power_model_version,
  CAST(NOT EXISTS(
    SELECT
      1
    FROM _wattson_cpuidle_counters_exist
  ) AS INTEGER) AS is_crude_estimate;
