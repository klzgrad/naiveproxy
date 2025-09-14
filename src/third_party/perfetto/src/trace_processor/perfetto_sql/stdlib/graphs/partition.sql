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

-- Partitions a tree into a forest of trees based on a given grouping key
-- in a structure-preserving way.
--
-- Specifically, for each tree in the output forest, all the nodes in that tree
-- have the same ancestors and descendants as in the original tree *iff* that
-- ancestor/descendent belonged to the same group.
--
-- Example:
-- Input
--
--   id | parent_id | group_key
--   ---|-----------|----------
--   1  | NULL      | 1
--   2  | 1         | 1
--   3  | NULL      | 2
--   4  | NULL      | 2
--   5  | 2         | 1
--   6  | NULL      | 3
--   7  | 4         | 2
--   8  | 4         | 1
--
-- Or as a graph:
-- ```
--         1 (1)
--        /
--       2 (1)
--      /  \
--     3 (2) 4 (2)
--           /   \
--         5 (1) 8 (1)
--        /  \
--     6 (3) 7 (2)
-- ```
-- Possible output (order of rows is implementation-defined)
--
--   id | parent_id | group_key
--   ---|-----------|-------
--   1  | NULL      | 1
--   2  | 1         | 1
--   3  | NULL      | 2
--   4  | NULL      | 2
--   5  | 2         | 1
--   6  | NULL      | 3
--   7  | 4         | 2
--   8  | 2         | 1
--
-- Or as a forest:
-- ```
--     1 (1)       3 (2)      4 (2)        6 (3)
--      |                      |
--     2 (1)                  7 (2)
--     /   \
--   5 (1) 8 (1)
-- ```
CREATE PERFETTO MACRO tree_structural_partition_by_group(
    -- A table/view/subquery corresponding to a tree which should be partitioned.
    -- This table must have the columns "id", "parent_id" and "group_key".
    --
    -- Note: the columns must contain uint32 similar to ids in trace processor
    -- tables (i.e. the values should be relatively dense and close to zero). The
    -- implementation makes assumptions on this for performance reasons and, if
    -- this criteria is not, can lead to enormous amounts of memory being
    -- allocated.
    tree_table TableOrSubquery
)
-- The returned table has the schema
-- (id LONG, parent_id LONG, group_key LONG).
RETURNS TableOrSubquery AS
(
  -- Rename the generic columns of __intrinsic_table_ptr to the actual columns.
  SELECT
    c0 AS id,
    c1 AS parent_id,
    c2 AS group_key
  FROM __intrinsic_table_ptr(
    (
      -- Aggregate function to perform the partitioning algorithm.
      SELECT
        __intrinsic_structural_tree_partition(g.id, g.parent_id, g.group_key)
      FROM $tree_table AS g
    )
  )
  -- Bind the dynamic columns in the |__intrinsic_table_ptr| to the columns of
  -- the partitioning table.
  WHERE
    __intrinsic_table_ptr_bind(c0, 'node_id')
    AND __intrinsic_table_ptr_bind(c1, 'parent_node_id')
    AND __intrinsic_table_ptr_bind(c2, 'group_key')
);
