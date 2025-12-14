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
--
INCLUDE PERFETTO MODULE android.broadcasts;

DROP VIEW IF EXISTS android_broadcasts_output;
CREATE PERFETTO VIEW android_broadcasts_output AS
SELECT AndroidBroadcastsMetric(
  'process_count_by_intent', (
    SELECT RepeatedField(
      AndroidBroadcastsMetric_BroadcastCountAggregation(
        'name', intent_action,
        'count', process_name_counts
      )
    )
    FROM (
      SELECT
        intent_action,
        COUNT(process_name) as process_name_counts
      FROM _android_broadcasts_minsdk_u
      GROUP BY intent_action
    )
  ),
  'broadcast_count_by_process', (
    SELECT RepeatedField(
      AndroidBroadcastsMetric_BroadcastCountAggregation(
        'name', process_name,
        'count', broadcast_counts
      )
    )
    FROM (
      SELECT
        process_name,
        COUNT(id) as broadcast_counts
      FROM _android_broadcasts_minsdk_u
      GROUP BY process_name
    )
  ),
  'brodcast_duration_agg_by_intent', (
    SELECT RepeatedField(
      AndroidBroadcastsMetric_BroadcastDurationAggregation(
        'name', intent_action,
        'avg_duration', avg_duration,
        'max_duration', max_duration,
        'sum_duration', sum_duration
      )
    )
    FROM (
      SELECT
        intent_action,
        AVG(dur) as avg_duration,
        SUM(dur) as sum_duration,
        MAX(dur) as max_duration
      FROM _android_broadcasts_minsdk_u
      GROUP BY intent_action
    )
  ),  'brodcast_duration_agg_by_process', (
    SELECT RepeatedField(
      AndroidBroadcastsMetric_BroadcastDurationAggregation(
        'name', process_name,
        'avg_duration', avg_duration,
        'max_duration', max_duration,
        'sum_duration', sum_duration
      )
    )
    FROM (
      SELECT
        process_name,
        AVG(dur) as avg_duration,
        SUM(dur) as sum_duration,
        MAX(dur) as max_duration
      FROM _android_broadcasts_minsdk_u
      GROUP BY process_name
    )
  )
)