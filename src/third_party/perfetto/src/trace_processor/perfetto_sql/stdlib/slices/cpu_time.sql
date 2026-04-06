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

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE linux.cpu.utilization.slice;

INCLUDE PERFETTO MODULE slices.with_context;

-- Time each thread slice spent running on CPU.
-- Requires scheduling data to be available in the trace.
CREATE PERFETTO TABLE thread_slice_cpu_time (
  -- Slice.
  id JOINID(slice.id),
  -- Name of the slice.
  name STRING,
  -- Id of the thread the slice is running on.
  utid JOINID(thread.id),
  -- Name of the thread.
  thread_name STRING,
  -- Id of the process the slice is running on.
  upid JOINID(process.id),
  -- Name of the process.
  process_name STRING,
  -- Duration of the time the slice was running.
  cpu_time LONG
) AS
SELECT
  id_0 AS id,
  name,
  ts.utid,
  thread_name,
  upid,
  process_name,
  sum(ii.dur) AS cpu_time
FROM _interval_intersect!((
  (SELECT * FROM thread_slice WHERE utid > 0 AND dur > 0),
  (SELECT * FROM sched WHERE dur > 0)
  ), (utid)) AS ii
JOIN thread_slice AS ts
  ON ts.id = ii.id_0
GROUP BY
  id
ORDER BY
  id;

-- CPU cycles per each slice.
CREATE PERFETTO VIEW thread_slice_cpu_cycles (
  -- Id of a slice.
  id JOINID(slice.id),
  -- Name of the slice.
  name STRING,
  -- Id of the thread the slice is running on.
  utid JOINID(thread.id),
  -- Name of the thread.
  thread_name STRING,
  -- Id of the process the slice is running on.
  upid JOINID(process.id),
  -- Name of the process.
  process_name STRING,
  -- Sum of CPU millicycles. Null if frequency couldn't be fetched for any
  -- period during the runtime of the slice.
  millicycles LONG,
  -- Sum of CPU megacycles. Null if frequency couldn't be fetched for any
  -- period during the runtime of the slice.
  megacycles LONG
) AS
SELECT
  id,
  name,
  utid,
  thread_name,
  upid,
  process_name,
  millicycles,
  megacycles
FROM cpu_cycles_per_thread_slice;
