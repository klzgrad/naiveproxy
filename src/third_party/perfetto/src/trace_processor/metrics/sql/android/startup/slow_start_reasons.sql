--
-- Copyright 2022 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.startup.startups;

SELECT RUN_METRIC('android/startup/thread_state_breakdown.sql');
SELECT RUN_METRIC('android/startup/system_state.sql');
SELECT RUN_METRIC('android/startup/mcycles_per_launch.sql');
-- Define helper functions related to slow start thresholds
SELECT RUN_METRIC('android/startup/slow_start_thresholds.sql');

CREATE OR REPLACE PERFETTO FUNCTION _is_spans_overlapping(
  ts1 LONG,
  ts_end1 LONG,
  ts2 LONG,
  ts_end2 LONG)
RETURNS BOOL AS
SELECT (IIF($ts1 < $ts2, $ts2, $ts1)
      < IIF($ts_end1 < $ts_end2, $ts_end1, $ts_end2));

CREATE OR REPLACE PERFETTO FUNCTION get_percent(num LONG, total LONG)
RETURNS STRING AS
  SELECT SUBSTRING(CAST(($num * 100 + 0.0) / $total AS STRING), 1, 5);

CREATE OR REPLACE PERFETTO FUNCTION get_ns_to_s(ns LONG)
RETURNS STRING AS
  SELECT CAST(($ns + 0.0) / 1e9 AS STRING);

CREATE OR REPLACE PERFETTO FUNCTION get_ns_to_ms(ns LONG)
RETURNS STRING AS
  SELECT SUBSTRING(CAST(($ns + 0.0) / 1e6 AS STRING), 1, 6);

