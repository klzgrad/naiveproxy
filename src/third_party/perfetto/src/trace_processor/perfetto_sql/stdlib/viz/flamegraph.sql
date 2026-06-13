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

-- sqlformat file off
-- The case sensitivity of this file matters so don't format it which
-- changes sensitivity.

INCLUDE PERFETTO MODULE graphs.scan;

CREATE PERFETTO MACRO _viz_flamegraph_hash_coalesce(col ColumnName)
RETURNS Expr AS IFNULL($col, 0);

-- For each frame in |tab|, returns a row containing the result of running
-- all the filtering operations over that frame's name.
CREATE PERFETTO MACRO _viz_flamegraph_prepare_filter(
  tab TableOrSubquery,
  show_stack Expr,
  hide_stack Expr,
  show_from_frame Expr,
  hide_frame Expr,
  pivot Expr,
  impossible_stack_bits Expr,
  grouping ColumnNameList
)
RETURNS TableOrSubquery
AS (
  SELECT
    *,
    IIF($hide_stack, $impossible_stack_bits, $show_stack) AS stackBits,
    $show_from_frame As showFromFrameBits,
    $hide_frame = 0 AS showFrame,
    $pivot AS isPivot,
    HASH(
      name,
      __intrinsic_token_apply!(_viz_flamegraph_hash_coalesce, $grouping)
    ) AS groupingHash
  FROM $tab
  ORDER BY id
);

-- Walks the forest from root to leaf and performs the following operations:
--  1) removes frames which were filtered out
--  2) make any pivot nodes become the roots
--  3) computes whether the stack as a whole should be retained or not
CREATE PERFETTO MACRO _viz_flamegraph_filter_frames(
  source TableOrSubquery,
  show_from_frame_bits Expr
)
RETURNS TableOrSubquery
AS (
  WITH edges AS (
    SELECT parentId AS source_node_id, id AS dest_node_id
    FROM $source
    WHERE parentId IS NOT NULL
  ),
  inits AS (
    SELECT
      id,
      IIF(
        showFrame AND showFromFrameBits = $show_from_frame_bits,
        id,
        NULL
      ) AS filteredId,
      NULL AS filteredParentId,
      NULL AS filteredUnpivotedParentId,
      IIF(
        showFrame,
        showFromFrameBits,
        0
      ) AS showFromFrameBits,
      IIF(
        showFrame AND showFromFrameBits = $show_from_frame_bits,
        stackBits,
        0
      ) AS stackBits
    FROM $source
    WHERE parentId IS NULL
  )
  SELECT
    g.filteredId AS id,
    g.filteredParentId AS parentId,
    g.filteredUnpivotedParentId AS unpivotedParentId,
    g.stackBits,
    SUM(t.value) AS value
  FROM _graph_scan!(
    edges,
    inits,
    (filteredId, filteredParentId, filteredUnpivotedParentId, showFromFrameBits, stackBits),
    (
      SELECT
        t.id,
        IIF(
          x.showFrame AND (t.showFromFrameBits | x.showFromFrameBits) = $show_from_frame_bits,
          t.id,
          t.filteredId
        ) AS filteredId,
        IIF(
          x.showFrame AND (t.showFromFrameBits | x.showFromFrameBits) = $show_from_frame_bits,
          IIF(x.isPivot, NULL, t.filteredId),
          t.filteredParentId
        ) AS filteredParentId,
        IIF(
          x.showFrame AND (t.showFromFrameBits | x.showFromFrameBits) = $show_from_frame_bits,
          t.filteredId,
          t.filteredParentId
        ) AS filteredUnpivotedParentId,
        IIF(
          x.showFrame,
          (t.showFromFrameBits | x.showFromFrameBits),
          t.showFromFrameBits
        ) AS showFromFrameBits,
        IIF(
          x.showFrame AND (t.showFromFrameBits | x.showFromFrameBits) = $show_from_frame_bits,
          (t.stackBits | x.stackBits),
          t.stackBits
        ) AS stackBits
      FROM $table t
      JOIN $source x USING (id)
    )
  ) g
  JOIN $source t USING (id)
  WHERE filteredId IS NOT NULL
  GROUP BY filteredId
  ORDER BY filteredId
);

