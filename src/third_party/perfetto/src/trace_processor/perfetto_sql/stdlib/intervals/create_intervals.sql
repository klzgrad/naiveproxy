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

-- Given a table of start timestamps and a table of end timestamps, creates
-- intervals by matching each start with the next end timestamp strictly greater
-- than it.
--
-- Both input tables must have a column named `ts`. Uses an efficient O(n+m)
-- two-pointer algorithm implemented in C++.
--
-- Example:
-- ```
-- SELECT * FROM _interval_create!(
--   (SELECT ts FROM starts_table),
--   (SELECT ts FROM ends_table)
-- )
-- ```
CREATE PERFETTO MACRO _interval_create(
    -- Table or subquery containing start timestamps (must have a `ts` column).
    starts_table TableOrSubquery,
    -- Table or subquery containing end timestamps (must have a `ts` column).
    ends_table TableOrSubquery
)
-- Table with the schema:
-- ts TIMESTAMP,
--     The start timestamp.
-- dur DURATION,
--     The duration from start to the matched end.
RETURNS TableOrSubquery AS
(
  SELECT
    c0 AS ts,
    c1 AS dur
  FROM __intrinsic_table_ptr(
    __intrinsic_interval_create(
      (
        SELECT
          __intrinsic_timestamp_set_agg(ordered_s.ts)
        FROM (
          SELECT
            ts
          FROM $starts_table
          ORDER BY
            ts
        ) AS ordered_s
      ),
      (
        SELECT
          __intrinsic_timestamp_set_agg(ordered_e.ts)
        FROM (
          SELECT
            ts
          FROM $ends_table
          ORDER BY
            ts
        ) AS ordered_e
      )
    )
  )
  WHERE
    __intrinsic_table_ptr_bind(c0, 'ts') AND __intrinsic_table_ptr_bind(c1, 'dur')
);