CREATE OR REPLACE PERFETTO FUNCTION get_main_thread_time_for_launch_in_runnable_state(
  startup_id LONG, num_threads INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceThreadSectionInfo(
    'start_timestamp', MIN(ts),
    'end_timestamp', MAX(ts + dur),
    'thread_section', RepeatedField(AndroidStartupMetric_TraceThreadSection(
      'start_timestamp', ts, 'end_timestamp', ts + dur,
      'thread_tid', tid, 'process_pid', pid,
      'thread_name', thread_name)))
  FROM (
    SELECT p.pid, ts, dur, thread.tid, thread_name
    FROM launch_threads_by_thread_state l, android_startup_processes p
    JOIN thread USING (utid)
    WHERE l.startup_id = $startup_id AND (state GLOB "R" OR state GLOB "R+") AND l.is_main_thread
      AND p.startup_id = $startup_id
    ORDER BY dur DESC
    LIMIT $num_threads);

CREATE OR REPLACE PERFETTO FUNCTION get_main_thread_time_for_launch_and_state(
  startup_id LONG, state STRING, num_threads INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceThreadSectionInfo(
    'start_timestamp', MIN(ts),
    'end_timestamp', MAX(ts + dur),
    'thread_section', RepeatedField(AndroidStartupMetric_TraceThreadSection(
      'start_timestamp', ts, 'end_timestamp', ts + dur,
      'thread_tid', tid, 'process_pid', pid,
      'thread_name', thread_name)))
  FROM (
    SELECT p.pid, ts, dur, thread.tid, thread_name
    FROM launch_threads_by_thread_state l, android_startup_processes p
    JOIN thread USING (utid)
    WHERE l.startup_id = $startup_id AND state GLOB $state AND l.is_main_thread
      AND p.startup_id = $startup_id
    ORDER BY dur DESC
    LIMIT $num_threads);

CREATE OR REPLACE PERFETTO FUNCTION get_main_thread_time_for_launch_state_and_io_wait(
  startup_id INT, state STRING, io_wait BOOL, num_threads INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceThreadSectionInfo(
    'start_timestamp', MIN(ts),
    'end_timestamp', MAX(ts + dur),
    'thread_section', RepeatedField(AndroidStartupMetric_TraceThreadSection(
      'start_timestamp', ts, 'end_timestamp', ts + dur,
      'thread_tid', tid, 'process_pid', pid,
      'thread_name', thread_name)))
  FROM (
    SELECT p.pid, ts, dur, thread.tid, thread_name
    FROM launch_threads_by_thread_state l, android_startup_processes p
    JOIN thread USING (utid)
    WHERE l.startup_id = $startup_id AND state GLOB $state
      AND l.is_main_thread AND l.io_wait = $io_wait
      AND p.startup_id = $startup_id
    ORDER BY dur DESC
    LIMIT $num_threads);

CREATE OR REPLACE PERFETTO FUNCTION get_thread_time_for_launch_state_and_thread(
  startup_id INT, state STRING, thread_name STRING, num_threads INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceThreadSectionInfo(
    'start_timestamp', MIN(ts),
    'end_timestamp', MAX(ts + dur),
    'thread_section', RepeatedField(AndroidStartupMetric_TraceThreadSection(
      'start_timestamp', ts, 'end_timestamp', ts + dur,
      'thread_tid', tid, 'process_pid', pid,
      'thread_name', thread_name)))
  FROM (
    SELECT p.pid, ts, dur, thread.tid, thread_name
    FROM launch_threads_by_thread_state l, android_startup_processes p
    JOIN thread USING (utid)
    WHERE l.startup_id = $startup_id AND state GLOB $state AND thread_name = $thread_name
      AND p.startup_id = $startup_id
    ORDER BY dur DESC
    LIMIT $num_threads);

CREATE OR REPLACE PERFETTO FUNCTION get_missing_baseline_profile_for_launch(
  startup_id LONG, pkg_name STRING)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', pid,
      'start_timestamp', slice_ts,
      'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id,
      'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT p.pid, tid, slice_ts, slice_dur, slice_id, slice_name
    FROM ANDROID_SLICES_FOR_STARTUP_AND_SLICE_NAME($startup_id,
      "location=* status=* filter=* reason=*"), android_startup_processes p
    WHERE
      -- when location is the package odex file and the reason is "install" or "install-dm",
      -- if the compilation filter is not "speed-profile", baseline/cloud profile is missing.
      SUBSTR(STR_SPLIT(slice_name, " status=", 0), LENGTH("location=") + 1)
        GLOB ("*" || $pkg_name || "*odex")
      AND (STR_SPLIT(slice_name, " reason=", 1) = "install"
      OR STR_SPLIT(slice_name, " reason=", 1) = "install-dm")
      AND p.startup_id = $startup_id
    ORDER BY slice_dur DESC
    LIMIT 1);

CREATE OR REPLACE PERFETTO FUNCTION get_run_from_apk(startup_id LONG)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', pid,
      'start_timestamp', slice_ts,
      'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id,
      'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT p.pid, tid, slice_ts, slice_dur, slice_id, slice_name
    FROM android_thread_slices_for_all_startups l, android_startup_processes p
    WHERE
      l.startup_id = $startup_id AND is_main_thread AND
      slice_name GLOB "location=* status=* filter=* reason=*" AND
      STR_SPLIT(STR_SPLIT(slice_name, " filter=", 1), " reason=", 0)
        GLOB ("*" || "run-from-apk" || "*")
      AND p.startup_id = $startup_id
    ORDER BY slice_dur DESC
    LIMIT 1);

CREATE OR REPLACE PERFETTO FUNCTION get_unlock_running_during_launch_slice(startup_id LONG,
  pid INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', $pid,
      'start_timestamp', slice_ts,
      'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id,
      'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT tid, slice.ts as slice_ts, slice.dur as slice_dur,
      slice.id as slice_id, slice.name as slice_name
    FROM slice, android_startups launches
    JOIN thread_track ON slice.track_id = thread_track.id
    JOIN thread USING(utid)
    JOIN process USING(upid)
    WHERE launches.startup_id = $startup_id
    AND slice.name = "KeyguardUpdateMonitor#onAuthenticationSucceeded"
    AND process.name = "com.android.systemui"
    AND slice.ts >= launches.ts
    AND (slice.ts + slice.dur) <= launches.ts_end
    LIMIT 1);

CREATE OR REPLACE PERFETTO FUNCTION get_gc_activity(startup_id LONG, num_slices INT)
RETURNS PROTO  AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', pid,
      'start_timestamp', slice_ts,
      'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id,
      'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT p.pid, tid, slice_ts, slice_dur, slice_id, slice_name
    FROM android_thread_slices_for_all_startups slice, android_startup_processes p
    WHERE
      p.startup_id = $startup_id AND
      slice.startup_id = $startup_id AND
      (
        slice_name GLOB "*semispace GC" OR
        slice_name GLOB "*mark sweep GC" OR
        slice_name GLOB "*concurrent copying GC"
      )
    ORDER BY slice_dur DESC
    LIMIT $num_slices);

CREATE OR REPLACE PERFETTO FUNCTION get_dur_on_main_thread_for_startup_and_slice(
  startup_id LONG, slice_name STRING, num_slices INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', pid,
      'start_timestamp', slice_ts,
      'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id,
      'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT p.pid, tid, slice_ts, slice_dur, slice_id, slice_name
    FROM android_thread_slices_for_all_startups l,
      android_startup_processes p
    WHERE l.startup_id = $startup_id AND p.startup_id == $startup_id
      AND slice_name GLOB $slice_name
    ORDER BY slice_dur DESC
    LIMIT $num_slices);

CREATE OR REPLACE PERFETTO FUNCTION get_main_thread_binder_transactions_blocked(
  startup_id LONG, threshold DOUBLE, num_slices INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', pid,
      'start_timestamp', slice_ts, 'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id, 'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT pid, request.tid as tid, request.slice_ts as slice_ts, request.slice_dur as slice_dur,
      request.id as slice_id, request.slice_name as slice_name
    FROM (
      SELECT p.pid, tid, slice_id as id, slice_dur, thread_name, process.name as process,
        s.arg_set_id, is_main_thread,
        slice_ts, s.utid, slice_name
      FROM android_thread_slices_for_all_startups s,
        android_startup_processes p
      JOIN process ON (
        EXTRACT_ARG(s.arg_set_id, "destination process") = process.pid
      )
      WHERE s.startup_id = $startup_id AND slice_name GLOB "binder transaction"
        AND slice_dur > $threshold AND p.startup_id = $startup_id
    ) request
    JOIN following_flow(request.id) arrow
    JOIN slice reply ON reply.id = arrow.slice_in
    JOIN thread USING (utid)
    WHERE reply.dur > $threshold AND request.is_main_thread
    ORDER BY request.slice_dur DESC
    LIMIT $num_slices);

CREATE OR REPLACE PERFETTO FUNCTION get_slices_concurrent_to_launch(
  startup_id INT, slice_glob STRING, num_slices INT, pid INT)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', $pid,
      'start_timestamp', ts, 'end_timestamp', ts + dur,
      'slice_id', id, 'slice_name', name)),
    'start_timestamp', MIN(ts),
    'end_timestamp', MAX(ts + dur))
  FROM (
    SELECT thread.tid, s.ts as ts, dur, s.id, s.name FROM slice s
    JOIN thread_track t ON s.track_id = t.id
    JOIN thread USING(utid)
    JOIN (
      SELECT ts, ts_end
      FROM android_startups
      WHERE startup_id = $startup_id
    ) launch
    WHERE
      s.name GLOB $slice_glob AND
      s.ts BETWEEN launch.ts AND launch.ts_end
    ORDER BY dur DESC LIMIT $num_slices);

