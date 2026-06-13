--
-- Copyright 2023 The Android Open Source Project
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
--

INCLUDE PERFETTO MODULE android.monitor_contention;

DROP VIEW IF EXISTS amc_process_agg;
CREATE PERFETTO VIEW amc_process_agg AS
WITH full_contention AS (
  Select process_name, COUNT(*) as total_contention_count, SUM(dur)
  as total_contention_dur from android_monitor_contention group by process_name
),
main_thread_contention AS
(
  Select process_name, COUNT(*) as main_thread_contention_count, SUM(dur)
  as main_thread_contention_dur from android_monitor_contention
  where is_blocked_thread_main=1 group by process_name
)
SELECT f.process_name, total_contention_count, total_contention_dur,
 main_thread_contention_count, main_thread_contention_dur
 from full_contention as f left join main_thread_contention as m on f.process_name = m.process_name;

DROP VIEW IF EXISTS android_monitor_contention_agg_output;
CREATE PERFETTO VIEW android_monitor_contention_agg_output AS
SELECT AndroidMonitorContentionAggMetric(
  'process_aggregation', (
    SELECT RepeatedField(
        AndroidMonitorContentionAggMetric_ProcessAggregation(
            'name', process_name,
            'total_contention_count', total_contention_count,
            'total_contention_dur', total_contention_dur,
            'main_thread_contention_count', main_thread_contention_count,
            'main_thread_contention_dur', main_thread_contention_dur
        )
    )
    FROM amc_process_agg
  )
);