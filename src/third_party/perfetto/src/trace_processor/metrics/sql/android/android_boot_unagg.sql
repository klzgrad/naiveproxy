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

INCLUDE PERFETTO MODULE android.app_process_starts;
INCLUDE PERFETTO MODULE android.garbage_collection;
INCLUDE PERFETTO MODULE android.suspend;

DROP VIEW IF EXISTS android_boot_unagg_output;
CREATE PERFETTO VIEW android_boot_unagg_output AS
SELECT AndroidBootUnagg(
  'android_app_process_start_metric', (
    SELECT AndroidAppProcessStartsMetric(
        'all_apps', (
            SELECT RepeatedField(
                AndroidAppProcessStartsMetric_ProcessStart(
                    'process_name', process_name,
                    'intent', intent,
                    'reason', reason,
                    'proc_start_dur', proc_start_dur,
                    'bind_app_dur', bind_app_dur,
                    'intent_dur', intent_dur,
                    'total_dur', total_dur
                )
            )
            FROM android_app_process_starts WHERE proc_start_ts > (SELECT COALESCE(MIN(ts), 0)
            FROM thread_slice WHERE name GLOB "*android.intent.action.USER_UNLOCKED*" ORDER BY ts
            ASC LIMIT 1)
        ),
        'started_by_broadcast', (
            SELECT RepeatedField(
                AndroidAppProcessStartsMetric_ProcessStart(
                    'process_name', process_name,
                    'intent', intent,
                    'reason', reason,
                    'proc_start_dur', proc_start_dur,
                    'bind_app_dur', bind_app_dur,
                    'intent_dur', intent_dur,
                    'total_dur', total_dur
                )
            )
            FROM android_app_process_starts WHERE proc_start_ts > (SELECT COALESCE(MIN(ts), 0)
            FROM thread_slice WHERE name GLOB "*android.intent.action.USER_UNLOCKED*" ORDER BY ts
            ASC LIMIT 1)
            AND reason = "broadcast"
        ),
        'started_by_service', (
            SELECT RepeatedField(
                AndroidAppProcessStartsMetric_ProcessStart(
                    'process_name', process_name,
                    'intent', intent,
                    'reason', reason,
                    'proc_start_dur', proc_start_dur,
                    'bind_app_dur', bind_app_dur,
                    'intent_dur', intent_dur,
                    'total_dur', total_dur
                )
            )
            FROM android_app_process_starts WHERE proc_start_ts > (SELECT COALESCE(MIN(ts), 0)
            FROM thread_slice WHERE name GLOB "*android.intent.action.USER_UNLOCKED*" ORDER BY ts
            ASC LIMIT 1 )
            AND reason = "service"
        )
    )),
    'android_post_boot_gc_metric', (SELECT AndroidGarbageCollectionUnaggMetric(
        'gc_events', (
            SELECT RepeatedField(
                AndroidGarbageCollectionUnaggMetric_GarbageCollectionEvent(
                    'thread_name', thread_name,
                    'process_name', process_name,
                    'gc_type', gc_type,
                    'is_mark_compact', is_mark_compact,
                    'reclaimed_mb', reclaimed_mb,
                    'min_heap_mb', min_heap_mb,
                    'max_heap_mb', max_heap_mb,
                    'mb_per_ms_of_running_gc', reclaimed_mb/(gc_running_dur/1e6),
                    'mb_per_ms_of_wall_gc', reclaimed_mb/(gc_dur/1e6),
                    'gc_dur', gc_dur,
                    'gc_running_dur', gc_running_dur,
                    'gc_runnable_dur', gc_runnable_dur,
                    'gc_unint_io_dur', gc_unint_io_dur,
                    'gc_unint_non_io_dur', gc_unint_non_io_dur,
                    'gc_int_dur', gc_int_dur,
                    'gc_ts', gc_ts,
                    'tid', tid,
                    'pid', pid,
                    'gc_monotonic_dur', _extract_duration_without_suspend(gc_ts, gc_dur)
                )
            ) FROM android_garbage_collection_events WHERE gc_ts > (SELECT COALESCE(MIN(ts), 0)
                FROM thread_slice WHERE name GLOB "*android.intent.action.USER_UNLOCKED*" ORDER BY ts
                ASC LIMIT 1
            )
        )
    ))
);
