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

-- TODO(altimin): `sched_humanly_readable_name` doesn't handle some corner
-- cases which thread_state.ts handles (as complex strings manipulations in
-- SQL are pretty painful), but they are pretty niche.

-- Translates a single-letter scheduling state to a human-readable string.
CREATE PERFETTO FUNCTION sched_state_to_human_readable_string(
    -- An individual character string representing the scheduling state of the
    -- kernel thread at the end of the slice.
    short_name STRING
)
-- Humanly readable string representing the scheduling state of the kernel
-- thread. The individual characters in the string mean the following: R
-- (runnable), S (awaiting a wakeup), D (in an uninterruptible sleep), T
-- (suspended), t (being traced), X (exiting), P (parked), W (waking), I
-- (idle), N (not contributing to the load average), K (wakeable on fatal
-- signals) and Z (zombie, awaiting cleanup).
RETURNS STRING AS
SELECT
  CASE $short_name
    WHEN 'Running'
    THEN 'Running'
    WHEN 'R'
    THEN 'Runnable'
    WHEN 'R+'
    THEN 'Runnable (Preempted)'
    WHEN 'S'
    THEN 'Sleeping'
    WHEN 'D'
    THEN 'Uninterruptible Sleep'
    WHEN 'T'
    THEN 'Stopped'
    WHEN 't'
    THEN 'Traced'
    WHEN 'X'
    THEN 'Exit (Dead)'
    WHEN 'Z'
    THEN 'Exit (Zombie)'
    WHEN 'x'
    THEN 'Task Dead'
    WHEN 'I'
    THEN 'Idle'
    WHEN 'K'
    THEN 'Wakekill'
    WHEN 'W'
    THEN 'Waking'
    WHEN 'P'
    THEN 'Parked'
    WHEN 'N'
    THEN 'No Load'
    -- ETW SPECIFIC STATES
    WHEN 'Stand By'
    THEN 'Stand By'
    WHEN 'Initialized'
    THEN 'Initialized'
    WHEN 'Waiting'
    THEN 'Waiting'
    WHEN 'Transition'
    THEN 'Transition'
    WHEN 'Deferred Ready'
    THEN 'Deferred Ready'
    ELSE $short_name
  END;

-- Translates a single-letter scheduling state and IO wait information to
-- a human-readable string.
CREATE PERFETTO FUNCTION sched_state_io_to_human_readable_string(
    -- An individual character string representing the scheduling state of the
    -- kernel thread at the end of the slice.
    sched_state STRING,
    -- A (posssibly NULL) boolean indicating, if the device was in uninterruptible
    -- sleep, if it was an IO sleep.
    io_wait BOOL
)
-- A human readable string with information about the scheduling state and IO wait.
RETURNS STRING AS
SELECT
  printf(
    '%s%s',
    sched_state_to_human_readable_string($sched_state),
    CASE $io_wait WHEN 1 THEN ' (IO)' WHEN 0 THEN ' (non-IO)' ELSE '' END
  );
