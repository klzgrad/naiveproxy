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

INCLUDE PERFETTO MODULE android.memory.heap_graph.class_tree;

INCLUDE PERFETTO MODULE graphs.scan;

-- Aggregates cumulative sizes for each object in the heap graph tree.
-- This performs an expensive bottom-up traversal of the tree to compute
-- cumulative sizes, native sizes, and object counts for each node.
CREATE PERFETTO TABLE _heap_graph_object_tree_aggregation AS
SELECT
  id,
  max(cumulative_size) AS cumulative_size,
  max(cumulative_native_size) AS cumulative_native_size,
  max(cumulative_count) AS cumulative_count
FROM _graph_aggregating_scan!(
      (
        SELECT id AS source_node_id, parent_id AS dest_node_id
        FROM _heap_graph_object_min_depth_tree
        WHERE parent_id IS NOT NULL
      ),
      (
        SELECT
          id,
          self_size AS cumulative_size,
          native_size AS cumulative_native_size,
          1 AS cumulative_count
        FROM heap_graph_object
      ),
      (cumulative_size, cumulative_native_size, cumulative_count),
      (
        SELECT
          t.id,
          SUM(t.cumulative_size) AS cumulative_size,
          SUM(t.cumulative_native_size) AS cumulative_native_size,
          SUM(t.cumulative_count) AS cumulative_count
        FROM $table t
        JOIN heap_graph_object o
          ON t.id = o.id
        GROUP BY t.id
      ))
GROUP BY
  id
ORDER BY
  id;

-- Returns object references with cumulative size information.
CREATE PERFETTO MACRO _heap_graph_object_references_agg(
    path_hashes TableOrSubquery,
    path_hash_values TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  WITH
    _path_hashes AS (
      SELECT
        *
      FROM $path_hashes
      JOIN $path_hash_values
        USING (path_hash)
    )
  SELECT
    path_hash,
    coalesce(c.deobfuscated_name, c.name) AS class_name,
    count(r.owned_id) AS outgoing_reference_count,
    cumulative_native_size + cumulative_size AS total_cumulative_size,
    cumulative_size,
    cumulative_native_size,
    cumulative_count,
    o.*
  FROM _path_hashes AS h
  JOIN heap_graph_object AS o
    ON h.id = o.id
  JOIN _heap_graph_object_tree_aggregation AS a
    ON h.id = a.id
  JOIN heap_graph_class AS c
    ON o.type_id = c.id
  JOIN heap_graph_reference AS r
    ON r.owner_id = o.id
  GROUP BY
    o.id
  ORDER BY
    total_cumulative_size DESC
);

-- Returns incoming references with cumulative size information.
CREATE PERFETTO MACRO _heap_graph_incoming_references_agg(
    path_hashes TableOrSubquery,
    path_hash_values TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  WITH
    _path_hashes AS (
      SELECT
        *
      FROM $path_hashes
      JOIN $path_hash_values
        USING (path_hash)
    )
  SELECT
    path_hash,
    coalesce(c.deobfuscated_name, c.name) AS class_name,
    coalesce(r.deobfuscated_field_name, r.field_name) AS field_name,
    r.field_type_name,
    a.cumulative_native_size + a.cumulative_size AS total_cumulative_size,
    a.cumulative_size,
    a.cumulative_native_size,
    a.cumulative_count,
    src.*
  FROM _path_hashes AS h
  JOIN heap_graph_object AS dst
    ON h.id = dst.id
  JOIN heap_graph_reference AS r
    ON r.owned_id = dst.id
  JOIN heap_graph_object AS src
    ON r.owner_id = src.id
  JOIN _heap_graph_object_tree_aggregation AS a
    ON dst.id = a.id
  JOIN heap_graph_class AS c
    ON src.type_id = c.id
  ORDER BY
    total_cumulative_size DESC
);

-- Returns outgoing references with cumulative size information.
CREATE PERFETTO MACRO _heap_graph_outgoing_references_agg(
    path_hashes TableOrSubquery,
    path_hash_values TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  WITH
    _path_hashes AS (
      SELECT
        *
      FROM $path_hashes
      JOIN $path_hash_values
        USING (path_hash)
    )
  SELECT
    path_hash,
    coalesce(c.deobfuscated_name, c.name) AS class_name,
    coalesce(r.deobfuscated_field_name, r.field_name) AS field_name,
    r.field_type_name,
    a.cumulative_native_size + a.cumulative_size AS total_cumulative_size,
    a.cumulative_size,
    a.cumulative_native_size,
    a.cumulative_count,
    dst.*
  FROM _path_hashes AS h
  JOIN heap_graph_object AS src
    ON h.id = src.id
  JOIN heap_graph_reference AS r
    ON r.owner_id = src.id
  JOIN heap_graph_object AS dst
    ON r.owned_id = dst.id
  JOIN _heap_graph_object_tree_aggregation AS a
    ON dst.id = a.id
  JOIN heap_graph_class AS c
    ON dst.type_id = c.id
  ORDER BY
    total_cumulative_size DESC
);
