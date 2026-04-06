
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

-- This file established the tables that define the relationships between rails
-- and subrails as well as the hierarchical power estimates of each rail

INCLUDE PERFETTO MODULE wattson.estimates;
INCLUDE PERFETTO MODULE wattson.utils;

-- The most basic rail components that form the "building blocks" from which all
-- other rails and components are derived. Average power over the entire trace
-- for each of these rail components.
DROP VIEW IF EXISTS _wattson_base_components_avg_mw;
CREATE PERFETTO VIEW _wattson_base_components_avg_mw AS
SELECT
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 0) as cpu0_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 1) as cpu1_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 2) as cpu2_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 3) as cpu3_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 4) as cpu4_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 5) as cpu5_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 6) as cpu6_poli,
  (SELECT m.policy FROM _dev_cpu_policy_map AS m WHERE m.cpu = 7) as cpu7_poli,
  -- Converts all mW of all slices into average mW of total trace
  SUM(ii.dur * ss.cpu0_mw) / SUM(ii.dur) as cpu0_mw,
  SUM(ii.dur * ss.cpu1_mw) / SUM(ii.dur) as cpu1_mw,
  SUM(ii.dur * ss.cpu2_mw) / SUM(ii.dur) as cpu2_mw,
  SUM(ii.dur * ss.cpu3_mw) / SUM(ii.dur) as cpu3_mw,
  SUM(ii.dur * ss.cpu4_mw) / SUM(ii.dur) as cpu4_mw,
  SUM(ii.dur * ss.cpu5_mw) / SUM(ii.dur) as cpu5_mw,
  SUM(ii.dur * ss.cpu6_mw) / SUM(ii.dur) as cpu6_mw,
  SUM(ii.dur * ss.cpu7_mw) / SUM(ii.dur) as cpu7_mw,
  SUM(ii.dur * ss.dsu_scu_mw) / SUM(ii.dur) as dsu_scu_mw,
  SUM(ii.dur * ss.gpu_mw) / SUM(ii.dur) as gpu_mw,
  SUM(ii.dur) as period_dur,
  ii.id_0 as period_id
FROM _interval_intersect!(
  (
    (SELECT period_id AS id, * FROM {{window_table}}),
    _ii_subquery!(_system_state_mw)
  ),
  ()
) ii
JOIN _system_state_mw AS ss ON ss._auto_id = id_1
GROUP BY period_id;

-- Macro that filters out CPUs that are unrelated to the policy of the table
-- passed in, and does some bookkeeping to put data in expected format
CREATE OR REPLACE PERFETTO MACRO
_get_valid_cpu_mw(policy_tbl_w_cpus TableOrSubQuery)
RETURNS TableOrSubquery AS
(
  WITH input_table_w_filter AS (
    SELECT
      *,
      COALESCE(
        cpu0_mw, cpu1_mw, cpu2_mw, cpu3_mw, cpu4_mw, cpu5_mw, cpu6_mw, cpu7_mw
      ) IS NOT NULL as is_defined,
      (
        IFNULL(cpu0_mw, 0) + IFNULL(cpu1_mw, 0) + IFNULL(cpu2_mw, 0)
         + IFNULL(cpu3_mw, 0) + IFNULL(cpu4_mw, 0) + IFNULL(cpu5_mw, 0)
         + IFNULL(cpu6_mw, 0) + IFNULL(cpu7_mw, 0)
      ) as sum_mw
    FROM $policy_tbl_w_cpus
  )
  SELECT
    is_defined,
    period_id,
    period_dur,
    cast_double!(IIF(is_defined, sum_mw, NULL)) as estimated_mw,
    cast_double!(
      IIF(is_defined, sum_mw * period_dur / 1e9, NULL)
    ) as estimated_mws,
    AndroidWattsonPolicyEstimate(
      'estimated_mw', cast_double!(IIF(is_defined, sum_mw, NULL)),
      'estimated_mws', cast_double!(
        IIF(is_defined, sum_mw * period_dur / 1e9, NULL)
      ),
      'cpu0', IIF(
        cpu0_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu0_mw,
          'estimated_mws', cpu0_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu1', IIF(
        cpu1_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu1_mw,
          'estimated_mws', cpu1_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu2', IIF(
        cpu2_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu2_mw,
          'estimated_mws', cpu2_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu3', IIF(
        cpu3_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu3_mw,
          'estimated_mws', cpu3_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu4', IIF(
        cpu4_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu4_mw,
          'estimated_mws', cpu4_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu5', IIF(
        cpu5_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu5_mw,
          'estimated_mws', cpu5_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu6', IIF(
        cpu6_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu6_mw,
          'estimated_mws', cpu6_mw * period_dur / 1e9
        ),
        NULL
      ),
      'cpu7', IIF(
        cpu7_mw,
        AndroidWattsonCpuEstimate(
          'estimated_mw', cpu7_mw,
          'estimated_mws', cpu7_mw * period_dur / 1e9
        ),
        NULL
      )
    ) AS proto
  FROM input_table_w_filter
);

