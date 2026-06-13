--
-- Copyright 2024 The Android Open Source Project
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

-- Samples from the traced_perf profiler.
CREATE PERFETTO VIEW perf_sample (
  -- Unique identifier for this perf sample.
  id ID,
  -- Timestamp of the sample.
  ts TIMESTAMP,
  -- Sampled thread.
  utid JOINID(thread.id),
  -- Core the sampled thread was running on.
  cpu LONG,
  -- Execution state (userspace/kernelspace) of the sampled thread.
  cpu_mode STRING,
  -- If set, unwound callstack of the sampled thread.
  callsite_id JOINID(stack_profile_callsite.id),
  -- If set, indicates that the unwinding for this sample encountered an error.
  -- Such samples still reference the best-effort result via the callsite_id,
  -- with a synthetic error frame at the point where unwinding stopped.
  unwind_error STRING,
  -- Distinguishes samples from different profiling streams
  -- (i.e. multiple data sources).
  perf_session_id JOINID(perf_session.id)
) AS
SELECT
  id,
  ts,
  utid,
  cpu,
  cpu_mode,
  callsite_id,
  unwind_error,
  perf_session_id
FROM __intrinsic_perf_sample;
