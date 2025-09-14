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

INCLUDE PERFETTO MODULE graphs.scan;

INCLUDE PERFETTO MODULE android.memory.heap_graph.raw_dominator_tree;

CREATE PERFETTO TABLE _heap_graph_dominator_tree_bottom_up_scan AS
SELECT
  *
FROM _graph_aggregating_scan!(
  (
    SELECT id AS source_node_id, idom_id AS dest_node_id
    FROM _raw_heap_graph_dominator_tree
    WHERE idom_id IS NOT NULL
  ),
  (
    SELECT
      p.id,
      1 AS subtree_count,
      o.self_size AS subtree_size_bytes,
      o.native_size AS subtree_native_size_bytes
    FROM _raw_heap_graph_dominator_tree p
    JOIN heap_graph_object o USING (id)
    LEFT JOIN _raw_heap_graph_dominator_tree c ON p.id = c.idom_id
    WHERE c.id IS NULL
  ),
  (subtree_count, subtree_size_bytes, subtree_native_size_bytes),
  (
    WITH children_agg AS (
      SELECT
        t.id,
        SUM(t.subtree_count) AS subtree_count,
        SUM(t.subtree_size_bytes) AS subtree_size_bytes,
        SUM(t.subtree_native_size_bytes) AS subtree_native_size_bytes
      FROM $table t
      GROUP BY t.id
    )
    SELECT
      c.id,
      c.subtree_count + 1 AS subtree_count,
      c.subtree_size_bytes + self_size AS subtree_size_bytes,
      c.subtree_native_size_bytes + native_size AS subtree_native_size_bytes
    FROM children_agg c
    JOIN heap_graph_object o USING (id)
  )
)
ORDER BY
  id;

CREATE PERFETTO TABLE _heap_graph_dominator_tree_top_down_scan AS
SELECT
  *
FROM _graph_scan!(
  (
    SELECT idom_id AS source_node_id, id AS dest_node_id
    FROM _raw_heap_graph_dominator_tree
    WHERE idom_id IS NOT NULL
  ),
  (
    SELECT id, 1 AS depth
    FROM _raw_heap_graph_dominator_tree
    WHERE idom_id IS NULL
  ),
  (depth),
  (SELECT t.id, t.depth + 1 AS depth FROM $table t)
)
ORDER BY
  id;

-- All reachable heap graph objects, their immediate dominators and summary of
-- their dominated sets.
-- The heap graph dominator tree is calculated by stdlib graphs.dominator_tree.
-- Each reachable object is a node in the dominator tree, their immediate
-- dominator is their parent node in the tree, and their dominated set is all
-- their descendants in the tree. All size information come from the
-- heap_graph_object prelude table.
CREATE PERFETTO TABLE heap_graph_dominator_tree (
  -- Heap graph object id.
  id LONG,
  -- Immediate dominator object id of the object. If the immediate dominator
  -- is the "super-root" (i.e. the object is a root or is dominated by multiple
  -- roots) then `idom_id` will be NULL.
  idom_id LONG,
  -- Count of all objects dominated by this object, self inclusive.
  dominated_obj_count LONG,
  -- Total self_size of all objects dominated by this object, self inclusive.
  dominated_size_bytes LONG,
  -- Total native_size of all objects dominated by this object, self inclusive.
  dominated_native_size_bytes LONG,
  -- Depth of the object in the dominator tree. Depth of root objects are 1.
  depth LONG
) AS
SELECT
  r.id,
  r.idom_id AS idom_id,
  d.subtree_count AS dominated_obj_count,
  d.subtree_size_bytes AS dominated_size_bytes,
  d.subtree_native_size_bytes AS dominated_native_size_bytes,
  t.depth
FROM _raw_heap_graph_dominator_tree AS r
JOIN _heap_graph_dominator_tree_bottom_up_scan AS d
  USING (id)
JOIN _heap_graph_dominator_tree_top_down_scan AS t
  USING (id)
WHERE
  r.id IS NOT NULL
ORDER BY
  id;