-- Automatically determines CPUs that correspond to policyX, and picks up NULL
-- otherwise.
DROP VIEW IF EXISTS _estimate_policy0_proto;
CREATE PERFETTO VIEW _estimate_policy0_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 0, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 0, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 0, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 0, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 0, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 0, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 0, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 0, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy1_proto;
CREATE PERFETTO VIEW _estimate_policy1_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 1, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 1, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 1, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 1, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 1, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 1, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 1, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 1, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy2_proto;
CREATE PERFETTO VIEW _estimate_policy2_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 2, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 2, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 2, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 2, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 2, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 2, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 2, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 2, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy3_proto;
CREATE PERFETTO VIEW _estimate_policy3_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 3, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 3, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 3, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 3, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 3, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 3, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 3, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 3, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy4_proto;
CREATE PERFETTO VIEW _estimate_policy4_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 4, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 4, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 4, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 4, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 4, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 4, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 4, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 4, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy5_proto;
CREATE PERFETTO VIEW _estimate_policy5_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 5, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 5, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 5, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 5, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 5, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 5, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 5, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 5, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy6_proto;
CREATE PERFETTO VIEW _estimate_policy6_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 6, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 6, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 6, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 6, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 6, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 6, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 6, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 6, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_policy7_proto;
CREATE PERFETTO VIEW _estimate_policy7_proto AS
SELECT * FROM _get_valid_cpu_mw!(
  (
    SELECT
      period_id,
      period_dur,
      IIF(cpu0_poli = 7, cpu0_mw, NULL) as cpu0_mw,
      IIF(cpu1_poli = 7, cpu1_mw, NULL) as cpu1_mw,
      IIF(cpu2_poli = 7, cpu2_mw, NULL) as cpu2_mw,
      IIF(cpu3_poli = 7, cpu3_mw, NULL) as cpu3_mw,
      IIF(cpu4_poli = 7, cpu4_mw, NULL) as cpu4_mw,
      IIF(cpu5_poli = 7, cpu5_mw, NULL) as cpu5_mw,
      IIF(cpu6_poli = 7, cpu6_mw, NULL) as cpu6_mw,
      IIF(cpu7_poli = 7, cpu7_mw, NULL) as cpu7_mw
    FROM _wattson_base_components_avg_mw
    GROUP BY period_id, period_dur
  )
);

DROP VIEW IF EXISTS _estimate_dsu_scu;
CREATE PERFETTO VIEW _estimate_dsu_scu AS
SELECT
  period_id,
  period_dur,
  dsu_scu_mw
FROM _wattson_base_components_avg_mw
GROUP BY period_id, period_dur;

