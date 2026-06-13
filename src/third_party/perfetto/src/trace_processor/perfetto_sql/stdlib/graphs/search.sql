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

-- Computes the "reachable" set of nodes in a directed graph from a given set
-- of starting nodes by performing a depth-first search on the graph. The
-- returned nodes are structured as a tree with parent-child relationships
-- corresponding to the order in which nodes were encountered by the DFS.
--
-- While this macro can be used directly by end users (hence being public),
-- it is primarily intended as a lower-level building block upon which higher
-- level functions/macros in the standard library can be built.
--
-- Example usage on traces containing heap graphs:
-- ```
-- -- Compute the reachable nodes from the first heap root.
-- SELECT *
-- FROM graph_reachable_dfs!(
--   (
--     SELECT
--       owner_id AS source_node_id,
--       owned_id as dest_node_id
--     FROM heap_graph_reference
--     WHERE owned_id IS NOT NULL
--   ),
--   (SELECT id FROM heap_graph_object WHERE root_type IS NOT NULL)
-- );
-- ```
CREATE PERFETTO MACRO graph_reachable_dfs(
    -- A table/view/subquery corresponding to a directed graph on which the
    -- reachability search should be performed. This table must have the columns
    -- "source_node_id" and "dest_node_id" corresponding to the two nodes on
    -- either end of the edges in the graph.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    graph_table TableOrSubquery,
    -- A table/view/subquery corresponding to the list of start nodes for
    -- the BFS. This table must have a single column "node_id".
    start_nodes TableOrSubquery
)
-- The returned table has the schema (node_id LONG, parent_node_id LONG).
-- |node_id| is the id of the node from the input graph and |parent_node_id|
-- is the id of the node which was the first encountered predecessor in a DFS
-- search of the graph.
RETURNS TableOrSubquery AS
(
  -- Rename the generic columns of __intrinsic_table_ptr to the actual columns.
  SELECT
    c0 AS node_id,
    c1 AS parent_node_id
  FROM __intrinsic_table_ptr(
    __intrinsic_dfs(
      (
        SELECT
          __intrinsic_graph_agg(g.source_node_id, g.dest_node_id)
        FROM $graph_table AS g
      ),
      (
        SELECT
          __intrinsic_array_agg(t.node_id) AS arr
        FROM $start_nodes AS t
      )
    )
  )
  -- Bind the dynamic columns in the |__intrinsic_table_ptr| to the columns of
  -- the DFS table.
  WHERE
    __intrinsic_table_ptr_bind(c0, 'node_id')
    AND __intrinsic_table_ptr_bind(c1, 'parent_node_id')
);

-- Computes the "reachable" set of nodes in a directed graph from a given
-- starting node by performing a breadth-first search on the graph. The returned
-- nodes are structured as a tree with parent-child relationships corresponding
-- to the order in which nodes were encountered by the BFS.
--
-- While this macro can be used directly by end users (hence being public),
-- it is primarily intended as a lower-level building block upon which higher
-- level functions/macros in the standard library can be built.
--
-- Example usage on traces containing heap graphs:
-- ```
-- -- Compute the reachable nodes from all heap roots.
-- SELECT *
-- FROM graph_reachable_bfs!(
--   (
--     SELECT
--       owner_id AS source_node_id,
--       owned_id as dest_node_id
--     FROM heap_graph_reference
--     WHERE owned_id IS NOT NULL
--   ),
--   (SELECT id FROM heap_graph_object WHERE root_type IS NOT NULL)
-- );
-- ```
CREATE PERFETTO MACRO graph_reachable_bfs(
    -- A table/view/subquery corresponding to a directed graph on which the
    -- reachability search should be performed. This table must have the columns
    -- "source_node_id" and "dest_node_id" corresponding to the two nodes on
    -- either end of the edges in the graph.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    graph_table TableOrSubquery,
    -- A table/view/subquery corresponding to the list of start nodes for
    -- the BFS. This table must have a single column "node_id".
    start_nodes TableOrSubquery
)
-- The returned table has the schema (node_id LONG, parent_node_id LONG).
-- |node_id| is the id of the node from the input graph and |parent_node_id|
-- is the id of the node which was the first encountered predecessor in a BFS
-- search of the graph.
RETURNS TableOrSubquery AS
(
  -- Rename the generic columns of __intrinsic_table_ptr to the actual columns.
  SELECT
    c0 AS node_id,
    c1 AS parent_node_id
  FROM __intrinsic_table_ptr(
    __intrinsic_bfs(
      (
        SELECT
          __intrinsic_graph_agg(g.source_node_id, g.dest_node_id)
        FROM $graph_table AS g
      ),
      (
        SELECT
          __intrinsic_array_agg(t.node_id) AS arr
        FROM $start_nodes AS t
      )
    )
  )
  -- Bind the dynamic columns in the |__intrinsic_table_ptr| to the columns of
  -- the DFS table.
  WHERE
    __intrinsic_table_ptr_bind(c0, 'node_id')
    AND __intrinsic_table_ptr_bind(c1, 'parent_node_id')
);

