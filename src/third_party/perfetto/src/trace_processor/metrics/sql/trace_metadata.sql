--
-- Copyright 2019 The Android Open Source Project
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
INCLUDE PERFETTO MODULE android.suspend;

DROP VIEW IF EXISTS trace_metadata_output;
CREATE PERFETTO VIEW trace_metadata_output AS
SELECT TraceMetadata(
  'trace_duration_ns', CAST(trace_dur() AS INT),
  'trace_uuid', (SELECT str_value FROM metadata WHERE name = 'trace_uuid' LIMIT 1),
  'android_build_fingerprint', (
    SELECT str_value FROM metadata WHERE name = 'android_build_fingerprint' LIMIT 1
  ),
  'android_incremental_build', (
    SELECT str_value FROM metadata WHERE name = 'android_incremental_build' LIMIT 1
  ),
  'android_device_manufacturer', (
    SELECT str_value FROM metadata WHERE name = 'android_device_manufacturer' LIMIT 1
  ),
  'statsd_triggering_subscription_id', (
    SELECT int_value FROM metadata
    WHERE name = 'statsd_triggering_subscription_id'
    LIMIT 1
  ),
  'unique_session_name', (
    SELECT str_value FROM metadata
    WHERE name = 'unique_session_name'
    LIMIT 1
  ),
  'trace_size_bytes', (
    SELECT int_value FROM metadata
    WHERE name = 'trace_size_bytes'
    LIMIT 1
  ),
  'trace_trigger', (
    SELECT RepeatedField(slice.name)
    FROM track JOIN slice ON track.id = slice.track_id
    WHERE track.name = 'Trace Triggers'
  ),
  'trace_causal_trigger', (
      SELECT str_value FROM metadata
      WHERE name = 'trace_trigger'
      LIMIT 1
  ),
  'trace_config_pbtxt', (
    SELECT str_value FROM metadata
    WHERE name = 'trace_config_pbtxt'
    LIMIT 1
  ),
  'sched_duration_ns', (
    SELECT IFNULL(MAX(TO_MONOTONIC(ts)) - MIN(TO_MONOTONIC(ts)), 0) FROM sched
  ),
  'tracing_started_ns', (
    SELECT int_value FROM metadata
    WHERE name='tracing_started_ns'
    LIMIT 1
  ),
  'android_sdk_version', (
    SELECT int_value FROM metadata
    WHERE name = 'android_sdk_version'
    LIMIT 1
  ),
  'android_profile_boot_classpath', (
    SELECT int_value FROM metadata
    WHERE name = 'android_profile_boot_classpath'
    LIMIT 1
  ),
  'android_profile_system_server', (
    SELECT int_value FROM metadata
    WHERE name = 'android_profile_system_server'
    LIMIT 1
  ),
  'suspend_count', (
    SELECT COUNT() FROM android_suspend_state WHERE power_state = 'suspended'
  ),
  'data_loss_count', (
      SELECT COUNT()
      FROM stats
      WHERE severity = 'data_loss' AND value > 0
  ),
  'error_count', (
      SELECT COUNT()
      FROM stats
      WHERE severity = 'error' AND value > 0
  )
);
