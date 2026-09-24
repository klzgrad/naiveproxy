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

-- Samples from the traced_perf profiler and perf.data files. One row per perf
-- sample, including counter-only samples which have no callstack.
CREATE PERFETTO VIEW perf_sample(
  -- Unique identifier for this perf sample. Joinable with stack_sample.id.
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
)
AS
SELECT
  ps.id,
  ps.ts,
  tc.utid,
  c.cpu AS cpu,
  -- Preserve perf_sample's legacy representation for an unknown CPU mode.
  COALESCE(ec.cpu_mode, 'unknown') AS cpu_mode,
  ps.callsite_id,
  ps.unwind_error,
  ps.session_id AS perf_session_id
FROM __intrinsic_profiler_sample AS ps
LEFT JOIN __intrinsic_profiler_task_context AS tc
  ON tc.id = ps.task_context_id
LEFT JOIN __intrinsic_profiler_execution_context AS ec
  ON ec.id = ps.execution_context_id
LEFT JOIN __intrinsic_cpu AS c
  ON c.id = ec.ucpu
WHERE
  ps.source = 'linux.perf';
