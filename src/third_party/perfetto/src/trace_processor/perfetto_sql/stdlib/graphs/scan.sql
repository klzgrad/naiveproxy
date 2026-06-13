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

CREATE PERFETTO MACRO _graph_scan_df_agg(x ColumnName, y ColumnName)
RETURNS _ProjectionFragment AS __intrinsic_stringify!($x), init_table.$y;

CREATE PERFETTO MACRO _graph_scan_bind(x ColumnName, y ColumnName)
RETURNS Expr AS __intrinsic_table_ptr_bind(result.$x, __intrinsic_stringify!($y));

CREATE PERFETTO MACRO _graph_scan_select(x ColumnName, y ColumnName)
RETURNS _ProjectionFragment AS result.$x as $y;

-- Performs a "scan" over the graph starting at `init_table` and using `graph_table`
-- for edges to follow.
--
-- See https://en.wikipedia.org/wiki/Prefix_sum#Scan_higher_order_function for
-- details of what a scan means.
CREATE PERFETTO MACRO _graph_scan(
  -- The table containing the edges of the graph. Needs to have the columns
  -- `source_node_id` and `dest_node_id`. Should not contain nulls.
  graph_table TableOrSubquery,
  -- The table of nodes to start the scan from. Needs to have the column `id`
  -- and all columns specified by `scan_columns`. Should not contain nulls.
  init_table TableOrSubquery,
  -- A parenthesised and comma separated list of columns which will be returned
  -- by the scan. Should match exactly both the names and order of the columns
  -- in both `init_table` and `step_query`.
  --
  -- Example: (cumulative_sum, cumulative_count).
  scan_columns ColumnNameList,
  -- A subquery which is reads all the data (from a variable table called $table)
  -- for a single step of the scan and performs some computation for each node in
  -- the step.
  --
  -- Should return a column `id` and all columns specified by `scan_columns`.
  step_query TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  select
    c0 as id,
    __intrinsic_token_apply!(
      _graph_scan_select,
      (c1, c2, c3, c4, c5, c6, c7),
      $scan_columns
    )
  from  __intrinsic_table_ptr(__intrinsic_graph_scan(
    (
      select __intrinsic_graph_agg(g.source_node_id, g.dest_node_id)
      from $graph_table g
    ),
    (
      select __intrinsic_row_dataframe_agg(
        'id', init_table.id,
        __intrinsic_token_apply!(
          _graph_scan_df_agg,
          $scan_columns,
          $scan_columns
        )
      )
      from $init_table AS init_table
    ),
    __intrinsic_stringify_ignore_table!($step_query),
    __intrinsic_stringify!($scan_columns)
  )) result
  where __intrinsic_table_ptr_bind(result.c0, 'id')
    and __intrinsic_token_apply_and!(
          _graph_scan_bind,
          (c1, c2, c3, c4, c5, c6, c7),
          $scan_columns
        )
);

-- Performs a "scan" over the graph starting at `init_table` and using `graph_table`
-- for edges to follow, aggregating on each node wherever possible using `agg_query`.
--
-- See https://en.wikipedia.org/wiki/Prefix_sum#Scan_higher_order_function for
-- details of what a scan means.
CREATE PERFETTO MACRO _graph_aggregating_scan(
  -- The table containing the edges of the graph. Needs to have the columns
  -- `source_node_id` and `dest_node_id`.
  graph_table TableOrSubquery,
  -- The table of nodes to start the scan from. Needs to have the column `id`
  -- and all columns specified by `agg_columns`.
  init_table TableOrSubquery,
  -- A parenthesised and comma separated list of columns which will be returned
  -- by the scan. Should match exactly both the names and order of the columns
  -- in both `init_table` and `agg_query`.
  --
  -- Example: (cumulative_sum, cumulative_count).
  agg_columns ColumnNameList,
  -- A subquery which aggregates the data for one step of the scan. Should contain
  -- the column `id` and all columns specified by `agg_columns`. Should read from
  -- a variable table labelled `$table`.
  agg_query TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  select
    c0 as id,
    __intrinsic_token_apply!(
      _graph_scan_select,
      (c1, c2, c3, c4, c5, c6, c7),
      $agg_columns
    )
  from  __intrinsic_table_ptr(__intrinsic_graph_aggregating_scan(
    (
      select __intrinsic_graph_agg(g.source_node_id, g.dest_node_id)
      from $graph_table g
    ),
    (
      select __intrinsic_row_dataframe_agg(
        'id', init_table.id,
        __intrinsic_token_apply!(
          _graph_scan_df_agg,
          $agg_columns,
          $agg_columns
        )
      )
      from $init_table AS init_table
    ),
    __intrinsic_stringify_ignore_table!($agg_query),
    __intrinsic_stringify!($agg_columns)
  )) result
  where __intrinsic_table_ptr_bind(result.c0, 'id')
    and __intrinsic_token_apply_and!(
          _graph_scan_bind,
          (c1, c2, c3, c4, c5, c6, c7),
          $agg_columns
        )
);
