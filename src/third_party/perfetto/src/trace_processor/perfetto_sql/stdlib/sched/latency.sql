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

INCLUDE PERFETTO MODULE sched.runnable;

CREATE PERFETTO VIEW _sched_with_thread_state_join AS
SELECT
  thread_state.id AS thread_state_id,
  sched.id AS sched_id
FROM sched
JOIN thread_state
  USING (utid, ts, dur);

-- Scheduling latency of running thread states.
-- For each time the thread was running, returns the duration of the runnable
-- state directly before.
CREATE PERFETTO TABLE sched_latency_for_running_interval (
  -- Running state of the thread.
  thread_state_id JOINID(thread_state.id),
  -- Id of a corresponding slice in a `sched` table.
  sched_id JOINID(sched.id),
  -- Thread with running state.
  utid JOINID(thread.id),
  -- Runnable state before thread is "running". Duration of this thread state
  -- is `latency_dur`. One of `thread_state.id`.
  runnable_latency_id JOINID(thread_state.id),
  -- Scheduling latency of thread state. Duration of thread state with
  -- `runnable_latency_id`.
  latency_dur LONG
) AS
SELECT
  r.id AS thread_state_id,
  sched_id,
  utid,
  prev_runnable_id AS runnable_latency_id,
  dur AS latency_dur
FROM sched_previous_runnable_on_thread AS r
JOIN thread_state AS prev_ts
  ON prev_runnable_id = prev_ts.id
JOIN _sched_with_thread_state_join
  ON thread_state_id = r.id;