-- Walks the forest from leaves to root and does the following:
--   1) removes nodes whose stacks are filtered out
--   2) computes the cumulative value for each node (i.e. the sum of the self
--      value of the node and all descendants).
CREATE PERFETTO MACRO _viz_flamegraph_accumulate(
  filtered TableOrSubquery,
  showStackBits Expr
)
RETURNS TableOrSubquery
AS (
  WITH edges AS (
    SELECT id AS source_node_id, parentId AS dest_node_id
    FROM $filtered
    WHERE parentId IS NOT NULL
  ), inits AS (
    SELECT f.id, f.value AS cumulativeValue
    FROM $filtered f
    LEFT JOIN $filtered c ON c.parentId = f.id
    WHERE c.id IS NULL AND f.stackBits = $showStackBits
  )
  SELECT id, cumulativeValue
  FROM _graph_aggregating_scan!(
    edges,
    inits,
    (cumulativeValue),
    (
      SELECT
        x.id,
        x.childValue + IIF(
          t.stackBits = $showStackBits,
          t.value,
          0
        ) AS cumulativeValue
      FROM (
        SELECT id, SUM(cumulativeValue) AS childValue
        FROM $table
        GROUP BY id
      ) x
      JOIN $filtered t USING (id)
    )
  )
  ORDER BY id
);

CREATE PERFETTO MACRO _viz_flamegraph_s_prefix(col ColumnName)
RETURNS Expr AS s.$col;

-- Propogates the cumulative value of the pivot nodes to the roots
-- and computes the "fingerprint" of the path.
CREATE PERFETTO MACRO _viz_flamegraph_upwards_hash(
  source TableOrSubquery,
  filtered TableOrSubquery,
  accumulated TableOrSubquery,
  grouping ColumnNameList,
  grouped ColumnNameList
)
RETURNS TableOrSubquery
AS (
  WITH edges AS (
    SELECT id AS source_node_id, unpivotedParentId AS dest_node_id
    FROM $filtered
    WHERE unpivotedParentId IS NOT NULL
  ),
  inits AS (
    SELECT
      f.id,
      HASH(-1, s.groupingHash) AS hash,
      NULL AS parentHash,
      -1 AS depth,
      a.cumulativeValue
    FROM $filtered f
    JOIN $source s USING (id)
    JOIN $accumulated a USING (id)
    WHERE s.isPivot AND a.cumulativeValue > 0
  )
  SELECT
    g.id,
    g.hash,
    g.parentHash,
    g.depth,
    s.name,
    __intrinsic_token_apply!(_viz_flamegraph_s_prefix, $grouping),
    __intrinsic_token_apply!(_viz_flamegraph_s_prefix, $grouped),
    f.value,
    g.cumulativeValue
  FROM _graph_scan!(
    edges,
    inits,
    (hash, parentHash, depth, cumulativeValue),
    (
      SELECT
        t.id,
        HASH(t.hash, x.groupingHash) AS hash,
        t.hash AS parentHash,
        t.depth - 1 AS depth,
        t.cumulativeValue
      FROM $table t
      JOIN $source x USING (id)
    )
  ) g
  JOIN $source s USING (id)
  JOIN $filtered f USING (id)
);

-- Computes the "fingerprint" of the path by walking from the laves
-- to the root.
CREATE PERFETTO MACRO _viz_flamegraph_downwards_hash(
  source TableOrSubquery,
  filtered TableOrSubquery,
  accumulated TableOrSubquery,
  grouping ColumnNameList,
  grouped ColumnNameList,
  showDownward Expr
)
RETURNS TableOrSubquery
AS (
  WITH
    edges AS (
      SELECT parentId AS source_node_id, id AS dest_node_id
      FROM $filtered
      WHERE parentId IS NOT NULL
    ),
    inits AS (
      SELECT
        f.id,
        HASH(1, s.groupingHash) AS hash,
        NULL AS parentHash,
        1 AS depth
      FROM $filtered f
      JOIN $source s USING (id)
      WHERE f.parentId IS NULL AND $showDownward
    )
  SELECT
    g.id,
    g.hash,
    g.parentHash,
    g.depth,
    s.name,
    __intrinsic_token_apply!(_viz_flamegraph_s_prefix, $grouping),
    __intrinsic_token_apply!(_viz_flamegraph_s_prefix, $grouped),
    f.value,
    a.cumulativeValue
  FROM _graph_scan!(
    edges,
    inits,
    (hash, parentHash, depth),
    (
      SELECT
        t.id,
        HASH(t.hash, x.groupingHash) AS hash,
        t.hash AS parentHash,
        t.depth + 1 AS depth
      FROM $table t
      JOIN $source x USING (id)
    )
  ) g
  JOIN $source s USING (id)
  JOIN $filtered f USING (id)
  JOIN $accumulated a USING (id)
  ORDER BY hash
);