CREATE OR REPLACE PERFETTO FUNCTION get_slices_for_startup_and_slice_name(
  startup_id INT, slice_name STRING, num_slices INT, pid int)
RETURNS PROTO AS
  SELECT AndroidStartupMetric_TraceSliceSectionInfo(
    'slice_section', RepeatedField(AndroidStartupMetric_TraceSliceSection(
      'thread_tid', tid,
      'process_pid', $pid,
      'start_timestamp', slice_ts, 'end_timestamp', slice_ts + slice_dur,
      'slice_id', slice_id, 'slice_name', slice_name)),
    'start_timestamp', MIN(slice_ts),
    'end_timestamp', MAX(slice_ts + slice_dur))
  FROM (
    SELECT tid, slice_ts, slice_dur, slice_id, slice_name
    FROM android_thread_slices_for_all_startups
    WHERE startup_id = $startup_id AND slice_name GLOB $slice_name
    ORDER BY slice_dur DESC
    LIMIT $num_slices);

CREATE OR REPLACE PERFETTO FUNCTION get_process_running_concurrent_to_launch(
  startup_id INT, process_glob STRING)
RETURNS STRING AS
  SELECT process.name
     FROM sched
     JOIN thread USING (utid)
     JOIN process USING (upid)
     JOIN (
       SELECT ts, ts_end
       FROM android_startups
       WHERE startup_id = $startup_id
       ) launch
     WHERE
       process.name GLOB $process_glob AND
       sched.ts BETWEEN launch.ts AND launch.ts_end
       ORDER BY (launch.ts_end - sched.ts) DESC
       LIMIT 1;

