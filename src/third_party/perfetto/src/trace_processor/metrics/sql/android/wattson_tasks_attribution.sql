
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

INCLUDE PERFETTO MODULE wattson.estimates;
INCLUDE PERFETTO MODULE wattson.tasks.attribution;
INCLUDE PERFETTO MODULE wattson.tasks.idle_transitions_attribution;
INCLUDE PERFETTO MODULE wattson.utils;

-- Take only the Wattson estimations that are in the window of interest
DROP VIEW IF EXISTS _windowed_threads_system_state;
CREATE PERFETTO VIEW _windowed_threads_system_state AS
SELECT
  ii.ts,
  ii.dur,
  ii.id_1 AS period_id,
  tasks.cpu,
  tasks.estimated_mw,
  tasks.thread_name,
  tasks.process_name,
  tasks.tid,
  tasks.pid,
  tasks.utid
FROM _interval_intersect!(
  (
    _ii_subquery!(_estimates_w_tasks_attribution),
    (SELECT ts, dur, period_id as id FROM {{window_table}})
  ),
  ()
) ii
JOIN _estimates_w_tasks_attribution AS tasks ON tasks._auto_id = id_0;

-- Get idle overhead attribution per thread
DROP VIEW IF EXISTS _per_thread_idle_attribution;
CREATE PERFETTO VIEW _per_thread_idle_attribution AS
SELECT
  SUM(cost.idle_cost_mws) as idle_cost_mws,
  cost.utid,
  period_window.period_id
FROM {{window_table}} AS period_window
CROSS JOIN _filter_idle_attribution(period_window.ts, period_window.dur) AS cost
GROUP BY utid, period_id;

-- Group by unique thread ID and disregard CPUs, summing of power over all CPUs
-- and all instances of the thread
DROP VIEW IF EXISTS _wattson_thread_attribution;
CREATE PERFETTO VIEW _wattson_thread_attribution AS
SELECT
  -- active time of thread divided by total time where Wattson is defined
  SUM(estimated_mw * dur) / 1000000000 as estimated_mws,
  (
    SUM(estimated_mw * dur) / (SELECT SUM(dur) from {{window_table}})
  ) as estimated_mw,
  -- Output zero idle cost for threads that don't cause wakeup
  COALESCE(idle_cost_mws, 0) as idle_cost_mws,
  thread_name,
  -- Ensure that all threads have the process field
  COALESCE(process_name, '') as process_name,
  tid,
  pid,
  period_id
FROM _windowed_threads_system_state
LEFT JOIN _per_thread_idle_attribution USING (utid, period_id)
GROUP BY utid, period_id
ORDER BY estimated_mw DESC;

-- Create proto format task attribution for each period
DROP VIEW IF EXISTS _wattson_per_task;
CREATE PERFETTO VIEW _wattson_per_task AS
SELECT
  period_id,
  (
    SELECT RepeatedField(
      AndroidWattsonTaskInfo(
        'estimated_mws', ROUND(estimated_mws, 6),
        'estimated_mw', ROUND(estimated_mw, 6),
        'idle_transitions_mws', ROUND(idle_cost_mws, 6),
        'total_mws', ROUND(estimated_mws + idle_cost_mws, 6),
        'thread_name', thread_name,
        'process_name', process_name,
        'thread_id', tid,
        'process_id', pid
      )
    )
  ) as proto
FROM _wattson_thread_attribution
GROUP BY period_id;

DROP VIEW IF EXISTS _wattson_tasks_metric_metadata;
CREATE PERFETTO VIEW _wattson_tasks_metric_metadata AS
SELECT
  4 AS metric_version,
  1 AS power_model_version,
  NOT EXISTS (SELECT 1 FROM _wattson_cpuidle_counters_exist) AS is_crude_estimate;
