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
-- distributed under the License is distributed ON an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE android.memory.heap_graph.excluded_refs;

INCLUDE PERFETTO MODULE android.memory.heap_graph.helpers;

INCLUDE PERFETTO MODULE graphs.search;

-- Converts the heap graph into a tree by performing a BFS on the graph from
-- the roots. This basically ends up with all paths being the shortest path
-- from the root to the node (with lower ids being picked in the case of ties).
CREATE PERFETTO TABLE _heap_graph_object_min_depth_tree AS
SELECT
  node_id AS id,
  parent_node_id AS parent_id
FROM graph_reachable_bfs!(
  (
    SELECT owner_id AS source_node_id, owned_id AS dest_node_id
    FROM heap_graph_reference ref
    WHERE ref.id NOT IN _excluded_refs AND ref.owned_id IS NOT NULL
    ORDER BY ref.owned_id
  ),
  (
    SELECT id AS node_id
    FROM heap_graph_object
    WHERE root_type IS NOT NULL
  )
)
ORDER BY
  id;

CREATE PERFETTO TABLE _heap_graph_path_hashes AS
SELECT
  *
FROM _heap_graph_type_path_hash!(_heap_graph_object_min_depth_tree);

CREATE PERFETTO TABLE _heap_graph_path_hashes_aggregated AS
SELECT
  *
FROM _heap_graph_path_hash_aggregate!(_heap_graph_path_hashes);

CREATE PERFETTO TABLE _heap_graph_class_tree AS
SELECT
  *
FROM _heap_graph_path_hashes_to_class_tree!(_heap_graph_path_hashes_aggregated);

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
    o.*
  FROM _path_hashes AS h
  JOIN heap_graph_object AS o
    ON h.id = o.id
  JOIN heap_graph_class AS c
    ON o.type_id = c.id
  JOIN heap_graph_reference AS r
    ON r.owner_id = o.id
  GROUP BY
    o.id
  ORDER BY
    outgoing_reference_count DESC
);

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
    src.*
  FROM _path_hashes AS h
  JOIN heap_graph_object AS dst
    ON h.id = dst.id
  JOIN heap_graph_reference AS r
    ON r.owned_id = dst.id
  JOIN heap_graph_object AS src
    ON r.owner_id = src.id
  JOIN heap_graph_class AS c
    ON src.type_id = c.id
  ORDER BY
    self_size DESC
);

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
    dst.*
  FROM _path_hashes AS h
  JOIN heap_graph_object AS src
    ON h.id = src.id
  JOIN heap_graph_reference AS r
    ON r.owner_id = src.id
  JOIN heap_graph_object AS dst
    ON r.owned_id = dst.id
  JOIN heap_graph_class AS c
    ON dst.type_id = c.id
  ORDER BY
    dst.self_size DESC
);

CREATE PERFETTO MACRO _heap_graph_retained_object_count_agg(
    path_hashes TableOrSubquery,
    path_hash_values TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    coalesce(c.deobfuscated_name, c.name) AS class_name,
    o.heap_type,
    o.root_type,
    o.reachable,
    sum(o.self_size) AS total_size,
    sum(o.native_size) AS total_native_size,
    count() AS count
  FROM graph_reachable_bfs
    !(
      (
        SELECT
          IFNULL(parent_id, id) AS source_node_id,
          IFNULL(id, parent_id) AS dest_node_id
        FROM _heap_graph_object_min_depth_tree
      ),
      (
        SELECT o.id AS node_id
        FROM $path_hashes h
        JOIN heap_graph_object o
          ON h.id = o.id
        JOIN heap_graph_class c
          ON o.type_id = c.id
        JOIN $path_hash_values USING(path_hash)
      )) AS b
  JOIN heap_graph_object AS o
    ON b.node_id = o.id
  JOIN heap_graph_class AS c
    ON o.type_id = c.id
  GROUP BY
    class_name,
    heap_type,
    root_type,
    reachable
  ORDER BY
    count DESC
);

CREATE PERFETTO MACRO _heap_graph_retaining_object_count_agg(
    path_hashes TableOrSubquery,
    path_hash_values TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    coalesce(c.deobfuscated_name, c.name) AS class_name,
    o.heap_type,
    o.root_type,
    o.reachable,
    sum(o.self_size) AS total_size,
    sum(o.native_size) AS total_native_size,
    count() AS count
  FROM graph_reachable_bfs
    !(
      (
        SELECT
          IFNULL(id, parent_id) AS source_node_id,
          IFNULL(parent_id, id) AS dest_node_id
        FROM _heap_graph_object_min_depth_tree
      ),
      (
        SELECT o.id AS node_id
        FROM $path_hashes h
        JOIN heap_graph_object o
          ON h.id = o.id
        JOIN heap_graph_class c
          ON o.type_id = c.id
        JOIN $path_hash_values USING(path_hash)
      )) AS b
  JOIN heap_graph_object AS o
    ON b.node_id = o.id
  JOIN heap_graph_class AS c
    ON o.type_id = c.id
  GROUP BY
    class_name,
    heap_type,
    root_type,
    reachable
  ORDER BY
    count DESC
);

CREATE PERFETTO MACRO _heap_graph_duplicate_objects_agg(
    path_hashes TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    count(DISTINCT path_hash) AS path_count,
    count() AS object_count,
    sum(o.self_size) AS total_size,
    sum(o.native_size) AS total_native_size,
    coalesce(c.deobfuscated_name, c.name) AS class_name
  FROM $path_hashes AS h
  JOIN heap_graph_object AS o
    ON h.id = o.id
  JOIN heap_graph_class AS c
    ON o.type_id = c.id
  GROUP BY
    class_name
  ORDER BY
    path_count DESC
);