CREATE OR REPLACE PERFETTO FUNCTION get_slow_start_reason_with_details(startup_id LONG)
RETURNS PROTO AS
      SELECT RepeatedField(AndroidStartupMetric_SlowStartReason(
        'reason_id', reason_id,
        'reason', slow_cause,
        'severity', severity,
        'launch_dur', launch_dur,
        'expected_value', expected_val,
        'actual_value', actual_val,
        'trace_slice_sections', trace_slices,
        'trace_thread_sections', trace_threads,
        'additional_info', extra))
      FROM (
        SELECT 'No baseline or cloud profiles' as slow_cause,
          launch.dur as launch_dur,
          'NO_BASELINE_OR_CLOUD_PROFILES' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          get_missing_baseline_profile_for_launch(launch.startup_id, launch.package)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND missing_baseline_profile_for_launch(launch.startup_id, launch.package)

        UNION ALL
        SELECT 'Optimized artifacts missing, run from apk' as slow_cause,
          launch.dur as launch_dur,
          'RUN_FROM_APK' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          get_run_from_apk(launch.startup_id)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND run_from_apk_for_launch(launch.startup_id)

        UNION ALL
        SELECT 'Unlock running during launch' as slow_cause,
          launch.dur as launch_dur,
          'UNLOCK_RUNNING' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          get_unlock_running_during_launch_slice(launch.startup_id,
            (SELECT pid FROM android_startup_processes WHERE launch.startup_id = startup_id))
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
         AND is_unlock_running_during_launch(launch.startup_id)

        UNION ALL
        SELECT 'App in debuggable mode' as slow_cause,
          launch.dur as launch_dur,
          'APP_IN_DEBUGGABLE_MODE' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          NULL as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND is_process_debuggable(launch.package)

        UNION ALL
        SELECT 'GC Activity' as slow_cause,
          launch.dur as launch_dur,
          'GC_ACTIVITY' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          get_gc_activity(launch.startup_id, 1)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND total_gc_time_by_launch(launch.startup_id) > 0

        UNION ALL
        SELECT 'dex2oat running during launch' AS slow_cause,
          launch.dur as launch_dur,
          'DEX2OAT_RUNNING' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          NULL as trace_slices,
          NULL as trace_threads,
          'Process: ' || get_process_running_concurrent_to_launch(launch.startup_id, '*dex2oat64')
            as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id AND
          dur_of_process_running_concurrent_to_launch(launch.startup_id, '*dex2oat64') > 0

        UNION ALL
        SELECT 'installd running during launch' AS slow_cause,
          launch.dur as launch_dur,
          'INSTALLD_RUNNING' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          NULL as trace_slices,
          NULL as trace_threads,
          'Process: ' || get_process_running_concurrent_to_launch(launch.startup_id, '*installd')
            as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id AND
          dur_of_process_running_concurrent_to_launch(launch.startup_id, '*installd') > 0

        UNION ALL
        SELECT 'Main Thread - Time spent in Runnable state' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_TIME_SPENT_IN_RUNNABLE' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_runnable_percentage(),
            'unit', 'PERCENTAGE',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value',
              main_thread_time_for_launch_in_runnable_state(launch.startup_id) * 100 / launch.dur,
            'dur', main_thread_time_for_launch_in_runnable_state(launch.startup_id)) as actual_val,
          NULL as trace_slices,
          get_main_thread_time_for_launch_in_runnable_state(launch.startup_id, 3)
            as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND main_thread_time_for_launch_in_runnable_state(launch.startup_id) >
            launch.dur / 100 * threshold_runnable_percentage()

        UNION ALL
        SELECT 'Main Thread - Time spent in interruptible sleep state' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_TIME_SPENT_IN_INTERRUPTIBLE_SLEEP' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_interruptible_sleep_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', main_thread_time_for_launch_and_state(launch.startup_id, 'S')) as actual_val,
          NULL as trace_slices,
          get_main_thread_time_for_launch_and_state(launch.startup_id, 'S', 3)
            as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND main_thread_time_for_launch_and_state(launch.startup_id, 'S') >
            threshold_interruptible_sleep_ns()

        UNION ALL
        SELECT 'Main Thread - Time spent in Blocking I/O' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_TIME_SPENT_IN_BLOCKING_IO' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_blocking_io_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', main_thread_time_for_launch_state_and_io_wait(
              launch.startup_id, 'D*', TRUE)) as actual_val,
          NULL as trace_slices,
          get_main_thread_time_for_launch_state_and_io_wait(
            launch.startup_id, 'D*', TRUE, 3)
            as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND main_thread_time_for_launch_state_and_io_wait(launch.startup_id, 'D*', TRUE) >
            threshold_blocking_io_ns()

        UNION ALL
        SELECT 'Main Thread - Time spent in OpenDexFilesFromOat*' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_TIME_SPENT_IN_OPEN_DEX_FILES_FROM_OAT' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_open_dex_files_from_oat_percentage(),
            'unit', 'PERCENTAGE',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_on_main_thread_for_startup_and_slice(
              launch.startup_id, 'OpenDexFilesFromOat*') * 100 / launch.dur,
            'dur', android_sum_dur_on_main_thread_for_startup_and_slice(
              launch.startup_id, 'OpenDexFilesFromOat*')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(launch.startup_id, 'OpenDexFilesFromOat*', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id AND
          android_sum_dur_on_main_thread_for_startup_and_slice(
          launch.startup_id, 'OpenDexFilesFromOat*') >
            launch.dur / 100 * threshold_open_dex_files_from_oat_percentage()

        UNION ALL
        SELECT 'Time spent in bindApplication' as slow_cause,
          launch.dur as launch_dur,
          'TIME_SPENT_IN_BIND_APPLICATION' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_bind_application_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_for_startup_and_slice(
              launch.startup_id, 'bindApplication')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(launch.startup_id, 'bindApplication', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND android_sum_dur_for_startup_and_slice(launch.startup_id, 'bindApplication') >
            threshold_bind_application_ns()

        UNION ALL
        SELECT 'Time spent in view inflation' as slow_cause,
          launch.dur as launch_dur,
          'TIME_SPENT_IN_VIEW_INFLATION' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_view_inflation_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_for_startup_and_slice(
              launch.startup_id, 'inflate')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(launch.startup_id, 'inflate', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND android_sum_dur_for_startup_and_slice(launch.startup_id, 'inflate') >
            threshold_view_inflation_ns()

        UNION ALL
        SELECT 'Time spent in ResourcesManager#getResources' as slow_cause,
          launch.dur as launch_dur,
          'TIME_SPENT_IN_RESOURCES_MANAGER_GET_RESOURCES' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_resources_manager_get_resources_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_for_startup_and_slice(
              launch.startup_id, 'ResourcesManager#getResources')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(
            launch.startup_id, 'ResourcesManager#getResources', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND android_sum_dur_for_startup_and_slice(
          launch.startup_id, 'ResourcesManager#getResources') >
            threshold_resources_manager_get_resources_ns()

        UNION ALL
        SELECT 'Time spent verifying classes' as slow_cause,
          launch.dur as launch_dur,
          'TIME_SPENT_VERIFYING_CLASSES' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_verify_classes_percentage(),
            'unit', 'PERCENTAGE',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_for_startup_and_slice(
              launch.startup_id, 'VerifyClass*') * 100 / launch.dur,
            'dur', android_sum_dur_for_startup_and_slice(
              launch.startup_id, 'VerifyClass*')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(launch.startup_id, 'VerifyClass*', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id AND
          android_sum_dur_for_startup_and_slice(launch.startup_id, 'VerifyClass*')
            > launch.dur / 100 * threshold_verify_classes_percentage()

        UNION ALL
        SELECT 'Potential CPU contention with another process' AS slow_cause,
          launch.dur as launch_dur,
          'POTENTIAL_CPU_CONTENTION_WITH_ANOTHER_PROCESS' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_potential_cpu_contention_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value',
              main_thread_time_for_launch_in_runnable_state(launch.startup_id)) as actual_val,
          NULL as trace_slices,
          get_main_thread_time_for_launch_in_runnable_state(launch.startup_id, 3)
            as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id AND
          main_thread_time_for_launch_in_runnable_state(launch.startup_id) >
            threshold_potential_cpu_contention_ns() AND
          most_active_process_for_launch(launch.startup_id) IS NOT NULL

        UNION ALL
        SELECT 'JIT Activity' as slow_cause,
          launch.dur as launch_dur,
          'JIT_ACTIVITY' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_jit_activity_ns(),
            'unit', 'NS',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', thread_time_for_launch_state_and_thread(
              launch.startup_id, 'Running', 'Jit thread pool')) as actual_val,
          NULL as trace_slices,
          get_thread_time_for_launch_state_and_thread(
            launch.startup_id, 'Running', 'Jit thread pool', 3)
            as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
        AND thread_time_for_launch_state_and_thread(
          launch.startup_id,
          'Running',
          'Jit thread pool'
        ) > threshold_jit_activity_ns()

        UNION ALL
        SELECT 'Main Thread - Lock contention' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_LOCK_CONTENTION' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_lock_contention_percentage(),
            'unit', 'PERCENTAGE',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_on_main_thread_for_startup_and_slice(
              launch.startup_id, 'Lock contention on*') * 100 / launch.dur,
            'dur', android_sum_dur_on_main_thread_for_startup_and_slice(
              launch.startup_id, 'Lock contention on*')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(launch.startup_id, 'Lock contention on*', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND android_sum_dur_on_main_thread_for_startup_and_slice(
          launch.startup_id,
          'Lock contention on*'
        ) > launch.dur / 100 * threshold_lock_contention_percentage()

        UNION ALL
        SELECT 'Main Thread - Monitor contention' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_MONITOR_CONTENTION' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_monitor_contention_percentage(),
            'unit', 'PERCENTAGE',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', android_sum_dur_on_main_thread_for_startup_and_slice(
              launch.startup_id, 'Lock contention on a monitor*') * 100 / launch.dur,
            'dur', android_sum_dur_on_main_thread_for_startup_and_slice(
              launch.startup_id, 'Lock contention on a monitor*')) as actual_val,
          get_dur_on_main_thread_for_startup_and_slice(
            launch.startup_id, 'Lock contention on a monitor*', 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND android_sum_dur_on_main_thread_for_startup_and_slice(
            launch.startup_id,
            'Lock contention on a monitor*'
          ) > launch.dur / 100 * threshold_monitor_contention_percentage()

        UNION ALL
        SELECT 'JIT compiled methods' as slow_cause,
          launch.dur as launch_dur,
          'JIT_COMPILED_METHODS' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_jit_compiled_methods_count(),
            'unit', 'COUNT',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', (SELECT COUNT(1)
              FROM ANDROID_SLICES_FOR_STARTUP_AND_SLICE_NAME(launch.startup_id, 'JIT compiling*')
              WHERE thread_name = 'Jit thread pool')) as actual_val,
          get_slices_for_startup_and_slice_name(launch.startup_id, 'JIT compiling*', 3,
            (SELECT pid FROM android_startup_processes WHERE launch.startup_id = startup_id))
            as trace_slices,
          NULL as traced_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND (
          SELECT COUNT(1)
          FROM ANDROID_SLICES_FOR_STARTUP_AND_SLICE_NAME(launch.startup_id, 'JIT compiling*')
          WHERE thread_name = 'Jit thread pool') > threshold_jit_compiled_methods_count()

        UNION ALL
        SELECT 'Broadcast dispatched count' as slow_cause,
          launch.dur as launch_dur,
          'BROADCAST_DISPATCHED_COUNT' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_broadcast_dispatched_count(),
            'unit', 'COUNT',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', count_slices_concurrent_to_launch(launch.startup_id,
              'Broadcast dispatched*')) as actual_val,
          get_slices_concurrent_to_launch(launch.startup_id, 'Broadcast dispatched*', 3,
            (SELECT pid FROM android_startup_processes WHERE launch.startup_id = startup_id))
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND count_slices_concurrent_to_launch(
          launch.startup_id,
          'Broadcast dispatched*') > threshold_broadcast_dispatched_count()

        UNION ALL
        SELECT 'Broadcast received count' as slow_cause,
          launch.dur as launch_dur,
          'BROADCAST_RECEIVED_COUNT' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', threshold_broadcast_received_count(),
            'unit', 'COUNT',
            'higher_expected', FALSE) as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', count_slices_concurrent_to_launch(launch.startup_id,
              'broadcastReceiveReg*')) as actual_val,
          get_slices_concurrent_to_launch(launch.startup_id, 'broadcastReceiveReg*', 3,
            (SELECT pid FROM android_startup_processes WHERE launch.startup_id = startup_id))
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND count_slices_concurrent_to_launch(
            launch.startup_id,
            'broadcastReceiveReg*') > threshold_broadcast_received_count()

        UNION ALL
        SELECT 'Startup running concurrent to launch' as slow_cause,
          launch.dur as launch_dur,
          'STARTUP_RUNNING_CONCURRENT' as reason_id,
          'ERROR' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          NULL as trace_slices,
          NULL as trace_threads,
          'Package: ' || (
            SELECT package
            FROM android_startups l
            WHERE l.startup_id != launch.startup_id
              AND _is_spans_overlapping(l.ts, l.ts_end, launch.ts, launch.ts_end)
              LIMIT 1) as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND EXISTS(
          SELECT package
          FROM android_startups l
          WHERE l.startup_id != launch.startup_id
            AND _is_spans_overlapping(l.ts, l.ts_end, launch.ts, launch.ts_end))

        UNION ALL
        SELECT 'Main Thread - Binder transactions blocked' as slow_cause,
          launch.dur as launch_dur,
          'MAIN_THREAD_BINDER_TRANSCATIONS_BLOCKED' as reason_id,
          'WARNING' as severity,
          AndroidStartupMetric_ThresholdValue(
            'value', FALSE,
            'unit', 'TRUE_OR_FALSE') as expected_val,
          AndroidStartupMetric_ActualValue(
            'value', TRUE) as actual_val,
          get_main_thread_binder_transactions_blocked(launch.startup_id, 2e7, 3)
            as trace_slices,
          NULL as trace_threads,
          NULL as extra
        FROM android_startups launch
        WHERE launch.startup_id = $startup_id
          AND (
          SELECT COUNT(1)
          FROM BINDER_TRANSACTION_REPLY_SLICES_FOR_LAUNCH(launch.startup_id, 2e7)) > 0
    );