-- Automatically populates the appropriate policy based on the device of the
-- trace. For policies that do not exist on the device, a NULL proto/estimate is
-- populated.
DROP VIEW IF EXISTS _estimate_cpu_subsystem_sum;
CREATE PERFETTO VIEW _estimate_cpu_subsystem_sum AS
WITH components AS (
  SELECT
    period_id,
    period_dur,
    dsu_scu.dsu_scu_mw,
    IIF(p0.is_defined, p0.estimated_mw, NULL) as p0_mw,
    IIF(p0.is_defined, p0.proto, NULL) as p0_proto,
    IIF(p1.is_defined, p1.estimated_mw, NULL) as p1_mw,
    IIF(p1.is_defined, p1.proto, NULL) as p1_proto,
    IIF(p2.is_defined, p2.estimated_mw, NULL) as p2_mw,
    IIF(p2.is_defined, p2.proto, NULL) as p2_proto,
    IIF(p3.is_defined, p3.estimated_mw, NULL) as p3_mw,
    IIF(p3.is_defined, p3.proto, NULL) as p3_proto,
    IIF(p4.is_defined, p4.estimated_mw, NULL) as p4_mw,
    IIF(p4.is_defined, p4.proto, NULL) as p4_proto,
    IIF(p5.is_defined, p5.estimated_mw, NULL) as p5_mw,
    IIF(p5.is_defined, p5.proto, NULL) as p5_proto,
    IIF(p6.is_defined, p6.estimated_mw, NULL) as p6_mw,
    IIF(p6.is_defined, p6.proto, NULL) as p6_proto,
    IIF(p7.is_defined, p7.estimated_mw, NULL) as p7_mw,
    IIF(p7.is_defined, p7.proto, NULL) as p7_proto
  FROM _estimate_policy0_proto AS p0
  JOIN _estimate_policy1_proto AS p1 USING (period_id, period_dur)
  JOIN _estimate_policy2_proto AS p2 USING (period_id, period_dur)
  JOIN _estimate_policy3_proto AS p3 USING (period_id, period_dur)
  JOIN _estimate_policy4_proto AS p4 USING (period_id, period_dur)
  JOIN _estimate_policy5_proto AS p5 USING (period_id, period_dur)
  JOIN _estimate_policy6_proto AS p6 USING (period_id, period_dur)
  JOIN _estimate_policy7_proto AS p7 USING (period_id, period_dur)
  JOIN _estimate_dsu_scu AS dsu_scu USING (period_id, period_dur)
),
components_w_sum AS (
  SELECT
    *,
    (
      IFNULL(p0_mw, 0) + IFNULL(p1_mw, 0) + IFNULL(p2_mw, 0) + IFNULL(p3_mw, 0)
      + IFNULL(p4_mw, 0) + IFNULL(p5_mw, 0) + IFNULL(p6_mw, 0)
      + IFNULL(p7_mw, 0) + dsu_scu_mw
    ) as sum_mw
  FROM components
)
SELECT
  period_id,
  period_dur,
  AndroidWattsonCpuSubsystemEstimate(
    'estimated_mw', sum_mw,
    'estimated_mws', sum_mw * period_dur / 1e9,
    'policy0', p0_proto,
    'policy1', p1_proto,
    'policy2', p2_proto,
    'policy3', p3_proto,
    'policy4', p4_proto,
    'policy5', p5_proto,
    'policy6', p6_proto,
    'policy7', p7_proto,
    'dsu_scu', AndroidWattsonDsuScuEstimate(
      'estimated_mw', dsu_scu_mw,
      'estimated_mws', dsu_scu_mw * period_dur / 1e9
    )
  ) as cpu_proto
FROM components_w_sum;

DROP VIEW IF EXISTS _estimate_gpu_subsystem_sum;
CREATE PERFETTO VIEW _estimate_gpu_subsystem_sum AS
SELECT
  period_id,
  period_dur,
  gpu_mw IS NOT NULL as defined,
  AndroidWattsonGpuSubsystemEstimate(
    'estimated_mw', gpu_mw,
    'estimated_mws', gpu_mw * period_dur / 1e9
  ) as gpu_proto
FROM _wattson_base_components_avg_mw;

DROP VIEW IF EXISTS _estimate_subsystems_sum;
CREATE PERFETTO VIEW _estimate_subsystems_sum AS
SELECT
  period_id,
  period_dur,
  cpu_ss.cpu_proto,
  IIF(gpu_ss.defined, gpu_ss.gpu_proto, NULL) as gpu_proto
FROM _estimate_cpu_subsystem_sum cpu_ss
JOIN _estimate_gpu_subsystem_sum gpu_ss
  USING (period_id, period_dur);

DROP VIEW IF EXISTS _wattson_rails_metric_metadata;
CREATE PERFETTO VIEW _wattson_rails_metric_metadata AS
SELECT
  4 AS metric_version,
  1 AS power_model_version,
  NOT EXISTS (SELECT 1 FROM _wattson_cpuidle_counters_exist) AS is_crude_estimate;
