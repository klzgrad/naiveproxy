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

INCLUDE PERFETTO MODULE slices.hierarchy;

-- View that provides stack_id and parent_stack_id for all slices by computing
-- them on-demand.
--
-- Note: This view computes stack hashes on-demand, which may be slower than
-- the previous C++ implementation.
CREATE PERFETTO VIEW slice_with_stack_id (
  -- Slice id.
  id ID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id ARGSETID,
  -- Alias of `slice.thread_ts`.
  thread_ts TIMESTAMP,
  -- Alias of `slice.thread_dur`.
  thread_dur LONG,
  -- Alias of `slice.thread_instruction_count`.
  thread_instruction_count LONG,
  -- Alias of `slice.thread_instruction_delta`.
  thread_instruction_delta LONG,
  -- A unique identifier obtained from the names and categories of all slices
  -- in this stack. Computed on-demand.
  stack_id LONG,
  -- The stack_id for the parent of this slice. 0 if there is no parent.
  parent_stack_id LONG
) AS
WITH
  slice_stack_hashes AS (
    SELECT
      s.id,
      coalesce(
        (
          SELECT
            hash(GROUP_CONCAT(hash(coalesce(category, '') || '|' || name), '|'))
          FROM _slice_ancestor_and_self(s.id)
          ORDER BY
            depth ASC
        ),
        0
      ) AS stack_hash
    FROM slice AS s
  )
SELECT
  s.id,
  s.ts,
  s.dur,
  s.track_id,
  s.category,
  s.name,
  s.depth,
  s.parent_id,
  s.arg_set_id,
  s.thread_ts,
  s.thread_dur,
  s.thread_instruction_count,
  s.thread_instruction_delta,
  sh.stack_hash AS stack_id,
  coalesce(parent_sh.stack_hash, 0) AS parent_stack_id
FROM slice AS s
JOIN slice_stack_hashes AS sh
  ON s.id = sh.id
LEFT JOIN slice_stack_hashes AS parent_sh
  ON s.parent_id = parent_sh.id;

-- Returns all slices that have the given stack_id, along with their ancestors.
--
-- The stack_id can be obtained from the slice_with_stack_id view.
CREATE PERFETTO FUNCTION ancestor_slice_by_stack(
    -- The stack hash to search for.
    stack_hash LONG
)
RETURNS TABLE (
  -- Slice id.
  id JOINID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id ARGSETID,
  -- Alias of `slice.thread_ts`.
  thread_ts TIMESTAMP,
  -- Alias of `slice.thread_dur`.
  thread_dur LONG
) AS
-- Find all slices with the matching stack hash
WITH
  matching_slices AS (
    SELECT
      id
    FROM slice_with_stack_id
    WHERE
      stack_id = $stack_hash
  )
-- For each matching slice, get all ancestors and self
SELECT DISTINCT
  anc.id,
  anc.ts,
  anc.dur,
  anc.track_id,
  anc.category,
  anc.name,
  anc.depth,
  anc.parent_id,
  anc.arg_set_id,
  anc.thread_ts,
  anc.thread_dur
FROM matching_slices AS ms
JOIN _slice_ancestor_and_self(ms.id) AS anc
  ON TRUE
ORDER BY
  anc.ts ASC;

-- Returns all slices that have the given stack_id, along with their descendants.
--
-- The stack_id can be obtained from the slice_with_stack_id view.
CREATE PERFETTO FUNCTION descendant_slice_by_stack(
    -- The stack hash to search for.
    stack_hash LONG
)
RETURNS TABLE (
  -- Slice id.
  id JOINID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id ARGSETID,
  -- Alias of `slice.thread_ts`.
  thread_ts TIMESTAMP,
  -- Alias of `slice.thread_dur`.
  thread_dur LONG
) AS
-- Find all slices with the matching stack hash
WITH
  matching_slices AS (
    SELECT
      id
    FROM slice_with_stack_id
    WHERE
      stack_id = $stack_hash
  )
-- For each matching slice, get all descendants and self
SELECT DISTINCT
  desc.id,
  desc.ts,
  desc.dur,
  desc.track_id,
  desc.category,
  desc.name,
  desc.depth,
  desc.parent_id,
  desc.arg_set_id,
  desc.thread_ts,
  desc.thread_dur
FROM matching_slices AS ms
JOIN _slice_descendant_and_self(ms.id) AS desc
  ON TRUE
ORDER BY
  desc.ts ASC;
