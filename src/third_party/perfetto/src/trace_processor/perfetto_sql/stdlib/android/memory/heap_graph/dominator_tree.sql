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

INCLUDE PERFETTO MODULE android.memory.heap_graph.raw_dominator_tree;

-- All reachable heap graph objects, their immediate dominators and summary of
-- their dominated sets.
-- The heap graph dominator tree is calculated by stdlib graphs.dominator_tree.
-- Each reachable object is a node in the dominator tree, their immediate
-- dominator is their parent node in the tree, and their dominated set is all
-- their descendants in the tree. All size information come from the
-- heap_graph_object prelude table.
CREATE PERFETTO TABLE heap_graph_dominator_tree(
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
)
AS
SELECT
  c0 AS id,
  c1 AS idom_id,
  c2 AS dominated_obj_count,
  c3 AS dominated_size_bytes,
  c4 AS dominated_native_size_bytes,
  c5 AS depth
FROM __intrinsic_table_ptr(
  (
    SELECT
      __intrinsic_tree_dominator_summary(
        __intrinsic_tree_from_table(
          'id',
          p.id,
          'parent_id',
          p.idom_id,
          'self_size',
          o.self_size,
          'native_size',
          o.native_size,
          'self_count',
          1
        )
      )
    FROM _raw_heap_graph_dominator_tree AS p
    JOIN heap_graph_object AS o USING (id)
  )
)
WHERE
  __intrinsic_table_ptr_bind(c0, 'id')
  AND __intrinsic_table_ptr_bind(c1, 'idom_id')
  AND __intrinsic_table_ptr_bind(c2, 'dominated_obj_count')
  AND __intrinsic_table_ptr_bind(c3, 'dominated_size_bytes')
  AND __intrinsic_table_ptr_bind(c4, 'dominated_native_size_bytes')
  AND __intrinsic_table_ptr_bind(c5, 'depth')
ORDER BY
  id;
