--
-- Copyright 2026 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- sqlformat file off

-- Returns the counter value for a perf sample given the sample ID
-- and counter name.
CREATE PERFETTO FUNCTION linux_perf_counter_for_sample(
    -- The ID of the perf sample.
    sample_id JOINID(perf_sample.id),
    -- The name of the counter (e.g., 'cpu-clock', 'instructions').
    counter_name STRING
)
-- The counter value, or NULL if not found.
RETURNS DOUBLE
  DELEGATES TO __intrinsic_perf_counter_for_sample;

-- sqlformat file on

-- Fully denormalized view joining perf samples with their counter values.
-- Note: This view has multiple rows per sample (one for each counter).
-- Use with caution for large traces as it may impact query performance.
CREATE PERFETTO VIEW linux_perf_sample_with_counters (
  -- The sample ID.
  sample_id JOINID(perf_sample.id),
  -- Timestamp of the sample.
  ts TIMESTAMP,
  -- Sampled thread ID.
  utid JOINID(thread.id),
  -- Core the sampled thread was running on.
  cpu LONG,
  -- Execution state (userspace/kernelspace).
  cpu_mode STRING,
  -- Unwound callstack of the sampled thread.
  callsite_id JOINID(stack_profile_callsite.id),
  -- Stack unwinding error if any.
  unwind_error STRING,
  -- Perf session ID.
  perf_session_id JOINID(perf_session.id),
  -- Counter ID referencing the counter table.
  counter_id JOINID(counter.id),
  -- Track ID for the counter.
  track_id JOINID(track.id),
  -- Counter value at this sample point.
  counter_value DOUBLE
) AS
SELECT
  ps.id AS sample_id,
  ps.ts,
  ps.utid,
  ps.cpu,
  ps.cpu_mode,
  ps.callsite_id,
  ps.unwind_error,
  ps.perf_session_id,
  pcs.counter_id,
  c.track_id,
  c.value AS counter_value
FROM __intrinsic_perf_sample AS ps
JOIN __intrinsic_perf_counter_set AS pcs
  ON ps.counter_set_id = pcs.perf_counter_set_id
JOIN counter AS c
  ON c.id = pcs.counter_id;
