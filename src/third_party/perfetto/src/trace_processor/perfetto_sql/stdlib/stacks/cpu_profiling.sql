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

INCLUDE PERFETTO MODULE callstacks.stack_profile;

-- Table containing all the timestamped samples of CPU profiling which occurred
-- during the trace: a convenience projection of the `stack_sample` view with
-- thread and cpu information denormalized.
--
-- This covers every callstack profiler source (linux perf, simpleperf, macOS
-- instruments, Chrome, legacy V8, gecko, the StackSample packet, ...) but only
-- samples from what is generally considered CPU profiling: sampling on time,
-- cpu cycles or instructions. Samples from sessions known to sample on some
-- other quantity (off-cpu sampling, tracepoint-based sampling, ...) are
-- excluded.
CREATE PERFETTO TABLE cpu_profiling_samples(
  -- The id of the sample. Joinable with stack_sample.id.
  id LONG,
  -- The timestamp of the sample.
  ts TIMESTAMP,
  -- The utid of the thread of the sample, if available.
  utid JOINID(thread.id),
  -- The tid of the sample, if available.
  tid LONG,
  -- The thread name of thread of the sample, if available.
  thread_name STRING,
  -- The ucpu of the sample, if available.
  ucpu LONG,
  -- The cpu of the sample, if available.
  cpu LONG,
  -- The callsite id of the sample.
  callsite_id LONG,
  -- The profiler that produced the sample (e.g. "linux.perf", "chrome").
  source STRING
)
AS
SELECT
  ss.id,
  ss.ts,
  tc.utid,
  t.tid,
  t.name AS thread_name,
  ec.ucpu,
  c.cpu,
  ss.callsite_id,
  ss.source
FROM stack_sample AS ss
LEFT JOIN stack_sample_task_context AS tc
  ON tc.id = ss.task_context_id
LEFT JOIN stack_sample_execution_context AS ec
  ON ec.id = ss.execution_context_id
LEFT JOIN stack_sample_session AS s
  ON s.id = ss.session_id
LEFT JOIN thread AS t
  ON t.utid = tc.utid
LEFT JOIN cpu AS c
  ON c.id = ec.ucpu
WHERE
  s.timebase_unit IS NULL
  OR s.timebase_unit IN ('ns', 'cycles', 'instructions')
ORDER BY
  ss.ts;

CREATE PERFETTO TABLE _cpu_profiling_self_callsites AS
SELECT *
FROM _callstacks_for_callsites!((SELECT callsite_id FROM cpu_profiling_samples))
ORDER BY
  id;

-- Table summarising the callstacks captured during any CPU profiling which
-- occurred during the trace.
--
-- Specifically, this table returns a tree containing all the callstacks seen
-- during the trace with `self_count` equal to the number of samples with that
-- frame as the leaf and `cumulative_count` equal to the number of samples with
-- the frame anywhere in the tree.
--
-- The data sources supported are the same as the `cpu_profiling_samples` table.
CREATE PERFETTO TABLE cpu_profiling_summary_tree(
  -- The id of the callstack; by callstack we mean a unique set of frames up to
  -- the root frame.
  id LONG,
  -- The id of the parent callstack for this callstack. NULL if this is root.
  parent_id LONG,
  -- The function name of the frame for this callstack.
  name STRING,
  -- The name of the mapping containing the frame. This can be a native binary,
  -- library, JAR or APK.
  mapping_name STRING,
  -- The name of the file containing the function.
  source_file STRING,
  -- The line number in the file the function is located at.
  line_number LONG,
  -- The number of samples with this function as the leaf frame.
  self_count LONG,
  -- The number of samples with this function appearing anywhere on the
  -- callstack.
  cumulative_count LONG
)
AS
SELECT
  id,
  parent_id,
  name,
  mapping_name,
  source_file,
  line_number,
  sum(self_count) AS self_count,
  sum(cumulative_count) AS cumulative_count
FROM (
  SELECT r.*, a.cumulative_count
  FROM _cpu_profiling_self_callsites AS r
  JOIN _callstacks_self_to_cumulative!((
    SELECT id, parent_id, self_count
    FROM _cpu_profiling_self_callsites
  )) AS a USING (
    id
  )
)
GROUP BY
  id;
