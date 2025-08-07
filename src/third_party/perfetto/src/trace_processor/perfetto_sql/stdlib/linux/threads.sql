--
-- Copyright 2024 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- All kernel threads of the trace. As kernel threads are processes, provides
-- also process data.
CREATE PERFETTO TABLE linux_kernel_threads (
  -- Upid of kernel thread. Alias of |process.upid|.
  upid JOINID(process.id),
  -- Utid of kernel thread. Alias of |thread.utid|.
  utid JOINID(thread.id),
  -- Pid of kernel thread. Alias of |process.pid|.
  pid LONG,
  -- Tid of kernel thread. Alias of |process.pid|.
  tid LONG,
  -- Name of kernel process. Alias of |process.name|.
  process_name STRING,
  -- Name of kernel thread. Alias of |thread.name|.
  thread_name STRING,
  -- Machine id of kernel thread. If NULL then it's a single machine trace.
  -- Alias of |process.machine_id|.
  machine_id LONG
) AS
WITH
  pid_2 AS (
    SELECT
      upid,
      pid,
      name,
      machine_id
    FROM process
    WHERE
      pid = 2
  ),
  parent_pid_2 AS (
    SELECT
      p.upid,
      p.pid,
      p.name,
      p.machine_id
    FROM process AS p
    JOIN pid_2
      ON p.parent_upid = pid_2.upid
  )
SELECT
  upid,
  utid,
  pid,
  tid,
  p.name AS process_name,
  t.name AS thread_name,
  p.machine_id
FROM (
  SELECT
    *
  FROM parent_pid_2
  UNION
  SELECT
    *
  FROM pid_2
) AS p
JOIN thread AS t
  USING (upid);
