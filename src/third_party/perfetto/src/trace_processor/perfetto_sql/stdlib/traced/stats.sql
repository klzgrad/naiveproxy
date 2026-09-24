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
--

-- Reports the duration of the flush operation for cloned traces, per
-- buffer and per (machine_id, trace_id) context.
CREATE PERFETTO TABLE traced_clone_flush_latency(
  -- Id of the buffer (matches the config).
  buffer_id LONG,
  -- Interval from the start of the clone operation to the end of the flush
  -- for this buffer.
  duration_ns LONG,
  -- Machine the stats came from.
  machine_id JOINID(machine.id),
  -- Trace the stats came from.
  trace_id LONG
)
AS
WITH
  clone_started_ns AS (
    SELECT machine_id, trace_id, value
    FROM stats
    WHERE
      name = 'traced_clone_started_timestamp_ns'
  )
SELECT
  s.idx AS buffer_id,
  s.value - cs.value AS duration_ns,
  s.machine_id,
  s.trace_id
FROM stats AS s
JOIN clone_started_ns AS cs
  ON s.machine_id IS cs.machine_id
  AND s.trace_id IS cs.trace_id
WHERE
  s.name = 'traced_buf_clone_done_timestamp_ns'
  AND cs.value != 0
ORDER BY
  s.machine_id,
  s.trace_id,
  s.idx;

-- Reports the delay in finalizing the trace from the trigger that causes
-- the clone operation, per buffer and per (machine_id, trace_id) context.
CREATE PERFETTO TABLE traced_trigger_clone_flush_latency(
  -- Id of the buffer.
  buffer_id LONG,
  -- Interval from the trigger that caused the clone operation to the end
  -- of the flush for this buffer.
  duration_ns LONG,
  -- Machine the stats came from.
  machine_id JOINID(machine.id),
  -- Trace the stats came from.
  trace_id LONG
)
AS
WITH
  clone_trigger_fired_ns AS (
    SELECT machine_id, trace_id, value
    FROM stats
    WHERE
      name = 'traced_clone_trigger_timestamp_ns'
  )
SELECT
  s.idx AS buffer_id,
  s.value - cs.value AS duration_ns,
  s.machine_id,
  s.trace_id
FROM stats AS s
JOIN clone_trigger_fired_ns AS cs
  ON s.machine_id IS cs.machine_id
  AND s.trace_id IS cs.trace_id
WHERE
  s.name = 'traced_buf_clone_done_timestamp_ns'
  AND cs.value != 0
ORDER BY
  s.machine_id,
  s.trace_id,
  s.idx;
