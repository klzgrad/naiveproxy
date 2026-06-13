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

SELECT RUN_METRIC('android/process_metadata.sql');

INCLUDE PERFETTO MODULE android.slices;
INCLUDE PERFETTO MODULE android.binder;
INCLUDE PERFETTO MODULE android.critical_blocking_calls;

DROP TABLE IF EXISTS process_info;
CREATE TABLE process_info AS
SELECT
  process.upid AS upid,
  process.name AS process_name,
  process_metadata.metadata AS process_metadata
FROM process
JOIN process_metadata USING (upid);

DROP TABLE IF EXISTS android_blocking_calls_unagg_calls;
CREATE TABLE android_blocking_calls_unagg_calls AS
SELECT
  name,
  COUNT(*) AS occurrences,
  MAX(dur) AS max_dur_ns,
  MIN(dur) AS min_dur_ns,
  SUM(dur) AS total_dur_ns,
  AVG(dur) AS avg_dur_ns,
  upid,
  process_name
FROM
  _android_critical_blocking_calls
GROUP BY name, upid, process_name;

DROP TABLE IF EXISTS filtered_processes_with_non_zero_blocking_calls;
CREATE TABLE filtered_processes_with_non_zero_blocking_calls AS
SELECT pi.upid,
  pi.process_name,
  pi.process_metadata
FROM process_info pi WHERE pi.upid IN
  (SELECT DISTINCT upid FROM _android_critical_blocking_calls);


DROP TABLE IF EXISTS filtered_processes_with_non_zero_blocking_calls;
CREATE TABLE filtered_processes_with_non_zero_blocking_calls AS
SELECT pi.upid,
  pi.process_name,
  pi.process_metadata
FROM process_info pi WHERE pi.upid IN
  (SELECT DISTINCT upid FROM _android_critical_blocking_calls);

DROP VIEW IF EXISTS android_blocking_calls_unagg_output;
CREATE PERFETTO VIEW android_blocking_calls_unagg_output AS
SELECT AndroidBlockingCallsUnagg(
  'process_with_blocking_calls', (
     SELECT RepeatedField(
       AndroidBlockingCallsUnagg_ProcessWithBlockingCalls(
         'process', e.process_metadata,
         'blocking_calls', (
            SELECT RepeatedField(
              AndroidBlockingCall(
                'name', d.name,
                'cnt', d.occurrences,
                'avg_dur_ms', CAST(avg_dur_ns / 1e6 AS INT),
                'total_dur_ms', CAST(total_dur_ns / 1e6 AS INT),
                'max_dur_ms', CAST(max_dur_ns / 1e6 AS INT),
                'min_dur_ms', CAST(min_dur_ns / 1e6 AS INT),
                'avg_dur_ns', CAST(d.avg_dur_ns AS INT),
                'total_dur_ns', d.total_dur_ns,
                'max_dur_ns', d.max_dur_ns,
                'min_dur_ns', d.min_dur_ns
              )
            ) FROM (
            SELECT b.name,
              b.occurrences,
              b.avg_dur_ns,
              b.total_dur_ns,
              b.max_dur_ns,
              b.min_dur_ns
            FROM android_blocking_calls_unagg_calls b INNER JOIN filtered_processes_with_non_zero_blocking_calls c
            ON b.upid = c.upid WHERE b.upid = e.upid
            ORDER BY total_dur_ns DESC
            ) d
         )
       )
     )
     FROM filtered_processes_with_non_zero_blocking_calls e
  )
);
