
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

INCLUDE PERFETTO MODULE android.startup.startups;
INCLUDE PERFETTO MODULE wattson.estimates;
INCLUDE PERFETTO MODULE wattson.tasks.task_slices;

-- Create the base table (`android_jank_cuj`) containing all completed CUJs
-- found in the trace.
SELECT RUN_METRIC('android/jank/cujs.sql');

DROP VIEW IF EXISTS _atrace_apps_window;
CREATE PERFETTO VIEW _atrace_apps_window AS
SELECT
  ts,
  dur,
  cuj_id as period_id
FROM android_jank_cuj;

SELECT RUN_METRIC(
  'android/wattson_tasks_attribution.sql',
  'window_table',
  '_atrace_apps_window'
);

DROP VIEW IF EXISTS wattson_atrace_apps_threads_output;
CREATE PERFETTO VIEW wattson_atrace_apps_threads_output AS
SELECT AndroidWattsonTasksAttributionMetric(
  'metric_version', metric_version,
  'power_model_version', power_model_version,
  'is_crude_estimate', is_crude_estimate,
  'period_info', (
    SELECT RepeatedField(
      AndroidWattsonTaskPeriodInfo(
        'period_id', task.period_id,
        'period_name', cuj.cuj_name,
        'task_info', task.proto
      )
    )
    FROM _wattson_per_task AS task
    JOIN android_jank_cuj AS cuj ON cuj.cuj_id = task.period_id
  )
)
FROM _wattson_tasks_metric_metadata;
