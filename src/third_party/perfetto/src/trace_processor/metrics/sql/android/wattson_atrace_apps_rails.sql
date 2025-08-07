
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

-- Create the base table (`android_jank_cuj`) containing all completed CUJs
-- found in the trace.
SELECT RUN_METRIC('android/jank/cujs.sql');

DROP VIEW IF EXISTS _wattson_cuj_windows;
CREATE PERFETTO VIEW _wattson_cuj_windows AS
SELECT
  ts,
  dur,
  cuj_id as period_id
FROM android_jank_cuj;

SELECT RUN_METRIC(
  'android/wattson_rail_relations.sql',
  'window_table', '_wattson_cuj_windows'
);

DROP VIEW IF EXISTS wattson_atrace_apps_rails_output;
CREATE PERFETTO VIEW wattson_atrace_apps_rails_output AS
SELECT AndroidWattsonTimePeriodMetric(
  'metric_version', 4,
  'power_model_version', 1,
  'period_info', (
    SELECT RepeatedField(
      AndroidWattsonEstimateInfo(
        'period_id', period_id,
        'period_name', cuj_name,
        'period_dur', period_dur,
        'cpu_subsystem', cpu_proto,
        'gpu_subsystem', gpu_proto
      )
    )
    FROM _estimate_subsystems_sum AS est
    JOIN android_jank_cuj AS cuj ON cuj.cuj_id = est.period_id
  )
);
