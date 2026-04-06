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

INCLUDE PERFETTO MODULE graphs.search;

-- Computes the "reachable" set of slices from the |flows| table, starting from slice ids
-- specified in |source_table|. This provides a more efficient result than with the in-built
-- following_flow operator.
CREATE PERFETTO MACRO _slice_following_flow(
    -- A table/view/subquery corresponding to the nodes to start the reachability search.
    -- This table must have a uint32 "id" column.
    source_table TableOrSubquery
)
-- The returned table has the schema (root_node_id, node_id LONG, parent_node_id LONG).
-- |root_node_id| is the id of the starting node under which this edge was encountered.
-- |node_id| is the id of the node from the input graph and |parent_node_id|
-- is the id of the node which was the first encountered predecessor in a DFS
-- search of the graph.
RETURNS TableOrSubquery AS
(
  SELECT
    *
  FROM graph_reachable_weight_bounded_dfs
    !((SELECT slice_out AS source_node_id, slice_in AS dest_node_id, 0 AS edge_weight FROM flow),
      (
        SELECT slice_out AS root_node_id, 1 AS root_target_weight
        FROM flow
        JOIN (SELECT id FROM $source_table) source
          ON slice_out = source.id
      ),
      1)
);
