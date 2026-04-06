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

-- All scheduled jobs and their latencies.
--
-- The table is populated by ATrace using the system server ATrace category
-- (`atrace_categories: "ss"`). You can also set the `atrace_apps` of interest.
--
-- This differs from the `android_job_scheduler_states` table
-- in the `android.job_scheduler_states` module which is populated
-- by the `ScheduledJobStateChanged` atom.
--
-- Using `android_job_scheduler_states` is preferred when the
-- `ATOM_SCHEDULED_JOB_STATE_CHANGED` is available in the trace since
-- it includes the constraint, screen, or charging state changes for
-- each job in a trace.
CREATE PERFETTO TABLE android_job_scheduler_events (
  -- Id of the scheduled job assigned by the app developer.
  job_id LONG,
  -- Uid of the process running the scheduled job.
  uid LONG,
  -- Package name of the process running the scheduled job.
  package_name STRING,
  -- Service component name of the scheduled job.
  job_service_name STRING,
  -- Thread track id of the job scheduler event slice.
  track_id JOINID(track.id),
  -- Slice id of the job scheduler event slice.
  id LONG,
  -- Timestamp the job was scheduled.
  ts TIMESTAMP,
  -- Duration of the scheduled job.
  dur DURATION
) AS
SELECT
  cast_int!(STR_SPLIT(slice.name, '#', 1)) AS job_id,
  cast_int!(STR_SPLIT(STR_SPLIT(slice.name, '<', 1), '>', 0)) AS uid,
  str_split(str_split(slice.name, '>', 1), '/', 0) AS package_name,
  str_split(str_split(slice.name, '/', 1), '#', 0) AS job_service_name,
  track_id,
  slice.id,
  slice.ts,
  iif(slice.dur = -1, trace_end() - slice.ts, slice.dur) AS dur
FROM slice
JOIN process_track
  ON slice.track_id = process_track.id
JOIN process
  ON process.upid = process_track.upid
WHERE
  process.name = 'system_server'
  AND slice.name GLOB '*job*'
  AND process_track.name = 'JobScheduler';
