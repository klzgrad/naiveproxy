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
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE std.metasql.unparenthesize;

-- Helper for __intrinsic_token_apply to fill in NULLs for a ColumnNameList.
CREATE PERFETTO MACRO __ifg_null(x Expr)
RETURNS Expr
AS NULL;

-- Fills in gaps within a table of slices, so data is present from trace_start
-- to trace_end for each group of columns.
--
-- Given a table of slices (`ts`, `dur`, `group_cols`, `data_cols`), this macro
-- returns a table where every distinct `group_cols` has contiguous data from
-- trace_start to trace_end. The `data_cols` are carried through from the source
-- and filled with nulls when gaps are found. The input must not have overlaps
-- within a set of `group_cols`
--
-- Notes:
-- * If grouping is not required, you may pass `(NULL)` for group_cols.
-- * To guarantee a group is present, it is safe to `UNION` a row with the
--   correct `group_cols` and all other columns as NULL.
--
-- Example:
-- ```
-- _intervals_fill_gaps!(
--   (uid, package_name),
--   (process_state),
--   process_state_table
-- )
-- ```
CREATE PERFETTO MACRO _intervals_fill_gaps(
  -- A parenthesized list of column names to group by, or (NULL) to not group.
  group_cols ColumnNameList,
  -- A parethensizes list of column names to carry through.
  data_cols ColumnNameList,
  -- The source data, with columns ts, dur, group_cols, and data_cols.
  data TableOrSubquery
)
RETURNS TableOrSubquery
AS (
  WITH SourceData AS (
    SELECT
      ts, dur,
      metasql_unparenthesize_exprlist!($group_cols),
      metasql_unparenthesize_exprlist!($data_cols)
    FROM $data
  ),
  Bounds AS (
    SELECT
      metasql_unparenthesize_exprlist!($group_cols),
      MIN(ts) AS min_ts,
      MAX(ts+dur) AS max_ts
    FROM SourceData
    GROUP BY metasql_unparenthesize_exprlist!($group_cols)
  ),
  Parts AS (
    -- Part 1: the base data from the table.
    SELECT
      ts, dur,
      metasql_unparenthesize_exprlist!($group_cols),
      metasql_unparenthesize_exprlist!($data_cols)
    FROM SourceData
    UNION ALL
    -- Part 2: the time from the trace start, to first slice.
    SELECT
      trace_start(), min_ts - trace_start(),
      metasql_unparenthesize_exprlist!($group_cols),
      __intrinsic_token_apply!(__ifg_null, $data_cols)
    FROM Bounds
    UNION ALL
    -- Part 3: the time from the last slice, to trace end.
    SELECT
      max_ts, trace_end() - max_ts,
      metasql_unparenthesize_exprlist!($group_cols),
      __intrinsic_token_apply!(__ifg_null, $data_cols)
    FROM Bounds
    UNION ALL
    -- Part 4.a: when there is no data for a group, return one row for the whole trace.
    SELECT
      trace_start(), trace_dur(),
      metasql_unparenthesize_exprlist!($group_cols),
      __intrinsic_token_apply!(__ifg_null, $data_cols)
    FROM Bounds
    WHERE min_ts IS NULL AND max_ts IS NULL
    UNION ALL
    -- Part 4.b: when there is no data at all, return null group_cols for the whole trace.
    SELECT
      trace_start(), trace_dur(),
      __intrinsic_token_apply!(__ifg_null, $group_cols),
      __intrinsic_token_apply!(__ifg_null, $data_cols)
    WHERE NOT EXISTS(SELECT * FROM SourceData)
    UNION ALL
    -- Part 5: the time between slices (from when one ends, to next start).
    SELECT
      ts+dur, LEAD(ts) OVER (PARTITION BY metasql_unparenthesize_exprlist!($group_cols) ORDER BY ts) - (ts+dur),
      metasql_unparenthesize_exprlist!($group_cols),
      __intrinsic_token_apply!(__ifg_null, $data_cols)
    FROM SourceData
  )
  SELECT *
  FROM Parts
  WHERE IFNULL(dur, 0) > 0
  ORDER BY ts
);
