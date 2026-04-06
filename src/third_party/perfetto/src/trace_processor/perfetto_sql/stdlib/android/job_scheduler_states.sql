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

INCLUDE PERFETTO MODULE android.battery.charging_states;

INCLUDE PERFETTO MODULE android.screen_state;

INCLUDE PERFETTO MODULE intervals.intersect;

CREATE PERFETTO TABLE _job_states AS
SELECT
  t.id AS track_id,
  s.ts,
  s.id AS slice_id,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.job_name') AS job_name,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.attribution_node[0].uid') AS uid,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.state') AS state,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.internal_stop_reason') AS internal_stop_reason,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.public_stop_reason') AS public_stop_reason,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.effective_priority') AS effective_priority,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_battery_not_low_constraint') AS has_battery_not_low_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_charging_constraint') AS has_charging_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_connectivity_constraint') AS has_connectivity_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_content_trigger_constraint') AS has_content_trigger_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_deadline_constraint') AS has_deadline_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_idle_constraint') AS has_idle_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_storage_not_low_constraint') AS has_storage_not_low_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.has_timing_delay_constraint') AS has_timing_delay_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_prefetch') = 1 AS is_prefetch,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_requested_expedited_job') AS is_requested_expedited_job,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_running_as_expedited_job') AS is_running_as_expedited_job,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.job_id') AS job_id,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.num_previous_attempts') AS num_previous_attempts,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.requested_priority') AS requested_priority,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.standby_bucket') AS standby_bucket,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_periodic') AS is_periodic,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_periodic') AS has_flex_constraint,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_requested_as_user_initiated_job') AS is_requested_as_user_initiated_job,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.is_running_as_user_initiated_job') AS is_running_as_user_initiated_job,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.deadline_ms') AS deadline_ms,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.job_start_latency_ms') AS job_start_latency_ms,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.num_uncompleted_work_items') AS num_uncompleted_work_items,
  extract_arg(arg_set_id, 'scheduled_job_state_changed.proc_state') AS proc_state
FROM track AS t
JOIN slice AS s
  ON (
    s.track_id = t.id
  )
WHERE
  t.name = 'Statsd Atoms' AND s.name = 'scheduled_job_state_changed';

CREATE PERFETTO TABLE _job_started AS
WITH
  cte AS (
    SELECT
      *,
      lead(state, 1) OVER (PARTITION BY uid, job_name, job_id ORDER BY uid, job_name, job_id, ts) AS lead_state,
      lead(ts, 1, trace_end()) OVER (PARTITION BY uid, job_name, job_id ORDER BY uid, job_name, job_id, ts) AS ts_lead,
      --- Filter out statsd lossy issue.
      lead(ts, 1) OVER (PARTITION BY uid, job_name, job_id ORDER BY uid, job_name, job_id, ts) IS NULL AS is_end_slice,
      lead(internal_stop_reason, 1, 'INTERNAL_STOP_REASON_UNKNOWN') OVER (PARTITION BY uid, job_name, job_id ORDER BY uid, job_name, job_id, ts) AS lead_internal_stop_reason,
      lead(public_stop_reason, 1, 'PUBLIC_STOP_REASON_UNKNOWN') OVER (PARTITION BY uid, job_name, job_id ORDER BY uid, job_name, job_id, ts) AS lead_public_stop_reason
    FROM _job_states
    WHERE
      state != 'CANCELLED'
  )
SELECT
  -- Job name is based on whether the tag and/or namespace are present:
  -- 1. Both tag and namespace are present: @<namespace>@<tag>:<package name>
  -- 2. Only tag is present:  <tag>:<package name>
  -- 3. Only namespace is present: @<namespace>@<package name>/<class name>
  CASE
    WHEN substr(job_name, 1, 1) = '@'
    THEN CASE
      WHEN substr(str_split(job_name, '/', 1), 1, 3) = 'com'
      THEN str_split(job_name, '/', 1)
      ELSE str_split(str_split(job_name, '/', 0), '@', 2)
    END
    ELSE str_split(job_name, '/', 0)
  END AS package_name,
  CASE
    WHEN substr(job_name, 1, 1) = '@'
    THEN str_split(job_name, '@', 1)
    ELSE str_split(job_name, '/', 1)
  END AS job_namespace,
  ts_lead - ts AS dur,
  iif(lead_state = 'SCHEDULED', TRUE, FALSE) AS is_rescheduled,
  *
FROM cte
WHERE
  is_end_slice = FALSE
  AND (
    ts_lead - ts
  ) > 0
  AND state = 'STARTED'
  AND lead_state IN ('FINISHED', 'SCHEDULED');

CREATE PERFETTO TABLE _charging_screen_states AS
SELECT
  row_number() OVER () AS id,
  ii.ts,
  ii.dur,
  c.charging_state,
  s.screen_state
FROM _interval_intersect!(
  (android_charging_states, android_screen_state),
  ()
) AS ii
JOIN android_charging_states AS c
  ON c.id = ii.id_0
JOIN android_screen_state AS s
  ON s.id = ii.id_1;

