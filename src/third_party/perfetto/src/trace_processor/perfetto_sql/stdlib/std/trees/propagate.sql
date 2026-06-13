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

-- Propagates column values from root toward leaves using BFS.
--
-- For each spec, a new output column is created by copying the source column
-- and then applying the aggregate operation downward through the tree.
-- For each parent→child edge: output[child] = agg(output[parent], output[child]).
--
-- Each spec is a string of the form 'AGG(source_col) AS output_col' where AGG
-- is one of SUM, MIN, MAX, FIRST, LAST. Case-insensitive.
--
-- Example usage:
-- ```
-- SELECT * FROM _tree_to_table!(
--   _tree_propagate_down(
--     _tree_from_table!((SELECT * FROM calls), (fn, ones)),
--     'SUM(ones) AS depth'
--   ),
--   (fn, depth)
-- );
-- ```
CREATE PERFETTO FUNCTION _tree_propagate_down(
    -- A TREE pointer from _tree_from_table or another tree operation.
    tree_ptr ANY,
    -- Propagation specs: 'AGG(source_col) AS output_col' (variadic)
    specs ANY...
)
-- Returns a TREE pointer with the propagated columns added.
RETURNS ANY
DELEGATES TO __intrinsic_tree_propagate_down;
