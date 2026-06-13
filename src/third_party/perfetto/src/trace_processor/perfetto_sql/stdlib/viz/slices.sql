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

-- sqlformat file off
-- This file is case-sensitive.

INCLUDE PERFETTO MODULE graphs.scan;
INCLUDE PERFETTO MODULE slices.with_context;

CREATE PERFETTO MACRO _viz_slice_ancestor_agg(
  inits TableOrSubquery,
  nodes TableOrSubquery
)
RETURNS TableOrSubquery
AS
(
  SELECT
    id,
    parent_id AS parentId,
    name,
    self_dur,
    self_count,
    1 AS simple_count
  FROM _graph_aggregating_scan!(
    (
      SELECT id AS source_node_id, parent_id AS dest_node_id
      FROM $nodes
      WHERE parent_id IS NOT NULL
    ),
    (SELECT id, dur, dur AS self_dur, 1 AS self_count FROM $inits),
    (dur, self_dur, self_count),
    (
      WITH agg AS (
        SELECT t.id, sum(t.dur) AS child_dur
        FROM $table t
        GROUP BY id
      )
      SELECT a.id, s.dur, s.dur - a.child_dur AS self_dur, 0 AS self_count
      FROM agg a
      JOIN $nodes s USING (id)
    )
  ) g
  JOIN $nodes s USING (id)
);

CREATE PERFETTO VIEW _viz_slices_for_ui_table AS
SELECT * FROM thread_or_process_slice
UNION ALL
SELECT
  slice.id,
  slice.ts,
  slice.dur,
  slice.category,
  slice.name,
  slice.track_id,
  track.name AS track_name,
  NULL AS thread_name,
  NULL AS utid,
  NULL AS tid,
  NULL AS process_name,
  NULL AS upid,
  NULL AS pid,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id
FROM slice
JOIN track ON slice.track_id = track.id
WHERE NOT (slice.track_id IN (SELECT id FROM process_track))
  AND NOT (slice.track_id IN (SELECT id FROM thread_track));
