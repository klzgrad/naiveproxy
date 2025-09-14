--
-- Copyright 2025 The Android Open Source Project
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

-- Symbolizes a table or subquery that contains the columns "file_name" "rel_pc" "mapping_id" "address" using
-- llvm_symbolizer and returns a table that contains function_name, file_name, line_number, mapping_id, address.
-- The input file_name is a column that contains the path to the elf file.
-- The input rel_pc is the relative address to be symbolized.
-- Currently also includes mapping_id and address as a way to join back symbolization results to original data.
CREATE PERFETTO MACRO _callstack_frame_symbolize(
    -- A subquery which returns a table with columns "file_name", "rel_pc", "mapping_id", and "address".
    frames TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    c0 AS function_name,
    c1 AS file_name,
    c2 AS line_number,
    c3 AS mapping_id,
    c4 AS address
  FROM __intrinsic_table_ptr(
    -- Result table of symbolization
    __intrinsic_symbolize(
      (
        SELECT
          __intrinsic_symbolize_agg(input.file_name, input.rel_pc, input.mapping_id, input.address)
        FROM (
          SELECT
            *
          FROM $frames
        ) AS input
      )
    )
  )
  WHERE
    __intrinsic_table_ptr_bind(c0, 'function_name')
    AND __intrinsic_table_ptr_bind(c1, 'file_name')
    AND __intrinsic_table_ptr_bind(c2, 'line_number')
    AND __intrinsic_table_ptr_bind(c3, 'mapping_id')
    AND __intrinsic_table_ptr_bind(c4, 'address')
);
