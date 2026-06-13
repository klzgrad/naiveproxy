--
-- Copyright 2025 The Android Open Source Project
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

-- View of scheduling slices with extended information.
-- It holds slices with kernel thread scheduling information. These slices are
-- collected when the Linux "ftrace" data source is used with the
-- "sched/switch" and "sched/wakeup*" events enabled.
--
-- The rows in this table will always have a matching row in the |thread_state|
-- table with |thread_state.state| = 'Running'
CREATE PERFETTO VIEW sched_with_thread_process (
  --  Unique identifier for this scheduling slice (Running period).
  id ID(sched.id),
  -- The timestamp at the start of the Running period.
  ts TIMESTAMP,
  -- The duration of the Running period.
  dur DURATION,
  -- Unique identifier of the thread that was running.
  utid JOINID(thread.id),
  -- Name of the thread that was running.
  thread_name STRING,
  -- Unique identifier of the process that the thread belongs to.
  upid JOINID(process.id),
  -- Name of the process that the thread belongs to.
  process_name STRING,
  -- The CPU that the slice executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- A string representing the scheduling state of the kernel thread at the end
  -- of the slice.  The individual characters in the string mean the following:
  -- R (runnable), S (awaiting a wakeup), D (in an uninterruptible sleep), T
  -- (suspended), t (being traced), X (exiting), P (parked), W (waking), I
  -- (idle), N (not contributing to the load average), K (wakeable on fatal
  -- signals) and Z (zombie, awaiting cleanup).
  end_state STRING,
  -- The kernel priority that the thread ran at.
  priority LONG
) AS
SELECT
  sched.id,
  sched.ts,
  sched.dur,
  utid,
  upid,
  thread.name AS thread_name,
  process.name AS process_name,
  sched.cpu,
  sched.end_state,
  sched.priority
FROM sched
JOIN thread
  USING (utid)
LEFT JOIN process
  USING (upid);
