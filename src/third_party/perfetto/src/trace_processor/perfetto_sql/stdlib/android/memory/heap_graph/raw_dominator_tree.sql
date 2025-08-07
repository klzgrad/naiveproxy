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

INCLUDE PERFETTO MODULE android.memory.heap_graph.excluded_refs;

INCLUDE PERFETTO MODULE graphs.dominator_tree;

-- The assigned id of the "super root".
-- Since a Java heap graph is a "forest" structure, we need to add a imaginary
-- "super root" node which connects all the roots of the forest into a single
-- connected component, so that the dominator tree algorithm can be performed.
CREATE PERFETTO FUNCTION _heap_graph_super_root_fn()
-- The assigned id of the "super root".
RETURNS LONG AS
SELECT
  id + 1
FROM heap_graph_object
ORDER BY
  id DESC
LIMIT 1;

CREATE PERFETTO TABLE _raw_heap_graph_dominator_tree AS
SELECT
  node_id AS id,
  iif(dominator_node_id = _heap_graph_super_root_fn(), NULL, dominator_node_id) AS idom_id
FROM graph_dominator_tree!(
  (
    SELECT
      ref.owner_id AS source_node_id,
      ref.owned_id AS dest_node_id
    FROM heap_graph_reference ref
    JOIN heap_graph_object source_node ON ref.owner_id = source_node.id
    WHERE source_node.reachable
      AND ref.id NOT IN _excluded_refs
      AND ref.owned_id IS NOT NULL
    UNION ALL
    SELECT
      (SELECT _heap_graph_super_root_fn()) as source_node_id,
      id AS dest_node_id
    FROM heap_graph_object
    WHERE root_type IS NOT NULL
  ),
  (SELECT _heap_graph_super_root_fn())
)
-- Excluding the imaginary root.
WHERE
  dominator_node_id IS NOT NULL
ORDER BY
  id;

CREATE PERFETTO INDEX _raw_heap_graph_dominator_tree_idom_id_idx ON _raw_heap_graph_dominator_tree(idom_id);
