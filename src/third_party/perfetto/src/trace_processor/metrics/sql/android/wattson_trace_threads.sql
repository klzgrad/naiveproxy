
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
INCLUDE PERFETTO MODULE wattson.tasks.task_slices;

-- This metric is defined to be for entire trace duration
DROP VIEW IF EXISTS _wattson_period_window;
CREATE PERFETTO VIEW _wattson_period_window AS
SELECT
  trace_start() as ts,
  trace_dur() as dur,
  1 as period_id;

SELECT RUN_METRIC(
  'android/wattson_tasks_attribution.sql',
  'window_table',
  '_wattson_period_window'
);

DROP VIEW IF EXISTS wattson_trace_threads_output;
CREATE PERFETTO VIEW wattson_trace_threads_output AS
SELECT AndroidWattsonTasksAttributionMetric(
  'metric_version', metric_version,
  'power_model_version', power_model_version,
  'is_crude_estimate', is_crude_estimate,
  'period_info', (
    SELECT RepeatedField(
      AndroidWattsonTaskPeriodInfo(
        'period_id', period_id,
        'task_info', proto
      )
    )
    FROM _wattson_per_task
  )
)
FROM _wattson_tasks_metric_metadata;
