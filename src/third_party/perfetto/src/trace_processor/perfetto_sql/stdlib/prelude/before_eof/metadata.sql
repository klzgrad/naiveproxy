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

-- Contains metadata about the trace and the system it was collected from.
CREATE PERFETTO VIEW metadata (
  -- Unique identifier for this metadata row.
  id ID,
  -- Name of the metadata entry.
  name STRING,
  -- Type of the metadata entry (e.g. 'single', 'multi').
  key_type STRING,
  -- Integer value of the entry.
  int_value LONG,
  -- String value of the entry.
  str_value STRING,
  -- Machine identifier.
  machine_id JOINID(machine.id),
  -- Trace identifier.
  trace_id LONG
) AS
SELECT
  id,
  name,
  key_type,
  int_value,
  str_value,
  machine_id,
  trace_id
FROM __intrinsic_metadata;
