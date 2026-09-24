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

INCLUDE PERFETTO MODULE prelude.after_eof.counters;

-- A sampling session: one row per data source instance of a stack profiler
-- (a linux perf session, a StackSample packet stream, ...).
CREATE PERFETTO TABLE stack_sample_session(
  -- Unique identifier for this session.
  id ID,
  -- The profiler that produced this session's samples (e.g. "linux.perf").
  source STRING,
  -- Unit of the quantity the profiler sampled on: the session's primary
  -- (timebase) counter (e.g. "ns", "cycles", "instructions", "count").
  -- NULL if unknown.
  timebase_unit STRING,
  -- Command line used to collect the data, if known.
  cmdline STRING
)
AS
SELECT s.id, s.source, s.timebase_unit, s.cmdline
FROM __intrinsic_profiler_session AS s
WHERE
  s.id IN (
    SELECT DISTINCT session_id
    FROM __intrinsic_profiler_sample
    WHERE
      callsite_id IS NOT NULL
      AND session_id IS NOT NULL
  );

-- Stackful asynchronous execution contexts referenced by task contexts.
CREATE PERFETTO TABLE stack_sample_async_context(
  -- Unique identifier for this asynchronous context.
  id ID,
  -- Human-readable name of the context.
  name STRING,
  -- Kind of context, e.g. "goroutine", "fiber" or "coroutine".
  kind STRING,
  -- Structural parent of this asynchronous context, if any.
  parent_id JOINID(stack_sample_async_context.id)
)
AS
WITH RECURSIVE
  referenced(id) AS (
    SELECT tc.async_context_id
    FROM __intrinsic_profiler_sample AS ps
    JOIN __intrinsic_profiler_task_context AS tc
      ON tc.id = ps.task_context_id
    WHERE
      ps.callsite_id IS NOT NULL
      AND tc.async_context_id IS NOT NULL
    UNION
    SELECT ac.parent_id
    FROM __intrinsic_profiler_async_context AS ac
    JOIN referenced AS r
      ON ac.id = r.id
    WHERE
      ac.parent_id IS NOT NULL
  )
SELECT id, name, kind, parent_id
FROM __intrinsic_profiler_async_context
WHERE
  id IN (SELECT id FROM referenced);

-- Tasks to which stack samples are attributed. A task can identify an OS
-- process, an OS thread, a stackful asynchronous context, or a combination.
CREATE PERFETTO TABLE stack_sample_task_context(
  -- Unique identifier for this task context.
  id ID,
  -- Process the sample is attributed to, if known.
  upid JOINID(process.id),
  -- Thread the sample is attributed to, if known.
  utid JOINID(thread.id),
  -- Stackful asynchronous context the sample is attributed to, if any.
  async_context_id JOINID(stack_sample_async_context.id)
)
AS
SELECT id, upid, utid, async_context_id
FROM __intrinsic_profiler_task_context
WHERE
  id IN (
    SELECT DISTINCT task_context_id
    FROM __intrinsic_profiler_sample
    WHERE
      callsite_id IS NOT NULL
  );

-- Execution states in which stack samples were captured.
CREATE PERFETTO TABLE stack_sample_execution_context(
  -- Unique identifier for this execution context.
  id ID,
  -- Unique core the sample was taken on, if known.
  ucpu JOINID(cpu.id),
  -- Privilege mode the sample was taken in (e.g. "user", "kernel").
  cpu_mode STRING
)
AS
SELECT id, ucpu, cpu_mode
FROM __intrinsic_profiler_execution_context
WHERE
  id IN (
    SELECT DISTINCT execution_context_id
    FROM __intrinsic_profiler_sample
    WHERE
      callsite_id IS NOT NULL
  );

