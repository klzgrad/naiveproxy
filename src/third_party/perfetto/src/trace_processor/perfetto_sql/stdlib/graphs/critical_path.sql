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
--

INCLUDE PERFETTO MODULE graphs.search;

-- Computes critical paths, the dependency graph of a task.
-- The critical path is a set of edges reachable from a root node with the sum of the edge
-- weights just exceeding the root node capacity. This ensures that the tasks in the critical path
-- completely 'covers' the root capacity.
-- Typically, every node represents a point in time on some task where it transitioned from
-- idle to active state.
--
-- Example usage on traces with Linux sched information:
-- ```
-- -- Compute the userspace critical path from every task sleep.
-- SELECT * FROM
--   critical_path_intervals!(
--   _wakeup_userspace_edges,
--   (SELECT id AS root_node_id, prev_id - id FROM _wakeup_graph WHERE prev_id IS NOT NULL));
-- ```
CREATE PERFETTO MACRO _critical_path(
    -- A table/view/subquery corresponding to a directed graph on which the
    -- reachability search should be performed. This table must have the columns
    -- "source_node_id", "dest_node_id" and "edge_weight" corresponding to the two nodes on
    -- either end of the edges in the graph and the edge weight.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    -- An edge weight is the absolute difference between the node ids forming the edge.
    graph_table TableOrSubQuery,
    -- A table/view/subquery corresponding to start nodes to |graph_table| which will be the
    -- roots of the reachability trees. This table must have the columns
    -- "root_node_id" and "capacity" corresponding to the starting node id and the capacity
    -- of the root node to contain edge weights.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    root_table TableOrSubQuery
)
-- The returned table has the schema (root_id LONG, id LONG, parent_id LONG).
-- |root_id| is the id of the root where the critical path computation started.
-- |id| is the id of a node in the critical path and |parent_id| is the predecessor of |id|.
RETURNS TableOrSubQuery AS
(
  WITH
    _edges AS (
      SELECT
        source_node_id,
        dest_node_id,
        edge_weight
      FROM $graph_table
    ),
    _roots AS (
      SELECT
        root_node_id,
        capacity AS root_target_weight
      FROM $root_table
    ),
    _search_bounds AS (
      SELECT
        min(root_node_id - root_target_weight) AS min_wakeup,
        max(root_node_id + root_target_weight) AS max_wakeup
      FROM _roots
    ),
    _graph AS (
      SELECT
        source_node_id,
        coalesce(dest_node_id, source_node_id) AS dest_node_id,
        edge_weight
      FROM _edges, _search_bounds
      WHERE
        source_node_id BETWEEN min_wakeup AND max_wakeup AND source_node_id IS NOT NULL
    )
  SELECT DISTINCT
    root_node_id AS root_id,
    parent_node_id AS parent_id,
    node_id AS id
  FROM graph_reachable_weight_bounded_dfs !(_graph, _roots, 1) AS cr
);

-- Flattens overlapping tasks within a critical path and flattens overlapping critical paths.
CREATE PERFETTO MACRO _critical_path_to_intervals(
    critical_path_table TableOrSubquery,
    node_table TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  WITH
    flat_tasks AS (
      SELECT
        node.ts,
        cr.root_id,
        cr.id,
        lead(node.ts) OVER (PARTITION BY cr.root_id ORDER BY cr.id) - node.ts AS dur
      FROM $critical_path_table AS cr
      JOIN $node_table AS node
        USING (id)
    ),
    span_starts AS (
      SELECT
        max(cr.ts, idle.ts - idle_dur) AS ts,
        idle.ts AS idle_end_ts,
        cr.ts + cr.dur AS cr_end_ts,
        cr.id,
        cr.root_id
      FROM flat_tasks AS cr
      JOIN $node_table AS idle
        ON cr.root_id = idle.id
    )
  SELECT
    ts,
    min(cr_end_ts, idle_end_ts) - ts AS dur,
    id,
    root_id
  FROM span_starts
  WHERE
    min(idle_end_ts, cr_end_ts) - ts > 0
);

-- Computes critical paths, the dependency graph of a task and returns a flattened view suitable
-- for displaying in a UI track without any overlapping intervals.
-- See the _critical_path MACRO above.
--
-- Example usage on traces with Linux sched information:
-- ```
-- -- Compute the userspace critical path from every task sleep.
-- SELECT * FROM
--   critical_path_intervals!(
--   _wakeup_userspace_edges,
--   (SELECT id AS root_node_id, prev_id - id FROM _wakeup_graph WHERE prev_id IS NOT NULL),
--  _wakeup_intervals);
-- ```
CREATE PERFETTO MACRO _critical_path_intervals(
    -- A table/view/subquery corresponding to a directed graph on which the
    -- reachability search should be performed. This table must have the columns
    -- "source_node_id", "dest_node_id" and "edge_weight" corresponding to the two nodes on
    -- either end of the edges in the graph and the edge weight.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    -- An edge weight is the absolute difference between the node ids forming the edge.
    graph_table TableOrSubQuery,
    -- A table/view/subquery corresponding to start nodes to |graph_table| which will be the
    -- roots of the reachability trees. This table must have the columns
    -- "root_node_id" and "capacity" corresponding to the starting node id and the capacity
    -- of the root node to contain edge weights.
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    root_table TableOrSubQuery,
    -- A table/view/subquery corresponding to the idle to active transition points on a task.
    -- This table must have the columns, "id", "ts", "dur" and "idle_dur". ts and dur is the
    -- timestamp when the task became active and how long it was active for respectively. idle_dur
    -- is the duration it was idle for before it became active at "ts".
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    -- There should be one row for every node id encountered in the |graph_table|.
    interval_table TableOrSubQuery
)
-- The returned table has the schema (id LONG, ts TIMESTAMP, dur DURATION, idle_dur LONG).
-- |root_node_id| is the id of the starting node under which this edge was encountered.
-- |node_id| is the id of the node from the input graph and |parent_node_id|
-- is the id of the node which was the first encountered predecessor in a DFS
-- search of the graph.
RETURNS TableOrSubQuery AS
(
  WITH
    _critical_path_nodes AS (
      SELECT
        root_id,
        id
      FROM _critical_path!($graph_table, $root_table)
    )
  SELECT
    root_id,
    id,
    ts,
    dur
  FROM _critical_path_to_intervals !(_critical_path_nodes, $interval_table)
  UNION ALL
  SELECT
    node.id AS root_id,
    node.id,
    node.ts,
    node.dur
  FROM $interval_table AS node
  JOIN $root_table
    ON root_node_id = id
);
