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

-- @module std.traceinfo.trace
-- Trace-level metadata functions and tables.

-- A table containing pivoted metadata for each trace in the session.
CREATE PERFETTO TABLE _metadata_by_trace (
  -- Trace identifier.
  trace_id LONG,
  -- Unique session name.
  unique_session_name STRING,
  -- Trace UUID.
  trace_uuid STRING,
  -- Trace type.
  trace_type STRING,
  -- Trace size in bytes.
  trace_size_bytes LONG,
  -- Trace trigger.
  trace_trigger STRING,
  -- Comma-separated list of machine names associated with this trace.
  machines STRING
) AS
WITH
  trace_machines AS (
    SELECT
      trace_id,
      GROUP_CONCAT(
        coalesce(extract_metadata_for_machine(machine_id, 'system_name'), 'Machine ' || machine_id),
        ', '
      ) AS machines
    FROM (
      SELECT DISTINCT
        trace_id,
        machine_id
      FROM metadata
      WHERE
        NOT trace_id IS NULL AND machine_id IS NOT NULL
    )
    GROUP BY
      1
  ),
  traces AS (
    SELECT DISTINCT
      trace_id
    FROM metadata
    WHERE
      trace_id IS NOT NULL
  )
SELECT
  trace_id,
  extract_metadata_for_trace(trace_id, 'unique_session_name') AS unique_session_name,
  extract_metadata_for_trace(trace_id, 'trace_uuid') AS trace_uuid,
  extract_metadata_for_trace(trace_id, 'trace_type') AS trace_type,
  extract_metadata_for_trace(trace_id, 'trace_size_bytes') AS trace_size_bytes,
  extract_metadata_for_trace(trace_id, 'trace_trigger') AS trace_trigger,
  coalesce(m.machines, '') AS machines
FROM traces
LEFT JOIN trace_machines AS m
  USING (trace_id);