-- Callstack samples from all profiler sources (linux perf, chrome, macOS
-- instruments, the StackSample packet, ...): every profiler sample which
-- captured a callstack. The underlying storage also holds samples without
-- callstacks (e.g. perf counter-only samples); those remain visible through
-- per-source views such as perf_sample.
CREATE PERFETTO VIEW stack_sample(
  -- Unique identifier for this sample.
  id ID,
  -- Timestamp of the sample.
  ts TIMESTAMP,
  -- The profiler that produced the sample (e.g. "linux.perf", "chrome",
  -- "instruments").
  source STRING,
  -- Process, thread and/or stackful asynchronous context this sample is
  -- attributed to, if known.
  task_context_id JOINID(stack_sample_task_context.id),
  -- CPU and privilege mode in which this sample was captured, if known.
  execution_context_id JOINID(stack_sample_execution_context.id),
  -- The captured callstack.
  callsite_id JOINID(stack_profile_callsite.id),
  -- The sampling session this sample came from, if known.
  session_id JOINID(stack_sample_session.id)
)
AS
SELECT
  id,
  ts,
  source,
  task_context_id,
  execution_context_id,
  callsite_id,
  session_id
FROM __intrinsic_profiler_sample
WHERE
  callsite_id IS NOT NULL;

-- Counter tracks whose values are associated with stack samples. A track is a
-- counter instance within one sampling session and can optionally be scoped to
-- a CPU.
CREATE PERFETTO TABLE stack_sample_counter_track(
  -- Unique identifier for this counter track.
  id ID(track.id),
  -- Name of the counter.
  name STRING,
  -- Type of the underlying track.
  type STRING,
  -- Parent track, if any.
  parent_id JOINID(track.id),
  -- Args describing the source of the track.
  source_arg_set_id ARGSETID,
  -- Machine identifier.
  machine_id JOINID(machine.id),
  -- Unit of the counter values.
  unit STRING,
  -- Human-readable description, if present.
  description STRING,
  -- Sampling session this counter belongs to.
  session_id JOINID(stack_sample_session.id),
  -- CPU this counter instance is scoped to, if any.
  cpu LONG,
  -- Whether this counter is the sampling timebase for the session.
  is_timebase BOOL
)
AS
SELECT
  ct.id,
  ct.name,
  ct.type,
  ct.parent_id,
  ct.source_arg_set_id,
  ct.machine_id,
  ct.unit,
  ct.description,
  coalesce(
    extract_arg(ct.dimension_arg_set_id, 'session_id'),
    extract_arg(ct.dimension_arg_set_id, 'perf_session_id')
  ) AS session_id,
  extract_arg(ct.dimension_arg_set_id, 'cpu') AS cpu,
  extract_arg(ct.source_arg_set_id, 'is_timebase') AS is_timebase
FROM counter_track AS ct
WHERE
  ct.id IN (
    SELECT DISTINCT c.track_id
    FROM __intrinsic_profiler_sample AS ps
    JOIN __intrinsic_profiler_counter_set AS pcs
      ON pcs.counter_set_id = ps.counter_set_id
    JOIN counter AS c
      ON c.id = pcs.counter_id
    WHERE
      ps.callsite_id IS NOT NULL
  );

-- Counter values recorded at a stack sample point. A sample can have one
-- primary timebase counter and any number of follower counters.
CREATE PERFETTO VIEW stack_sample_counter(
  -- Unique identifier for this counter value.
  id ID(counter.id),
  -- Stack sample this value was recorded with.
  stack_sample_id JOINID(stack_sample.id),
  -- Counter instance this value belongs to.
  track_id JOINID(stack_sample_counter_track.id),
  -- Counter value recorded at the sample point.
  value DOUBLE
)
AS
SELECT c.id, ps.id AS stack_sample_id, c.track_id, c.value
FROM __intrinsic_profiler_sample AS ps
JOIN __intrinsic_profiler_counter_set AS pcs
  ON pcs.counter_set_id = ps.counter_set_id
JOIN __intrinsic_counter AS c
  ON c.id = pcs.counter_id
WHERE
  ps.callsite_id IS NOT NULL;
