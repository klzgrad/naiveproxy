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

INCLUDE PERFETTO MODULE intervals.intersect;

-- Extends `interval_intersect_single!` to handle instants within a time range.
-- Returns all original columns from the input table/subquery, plus `ii_dur`:
-- the duration of the slice segment within the specified bounds.
-- Slices with `dur < 0` are ignored. Useful for aggregations.
CREATE PERFETTO MACRO _intersect_slices(
    ts Expr,
    dur Expr,
    t TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    slices.*,
    ii.dur AS ii_dur
  FROM _interval_intersect_single!(
    $ts,
    $dur,
    (SELECT * FROM ($t) WHERE dur > 0)
  ) AS ii
  JOIN (
    $t
  ) AS slices
    USING (id)
  UNION ALL
  SELECT
    *,
    dur AS ii_dur
  FROM (
    $t
  )
  WHERE
    dur = 0 AND ts >= $ts AND ts < $ts + $dur
);
