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
-- during the trace.
--
-- Currently, this table is backed by the following data sources:
--  * Linux perf
--  * Simpleperf proto format
--  * macOS instruments
--  * Chrome CPU profiling
--  * Legacy V8 CPU profiling
--  * Profiling data in Gecko traces
CREATE PERFETTO TABLE cpu_profiling_samples (
  -- The id of the sample.
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
  callsite_id LONG
) AS
WITH
  raw_samples AS (
    -- Linux perf samples.
    SELECT
      p.ts,
      p.utid,
      p.cpu AS ucpu,
      p.callsite_id
    FROM perf_sample AS p
    UNION ALL
    -- Instruments samples.
    SELECT
      p.ts,
      p.utid,
      p.cpu AS ucpu,
      p.callsite_id
    FROM instruments_sample AS p
    UNION ALL
    -- All other CPU profiling.
    SELECT
      s.ts,
      s.utid,
      NULL AS ucpu,
      s.callsite_id
    FROM cpu_profile_stack_sample AS s
  )
SELECT
  row_number() OVER (ORDER BY ts) AS id,
  r.*,
  t.tid,
  t.name AS thread_name,
  c.cpu
FROM raw_samples AS r
LEFT JOIN thread AS t
  USING (utid)
LEFT JOIN cpu AS c
  USING (ucpu)
ORDER BY
  ts;

CREATE PERFETTO TABLE _cpu_profiling_self_callsites AS
SELECT
  *
FROM _callstacks_for_callsites!((
  SELECT callsite_id
  FROM cpu_profiling_samples
))
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
CREATE PERFETTO TABLE cpu_profiling_summary_tree (
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
) AS
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
  SELECT
    r.*,
    a.cumulative_count
  FROM _cpu_profiling_self_callsites AS r
  JOIN _callstacks_self_to_cumulative!((
    SELECT id, parent_id, self_count
    FROM _cpu_profiling_self_callsites
  )) AS a
    USING (id)
)
GROUP BY
  id;
