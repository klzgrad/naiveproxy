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
INCLUDE PERFETTO MODULE android.oom_adjuster;

DROP VIEW IF EXISTS android_oom_adj_intervals_with_detailed_bucket_name;
CREATE PERFETTO VIEW android_oom_adj_intervals_with_detailed_bucket_name (
  -- Timestamp the oom_adj score of the process changed
  ts INT,
  -- Duration until the next oom_adj score change of the process.
  dur INT,
  -- oom_adj score of the process.
  score INT,
  -- oom_adj bucket of the process.
  bucket STRING,
  -- Upid of the process having an oom_adj update.
  upid INT,
  -- Name of the process having an oom_adj update.
  process_name STRING,
  -- Slice id of the latest oom_adj update in the system_server.
  oom_adj_id INT,
  -- Timestamp of the latest oom_adj update in the system_server.
  oom_adj_ts INT,
  -- Duration of the latest oom_adj update in the system_server.
  oom_adj_dur INT,
  -- Track id of the latest oom_adj update in the system_server
  oom_adj_track_id INT,
  -- Thread name of the latest oom_adj update in the system_server.
  oom_adj_thread_name STRING,
  -- Reason for the latest oom_adj update in the system_server.
  oom_adj_reason STRING,
  -- Trigger for the latest oom_adj update in the system_server.
  oom_adj_trigger STRING
  ) AS
SELECT
  ts,
  dur,
  score,
  android_oom_adj_score_to_detailed_bucket_name(score, android_appid) AS bucket,
  upid,
  process_name,
  oom_adj_id,
  oom_adj_ts,
  oom_adj_dur,
  oom_adj_track_id,
  oom_adj_thread_name,
  oom_adj_reason,
  oom_adj_trigger
FROM _oom_adjuster_intervals;

DROP TABLE IF EXISTS _oom_adj_events_with_src_bucket;
CREATE PERFETTO TABLE _oom_adj_events_with_src_bucket
AS
SELECT
  LAG(bucket) OVER (PARTITION BY upid ORDER BY ts) AS src_bucket,
  ts,
  bucket,
  process_name,
  oom_adj_reason
FROM android_oom_adj_intervals_with_detailed_bucket_name;

DROP VIEW IF EXISTS oom_adj_events_by_process_name;
CREATE PERFETTO VIEW oom_adj_events_by_process_name AS
SELECT
  src_bucket,
  bucket,
  count(ts) as count,
  process_name
FROM _oom_adj_events_with_src_bucket
GROUP BY process_name, bucket, src_bucket;

DROP VIEW IF EXISTS oom_adj_events_global_by_bucket;
CREATE PERFETTO VIEW oom_adj_events_global_by_bucket AS
SELECT
  src_bucket,
  bucket,
  count(ts) as count,
  NULL as name
FROM _oom_adj_events_with_src_bucket
GROUP BY bucket, src_bucket;

DROP VIEW IF EXISTS oom_adj_events_by_oom_adj_reason;
CREATE PERFETTO VIEW oom_adj_events_by_oom_adj_reason AS
SELECT
  src_bucket,
  bucket,
  count(ts) as count,
  oom_adj_reason as name
FROM _oom_adj_events_with_src_bucket
GROUP BY bucket, src_bucket, oom_adj_reason;

DROP VIEW IF EXISTS android_oom_adjuster_output;
CREATE PERFETTO VIEW android_oom_adjuster_output AS
SELECT AndroidOomAdjusterMetric(
  'oom_adjuster_transition_counts_by_process', (
    SELECT RepeatedField(
      AndroidOomAdjusterMetric_OomAdjusterTransitionCounts(
        'name', process_name,
        'src_bucket', src_bucket,
        'dest_bucket', bucket,
        'count', count
      )
    ) FROM oom_adj_events_by_process_name
  ),
  'oom_adjuster_transition_counts_global', (
    SELECT RepeatedField(
      AndroidOomAdjusterMetric_OomAdjusterTransitionCounts(
        'name', name,
        'src_bucket', src_bucket,
        'dest_bucket', bucket,
        'count', count
      )
    )
    FROM oom_adj_events_global_by_bucket
  ),
  'oom_adjuster_transition_counts_by_oom_adj_reason',(
    SELECT RepeatedField(
      AndroidOomAdjusterMetric_OomAdjusterTransitionCounts(
        'name', name,
        'src_bucket', src_bucket,
        'dest_bucket', bucket,
        'count', count
      )
    )
    FROM oom_adj_events_by_oom_adj_reason
  ),
  'oom_adj_bucket_duration_agg_global',(SELECT RepeatedField(
    AndroidOomAdjusterMetric_OomAdjBucketDurationAggregation(
          'name', name,
          'bucket', bucket,
          'total_dur', total_dur
        )
    )
    FROM (
        SELECT NULL as name, bucket, SUM(dur) as total_dur
        FROM android_oom_adj_intervals_with_detailed_bucket_name GROUP BY bucket
    )
  ),
  'oom_adj_bucket_duration_agg_by_process',(SELECT RepeatedField(
      AndroidOomAdjusterMetric_OomAdjBucketDurationAggregation(
          'name', name,
          'bucket', bucket,
          'total_dur', total_dur
      )
    )
    FROM (
      SELECT process_name as name, bucket, SUM(dur) as total_dur
      FROM android_oom_adj_intervals_with_detailed_bucket_name
      WHERE process_name IS NOT NULL
      GROUP BY process_name, bucket
    )
  ),
  'oom_adj_duration_agg', (SELECT RepeatedField(
      AndroidOomAdjusterMetric_OomAdjDurationAggregation(
          'min_oom_adj_dur', min_oom_adj_dur,
          'max_oom_adj_dur', max_oom_adj_dur,
          'avg_oom_adj_dur', avg_oom_adj_dur,
          'oom_adj_event_count', oom_adj_event_count,
          'oom_adj_reason', oom_adj_reason
      )
    )
    FROM (
      SELECT
        MIN(oom_adj_dur) as min_oom_adj_dur,
        MAX(oom_adj_dur) as max_oom_adj_dur,
        AVG(oom_adj_dur) as avg_oom_adj_dur,
        COUNT(DISTINCT(oom_adj_id)) oom_adj_event_count,
        oom_adj_reason
      FROM android_oom_adj_intervals_with_detailed_bucket_name GROUP BY oom_adj_reason
    )
  )
);
