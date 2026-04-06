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

INCLUDE PERFETTO MODULE wattson.tasks.task_slices;

INCLUDE PERFETTO MODULE wattson.ui.continuous_estimates;

INCLUDE PERFETTO MODULE wattson.utils;

INCLUDE PERFETTO MODULE wattson.device_infos;

INCLUDE PERFETTO MODULE intervals.intersect;

CREATE PERFETTO TABLE _unioned_wattson_estimates_mw AS
SELECT
  ts,
  dur,
  0 AS cpu,
  cpu0_mw AS estimated_mw
FROM _system_state_cpu0_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      0 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  1 AS cpu,
  cpu1_mw AS estimated_mw
FROM _system_state_cpu1_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      1 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  2 AS cpu,
  cpu2_mw AS estimated_mw
FROM _system_state_cpu2_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      2 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  3 AS cpu,
  cpu3_mw AS estimated_mw
FROM _system_state_cpu3_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      3 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  4 AS cpu,
  cpu4_mw AS estimated_mw
FROM _system_state_cpu4_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      4 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  5 AS cpu,
  cpu5_mw AS estimated_mw
FROM _system_state_cpu5_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      5 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  6 AS cpu,
  cpu6_mw AS estimated_mw
FROM _system_state_cpu6_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      6 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  7 AS cpu,
  cpu7_mw AS estimated_mw
FROM _system_state_cpu7_mw
WHERE
  EXISTS(
    SELECT
      cpu
    FROM _dev_cpu_policy_map
    WHERE
      7 = cpu
  )
UNION ALL
SELECT
  ts,
  dur,
  -1 AS cpu,
  dsu_scu_mw AS estimated_mw
FROM _system_state_dsu_scu_mw;

CREATE PERFETTO TABLE _estimates_w_tasks_attribution AS
SELECT
  ii.ts,
  ii.dur,
  ii.cpu,
  uw.estimated_mw,
  s.thread_name,
  s.process_name,
  s.package_name,
  s.tid,
  s.pid,
  s.uid,
  s.utid,
  s.upid
FROM _interval_intersect!(
  (
    _ii_subquery!(_unioned_wattson_estimates_mw),
    _ii_subquery!(_wattson_task_slices)
  ),
  (cpu)
) AS ii
JOIN _unioned_wattson_estimates_mw AS uw
  ON uw._auto_id = id_0
JOIN _wattson_task_slices AS s
  ON s._auto_id = id_1;
