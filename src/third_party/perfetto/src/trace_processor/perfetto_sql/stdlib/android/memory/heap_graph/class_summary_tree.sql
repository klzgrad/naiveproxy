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
-- distributed under the License is distributed ON an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE android.memory.heap_graph.class_tree;

INCLUDE PERFETTO MODULE graphs.scan;

CREATE PERFETTO TABLE _heap_graph_class_tree_cumulatives AS
SELECT
  *
FROM _graph_aggregating_scan!(
  (
    SELECT id AS source_node_id, parent_id AS dest_node_id
    FROM _heap_graph_class_tree
    WHERE parent_id IS NOT NULL
  ),
  (
    SELECT
      p.id,
      p.self_count AS cumulative_count,
      p.self_size AS cumulative_size
    FROM _heap_graph_class_tree p
    LEFT JOIN _heap_graph_class_tree c ON c.parent_id = p.id
    WHERE c.id IS NULL
  ),
  (cumulative_count, cumulative_size),
  (
    WITH agg AS (
      SELECT
        t.id,
        SUM(t.cumulative_count) AS child_count,
        SUM(t.cumulative_size) AS child_size
      FROM $table t
      GROUP BY t.id
    )
    SELECT
      a.id,
      a.child_count + r.self_count as cumulative_count,
      a.child_size + r.self_size as cumulative_size
    FROM agg a
    JOIN _heap_graph_class_tree r USING (id)
  )
) AS a
ORDER BY
  id;

-- Table containing all the Android heap graphs in the trace converted to a
-- shortest-path tree and then aggregated by class name.
--
-- This table contains a "flamegraph-like" representation of the contents of the
-- heap graph.
CREATE PERFETTO TABLE android_heap_graph_class_summary_tree (
  -- The timestamp the heap graph was dumped at.
  graph_sample_ts TIMESTAMP,
  -- The upid of the process.
  upid JOINID(process.id),
  -- The id of the node in the class tree.
  id LONG,
  -- The parent id of the node in the class tree or NULL if this is the root.
  parent_id LONG,
  -- The name of the class.
  name STRING,
  -- A string describing the type of Java root if this node is a root or NULL
  -- if this node is not a root.
  root_type STRING,
  -- The count of objects with the same class name and the same path to the
  -- root.
  self_count LONG,
  -- The size of objects with the same class name and the same path to the
  -- root.
  self_size LONG,
  -- The sum of `self_count` of this node and all descendants of this node.
  cumulative_count LONG,
  -- The sum of `self_size` of this node and all descendants of this node.
  cumulative_size LONG
) AS
SELECT
  t.graph_sample_ts,
  t.upid,
  t.id,
  t.parent_id,
  t.name,
  t.root_type,
  t.self_count,
  t.self_size,
  c.cumulative_count,
  c.cumulative_size
FROM _heap_graph_class_tree AS t
JOIN _heap_graph_class_tree_cumulatives AS c
  USING (id);