-- This table returns constraint changes that a
-- job will go through in a single trace.
--
-- Values in this table are derived from the the `ScheduledJobStateChanged`
-- atom. This table differs from the
-- `android_job_scheduler_with_screen_charging_states` in this module
-- (`android.job_scheduler_states`) by only having job constraint information.
--
-- See documentation for the `android_job_scheduler_with_screen_charging_states`
-- for how tables in this module differ from `android_job_scheduler_events`
-- table in the `android.job_scheduler` module and how to populate this table.
CREATE PERFETTO TABLE android_job_scheduler_states (
  -- Unique identifier for job scheduler state.
  id ID,
  -- Timestamp of job state slice.
  ts TIMESTAMP,
  -- Duration of job state slice.
  dur DURATION,
  -- Id of the slice.
  slice_id JOINID(slice.id),
  -- Name of the job (as named by the app).
  job_name STRING,
  -- Uid associated with job.
  uid LONG,
  -- Id of job (assigned by app for T- builds and system generated in U+
  -- builds).
  job_id LONG,
  -- Package that the job belongs (ex: associated app).
  package_name STRING,
  -- Namespace of job.
  job_namespace STRING,
  -- Priority at which JobScheduler ran the job.
  effective_priority LONG,
  -- True if app requested job should run when the device battery is not low.
  has_battery_not_low_constraint BOOL,
  -- True if app requested job should run when the device is charging.
  has_charging_constraint BOOL,
  -- True if app requested job should run when device has connectivity.
  has_connectivity_constraint BOOL,
  -- True if app requested job should run when there is a content trigger.
  has_content_trigger_constraint BOOL,
  -- True if app requested there is a deadline by which the job should run.
  has_deadline_constraint BOOL,
  -- True if app requested job should run when device is idle.
  has_idle_constraint BOOL,
  -- True if app requested job should run when device storage is not low.
  has_storage_not_low_constraint BOOL,
  -- True if app requested job has a timing delay.
  has_timing_delay_constraint BOOL,
  -- True if app requested job should run within hours of app launch.
  is_prefetch BOOL,
  -- True if app requested that the job is run as an expedited job.
  is_requested_expedited_job BOOL,
  -- The job is run as an expedited job.
  is_running_as_expedited_job BOOL,
  -- Number of previous attempts at running job.
  num_previous_attempts TIMESTAMP,
  -- The requested priority at which the job should run.
  requested_priority LONG,
  -- The job's standby bucket (one of: Active, Working Set, Frequent, Rare,
  -- Never, Restricted, Exempt).
  standby_bucket STRING,
  -- Job should run in intervals.
  is_periodic BOOL,
  -- True if the job should run as a flex job.
  has_flex_constraint BOOL,
  -- True is app has requested that a job be run as a user initiated job.
  is_requested_as_user_initiated_job BOOL,
  -- True if job is running as a user initiated job.
  is_running_as_user_initiated_job BOOL,
  -- Deadline that job has requested and valid if has_deadline_constraint is
  -- true.
  deadline_ms LONG,
  -- The latency in ms between when a job is scheduled and when it actually
  -- starts.
  job_start_latency_ms LONG,
  -- Number of uncompleted job work items.
  num_uncompleted_work_items LONG,
  -- Process state of the process responsible for running the job.
  proc_state STRING,
  -- Internal stop reason for a job.
  internal_stop_reason STRING,
  -- Public stop reason for a job.
  public_stop_reason STRING
) AS
SELECT
  row_number() OVER (ORDER BY ts) AS id,
  ts,
  dur,
  slice_id,
  job_name,
  uid,
  job_id,
  package_name,
  job_namespace,
  effective_priority,
  has_battery_not_low_constraint,
  has_charging_constraint,
  has_connectivity_constraint,
  has_content_trigger_constraint,
  has_deadline_constraint,
  has_idle_constraint,
  has_storage_not_low_constraint,
  has_timing_delay_constraint,
  is_prefetch,
  is_requested_expedited_job,
  is_running_as_expedited_job,
  num_previous_attempts,
  requested_priority,
  standby_bucket,
  is_periodic,
  has_flex_constraint,
  is_requested_as_user_initiated_job,
  is_running_as_user_initiated_job,
  deadline_ms,
  job_start_latency_ms,
  num_uncompleted_work_items,
  proc_state,
  lead_internal_stop_reason AS internal_stop_reason,
  lead_public_stop_reason AS public_stop_reason
FROM _job_started;

