--
-- Copyright 2025 The Android Open Source Project
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

CREATE PERFETTO TABLE _appleos_instruments_raw_callstacks AS
SELECT
  *
FROM _callstacks_for_callsites!((
  SELECT p.callsite_id
  FROM instruments_sample p
)) AS c
ORDER BY
  c.id;

-- This index is added to optimize the creation of the
-- appleos_instruments_samples_summary_tree table by speeding up the
-- leaf-finding query in _callstacks_self_to_cumulative.
CREATE PERFETTO INDEX _appleos_instruments_raw_callstacks_parent_id_idx ON _appleos_instruments_raw_callstacks(parent_id);

-- Table summarising the callstacks captured during all
-- instruments samples in the trace.
--
-- Specifically, this table returns a tree containing all
-- the callstacks seen during the trace with `self_count`
-- equal to the number of samples with that frame as the
-- leaf and `cumulative_count` equal to the number of
-- samples with the frame anywhere in the tree.
CREATE PERFETTO TABLE appleos_instruments_samples_summary_tree (
  -- The id of the callstack. A callstack in this context
  -- is a unique set of frames up to the root.
  id LONG,
  -- The id of the parent callstack for this callstack.
  parent_id LONG,
  -- The function name of the frame for this callstack.
  name STRING,
  -- The name of the mapping containing the frame. This
  -- can be a native binary, library, or JIT.
  mapping_name STRING,
  -- The name of the file containing the function.
  source_file STRING,
  -- The line number in the file the function is located at.
  line_number LONG,
  -- The number of samples with this function as the leaf
  -- frame.
  self_count LONG,
  -- The number of samples with this function appearing
  -- anywhere on the callstack.
  cumulative_count LONG
) AS
SELECT
  r.*,
  a.cumulative_count
FROM _callstacks_self_to_cumulative!((
  SELECT id, parent_id, self_count
  FROM _appleos_instruments_raw_callstacks
)) AS a
JOIN _appleos_instruments_raw_callstacks AS r
  USING (id)
ORDER BY
  r.id;
