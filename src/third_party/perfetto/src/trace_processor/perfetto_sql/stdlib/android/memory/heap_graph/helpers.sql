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

INCLUDE PERFETTO MODULE graphs.scan;

-- Given a table containing a "tree-ified" heap graph object table (i.e.
-- by using a dominator tree or shortest path algorithm), computes a hash of
-- the path from the root to each node in the graph based on class names.
--
-- This allows an SQL aggregation of all nodes which have the same hash to
-- build a "class-tree" instead of the object tree.
CREATE PERFETTO MACRO _heap_graph_type_path_hash(
    tab TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    id,
    path_hash,
    parent_path_hash
  FROM _graph_scan!(
    (
      SELECT parent_id AS source_node_id, id AS dest_node_id
      FROM $tab
      WHERE parent_id IS NOT NULL
    ),
    (
      SELECT
        t.id,
        o.type_id as parent_type_id,
        HASH(
          o.upid,
          o.graph_sample_ts,
          o.type_id,
          IFNULL(o.root_type, ''),
          IFNULL(o.heap_type, '')
        ) AS path_hash,
        0 AS parent_path_hash
      FROM $tab t
      JOIN heap_graph_object o USING (id)
      WHERE t.parent_id IS NULL
    ),
    (parent_type_id, path_hash, parent_path_hash),
    (
      SELECT
        t.id,
        o.type_id as parent_type_id,
        IIF(
          o.type_id = t.parent_type_id,
          t.path_hash,
          HASH(t.path_hash, o.type_id, IFNULL(o.heap_type, ""))
        ) AS path_hash,
        IIF(
          o.type_id = t.parent_type_id,
          t.parent_path_hash,
          t.path_hash
        ) AS parent_path_hash
      FROM $table t
      JOIN heap_graph_object o USING (id)
    )
  )
  ORDER BY
    id
);

-- Given a table containing heap graph tree-table with path hashes computed
-- (see _heap_graph_type_path_hash macro), aggregates together all nodes
-- with the same hash and also splits out "native size" as a separate node under
-- the nodes which contain the native size.
CREATE PERFETTO MACRO _heap_graph_path_hash_aggregate(
    tab TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  WITH
    x AS (
      SELECT
        o.graph_sample_ts,
        o.upid,
        path_hash,
        parent_path_hash,
        coalesce(c.deobfuscated_name, c.name) AS name,
        o.root_type,
        o.heap_type,
        count() AS self_count,
        sum(o.self_size) AS self_size,
        sum(o.native_size > 0) AS self_native_count,
        sum(o.native_size) AS self_native_size
      FROM $tab
      JOIN heap_graph_object AS o
        USING (id)
      JOIN heap_graph_class AS c
        ON o.type_id = c.id
      GROUP BY
        path_hash
    )
  SELECT
    graph_sample_ts,
    upid,
    hash(path_hash, 'native', '') AS path_hash,
    path_hash AS parent_path_hash,
    '[native] ' || x.name AS name,
    root_type,
    'HEAP_TYPE_NATIVE' AS heap_type,
    sum(x.self_native_count) AS self_count,
    sum(x.self_native_size) AS self_size
  FROM x
  WHERE
    x.self_native_size > 0
  GROUP BY
    path_hash
  UNION ALL
  SELECT
    graph_sample_ts,
    upid,
    path_hash,
    parent_path_hash,
    name,
    root_type,
    heap_type,
    self_count,
    self_size
  FROM x
  ORDER BY
    path_hash
);

-- Given a table containing heap graph tree-table aggregated by path hashes
-- (see _heap_graph_path_hash_aggregate) computes the "class tree" by converting
-- the path hashes to ids.
--
-- Note that |tab| *must* be a Perfetto (e.g. not a subquery) for this macro
-- to work.
CREATE PERFETTO MACRO _heap_graph_path_hashes_to_class_tree(
    tab TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    graph_sample_ts,
    upid,
    _auto_id AS id,
    (
      SELECT
        p._auto_id
      FROM $tab AS p
      WHERE
        c.parent_path_hash = p.path_hash
    ) AS parent_id,
    name,
    root_type,
    heap_type,
    self_count,
    self_size,
    path_hash AS path_hash_stable
  FROM $tab AS c
  ORDER BY
    id
);