CREATE PERFETTO MACRO _col_list_id(a ColumnName)
RETURNS Expr AS $a;

-- Converts a table of hashes and paretn hashes into ids and parent
-- ids, grouping all hashes together.
CREATE PERFETTO MACRO _viz_flamegraph_merge_hashes(
  hashed TableOrSubquery,
  grouping ColumnNameList,
  grouped_agged_exprs ColumnNameList
)
RETURNS TableOrSubquery
AS (
  SELECT
    _auto_id AS id,
    (
      SELECT p._auto_id
      FROM $hashed p
      WHERE p.hash = c.parentHash
      LIMIT 1
    ) AS parentId,
    depth,
    name,
    -- The grouping columns should be passed through as-is because the
    -- hash took them into account: we would not merged any nodes where
    -- the grouping columns were different.
    __intrinsic_token_apply!(_col_list_id, $grouping),
    __intrinsic_token_apply!(_col_list_id, $grouped_agged_exprs),
    SUM(value) AS value,
    SUM(cumulativeValue) AS cumulativeValue
  FROM $hashed c
  GROUP BY hash
);

-- Performs a "layout" of nodes in the flamegraph relative to their
-- siblings.
CREATE PERFETTO MACRO _viz_flamegraph_local_layout(
  merged TableOrSubquery
)
RETURNS TableOrSubquery
AS (
  WITH partial_layout AS (
    SELECT
      id,
      cumulativeValue,
      SUM(cumulativeValue) OVER win AS xEnd
    FROM $merged
    WHERE cumulativeValue > 0
    WINDOW win AS (
      PARTITION BY parentId, depth
      ORDER BY cumulativeValue DESC
      ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    )
  )
  SELECT id, xEnd - cumulativeValue as xStart, xEnd
  FROM partial_layout
  ORDER BY id
);

-- Walks the graph from root to leaf, propogating the layout of
-- parents to their children.
CREATE PERFETTO MACRO _viz_flamegraph_global_layout(
  merged TableOrSubquery,
  layout TableOrSubquery,
  grouping ColumnNameList,
  grouped ColumnNameList
)
RETURNS TableOrSubquery
AS (
  WITH edges AS (
    SELECT parentId AS source_node_id, id AS dest_node_id
    FROM $merged
    WHERE parentId IS NOT NULL
  ),
  inits AS (
    SELECT h.id, 1 AS rootDistance, l.xStart, l.xEnd
    FROM $merged h
    JOIN $layout l USING (id)
    WHERE h.parentId IS NULL
  )
  SELECT
    s.id,
    IFNULL(s.parentId, -1) AS parentId,
    IIF(s.name = '', 'unknown', s.name) AS name,
    __intrinsic_token_apply!(_viz_flamegraph_s_prefix, $grouping),
    __intrinsic_token_apply!(_viz_flamegraph_s_prefix, $grouped),
    s.value AS selfValue,
    s.cumulativeValue,
    p.cumulativeValue AS parentCumulativeValue,
    s.depth,
    g.xStart,
    g.xEnd
  FROM _graph_scan!(
    edges,
    inits,
    (rootDistance, xStart, xEnd),
    (
      SELECT
        t.id,
        t.rootDistance + 1 as rootDistance,
        t.xStart + w.xStart AS xStart,
        t.xStart + w.xEnd AS xEnd
      FROM $table t
      JOIN $layout w USING (id)
    )
  ) g
  JOIN $merged s USING (id)
  LEFT JOIN $merged p ON s.parentId = p.id
  ORDER BY rootDistance, xStart
);
