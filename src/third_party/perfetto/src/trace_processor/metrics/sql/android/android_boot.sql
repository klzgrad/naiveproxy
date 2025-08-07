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

INCLUDE PERFETTO MODULE android.app_process_starts;
INCLUDE PERFETTO MODULE android.broadcasts;
INCLUDE PERFETTO MODULE android.garbage_collection;
INCLUDE PERFETTO MODULE android.oom_adjuster;
INCLUDE PERFETTO MODULE android.process_metadata;

DROP VIEW IF EXISTS android_oom_adj_intervals_with_detailed_bucket_name;
CREATE PERFETTO VIEW android_oom_adj_intervals_with_detailed_bucket_name AS
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

CREATE OR REPLACE PERFETTO FUNCTION get_durations(process_name STRING)
RETURNS TABLE(uint_sleep_dur LONG, total_dur LONG) AS
SELECT
    SUM(CASE WHEN thread_state.state="D" then thread_state.dur ELSE 0 END) AS uint_sleep_dur,
    SUM(thread_state.dur) as total_dur
FROM android_process_metadata
INNER JOIN thread ON thread.upid=android_process_metadata.upid
INNER JOIN thread_state ON thread.utid=thread_state.utid WHERE android_process_metadata.process_name=$process_name;

CREATE OR REPLACE PERFETTO FUNCTION first_user_unlocked() RETURNS INT AS
SELECT COALESCE(MIN(ts), 0) FROM thread_slice
WHERE name GLOB "*android.intent.action.USER_UNLOCKED*";

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
  WHERE ts > first_user_unlocked()
GROUP BY process_name, bucket, src_bucket;

DROP VIEW IF EXISTS oom_adj_events_global_by_bucket;
CREATE PERFETTO VIEW oom_adj_events_global_by_bucket AS
SELECT
  src_bucket,
  bucket,
  count(ts) as count,
  NULL as name
FROM _oom_adj_events_with_src_bucket
WHERE
  ts > first_user_unlocked()
GROUP BY bucket, src_bucket;

DROP VIEW IF EXISTS oom_adj_events_by_oom_adj_reason;
CREATE PERFETTO VIEW oom_adj_events_by_oom_adj_reason AS
SELECT
  src_bucket,
  bucket,
  count(ts) as count,
  oom_adj_reason as name
FROM _oom_adj_events_with_src_bucket
WHERE ts > first_user_unlocked()
GROUP BY bucket, src_bucket, oom_adj_reason;

