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

CREATE PERFETTO MACRO _ii_df_agg(x ColumnName, y ColumnName)
RETURNS _ProjectionFragment AS __intrinsic_stringify!($x), input.$y;

CREATE PERFETTO MACRO _ii_df_bind(x Expr, y Expr)
RETURNS Expr AS __intrinsic_table_ptr_bind($x, __intrinsic_stringify!($y));

CREATE PERFETTO MACRO _ii_df_select(x ColumnName, y Expr)
RETURNS _ProjectionFragment AS $x AS $y;

CREATE PERFETTO MACRO __first_arg(x Expr, y Expr)
RETURNS Expr AS $x;

CREATE PERFETTO MACRO _interval_agg(
  tab TableOrSubquery,
  agg_columns ColumnNameList
)
RETURNS TableOrSubquery AS
(
  SELECT __intrinsic_interval_tree_intervals_agg(
    input.id,
    input.ts,
    input.dur
    __intrinsic_token_apply_prefix!(
      _ii_df_agg,
      $agg_columns,
      $agg_columns
    )
  )
  FROM (SELECT * FROM $tab ORDER BY ts) input
);

CREATE PERFETTO MACRO _interval_intersect(
  tabs _TableNameList,
  agg_columns ColumnNameList
)
RETURNS TableOrSubquery AS
(
  SELECT
    c0 AS ts,
    c1 AS dur,
    -- Columns for tables ids, in the order of provided tables.
    __intrinsic_token_apply!(
      __first_arg,
      (c2 AS id_0, c3 AS id_1, c4 AS id_2, c5 AS id_3, c6 AS id_4),
      $tabs
    )
    -- Columns for partitions, one for each column with partition.
    __intrinsic_token_apply_prefix!(
      _ii_df_select,
      (c7, c8, c9, c10),
      $agg_columns
    )
  -- Interval intersect result table.
  FROM __intrinsic_table_ptr(
    __intrinsic_interval_intersect(
      __intrinsic_token_apply!(
        _interval_agg,
        $tabs,
        ($agg_columns, $agg_columns, $agg_columns, $agg_columns, $agg_columns)
      ),
      __intrinsic_stringify!($agg_columns)
    )
  )

  -- Bind the resulting columns
  WHERE __intrinsic_table_ptr_bind(c0, 'ts')
    AND __intrinsic_table_ptr_bind(c1, 'dur')
    -- Id columns
    AND __intrinsic_table_ptr_bind(c2, 'id_0')
    AND __intrinsic_table_ptr_bind(c3, 'id_1')
    AND __intrinsic_table_ptr_bind(c4, 'id_2')
    AND __intrinsic_table_ptr_bind(c5, 'id_3')
    AND __intrinsic_table_ptr_bind(c6, 'id_4')

    -- Partition columns.
    __intrinsic_token_apply_and_prefix!(
      _ii_df_bind,
      (c7, c8, c9, c10),
      $agg_columns
    )
);

CREATE PERFETTO MACRO _interval_intersect_single(
  ts Expr,
  dur Expr,
  t TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
  id_0 AS id,
  ts,
  dur
  FROM _interval_intersect!(
    ($t, (SELECT 0 AS id, $ts AS ts, $dur AS dur)),
    ()
  )
);

-- Given a list of intervals (ts, dur), this macro generates a list of interval
-- end points as well as the intervals that intersect with those points.
--
-- e.g. input (10, 20), (20, 25)
--
--         10      30
--        A |-------|
--             B|----------|
--             20         45
--
-- would generate the output:
-- ```
-- ts,dur,group_id,id,interval_ends_at_ts
-- 10,10,1,A,0
-- 20,10,2,A,0
-- 20,10,2,B,0
-- 30,15,3,A,1
-- 30,15,3,B,0
-- 45,0,4,B,1
-- ```
--
-- Runtime is O(n log n + m), where n is the number of intervals and m
-- is the size of the output.
CREATE PERFETTO MACRO interval_self_intersect(
  -- Table or subquery containing interval data.
  intervals TableOrSubquery)
RETURNS TableOrSubquery
AS
(
  WITH RECURSIVE
    _end_points AS (
      SELECT
        ts,
        id,
        TRUE AS is_start
      FROM $intervals
      UNION ALL
      SELECT
        ts + dur AS ts,
        id,
        FALSE AS is_start
      FROM $intervals
    ),
    _with_next_ts AS (
      SELECT
        *,
        LEAD(ts, 1, NULL) OVER (ORDER BY ts) AS next_ts
      FROM _end_points
      ORDER BY ts
    ),
    _group_by_ts AS (
       SELECT
         ts,
         MAX(next_ts) AS next_group_ts,
         ROW_NUMBER() OVER (ORDER BY ts) AS group_id
       FROM _with_next_ts
       GROUP BY ts
    ),
    _end_points_w_group_info AS (
      SELECT *
      FROM _with_next_ts
      JOIN _group_by_ts USING (ts)
    ),
    -- Algorithm: Consider endpoints from left to right (increasing group_id).
    -- As we scan, we keep a set of open intervals:
    --    + if a new interval opens at ts, add it to the set
    --    + if a current interval closes at ts, remove it from the set
    -- At each timestamp (start or end), we record this set of open intervals
    scan(group_id, ts, dur, id) AS (
      -- Base case: we open intervals
      SELECT
        group_id,
        ts,
        IFNULL(next_group_ts - ts, 0) AS dur,
        id
      FROM _end_points_w_group_info
      WHERE is_start = 1
      UNION ALL
      -- Recursive: look at intervals from previous sequence number
      -- and keep all that remain open
      SELECT
        cur.group_id,
        cur.ts,
        IFNULL(next_group_ts - cur.ts, 0) AS dur,
        prev.id
      FROM
        _end_points_w_group_info cur
      JOIN
        scan prev ON (cur.group_id = prev.group_id + 1)
      WHERE
        prev.id <> cur.id
      -- this order by makes the join more efficient
      ORDER BY group_id ASC
  )
  SELECT ts, dur, group_id, id, FALSE AS interval_ends_at_ts FROM scan
  UNION ALL
  SELECT
    ts,
    IFNULL(next_ts - ts, 0) AS dur,
    group_id,
    id,
    TRUE AS interval_ends_at_ts
  FROM _end_points_w_group_info WHERE is_start = 0
);
