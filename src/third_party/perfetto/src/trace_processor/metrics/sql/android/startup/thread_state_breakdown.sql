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

DROP VIEW IF EXISTS thread_state_extended;
CREATE PERFETTO VIEW thread_state_extended AS
SELECT
  ts,
  IIF(dur = -1, trace_end(), dur) AS dur,
  utid,
  state,
  io_wait
FROM thread_state;

DROP TABLE IF EXISTS launch_threads_by_thread_state;
CREATE VIRTUAL TABLE launch_threads_by_thread_state
USING SPAN_JOIN(
  android_startup_threads PARTITIONED utid,
  thread_state_extended PARTITIONED utid
);

-- Materialized to avoid repeatedly span joining per each thread state.
DROP TABLE IF EXISTS launch_thread_state_io_wait_dur_sum;
CREATE PERFETTO TABLE launch_thread_state_io_wait_dur_sum AS
SELECT startup_id, state, is_main_thread, thread_name, io_wait, SUM(dur) AS dur
FROM launch_threads_by_thread_state l
JOIN android_startup_processes p USING (startup_id)
WHERE
  -- If it is a main thread, only add it if it is the lauching thread.
  (is_main_thread AND p.startup_type NOT NULL)
  -- Allowlist specific threads which need this. Do not add to this list
  -- without careful consideration as every thread added here can cause
  -- memory usage to balloon.
  OR thread_name IN (
    'Jit thread pool'
  )
GROUP BY 1, 2, 3, 4, 5;

DROP VIEW IF EXISTS launch_thread_state_dur_sum;
CREATE PERFETTO VIEW launch_thread_state_dur_sum AS
SELECT startup_id, state, is_main_thread, thread_name, SUM(dur) AS dur
FROM launch_thread_state_io_wait_dur_sum
GROUP BY 1, 2, 3, 4;

-- Given a launch id and thread state value, returns the aggregate sum
-- of time spent in that state by the main thread of the process being started up.
CREATE OR REPLACE PERFETTO FUNCTION main_thread_time_for_launch_and_state(startup_id INT, state STRING)
RETURNS INT AS
SELECT SUM(dur)
FROM launch_thread_state_dur_sum l
WHERE l.startup_id = $startup_id AND state GLOB $state AND is_main_thread;

-- Given a launch id, returns the aggregate sum of time spent in runnable state
-- by the main thread of the process being started up.
CREATE OR REPLACE PERFETTO FUNCTION main_thread_time_for_launch_in_runnable_state(startup_id INT)
RETURNS INT AS
SELECT IFNULL(main_thread_time_for_launch_and_state($startup_id, "R"), 0)
      + IFNULL(main_thread_time_for_launch_and_state($startup_id, "R+"), 0);

-- Given a launch id, thread state  and io_wait value, returns the aggregate sum
-- of time spent in that state by the main thread of the process being started up.
CREATE OR REPLACE PERFETTO FUNCTION main_thread_time_for_launch_state_and_io_wait(startup_id INT, state STRING, io_wait BOOL)
RETURNS INT AS
SELECT SUM(dur)
FROM launch_thread_state_io_wait_dur_sum l
WHERE l.startup_id = $startup_id AND state GLOB $state
  AND is_main_thread AND l.io_wait = $io_wait;


-- Given a launch id, thread state value and name of a thread, returns the aggregate sum
-- of time spent in that state by that thread. Note: only threads of the processes
-- being started are considered by this function - if a thread from a different name
-- happens to match the name passed, it will *not* be included.
CREATE OR REPLACE PERFETTO FUNCTION thread_time_for_launch_state_and_thread(startup_id INT, state STRING, thread_name STRING)
RETURNS INT AS
SELECT SUM(dur)
FROM launch_thread_state_dur_sum l
WHERE l.startup_id = $startup_id AND state GLOB $state AND thread_name = $thread_name;


-- Given a launch id, returns the duration between the launch and a running state thread of
-- startup process.
CREATE OR REPLACE PERFETTO FUNCTION time_to_running_state_for_launch(startup_id LONG)
RETURNS PROTO AS
  SELECT NULL_IF_EMPTY(
    STARTUP_SLICE_PROTO(
      IIF(MIN(l.ts) > launches.ts, MIN(l.ts) - launches.ts, NULL)))
  FROM launch_threads_by_thread_state l
  JOIN android_startups launches USING(startup_id)
  WHERE l.startup_id = $startup_id AND l.state = "Running";