-- Computes the next sibling node in a directed graph. The next node under a parent node
-- is determined by on the |sort_key|, which should be unique for every node under a parent.
-- The order of the next sibling is undefined if the |sort_key| is not unique.
--
-- Example usage:
-- ```
-- -- Compute the next sibling:
-- SELECT *
-- FROM graph_next_sibling!(
--   (
--     SELECT
--       id AS node_id,
--       parent_id AS node_parent_id,
--       ts AS sort_key
--     FROM slice
--   )
-- );
-- ```
CREATE PERFETTO MACRO graph_next_sibling(
    -- A table/view/subquery corresponding to a directed graph for which to find the next sibling.
    -- This table must have the columns "node_id", "node_parent_id" and "sort_key".
    graph_table TableOrSubquery
)
-- The returned table has the schema (node_id LONG, next_node_id LONG).
-- |node_id| is the id of the node from the input graph and |next_node_id|
-- is the id of the node which is its next sibling.
RETURNS TableOrSubquery AS
(
  SELECT
    node_id,
    lead(node_id) OVER (PARTITION BY node_parent_id ORDER BY sort_key) AS next_node_id
  FROM $graph_table
);

-- Computes the "reachable" set of nodes in a directed graph from a set of
-- starting (root) nodes by performing a depth-first search from each root node on the graph.
-- The search is bounded by the sum of edge weights on the path and the root node specifies the
-- max weight (inclusive) allowed before stopping the search.
-- The returned nodes are structured as a tree with parent-child relationships corresponding
-- to the order in which nodes were encountered by the DFS. Each row also has the root node from
-- which where the edge was encountered.
--
-- While this macro can be used directly by end users (hence being public),
-- it is primarily intended as a lower-level building block upon which higher
-- level functions/macros in the standard library can be built.
--
-- Example usage on traces with sched info:
-- ```
-- -- Compute the reachable nodes from a sched wakeup chain
-- INCLUDE PERFETTO MODULE sched.thread_executing_spans;
--
-- SELECT *
-- FROM
--   graph_reachable_dfs_bounded
--    !(
--      (
--        SELECT
--          id AS source_node_id,
--          COALESCE(parent_id, id) AS dest_node_id,
--          id - COALESCE(parent_id, id) AS edge_weight
--        FROM _wakeup_chain
--      ),
--      (
--        SELECT
--          id AS root_node_id,
--          id - COALESCE(prev_id, id) AS root_target_weight
--        FROM _wakeup_chain
--      ));
-- ```
CREATE PERFETTO MACRO graph_reachable_weight_bounded_dfs(
    -- A table/view/subquery corresponding to a directed graph on which the
    -- reachability search should be performed. This table must have the columns
    -- "source_node_id" and "dest_node_id" corresponding to the two nodes on
    -- either end of the edges in the graph and an "edge_weight" corresponding to the
    -- weight of the edge between the node.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    graph_table TableOrSubquery,
    -- A table/view/subquery corresponding to start nodes to |graph_table| which will be the
    -- roots of the reachability trees. This table must have the columns
    -- "root_node_id" and "root_target_weight" corresponding to the starting node id and the max
    -- weight allowed on the tree.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    root_table TableOrSubquery,
    -- Whether the target_weight is a floor weight or ceiling weight.
    -- If it's floor, the search stops right after we exceed the target weight, and we
    -- include the node that pushed just passed the target. If ceiling, the search stops
    -- right before the target weight and the node that would have pushed us passed the
    -- target is not included.
    is_target_weight_floor Expr
)
-- The returned table has the schema (root_node_id, node_id LONG, parent_node_id LONG).
-- |root_node_id| is the id of the starting node under which this edge was encountered.
-- |node_id| is the id of the node from the input graph and |parent_node_id|
-- is the id of the node which was the first encountered predecessor in a DFS
-- search of the graph.
RETURNS TableOrSubquery AS
(
  WITH
    __temp_graph_table AS (
      SELECT
        *
      FROM $graph_table
    ),
    __temp_root_table AS (
      SELECT
        *
      FROM $root_table
    )
  SELECT
    dt.root_node_id,
    dt.node_id,
    dt.parent_node_id
  FROM __intrinsic_dfs_weight_bounded(
    (
      SELECT
        repeatedfield(source_node_id)
      FROM __temp_graph_table
    ),
    (
      SELECT
        repeatedfield(dest_node_id)
      FROM __temp_graph_table
    ),
    (
      SELECT
        repeatedfield(edge_weight)
      FROM __temp_graph_table
    ),
    (
      SELECT
        repeatedfield(root_node_id)
      FROM __temp_root_table
    ),
    (
      SELECT
        repeatedfield(root_target_weight)
      FROM __temp_root_table
    ),
    $is_target_weight_floor
  ) AS dt
);
