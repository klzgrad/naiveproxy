--
-- Copyright 2023 The Android Open Source Project
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

-- Standardizes an Android thread name by extracting its core identifier to make it
-- possible to aggregate by name.
--
-- Removes extra parts of a thread name, like identifiers, leaving only the main prefix.
-- Splits the name at ('-', '[', ':' , ' ').
--
-- Some Examples:
--   Given thread_name = "RenderThread-1[123]",
--   returns "RenderThread".
--
--   Given thread_name = "binder:5543_E"
--   returns "binder".
--
--   Given thread_name = "pool-3-thread-5",
--   returns "pool".
--
--   Given thread_name = "MainThread",
--   returns "MainThread".
CREATE PERFETTO FUNCTION android_standardize_thread_name(
    -- The full android thread name to be processed.
    thread_name STRING
)
-- Simplified name
RETURNS STRING AS
SELECT
  CASE
    WHEN $thread_name GLOB 'kworker/*'
    THEN 'kworker'
    ELSE __intrinsic_strip_hex(
      str_split(str_split(str_split(str_split($thread_name, "-", 0), "[", 0), ":", 0), " ", 0),
      1
    )
  END;

-- Per <process, thread_name_prefix> stats of threads created in a process
CREATE PERFETTO FUNCTION _android_thread_creation_spam_per_thread(
    -- Maximum duration between creating and destroying a thread before their the
    -- thread creation event is considered. If NULL, considers all thread creations.
    max_thread_dur DOUBLE,
    -- Sliding window duration for counting the thread creations. Each window
    -- starts at the first thread creation per <process, thread_name_prefix>.
    sliding_window_dur DOUBLE
)
RETURNS TABLE (
  -- Process name creating threads.
  process_name STRING,
  -- Unique process pid creating threads.
  upid JOINID(process.id),
  -- String prefix of thread names created.
  thread_name_prefix STRING,
  -- Max number of threads created within a time window.
  max_count_per_sec LONG
) AS
WITH
  x AS (
    SELECT
      upid,
      process.name AS process_name,
      android_standardize_thread_name(thread.name) AS thread_name_prefix,
      count(thread.start_ts) OVER (PARTITION BY upid, android_standardize_thread_name(thread.name) ORDER BY thread.start_ts RANGE BETWEEN CURRENT ROW AND cast_int!($sliding_window_dur) FOLLOWING) AS count
    FROM thread
    JOIN process
      USING (upid)
    WHERE
      (
        $max_thread_dur AND (
          thread.end_ts - thread.start_ts
        ) <= $max_thread_dur
      )
      OR $max_thread_dur IS NULL
  )
SELECT
  process_name,
  upid,
  thread_name_prefix,
  max(count) AS max_count_per_sec
FROM x
GROUP BY
  upid,
  thread_name_prefix
HAVING
  max_count_per_sec > 0
ORDER BY
  count DESC;

-- Per process stats of threads created in a process
CREATE PERFETTO FUNCTION _android_thread_creation_spam_per_process(
    -- Maximum duration between creating and destroying a thread before their
    -- thread creation event is considered. If NULL, considers all thread creations.
    max_thread_dur DOUBLE,
    -- Sliding window duration for counting the thread creations. Each window
    -- starts at the first thread creation per <process>.
    sliding_window_dur DOUBLE
)
RETURNS TABLE (
  -- Process name creating threads.
  process_name STRING,
  -- Unique process pid creating threads.
  upid JOINID(process.id),
  -- Max number of threads created within a time window.
  max_count_per_sec LONG
) AS
WITH
  x AS (
    SELECT
      upid,
      process.name AS process_name,
      count(thread.start_ts) OVER (PARTITION BY upid ORDER BY thread.start_ts RANGE BETWEEN CURRENT ROW AND cast_int!($sliding_window_dur) FOLLOWING) AS count
    FROM thread
    JOIN process
      USING (upid)
    WHERE
      (
        $max_thread_dur AND (
          thread.end_ts - thread.start_ts
        ) <= $max_thread_dur
      )
      OR $max_thread_dur IS NULL
  )
SELECT
  process_name,
  upid,
  max(count) AS max_count_per_sec
FROM x
GROUP BY
  upid
HAVING
  max_count_per_sec > 0
ORDER BY
  count DESC;
