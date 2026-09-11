--
-- Copyright 2026 The Android Open Source Project
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

-- Query helpers backing the experimental_flamegraph table function for heap
-- graph dumps.

INCLUDE PERFETTO MODULE android.memory.heap_graph.class_tree;

-- Expands the heap graph dump at (upid, ts) into its shortest-path object
-- tree and produces one frame row per object: name, mapping, and the value
-- columns (count, size, alloc_count, alloc_size) which the flamegraph
-- pipeline aggregates into self_*/cumulative_* output pairs. The BFS tree is
-- the stdlib's canonical _heap_graph_object_min_depth_tree; the class tree
-- module aggregates differently (path hashes keyed on heap type and root
-- type, no root suffix in names), so the frames are built here to preserve
-- the experimental_flamegraph output shape. Object ids are doubled so
-- synthetic "[native]" children (id = 2*object_id + 1) sort after their
-- parent (2*object_id) while staying unique within the tree.
CREATE PERFETTO MACRO _experimental_flamegraph_heap_graph_frames(
  upid Expr,
  ts Expr
)
RETURNS TableOrSubquery
AS (
  WITH
    tree AS MATERIALIZED (
      SELECT t.id, t.parent_id
      FROM _heap_graph_object_min_depth_tree AS t
      JOIN heap_graph_object AS o USING (id)
      WHERE o.upid = $upid AND o.graph_sample_ts = $ts
      UNION ALL
      -- graph_reachable_bfs! emits nothing for an empty edge set, so surface
      -- the roots of dumps with no references directly.
      SELECT o.id AS id, NULL AS parent_id
      FROM heap_graph_object AS o
      WHERE o.upid = $upid AND o.graph_sample_ts = $ts
        AND o.root_type IS NOT NULL
        AND NOT EXISTS (
          SELECT 1 FROM _heap_graph_object_min_depth_tree AS t WHERE t.id = o.id
        )
    ),
    frames AS MATERIALIZED (
      SELECT
        o.id * 2 AS id,
        t.parent_id * 2 AS parent_id,
        coalesce(c.deobfuscated_name, c.name)
          || iif(o.root_type IS NULL, '', ' [' || o.root_type || ']') AS name,
        'JAVA' AS map_name,
        1 AS count,
        o.self_size AS size,
        0 AS alloc_count,
        0 AS alloc_size
      FROM tree AS t
      JOIN heap_graph_object AS o USING (id)
      JOIN heap_graph_class AS c ON c.id = o.type_id
      UNION ALL
      SELECT
        o.id * 2 + 1 AS id,
        o.id * 2 AS parent_id,
        '[native] ' || coalesce(c.deobfuscated_name, c.name)
          || iif(o.root_type IS NULL, '', ' [' || o.root_type || ']') AS name,
        'JAVA' AS map_name,
        1 AS count,
        o.native_size AS size,
        0 AS alloc_count,
        0 AS alloc_size
      FROM tree AS t
      JOIN heap_graph_object AS o USING (id)
      JOIN heap_graph_class AS c ON c.id = o.type_id
      WHERE o.native_size != 0
    )
  SELECT id, parent_id, name, map_name, count, size, alloc_count, alloc_size
  FROM frames
);

-- Returns whether the heap graph dump at (upid, ts) has any roots and whether
-- it was recorded as truncated during import. experimental_flamegraph checks
-- this before running the flamegraph pipeline, which needs a non-empty tree.
CREATE PERFETTO MACRO _experimental_flamegraph_heap_graph_state(
  upid Expr,
  ts Expr
)
RETURNS TableOrSubquery
AS (
  SELECT
    EXISTS(
      SELECT 1 FROM heap_graph_object AS o
      WHERE o.upid = $upid AND o.graph_sample_ts = $ts
        AND o.root_type IS NOT NULL
    ) AS has_roots,
    (SELECT truncated FROM heap_graph AS h
     WHERE h.upid = $upid AND h.ts = $ts) AS truncated
);
