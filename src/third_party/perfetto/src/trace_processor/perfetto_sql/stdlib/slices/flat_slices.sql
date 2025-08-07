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
INCLUDE PERFETTO MODULE slices.with_context;

INCLUDE PERFETTO MODULE intervals.overlap;

-- The concept of a "flat slice" is to take the data in the slice table and
-- remove all notion of nesting; we do this by projecting every slice in a stack to
-- their ancestor slice, i.e at any point in time, taking the  most specific active
-- slice (i.e. the slice at the bottom of the stack) and representing that as the
-- *only* slice that was running during that period.
--
-- This concept becomes very useful when you try and linearise a trace and
-- compare it with other traces spanning the same user action; "self time" (i.e.
-- time spent in a slice but *not* any children) is easily computed and span
-- joins with thread state become possible without limiting to only depth zero
--- slices.
--
-- Note that, no slices will be generated for intervals without without any slices.
--
-- As an example, consider the following slice stack:
-- ```
-- A-------------B.
-- ----C----D----.
-- ```
-- The flattened slice will be:
-- ```
-- A----C----D----B.
-- ```
--
-- @column slice_id           Id of most active slice.
-- @column ts                 Timestamp when `slice.id` became the most active slice.
-- @column dur                Duration of `slice.id` as the most active slice until the next active slice.
-- @column depth              Depth of `slice.id` in the original stack.
-- @column name               Name of `slice.id`.
-- @column root_id            Id of of the top most slice of the stack.
-- @column track_id           Alias for `slice.track_id`.
-- @column utid               Alias for `thread.utid`.
-- @column tid                Alias for `thread.tid`
-- @column thread_name        Alias for `thread.name`.
-- @column upid               Alias for `process.upid`.
-- @column pid                Alias for `process.pid`.
-- @column process_name       Alias for `process.name`.
CREATE PERFETTO TABLE _slice_flattened AS
WITH
  root_slices AS (
    SELECT
      *
    FROM slice
    WHERE
      parent_id IS NULL
  ),
  child_slices AS (
    SELECT
      anc.id AS root_id,
      slice.*
    FROM slice, ancestor_slice(slice.id) AS anc
    WHERE
      slice.parent_id IS NOT NULL
  ),
  flat_slices AS (
    SELECT
      root_id,
      id,
      ts,
      dur
    FROM _intervals_flatten !(_intervals_merge_root_and_children!(root_slices, child_slices))
  )
SELECT
  id AS slice_id,
  flat_slices.ts,
  flat_slices.dur,
  depth,
  name,
  root_id,
  track_id,
  utid,
  tid,
  thread_name,
  upid,
  pid,
  process_name
FROM flat_slices
JOIN thread_slice
  USING (id);

CREATE PERFETTO INDEX _slice_flattened_id_idx ON _slice_flattened(slice_id);

CREATE PERFETTO INDEX _slice_flattened_ts_idx ON _slice_flattened(ts);