DROP VIEW IF EXISTS android_boot_output;
CREATE PERFETTO VIEW android_boot_output AS
SELECT AndroidBootMetric(
    'system_server_durations', (
        SELECT NULL_IF_EMPTY(ProcessStateDurations(
            'total_dur', total_dur,
            'uninterruptible_sleep_dur', uint_sleep_dur))
        FROM get_durations('system_server')),
    'systemui_durations', (
        SELECT NULL_IF_EMPTY(ProcessStateDurations(
            'total_dur', total_dur,
            'uninterruptible_sleep_dur', uint_sleep_dur))
        FROM get_durations('com.android.systemui')),
    'launcher_durations', (
        SELECT NULL_IF_EMPTY(ProcessStateDurations(
            'total_dur', total_dur,
            'uninterruptible_sleep_dur', uint_sleep_dur))
        FROM get_durations('com.google.android.apps.nexuslauncher')),
    'gms_durations', (
        SELECT NULL_IF_EMPTY(ProcessStateDurations(
            'total_dur', total_dur,
            'uninterruptible_sleep_dur', uint_sleep_dur))
        FROM get_durations('com.google.android.gms.persistent')),
    'launcher_breakdown', (
        SELECT NULL_IF_EMPTY(AndroidBootMetric_LauncherBreakdown(
            'cold_start_dur', dur))
        FROM slice where name="LauncherColdStartup"),
    'full_trace_process_start_aggregation', (
        SELECT NULL_IF_EMPTY(AndroidBootMetric_ProcessStartAggregation(
            'total_start_sum', (SELECT SUM(total_dur) FROM android_app_process_starts),
            'num_of_processes', (SELECT COUNT(*) FROM android_app_process_starts),
            'average_start_time', (SELECT AVG(total_dur) FROM android_app_process_starts)))
          FROM android_app_process_starts),
    'post_boot_process_start_aggregation', (
        SELECT NULL_IF_EMPTY(AndroidBootMetric_ProcessStartAggregation(
            'total_start_sum', (
              SELECT SUM(total_dur)
              FROM android_app_process_starts
              WHERE proc_start_ts > first_user_unlocked()
            ),
            'num_of_processes', (
              SELECT COUNT(*)
              FROM android_app_process_starts
              WHERE proc_start_ts > first_user_unlocked()
            ),
            'average_start_time', (
              SELECT AVG(total_dur)
              FROM android_app_process_starts
              WHERE proc_start_ts > first_user_unlocked()
            )
        ))
    ),
    'full_trace_gc_aggregation', (
        SELECT NULL_IF_EMPTY(AndroidBootMetric_GarbageCollectionAggregation(
            'total_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
            ),
            'num_of_processes_with_gc', (SELECT COUNT(process_name) FROM android_garbage_collection_events
            ),
            'num_of_threads_with_gc', (
              SELECT SUM(cnt) FROM (
                SELECT COUNT(*) AS cnt
                FROM android_garbage_collection_events
                GROUP by thread_name, process_name
              )
            ),
            'avg_gc_duration', (SELECT AVG(gc_dur) FROM android_garbage_collection_events),
            'avg_running_gc_duration', (SELECT AVG(gc_running_dur) FROM android_garbage_collection_events),
            'full_gc_count', (
              SELECT COUNT(*)
              FROM android_garbage_collection_events
              WHERE gc_type = "full"
            ),
            'collector_transition_gc_count', (
              SELECT COUNT(*)
              FROM android_garbage_collection_events
              WHERE gc_type = "collector_transition"
            ),
            'young_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "young"
            ),
            'native_alloc_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "native_alloc"
            ),
            'explicit_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "explicit_gc"
            ),
            'alloc_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "alloc_gc"
            ),
            'mb_per_ms_of_gc', (SELECT SUM(reclaimed_mb)/SUM(gc_running_dur/1e6) AS mb_per_ms_dur
              FROM android_garbage_collection_events
            )
        ))
    ),
    'post_boot_gc_aggregation', (
        SELECT NULL_IF_EMPTY(AndroidBootMetric_GarbageCollectionAggregation(
            'total_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_ts > first_user_unlocked()
            ),
            'num_of_processes_with_gc', (SELECT COUNT(process_name) FROM android_garbage_collection_events
              WHERE gc_ts > first_user_unlocked()
            ),
            'num_of_threads_with_gc', (SELECT SUM(cnt) FROM (SELECT COUNT(*) AS cnt
              FROM android_garbage_collection_events
              WHERE gc_ts > first_user_unlocked()
              GROUP by thread_name, process_name)
            ),
            'avg_gc_duration', (SELECT AVG(gc_dur) FROM android_garbage_collection_events
              WHERE gc_ts > first_user_unlocked()
            ),
            'avg_running_gc_duration', (SELECT AVG(gc_running_dur) FROM android_garbage_collection_events
              WHERE gc_ts > first_user_unlocked()
            ),
            'full_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "full" AND gc_ts > first_user_unlocked()
            ),
            'collector_transition_gc_count', (SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "collector_transition" AND gc_ts > (
                SELECT COALESCE(MIN(ts), 0)
                FROM thread_slice
                WHERE name GLOB "*android.intent.action.USER_UNLOCKED*"
                ORDER BY ts ASC LIMIT 1
              )
            ),
            'young_gc_count', (
              SELECT COUNT(*)
              FROM android_garbage_collection_events
              WHERE gc_type = "young" AND gc_ts > (
                SELECT COALESCE(MIN(ts), 0)
                FROM thread_slice
                WHERE name GLOB "*android.intent.action.USER_UNLOCKED*"
                ORDER BY ts ASC LIMIT 1
              )
            ),
            'native_alloc_gc_count', (
              SELECT COUNT(*)
              FROM android_garbage_collection_events
              WHERE gc_type = "native_alloc" AND gc_ts > first_user_unlocked()
            ),
            'explicit_gc_count', (
              SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "explicit_gc" AND gc_ts > (
                SELECT COALESCE(MIN(ts), 0)
                FROM thread_slice
                WHERE name GLOB "*android.intent.action.USER_UNLOCKED*"
                ORDER BY ts ASC LIMIT 1
              )
            ),
            'alloc_gc_count', (
              SELECT COUNT(*) FROM android_garbage_collection_events
              WHERE gc_type = "alloc_gc" AND gc_ts > first_user_unlocked()
            ),
            'mb_per_ms_of_gc', (
              SELECT
                SUM(reclaimed_mb)/SUM(gc_running_dur/1e6) AS mb_per_ms_dur
              FROM android_garbage_collection_events
              WHERE gc_ts > first_user_unlocked()
            )
        ))
    ),
    'post_boot_oom_adjuster_transition_counts_by_process', (
      SELECT RepeatedField(
        AndroidBootMetric_OomAdjusterTransitionCounts(
          'name', process_name,
          'src_bucket', src_bucket,
          'dest_bucket', bucket,
          'count', count
        )
      ) FROM oom_adj_events_by_process_name
    ),
    'post_boot_oom_adjuster_transition_counts_global', (
      SELECT RepeatedField(
        AndroidBootMetric_OomAdjusterTransitionCounts(
          'name', name,
          'src_bucket', src_bucket,
          'dest_bucket', bucket,
          'count', count
        )
      )
      FROM oom_adj_events_global_by_bucket
    ),
    'post_boot_oom_adjuster_transition_counts_by_oom_adj_reason',(
      SELECT RepeatedField(
        AndroidBootMetric_OomAdjusterTransitionCounts(
          'name', name,
          'src_bucket', src_bucket,
          'dest_bucket', bucket,
          'count', count
        )
      )
      FROM oom_adj_events_by_oom_adj_reason
    ),
    'post_boot_oom_adj_bucket_duration_agg_global',(SELECT RepeatedField(
      AndroidBootMetric_OomAdjBucketDurationAggregation(
            'name', name,
            'bucket', bucket,
            'total_dur', total_dur
      ))
      FROM (
        SELECT
          NULL as name,
          bucket,
          SUM(dur) as total_dur
        FROM android_oom_adj_intervals_with_detailed_bucket_name
          WHERE ts > first_user_unlocked()
        GROUP BY bucket)
    ),
    'post_boot_oom_adj_bucket_duration_agg_by_process',(SELECT RepeatedField(
        AndroidBootMetric_OomAdjBucketDurationAggregation(
            'name', name,
            'bucket', bucket,
            'total_dur', total_dur
        )
    )
    FROM (
      SELECT
        process_name as name,
        bucket,
        SUM(dur) as total_dur
      FROM android_oom_adj_intervals_with_detailed_bucket_name
      WHERE ts > first_user_unlocked()
      AND process_name IS NOT NULL
      GROUP BY process_name, bucket)
    ),
    'post_boot_oom_adj_duration_agg',
    (SELECT RepeatedField(
        AndroidBootMetric_OomAdjDurationAggregation(
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
      FROM android_oom_adj_intervals_with_detailed_bucket_name
      WHERE ts > first_user_unlocked()
      GROUP BY oom_adj_reason
      )
    ),
  'post_boot_broadcast_process_count_by_intent', (
    SELECT RepeatedField(
      AndroidBootMetric_BroadcastCountAggregation(
        'name', intent_action,
        'count', process_name_counts
      )
    )
    FROM (
      SELECT
        intent_action,
        COUNT(process_name) as process_name_counts
      FROM _android_broadcasts_minsdk_u
      WHERE ts > first_user_unlocked()
      GROUP BY intent_action
    )
  ),
  'post_boot_broadcast_count_by_process', (
    SELECT RepeatedField(
      AndroidBootMetric_BroadcastCountAggregation(
        'name', process_name,
        'count', broadcast_counts
      )
    )
    FROM (
      SELECT
        process_name,
        COUNT(id) as broadcast_counts
      FROM _android_broadcasts_minsdk_u
      WHERE ts > first_user_unlocked()
      GROUP BY process_name
    )
  ),
  'post_boot_brodcast_duration_agg_by_intent', (
    SELECT RepeatedField(
      AndroidBootMetric_BroadcastDurationAggregation(
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
      WHERE ts > first_user_unlocked()
      GROUP BY intent_action
    )
  ),  'post_boot_brodcast_duration_agg_by_process', (
    SELECT RepeatedField(
      AndroidBootMetric_BroadcastDurationAggregation(
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
      WHERE ts > first_user_unlocked()
      GROUP BY process_name
    )
  )
);
