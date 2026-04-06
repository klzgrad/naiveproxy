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

-- @module std.traceinfo.metadata_for_primary_scope
-- Metadata summary table.

-- A table containing a summary of metadata entries, picking the primary entry
-- for each name (favoring the root trace and machine).
CREATE PERFETTO TABLE _metadata_for_primary_scope (
  -- Name of the metadata entry.
  name STRING,
  -- String value of the entry.
  str_value STRING,
  -- Integer value of the entry.
  int_value LONG,
  -- Type of the metadata entry (e.g. 'single', 'multi').
  key_type STRING
) AS
SELECT
  name,
  str_value,
  int_value,
  key_type
FROM (
  SELECT
    name,
    str_value,
    int_value,
    key_type,
    row_number() OVER (PARTITION BY name ORDER BY trace_id IS NULL, trace_id ASC, machine_id IS NULL, machine_id ASC) AS rn
  FROM metadata
)
WHERE
  rn = 1;
