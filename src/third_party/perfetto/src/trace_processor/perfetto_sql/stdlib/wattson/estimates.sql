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

INCLUDE PERFETTO MODULE wattson.utils;

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE wattson.cpu.estimates;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE wattson.gpu.estimates;

INCLUDE PERFETTO MODULE wattson.tpu.estimates;

-- Need to use SPAN_OUTER_JOIN because depending on the trace points enabled,
-- it's possible one of the tables is empty
CREATE VIRTUAL TABLE _cpu_gpu_system_state_mw USING SPAN_OUTER_JOIN(_cpu_estimates_mw, _gpu_estimates_mw);

CREATE VIRTUAL TABLE _cpu_gpu_tpu_system_state_mw USING SPAN_OUTER_JOIN(_cpu_gpu_system_state_mw, _tpu_estimates_mw);

-- The most basic components of Wattson, all normalized to be in mW on a per
-- system state basis
CREATE PERFETTO TABLE _system_state_mw AS
SELECT * FROM _cpu_gpu_tpu_system_state_mw;

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
RETURNS TableOrSubquery
AS (
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
    cast_double!(sum(ii.dur * ss.cpu0_mw) / nullif(sum(ii.dur), 0)) AS cpu0_mw,
    cast_double!(sum(ii.dur * ss.cpu1_mw) / nullif(sum(ii.dur), 0)) AS cpu1_mw,
    cast_double!(sum(ii.dur * ss.cpu2_mw) / nullif(sum(ii.dur), 0)) AS cpu2_mw,
    cast_double!(sum(ii.dur * ss.cpu3_mw) / nullif(sum(ii.dur), 0)) AS cpu3_mw,
    cast_double!(sum(ii.dur * ss.cpu4_mw) / nullif(sum(ii.dur), 0)) AS cpu4_mw,
    cast_double!(sum(ii.dur * ss.cpu5_mw) / nullif(sum(ii.dur), 0)) AS cpu5_mw,
    cast_double!(sum(ii.dur * ss.cpu6_mw) / nullif(sum(ii.dur), 0)) AS cpu6_mw,
    cast_double!(sum(ii.dur * ss.cpu7_mw) / nullif(sum(ii.dur), 0)) AS cpu7_mw,
    cast_double!(sum(ii.dur * ss.dsu_scu_mw) / nullif(sum(ii.dur), 0)) AS dsu_scu_mw,
    cast_double!(sum(ii.dur * ss.gpu_mw) / nullif(sum(ii.dur), 0)) AS gpu_mw,
    cast_double!(sum(ii.dur * ss.tpu_mw) / nullif(sum(ii.dur), 0)) AS tpu_mw,
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
