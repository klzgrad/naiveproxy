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

-- Reports the duration of the flush operation for cloned traces (for each
-- buffer).
CREATE PERFETTO TABLE traced_clone_flush_latency (
  -- Id of the buffer (matches the config).
  buffer_id LONG,
  -- Interval from the start of the clone operation to the end of the flush for
  -- this buffer.
  duration_ns LONG
) AS
WITH
  clone_started_ns AS (
    SELECT
      value
    FROM stats
    WHERE
      name = 'traced_clone_started_timestamp_ns'
    LIMIT 1
  )
SELECT
  idx AS buffer_id,
  value - (
    SELECT
      value
    FROM clone_started_ns
  ) AS duration_ns
FROM stats
WHERE
  name = 'traced_buf_clone_done_timestamp_ns'
  AND (
    SELECT
      value
    FROM clone_started_ns
  ) != 0
ORDER BY
  idx;

-- Reports the delay in finalizing the trace from the trigger that causes the
-- clone operation.
CREATE PERFETTO TABLE traced_trigger_clone_flush_latency (
  -- Id of the buffer.
  buffer_id LONG,
  -- Interval from the trigger that caused the clone operation to the end of
  -- the flush for this buffer.
  duration_ns LONG
) AS
WITH
  clone_trigger_fired_ns AS (
    SELECT
      value
    FROM stats
    WHERE
      name = 'traced_clone_trigger_timestamp_ns'
    LIMIT 1
  )
SELECT
  idx AS buffer_id,
  value - (
    SELECT
      value
    FROM clone_trigger_fired_ns
  ) AS duration_ns
FROM stats
WHERE
  name = 'traced_buf_clone_done_timestamp_ns'
  AND (
    SELECT
      value
    FROM clone_trigger_fired_ns
  ) != 0
ORDER BY
  idx;
