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

-- Params:
-- @target_duration_ms: find stack samples on tasks that are longer than
-- this value

-- @thread_name: thread name to look for stack samples on

-- @task_name: a task name following chrome_tasks.sql naming convention to
-- find stack samples on.

INCLUDE PERFETTO MODULE chrome.tasks;

CREATE OR REPLACE PERFETTO FUNCTION describe_symbol(symbol STRING, frame_name STRING)
RETURNS STRING AS
SELECT COALESCE($symbol,
  CASE WHEN demangle($frame_name) IS NULL
  THEN $frame_name
  ELSE demangle($frame_name)
  END);

-- Get all Chrome tasks that match a specific name on a specific thread.
-- The timestamps for those tasks are going to be used later on to gather
-- information about stack traces that were collected during running them.
DROP VIEW IF EXISTS chrome_targeted_task;
CREATE PERFETTO VIEW chrome_targeted_task AS
SELECT
  chrome_tasks.full_name AS full_name,
  chrome_tasks.dur AS dur,
  chrome_tasks.ts AS ts,
  chrome_tasks.id AS id,
  utid AS utid
FROM
  chrome_tasks
WHERE
  chrome_tasks.dur >= {{target_duration_ms}} * 1e6
  AND chrome_tasks.thread_name = {{thread_name}}
  AND chrome_tasks.full_name = {{task_name}};


-- Get all frames attached to callsite ids, as frames can be
-- reused between stack frames, callsite ids are unique per
-- stack sample.
DROP VIEW IF EXISTS chrome_non_symbolized_frames;
CREATE PERFETTO VIEW chrome_non_symbolized_frames AS
SELECT
  frames.name AS frame_name,
  callsite.id AS callsite_id,
  *
FROM
  stack_profile_frame frames
JOIN stack_profile_callsite callsite
  ON callsite.frame_id = frames.id;

-- Only lowest child frames are join-able with chrome_non_symbolized_frames
-- which we need for the time at which the callstack was taken.
DROP VIEW IF EXISTS chrome_symbolized_child_frames;
CREATE PERFETTO VIEW chrome_symbolized_child_frames AS
SELECT
  thread.name AS thread_name,
  sample.utid AS sample_utid,
  *
FROM
  chrome_non_symbolized_frames frames
JOIN cpu_profile_stack_sample sample USING(callsite_id)
JOIN thread USING(utid)
JOIN process USING(upid);

-- Not all frames are symbolized, in cases where those frames
-- are not symbolized, use the file name as it is usually descriptive
-- of the function.
DROP VIEW IF EXISTS chrome_thread_symbolized_child_frames;
CREATE PERFETTO VIEW chrome_thread_symbolized_child_frames AS
SELECT
  describe_symbol(symbol.name, frame_name) AS description,
  depth,
  ts,
  callsite_id,
  sample_utid
FROM chrome_symbolized_child_frames
LEFT JOIN stack_profile_symbol symbol USING(symbol_set_id)
WHERE thread_name = {{thread_name}} ORDER BY ts DESC;

-- Since only leaf stack frames have a timestamp, let's export this
-- timestamp to all it's ancestor frames to use it later on for
-- filtering frames within specific windows
DROP VIEW IF EXISTS chrome_non_symbolized_frames_timed;
CREATE PERFETTO VIEW chrome_non_symbolized_frames_timed AS
SELECT
  chrome_non_symbolized_frames.frame_name,
  chrome_non_symbolized_frames.depth,
  chrome_thread_symbolized_child_frames.ts,
  chrome_thread_symbolized_child_frames.sample_utid,
  chrome_non_symbolized_frames.callsite_id,
  symbol_set_id,
  chrome_non_symbolized_frames.frame_id
FROM chrome_thread_symbolized_child_frames
JOIN experimental_ancestor_stack_profile_callsite(
    chrome_thread_symbolized_child_frames.callsite_id) child
JOIN chrome_non_symbolized_frames
  ON chrome_non_symbolized_frames.callsite_id = child.id;

DROP VIEW IF EXISTS chrome_frames_timed_and_symbolized;
CREATE PERFETTO VIEW chrome_frames_timed_and_symbolized AS
SELECT
  describe_symbol(symbol.name, frame_name) AS description,
  ts,
  depth,
  callsite_id,
  sample_utid
FROM chrome_non_symbolized_frames_timed
LEFT JOIN stack_profile_symbol symbol
  USING(symbol_set_id)
ORDER BY DEPTH ASC;

-- Union leaf stack frames with all stack frames after the timestamp
-- is attached to get a view of all frames timestamped.
DROP VIEW IF EXISTS all_frames;
CREATE PERFETTO VIEW all_frames AS
SELECT
  *
FROM
  (SELECT
    * FROM
    chrome_frames_timed_and_symbolized
    UNION
    SELECT
      description,
      ts,
      depth,
      callsite_id,
      sample_utid
    FROM chrome_thread_symbolized_child_frames)
ORDER BY depth ASC;

-- Filter stack samples that happened only during the specified
-- task on the specified thread.
DROP VIEW IF EXISTS chrome_stack_samples_for_task;
CREATE PERFETTO VIEW chrome_stack_samples_for_task AS
SELECT
  all_frames.*
FROM
  all_frames JOIN
  chrome_targeted_task ON
    all_frames.sample_utid = chrome_targeted_task.utid
    AND all_frames.ts >= chrome_targeted_task.ts
    AND all_frames.ts <= chrome_targeted_task.ts + chrome_targeted_task.dur;
