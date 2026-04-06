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

-- sqlformat file off

-- Helper macro to generate 'col_name', t.col_name pairs for tree_from_table.
CREATE PERFETTO MACRO _tree_from_table_col(col ColumnName)
RETURNS _ProjectionFragment AS __intrinsic_stringify!($col), t.$col;

-- Creates a tree structure from a table with id, parent_id, and additional columns.
--
-- The source table must have columns named 'id' and 'parent_id'.
--
-- Example usage:
-- ```
-- SELECT _tree_from_table!(
--   (SELECT id, parent_id, name, value FROM my_table),
--   (name, value)
-- );
-- ```
CREATE PERFETTO MACRO _tree_from_table(
    -- A table/view/subquery containing the tree data.
    -- Must have columns 'id' and 'parent_id'.
    source_table TableOrSubquery,
    -- Additional columns to pass through (parenthesized, comma-separated).
    columns ColumnNameList
)
-- Returns a TREE pointer that can be used with _tree_to_table! or other
-- tree operations.
RETURNS Expr AS
(
  SELECT __intrinsic_tree_from_table(
    'id', t.id,
    'parent_id', t.parent_id,
    __intrinsic_token_apply!(_tree_from_table_col, $columns)
  )
  FROM $source_table AS t
);

-- Helper macro to generate column selection for tree_to_table.
-- Maps column index (c4, c5, ...) to column name.
CREATE PERFETTO MACRO _tree_to_table_col_select(idx ColumnName, col ColumnName)
RETURNS _ProjectionFragment AS $idx AS $col;

-- Helper macro to generate column binding for tree_to_table.
CREATE PERFETTO MACRO _tree_to_table_col_bind(idx ColumnName, col ColumnName)
RETURNS Expr AS
  __intrinsic_table_ptr_bind($idx, __intrinsic_stringify!($col));

-- Converts a tree back to a table.
--
-- The returned table will have:
-- - `_tree_id`: row index in the tree (0-based)
-- - `_tree_parent_id`: parent row index (NULL for roots)
-- - `id`: original id column
-- - `parent_id`: original parent_id column
-- - Additional columns passed to _tree_from_table!
--
-- Example usage:
-- ```
-- SELECT * FROM _tree_to_table!(
--   _tree_from_table!(
--     (SELECT id, parent_id, name FROM my_table),
--     (name)
--   ),
--   (name)
-- );
-- ```
CREATE PERFETTO MACRO _tree_to_table(
    -- A TREE pointer from _tree_from_table!
    tree_ptr Expr,
    -- Additional columns to include (must match columns passed to _tree_from_table!)
    columns ColumnNameList
)
RETURNS TableOrSubquery AS
(
  SELECT
    c0 AS _tree_id,
    c1 AS _tree_parent_id,
    c2 AS id,
    c3 AS parent_id,
    __intrinsic_token_apply!(
      _tree_to_table_col_select,
      (c4, c5, c6, c7, c8, c9),
      $columns
    )
  FROM __intrinsic_table_ptr(__intrinsic_tree_to_table($tree_ptr))
  WHERE
    __intrinsic_table_ptr_bind(c0, '_tree_id')
    AND __intrinsic_table_ptr_bind(c1, '_tree_parent_id')
    AND __intrinsic_table_ptr_bind(c2, 'id')
    AND __intrinsic_table_ptr_bind(c3, 'parent_id')
    AND __intrinsic_token_apply_and!(
      _tree_to_table_col_bind,
      (c4, c5, c6, c7, c8, c9),
      $columns
    )
);
