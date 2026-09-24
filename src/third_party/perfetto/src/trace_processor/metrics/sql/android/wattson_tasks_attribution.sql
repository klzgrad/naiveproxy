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

INCLUDE PERFETTO MODULE wattson.aggregation;
INCLUDE PERFETTO MODULE wattson.estimates;
INCLUDE PERFETTO MODULE wattson.utils;

-- Group by unique thread ID and disregard CPUs, summing of power over all CPUs
-- and all instances of the thread
DROP VIEW IF EXISTS _wattson_thread_attribution;
CREATE PERFETTO VIEW _wattson_thread_attribution AS
SELECT
  estimated_mws,
  estimated_mw,
  idle_transitions_mws AS idle_cost_mws,
  thread_name,
  process_name,
  tid,
  pid,
  period_id
FROM wattson_threads_aggregation!((
  SELECT ts, dur, period_id FROM {{window_table}}
))
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
