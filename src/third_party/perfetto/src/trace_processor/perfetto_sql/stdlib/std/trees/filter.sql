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

-- Creates a filter constraint for tree operations.
--
-- Example: _tree_constraint('name', '!=', 'skip')
CREATE PERFETTO FUNCTION _tree_constraint(
    -- Column name to filter on
    column STRING,
    -- Operator: '=', '!=', '<', '>', '<=', '>=', 'GLOB', etc.
    op STRING,
    -- Value to compare against (can be any type)
    value ANY
)
-- Returns a constraint pointer
RETURNS ANY
DELEGATES TO __intrinsic_tree_constraint;

-- Combines multiple constraints with AND logic.
--
-- Example: _tree_where(_tree_constraint('x', '>', 10), _tree_constraint('y', '<', 100))
CREATE PERFETTO FUNCTION _tree_where(
    -- Constraints from _tree_constraint (variadic)
    constraints ANY...
)
-- Returns a combined constraint pointer
RETURNS ANY
DELEGATES TO __intrinsic_tree_where_and;

-- Filters a tree structure using constraints, keeping only nodes that match and reparenting orphans.
--
-- Implements the filter operation from RFC 0016: removes filtered nodes from the tree
-- and reparents their children to the closest surviving ancestor.
--
-- All filtering logic happens in C++ via __intrinsic_tree_filter().
--
-- Example usage:
-- ```
-- SELECT * FROM _tree_to_table!(
--   _tree_filter(
--     _tree_from_table!((SELECT * FROM my_tree), (name)),
--     _tree_where(_tree_constraint('name', 'GLOB', '*foo*'))
--   ),
--   (name)
-- );
-- ```
CREATE PERFETTO FUNCTION _tree_filter(
    -- A TREE pointer from _tree_from_table or another tree operation.
    tree_ptr ANY,
    -- Filter constraints from _tree_where or _tree_where_or
    where_clause ANY
)
-- Returns a filtered TREE pointer
RETURNS ANY
DELEGATES TO __intrinsic_tree_filter;
