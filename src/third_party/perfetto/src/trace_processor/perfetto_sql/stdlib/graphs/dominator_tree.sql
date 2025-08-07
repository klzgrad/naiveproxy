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

-- Given a table containing a directed flow-graph and an entry node, computes
-- the "dominator tree" for the graph. See [1] for an explanation of what a
-- dominator tree is.
--
-- [1] https://en.wikipedia.org/wiki/Dominator_(graph_theory)
--
-- Example usage on traces containing heap graphs:
-- ```
-- CREATE PERFETTO VIEW dominator_compatible_heap_graph AS
-- -- Extract the edges from the heap graph which correspond to references
-- -- between objects.
-- SELECT
--   owner_id AS source_node_id,
--   owned_id as dest_node_id
-- FROM heap_graph_reference
-- JOIN heap_graph_object owner on heap_graph_reference.owner_id = owner.id
-- WHERE owned_id IS NOT NULL AND owner.reachable
-- UNION ALL
-- -- Since a Java heap graph is a "forest" structure, we need to add a dummy
-- -- "root" node which connects all the roots of the forest into a single
-- -- connected component.
-- SELECT
--   (SELECT max(id) + 1 FROM heap_graph_object) as source_node_id,
--   id
-- FROM heap_graph_object
-- WHERE root_type IS NOT NULL;
--
-- SELECT *
-- FROM graph_dominator_tree!(
--   dominator_compatible_heap_graph,
--   (SELECT max(id) + 1 FROM heap_graph_object)
-- );
-- ```
CREATE PERFETTO MACRO graph_dominator_tree(
    -- A table/view/subquery corresponding to a directed flow-graph on which the
    -- dominator tree should be computed. This table must have the columns
    -- "source_node_id" and "dest_node_id" corresponding to the two nodes on
    -- either end of the edges in the graph.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    --
    -- Note: this means that the graph *must* be a single fully connected
    -- component with |root_node_id| (see below) being the "entry node" for this
    -- component. Specifically, all nodes *must* be reachable by following paths
    -- from the root node. Failing to adhere to this property will result in
    -- undefined behaviour.
    --
    -- If working with a "forest"-like structure, a dummy node should be added which
    -- links all the roots of the forest together into a single component; an example
    -- of this can be found in the heap graph example query above.
    graph_table TableOrSubquery,
    -- The entry node to |graph_table| which will be the root of the dominator
    -- tree.
    root_node_id Expr
)
-- The returned table has the schema (node_id LONG, dominator_node_id LONG).
-- |node_id| is the id of the node from the input graph and |dominator_node_id|
-- is the id of the node in the input flow-graph which is the "dominator" of
-- |node_id|.
RETURNS TableOrSubquery AS
(
  -- Rename the generic columns of __intrinsic_table_ptr to the actual columns.
  SELECT
    c0 AS node_id,
    c1 AS dominator_node_id
  FROM __intrinsic_table_ptr(
    (
      -- Aggregate function to perform a DFS on the nodes on the input graph.
      SELECT
        __intrinsic_dominator_tree(g.source_node_id, g.dest_node_id, $root_node_id)
      FROM $graph_table AS g
    )
  )
  -- Bind the dynamic columns in the |__intrinsic_table_ptr| to the columns of
  -- the dominator tree table.
  WHERE
    __intrinsic_table_ptr_bind(c0, 'node_id')
    AND __intrinsic_table_ptr_bind(c1, 'dominator_node_id')
);