-- This table returns the constraint, charging,
-- and screen state changes that a job will go through
-- in a single trace.
--
-- Values from this table are derived from
-- the `ScheduledJobStateChanged` atom. This differs from the
-- `android_job_scheduler_events` table in the `android.job_scheduler` module
-- which is derived from ATrace the system server category
-- (`atrace_categories: "ss"`).
--
-- This also differs from the `android_job_scheduler_states` in this module
-- (`android.job_scheduler_states`) by providing charging and screen state
-- changes.
--
-- To populate this table, enable the Statsd Tracing Config with the
-- ATOM_SCHEDULED_JOB_STATE_CHANGED push atom id.
-- https://perfetto.dev/docs/reference/trace-config-proto#StatsdTracingConfig
--
-- This table is preferred over `android_job_scheduler_events`
-- since it contains more information and should be used whenever
-- `ATOM_SCHEDULED_JOB_STATE_CHANGED` is available in a trace.
CREATE PERFETTO TABLE android_job_scheduler_with_screen_charging_states (
  -- Timestamp of job.
  ts TIMESTAMP,
  -- Duration of slice in ns.
  dur DURATION,
  -- Id of the slice.
  slice_id JOINID(slice.id),
  -- Name of the job (as named by the app).
  job_name STRING,
  -- Id of job (assigned by app for T- builds and system generated in U+
  -- builds).
  job_id LONG,
  -- Uid associated with job.
  uid LONG,
  -- Duration of entire job in ns.
  job_dur DURATION,
  -- Package that the job belongs (ex: associated app).
  package_name STRING,
  -- Namespace of job.
  job_namespace STRING,
  -- Device charging state during job (one of: Charging, Discharging, Not charging,
  -- Full, Unknown).
  charging_state STRING,
  -- Device screen state during job (one of: Screen off, Screen on, Always-on display
  -- (doze), Unknown).
  screen_state STRING,
  -- Priority at which JobScheduler ran the job.
  effective_priority LONG,
  -- True if app requested job should run when the device battery is not low.
  has_battery_not_low_constraint BOOL,
  -- True if app requested job should run when the device is charging.
  has_charging_constraint BOOL,
  -- True if app requested job should run when device has connectivity.
  has_connectivity_constraint BOOL,
  -- True if app requested job should run when there is a content trigger.
  has_content_trigger_constraint BOOL,
  -- True if app requested there is a deadline by which the job should run.
  has_deadline_constraint BOOL,
  -- True if app requested job should run when device is idle.
  has_idle_constraint BOOL,
  -- True if app requested job should run when device storage is not low.
  has_storage_not_low_constraint BOOL,
  -- True if app requested job has a timing delay.
  has_timing_delay_constraint BOOL,
  -- True if app requested job should run within hours of app launch.
  is_prefetch BOOL,
  -- True if app requested that the job is run as an expedited job.
  is_requested_expedited_job BOOL,
  -- The job is run as an expedited job.
  is_running_as_expedited_job BOOL,
  -- Number of previous attempts at running job.
  num_previous_attempts TIMESTAMP,
  -- The requested priority at which the job should run.
  requested_priority LONG,
  -- The job's standby bucket (one of: Active, Working Set, Frequent, Rare,
  -- Never, Restricted, Exempt).
  standby_bucket STRING,
  -- Job should run in intervals.
  is_periodic BOOL,
  -- True if the job should run as a flex job.
  has_flex_constraint BOOL,
  -- True is app has requested that a job be run as a user initiated job.
  is_requested_as_user_initiated_job BOOL,
  -- True if job is running as a user initiated job.
  is_running_as_user_initiated_job BOOL,
  -- Deadline that job has requested and valid if has_deadline_constraint is
  -- true.
  deadline_ms LONG,
  -- The latency in ms between when a job is scheduled and when it actually
  -- starts.
  job_start_latency_ms LONG,
  -- Number of uncompleted job work items.
  num_uncompleted_work_items LONG,
  -- Process state of the process responsible for running the job.
  proc_state STRING,
  -- Internal stop reason for a job.
  internal_stop_reason STRING,
  -- Public stop reason for a job.
  public_stop_reason STRING
) AS
SELECT
  ii.ts,
  ii.dur,
  js.slice_id,
  js.job_name || '_' || js.job_id AS job_name,
  js.uid,
  js.job_id,
  js.dur AS job_dur,
  js.package_name,
  js.job_namespace,
  c.charging_state,
  c.screen_state,
  js.effective_priority,
  js.has_battery_not_low_constraint,
  js.has_charging_constraint,
  js.has_connectivity_constraint,
  js.has_content_trigger_constraint,
  js.has_deadline_constraint,
  js.has_idle_constraint,
  js.has_storage_not_low_constraint,
  js.has_timing_delay_constraint,
  js.is_prefetch,
  js.is_requested_expedited_job,
  js.is_running_as_expedited_job,
  js.num_previous_attempts,
  js.requested_priority,
  js.standby_bucket,
  js.is_periodic,
  js.has_flex_constraint,
  js.is_requested_as_user_initiated_job,
  js.is_running_as_user_initiated_job,
  js.deadline_ms,
  js.job_start_latency_ms,
  js.num_uncompleted_work_items,
  js.proc_state,
  js.internal_stop_reason,
  js.public_stop_reason
FROM _interval_intersect!(
        (_charging_screen_states,
        android_job_scheduler_states),
        ()
      ) AS ii
JOIN _charging_screen_states AS c
  ON c.id = ii.id_0
JOIN android_job_scheduler_states AS js
  ON js.id = ii.id_1;
