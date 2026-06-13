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

CREATE PERFETTO MACRO _interval_agg_with_col_names(
  tab TableOrSubquery,
  agg_columns ColumnNameList,
  id_col ColumnName,
  ts_col ColumnName,
  dur_col ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT __intrinsic_interval_tree_intervals_agg(
    input.$id_col,
    input.$ts_col,
    input.$dur_col
    __intrinsic_token_apply_prefix!(
      _ii_df_agg,
      $agg_columns,
      $agg_columns
    )
  )
  FROM (SELECT * FROM $tab ORDER BY $ts_col) input
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
      (
        c2 AS id_0, c3 AS id_1, c4 AS id_2, c5 AS id_3, c6 AS id_4,
        c7 AS id_5, c8 AS id_6, c9 AS id_7, c10 AS id_8, c11 AS id_9
      ),
      $tabs
    )
    -- Columns for partitions, one for each column with partition.
    __intrinsic_token_apply_prefix!(
      _ii_df_select,
      (c12, c13, c14, c15),
      $agg_columns
    )
  -- Interval intersect result table.
  FROM __intrinsic_table_ptr(
    __intrinsic_interval_intersect(
      __intrinsic_token_apply!(
        _interval_agg,
        $tabs,
        (
          $agg_columns, $agg_columns, $agg_columns, $agg_columns, $agg_columns,
          $agg_columns, $agg_columns, $agg_columns, $agg_columns, $agg_columns
        )
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
    AND __intrinsic_table_ptr_bind(c7, 'id_5')
    AND __intrinsic_table_ptr_bind(c8, 'id_6')
    AND __intrinsic_table_ptr_bind(c9, 'id_7')
    AND __intrinsic_table_ptr_bind(c10, 'id_8')
    AND __intrinsic_table_ptr_bind(c11, 'id_9')

    -- Partition columns.
    __intrinsic_token_apply_and_prefix!(
      _ii_df_bind,
      (c12, c13, c14, c15),
      $agg_columns
    )
);

-- Helper macro to rename columns to standard names
CREATE PERFETTO MACRO _interval_rename_cols(
  tab TableOrSubquery,
  agg_columns ColumnNameList,
  id_col ColumnName,
  ts_col ColumnName,
  dur_col ColumnName
)
RETURNS TableOrSubquery AS
(
  SELECT $id_col AS id, $ts_col AS ts, $dur_col AS dur
  __intrinsic_token_apply_prefix!(_ii_df_select, $agg_columns, $agg_columns)
  FROM $tab
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

-- Intersects two tables of intervals, allowing custom column names for id, ts, and dur.
--
-- Each table can have different column names.
--
-- Example:
--   SELECT * FROM _interval_intersect_with_col_names!(
--     table1, id1, ts1, dur1,
--     table2, id2, ts2, dur2,
--     (partition_col)
--   )
CREATE PERFETTO MACRO _interval_intersect_with_col_names(
  -- First table to intersect.
  tab1 TableOrSubquery,
  -- Name of the id column in tab1.
  id_col1 ColumnName,
  -- Name of the timestamp column in tab1.
  ts_col1 ColumnName,
  -- Name of the duration column in tab1.
  dur_col1 ColumnName,
  -- Second table to intersect.
  tab2 TableOrSubquery,
  -- Name of the id column in tab2.
  id_col2 ColumnName,
  -- Name of the timestamp column in tab2.
  ts_col2 ColumnName,
  -- Name of the duration column in tab2.
  dur_col2 ColumnName,
  -- List of partition columns (can be empty with ()).
  agg_columns ColumnNameList
)
RETURNS TableOrSubquery AS
(
  _interval_intersect!(
    (
      _interval_rename_cols!($tab1, $agg_columns, $id_col1, $ts_col1, $dur_col1),
      _interval_rename_cols!($tab2, $agg_columns, $id_col2, $ts_col2, $dur_col2)
    ),
    $agg_columns
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
  intervals TableOrSubquery
)
RETURNS TableOrSubquery
AS
(
  WITH
    _all_endpoints AS (
      SELECT id, ts, TRUE as is_start FROM $intervals
      UNION
      SELECT id, ts + dur AS ts, FALSE as is_start FROM $intervals
    ),
    _atomic_segments AS (
      SELECT
        ROW_NUMBER() OVER (ORDER BY ts) AS id,
        ts,
        IFNULL(LEAD(ts) OVER (ORDER BY ts) - ts, 0) AS dur
      FROM _all_endpoints
      GROUP BY ts
    ),
    _ii AS (
      SELECT
        ii.ts,
        ii.dur,
        ii.id_0 AS group_id,
        ii.id_1 AS original_id
      FROM _interval_intersect!((_atomic_segments, $intervals), ()) ii
    ),
    _original_ends AS (
      SELECT id, ts + dur AS end_ts FROM $intervals
    )
  -- Part A: Standard segments
  SELECT
    ts,
    dur,
    group_id,
    original_id AS id,
    FALSE AS interval_ends_at_ts
  FROM _ii
  WHERE dur > 0

  UNION ALL

  -- Part B: End markers.
  -- We join back to _atomic_segments to get the 'next' duration
  -- to match the original implementation's quirk.
  SELECT
    e.ts AS ts,
    a.dur AS dur,
    a.id AS group_id,
    e.id AS id,
    TRUE AS interval_ends_at_ts
  FROM _all_endpoints e
  JOIN _atomic_segments a ON a.ts = e.ts
  WHERE e.is_start = FALSE
);
