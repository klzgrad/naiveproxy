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

-- Creates slice that defines the user inserted Wattson markers. This table will
-- provide the proper Wattson markers slice if user inserts only one pair of
-- Wattson markers, which is the pre-defined agreement.
CREATE PERFETTO TABLE _wattson_markers_window AS
WITH
  markers AS (
    SELECT
      min(ts) FILTER(WHERE
        name = 'wattson_start') AS start,
      max(ts) FILTER(WHERE
        name = 'wattson_stop') AS stop
    FROM slice
    WHERE
      name IN ('wattson_start', 'wattson_stop')
  )
SELECT
  start AS ts,
  stop - start AS dur,
  'Markers window' AS name
FROM markers
WHERE
  start IS NOT NULL;

-- Helper macro for using Perfetto table with interval intersect
CREATE PERFETTO MACRO _ii_subquery(
    tab TableOrSubquery
)
RETURNS TableOrSubquery AS
(
  SELECT
    _auto_id AS id,
    *
  FROM $tab
);

-- DSU dependency policy
CREATE PERFETTO MACRO _dsu_dep()
RETURNS Expr AS
255;
