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

INCLUDE PERFETTO MODULE prelude.after_eof.views;

-- Lists all metrics built-into trace processor.
CREATE PERFETTO VIEW trace_metrics (
  -- The name of the metric.
  name STRING
) AS
SELECT
  name
FROM _trace_metrics;

-- Definition of `trace_bounds` table. The values are being filled by Trace
-- Processor when parsing the trace.
-- It is recommended to depend on the `trace_start()` and `trace_end()`
-- functions rather than directly on `trace_bounds`.
CREATE PERFETTO VIEW trace_bounds (
  -- First ts in the trace.
  start_ts TIMESTAMP,
  -- End of the trace.
  end_ts TIMESTAMP
) AS
SELECT
  start_ts,
  end_ts
FROM _trace_bounds;

-- Tracks are a fundamental concept in trace processor and represent a
-- "timeline" for events of the same type and with the same context. See
-- https://perfetto.dev/docs/analysis/trace-processor#tracks for a more
-- detailed explanation, with examples.
CREATE PERFETTO VIEW track (
  -- Unique identifier for this track. Identical to |track_id|, prefer using
  -- |track_id| instead.
  id ID,
  -- Name of the track; can be null for some types of tracks (e.g. thread
  -- tracks).
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The dimensions of the track which uniquely identify the track within a
  -- given `type`.
  --
  -- Join with the `args` table or use the `EXTRACT_ARG` helper function to
  -- expand the args.
  dimension_arg_set_id ARGSETID,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Generic key-value pairs containing extra information about the track.
  --
  -- Join with the `args` table or use the `EXTRACT_ARG` helper function to
  -- expand the args.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG
) AS
SELECT
  id,
  name,
  type,
  dimension_arg_set_id,
  parent_id,
  source_arg_set_id,
  machine_id
FROM __intrinsic_track;

-- Contains information about the CPUs on the device this trace was taken on.
CREATE PERFETTO VIEW cpu (
  -- Unique identifier for this CPU. Identical to |ucpu|, prefer using |ucpu|
  -- instead.
  id ID,
  -- Unique identifier for this CPU. Isn't equal to |cpu| for remote machines
  -- and is equal to |cpu| for the host machine.
  ucpu ID,
  -- The 0-based CPU core identifier.
  cpu LONG,
  -- The cluster id is shared by CPUs in the same cluster.
  cluster_id LONG,
  -- A string describing this core.
  processor STRING,
  -- Machine identifier, non-null for CPUs on a remote machine.
  machine_id LONG,
  -- Capacity of a CPU of a device, a metric which indicates the
  -- relative performance of a CPU on a device
  -- For details see:
  -- https://www.kernel.org/doc/Documentation/devicetree/bindings/arm/cpu-capacity.txt
  capacity LONG,
  -- Extra key/value pairs associated with this cpu.
  arg_set_id ARGSETID
) AS
SELECT
  id,
  id AS ucpu,
  cpu,
  cluster_id,
  processor,
  machine_id,
  capacity,
  arg_set_id
FROM __intrinsic_cpu
WHERE
  cpu IS NOT NULL;

-- Contains the frequency values that the CPUs on the device are capable of
-- running at.
CREATE PERFETTO VIEW cpu_available_frequencies (
  -- Unique identifier for this cpu frequency.
  id ID,
  -- The CPU for this frequency, meaningful only in single machine traces.
  -- For multi-machine, join with the `cpu` table on `ucpu` to get the CPU
  -- identifier of each machine.
  cpu LONG,
  -- CPU frequency in KHz.
  freq LONG,
  -- The CPU that the slice executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  ucpu LONG
) AS
SELECT
  id,
  ucpu AS cpu,
  freq,
  ucpu
FROM __intrinsic_cpu_freq;

-- This table holds slices with kernel thread scheduling information. These
-- slices are collected when the Linux "ftrace" data source is used with the
-- "sched/switch" and "sched/wakeup*" events enabled.
--
-- The rows in this table will always have a matching row in the |thread_state|
-- table with |thread_state.state| = 'Running'
CREATE PERFETTO VIEW sched_slice (
  --  Unique identifier for this scheduling slice.
  id ID,
  -- The timestamp at the start of the slice.
  ts TIMESTAMP,
  -- The duration of the slice.
  dur DURATION,
  -- The CPU that the slice executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- The thread's unique id in the trace.
  utid JOINID(thread.id),
  -- A string representing the scheduling state of the kernel
  -- thread at the end of the slice.  The individual characters in
  -- the string mean the following: R (runnable), S (awaiting a
  -- wakeup), D (in an uninterruptible sleep), T (suspended),
  -- t (being traced), X (exiting), P (parked), W (waking),
  -- I (idle), N (not contributing to the load average),
  -- K (wakeable on fatal signals) and Z (zombie, awaiting
  -- cleanup).
  end_state STRING,
  -- The kernel priority that the thread ran at.
  priority LONG,
  -- The unique CPU identifier that the slice executed on.
  ucpu LONG
) AS
SELECT
  id,
  ts,
  dur,
  ucpu AS cpu,
  utid,
  end_state,
  priority,
  ucpu
FROM __intrinsic_sched_slice;

-- Shorter alias for table `sched_slice`.
CREATE PERFETTO VIEW sched (
  -- Alias for `sched_slice.id`.
  id ID,
  -- Alias for `sched_slice.ts`.
  ts TIMESTAMP,
  -- Alias for `sched_slice.dur`.
  dur DURATION,
  -- Alias for `sched_slice.cpu`.
  cpu LONG,
  -- Alias for `sched_slice.utid`.
  utid JOINID(thread.id),
  -- Alias for `sched_slice.end_state`.
  end_state STRING,
  -- Alias for `sched_slice.priority`.
  priority LONG,
  -- Alias for `sched_slice.ucpu`.
  ucpu LONG,
  -- Legacy column, should no longer be used.
  ts_end LONG
) AS
SELECT
  *,
  ts + dur AS ts_end
FROM sched_slice;

-- This table contains the scheduling state of every thread on the system during
-- the trace.
--
-- The rows in this table which have |state| = 'Running', will have a
-- corresponding row in the |sched_slice| table.
CREATE PERFETTO VIEW thread_state (
  -- Unique identifier for this thread state.
  id ID,
  -- The timestamp at the start of the slice.
  ts TIMESTAMP,
  -- The duration of the slice.
  dur DURATION,
  -- The CPU that the thread executed on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- The thread's unique id in the trace.
  utid JOINID(thread.id),
  -- The scheduling state of the thread. Can be "Running" or any of the states
  -- described in |sched_slice.end_state|.
  state STRING,
  -- Indicates whether this thread was blocked on IO.
  io_wait LONG,
  -- The function in the kernel this thread was blocked on.
  blocked_function STRING,
  -- The unique thread id of the thread which caused a wakeup of this thread.
  waker_utid JOINID(thread.id),
  -- The unique thread state id which caused a wakeup of this thread.
  waker_id JOINID(thread_state.id),
  -- Whether the wakeup was from interrupt context or process context.
  irq_context LONG,
  -- The unique CPU identifier that the thread executed on.
  ucpu LONG
) AS
SELECT
  id,
  ts,
  dur,
  ucpu AS cpu,
  utid,
  state,
  io_wait,
  blocked_function,
  waker_utid,
  waker_id,
  irq_context,
  ucpu
FROM __intrinsic_thread_state;

-- Contains all the ftrace events in the trace. This table exists only for
-- debugging purposes and should not be relied on in production usecases (i.e.
-- metrics, standard library etc). Note also that this table might be empty if
-- raw ftrace parsing has been disabled.
CREATE PERFETTO VIEW ftrace_event (
  -- Unique identifier for this ftrace event.
  id ID,
  -- The timestamp of this event.
  ts TIMESTAMP,
  -- The ftrace event name.
  name STRING,
  -- The CPU this event was emitted on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- The thread this event was emitted on.
  utid JOINID(thread.id),
  -- The set of key/value pairs associated with this event.
  arg_set_id ARGSETID,
  -- Ftrace event flags for this event. Currently only emitted for
  -- sched_waking events.
  common_flags LONG,
  -- The unique CPU identifier that this event was emitted on.
  ucpu LONG
) AS
SELECT
  id,
  ts,
  name,
  ucpu AS cpu,
  utid,
  arg_set_id,
  common_flags,
  ucpu
FROM __intrinsic_ftrace_event;

-- This table is deprecated. Use `ftrace_event` instead which contains the same
-- rows; this table is simply a (badly named) alias.
CREATE PERFETTO VIEW raw (
  -- Unique identifier for this raw event.
  id ID,
  -- The timestamp of this event.
  ts TIMESTAMP,
  -- The name of the event. For ftrace events, this will be the ftrace event
  -- name.
  name STRING,
  -- The CPU this event was emitted on (meaningful only in single machine
  -- traces). For multi-machine, join with the `cpu` table on `ucpu` to get the
  -- CPU identifier of each machine.
  cpu LONG,
  -- The thread this event was emitted on.
  utid JOINID(thread.id),
  -- The set of key/value pairs associated with this event.
  arg_set_id ARGSETID,
  -- Ftrace event flags for this event. Currently only emitted for sched_waking
  -- events.
  common_flags LONG,
  -- The unique CPU identifier that this event was emitted on.
  ucpu LONG
) AS
SELECT
  *
FROM ftrace_event;

-- Tracks which are associated to a single thread.
CREATE PERFETTO TABLE thread_track (
  -- Unique identifier for this thread track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The utid that the track is associated with.
  utid JOINID(thread.id)
) AS
SELECT
  t.id,
  t.name,
  t.type,
  t.parent_id,
  t.source_arg_set_id,
  t.machine_id,
  a.int_value AS utid
FROM __intrinsic_track AS t
JOIN args AS a
  ON t.dimension_arg_set_id = a.arg_set_id
WHERE
  t.event_type = 'slice' AND a.key = 'utid';

-- Tracks which are associated to a single process.
CREATE PERFETTO TABLE process_track (
  -- Unique identifier for this process track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The upid that the track is associated with.
  upid JOINID(process.id)
) AS
SELECT
  t.id,
  t.name,
  t.type,
  t.parent_id,
  t.source_arg_set_id,
  t.machine_id,
  a.int_value AS upid
FROM __intrinsic_track AS t
JOIN args AS a
  ON t.dimension_arg_set_id = a.arg_set_id
WHERE
  t.event_type = 'slice' AND a.key = 'upid';

-- Tracks which are associated to a single CPU.
CREATE PERFETTO TABLE cpu_track (
  -- Unique identifier for this cpu track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The CPU that the track is associated with.
  cpu LONG
) AS
SELECT
  t.id,
  t.name,
  t.type,
  t.parent_id,
  t.source_arg_set_id,
  t.machine_id,
  a.int_value AS cpu
FROM __intrinsic_track AS t
JOIN args AS a
  ON t.dimension_arg_set_id = a.arg_set_id
WHERE
  t.event_type = 'slice' AND a.key = 'cpu';

-- Table containing tracks which are loosely tied to a GPU.
--
-- NOTE: this table is deprecated due to inconsistency of it's design with
-- other track tables (e.g. not having a GPU column, mixing a bunch of different
-- tracks which are barely related). Please use the track table directly
-- instead.
CREATE PERFETTO TABLE gpu_track (
  -- Unique identifier for this cpu track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- The dimensions of the track which uniquely identify the track within a
  -- given type.
  dimension_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The source of the track. Deprecated.
  scope STRING,
  -- The description for the track.
  description STRING,
  -- The context id for the GPU this track is associated to.
  context_id LONG
) AS
SELECT
  id,
  name,
  type,
  parent_id,
  source_arg_set_id,
  dimension_arg_set_id,
  machine_id,
  type AS scope,
  extract_arg(source_arg_set_id, 'description') AS description,
  extract_arg(dimension_arg_set_id, 'context_id') AS context_id
FROM __intrinsic_track
WHERE
  type IN ('drm_vblank', 'drm_sched_ring', 'drm_fence', 'mali_mcu_state', 'gpu_render_stage', 'vulkan_events', 'gpu_log', 'graphics_frame_event');

-- Tracks containing counter-like events.
CREATE PERFETTO VIEW counter_track (
  -- Unique identifier for this cpu counter track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The dimensions of the track which uniquely identify the track within a
  -- given type.
  dimension_arg_set_id ARGSETID,
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The units of the counter. This column is rarely filled.
  unit STRING,
  -- The description for this track. For debugging purposes only.
  description STRING
) AS
SELECT
  id,
  name,
  NULL AS parent_id,
  type,
  dimension_arg_set_id,
  source_arg_set_id,
  machine_id,
  counter_unit AS unit,
  extract_arg(source_arg_set_id, 'description') AS description
FROM __intrinsic_track
WHERE
  event_type = 'counter';

-- Tracks containing counter-like events associated to a CPU.
CREATE PERFETTO TABLE cpu_counter_track (
  -- Unique identifier for this cpu counter track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The units of the counter. This column is rarely filled.
  unit STRING,
  -- The description for this track. For debugging purposes only.
  description STRING,
  -- The CPU that the track is associated with.
  cpu LONG
) AS
SELECT
  ct.id,
  ct.name,
  ct.type,
  ct.parent_id,
  ct.source_arg_set_id,
  ct.machine_id,
  ct.unit,
  ct.description,
  args.int_value AS cpu
FROM counter_track AS ct
JOIN args
  ON ct.dimension_arg_set_id = args.arg_set_id
WHERE
  args.key = 'cpu';

-- Tracks containing counter-like events associated to a GPU.
CREATE PERFETTO TABLE gpu_counter_track (
  -- Unique identifier for this gpu counter track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The units of the counter. This column is rarely filled.
  unit STRING,
  -- The description for this track. For debugging purposes only.
  description STRING,
  -- The GPU that the track is associated with.
  gpu_id LONG
) AS
SELECT
  ct.id,
  ct.name,
  ct.type,
  ct.parent_id,
  ct.source_arg_set_id,
  ct.machine_id,
  ct.unit,
  ct.description,
  args.int_value AS gpu_id
FROM counter_track AS ct
JOIN args
  ON ct.dimension_arg_set_id = args.arg_set_id
WHERE
  args.key = 'gpu';

-- Tracks containing counter-like events associated to a process.
CREATE PERFETTO TABLE process_counter_track (
  -- Unique identifier for this process counter track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The units of the counter. This column is rarely filled.
  unit STRING,
  -- The description for this track. For debugging purposes only.
  description STRING,
  -- The upid of the process that the track is associated with.
  upid LONG
) AS
SELECT
  ct.id,
  ct.name,
  ct.type,
  ct.parent_id,
  ct.source_arg_set_id,
  ct.machine_id,
  ct.unit,
  ct.description,
  args.int_value AS upid
FROM counter_track AS ct
JOIN args
  ON ct.dimension_arg_set_id = args.arg_set_id
WHERE
  args.key = 'upid';

-- Tracks containing counter-like events associated to a thread.
CREATE PERFETTO TABLE thread_counter_track (
  -- Unique identifier for this thread counter track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id JOINID(track.id),
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The units of the counter. This column is rarely filled.
  unit STRING,
  -- The description for this track. For debugging purposes only.
  description STRING,
  -- The utid of the thread that the track is associated with.
  utid LONG
) AS
SELECT
  ct.id,
  ct.name,
  ct.type,
  ct.parent_id,
  ct.source_arg_set_id,
  ct.machine_id,
  ct.unit,
  ct.description,
  args.int_value AS utid
FROM counter_track AS ct
JOIN args
  ON ct.dimension_arg_set_id = args.arg_set_id
WHERE
  args.key = 'utid';

-- Tracks containing counter-like events collected from Linux perf.
CREATE PERFETTO TABLE perf_counter_track (
  -- Unique identifier for this thread counter track.
  id ID(track.id),
  -- Name of the track.
  name STRING,
  -- The type of a track indicates the type of data the track contains.
  --
  -- Every track is uniquely identified by the the combination of the
  -- type and a set of dimensions: type allow identifying a set of tracks
  -- with the same type of data within the whole universe of tracks while
  -- dimensions allow distinguishing between different tracks in that set.
  type STRING,
  -- The track which is the "parent" of this track. Only non-null for tracks
  -- created using Perfetto's track_event API.
  parent_id JOINID(track.id),
  -- Args for this track which store information about "source" of this track in
  -- the trace. For example: whether this track orginated from atrace, Chrome
  -- tracepoints etc.
  source_arg_set_id ARGSETID,
  -- Machine identifier, non-null for tracks on a remote machine.
  machine_id LONG,
  -- The units of the counter. This column is rarely filled.
  unit STRING,
  -- The description for this track. For debugging purposes only.
  description STRING,
  -- The id of the perf session this counter was captured on.
  perf_session_id LONG,
  -- The CPU the counter is associated with.
  cpu LONG,
  -- Whether this counter is the sampling timebase for the session.
  is_timebase BOOL
) AS
SELECT
  ct.id,
  ct.name,
  ct.type,
  ct.parent_id,
  ct.source_arg_set_id,
  ct.machine_id,
  ct.unit,
  ct.description,
  extract_arg(ct.dimension_arg_set_id, 'perf_session_id') AS perf_session_id,
  extract_arg(ct.dimension_arg_set_id, 'cpu') AS cpu,
  extract_arg(ct.source_arg_set_id, 'is_timebase') AS is_timebase
FROM counter_track AS ct
WHERE
  ct.type = 'perf_counter';

-- Alias of the `counter` table.
CREATE PERFETTO VIEW counters (
  -- Alias of `counter.id`.
  id ID,
  -- Alias of `counter.ts`.
  ts TIMESTAMP,
  -- Alias of `counter.track_id`.
  track_id JOINID(track.id),
  -- Alias of `counter.value`.
  value DOUBLE,
  -- Alias of `counter.arg_set_id`.
  arg_set_id LONG,
  -- Legacy column, should no longer be used.
  name STRING,
  -- Legacy column, should no longer be used.
  unit STRING
) AS
SELECT
  v.*,
  t.name,
  t.unit
FROM counter AS v
JOIN counter_track AS t
  ON v.track_id = t.id
ORDER BY
  ts;

-- Table containing graphics frame events on Android.
CREATE PERFETTO VIEW frame_slice (
  -- Alias of `slice.id`.
  id ID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(frame_slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id LONG,
  -- Name of the graphics layer this slice happened on.
  layer_name STRING,
  -- The frame number this slice is associated with.
  frame_number LONG,
  -- The time between queue and acquire for this buffer and layer.
  queue_to_acquire_time LONG,
  -- The time between acquire and latch for this buffer and layer.
  acquire_to_latch_time LONG,
  -- The time between latch and present for this buffer and layer.
  latch_to_present_time LONG
) AS
SELECT
  s.id,
  s.ts,
  s.dur,
  s.track_id,
  s.category,
  s.name,
  s.depth,
  s.parent_id,
  s.arg_set_id,
  extract_arg(s.arg_set_id, 'layer_name') AS layer_name,
  extract_arg(s.arg_set_id, 'frame_number') AS frame_number,
  extract_arg(s.arg_set_id, 'queue_to_acquire_time') AS queue_to_acquire_time,
  extract_arg(s.arg_set_id, 'acquire_to_latch_time') AS acquire_to_latch_time,
  extract_arg(s.arg_set_id, 'latch_to_present_time') AS latch_to_present_time
FROM slice AS s
JOIN track AS t
  ON s.track_id = t.id
WHERE
  t.type = 'graphics_frame_event';

-- Table containing graphics frame events on Android.
CREATE PERFETTO VIEW gpu_slice (
  -- Alias of `slice.id`.
  id ID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(frame_slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id LONG,
  -- Context ID.
  context_id LONG,
  -- Render target ID.
  render_target LONG,
  -- The name of the render target.
  render_target_name STRING,
  -- Render pass ID.
  render_pass LONG,
  -- The name of the render pass.
  render_pass_name STRING,
  -- The command buffer ID.
  command_buffer LONG,
  -- The name of the command buffer.
  command_buffer_name STRING,
  -- Frame id.
  frame_id LONG,
  -- The submission id.
  submission_id LONG,
  -- The hardware queue id.
  hw_queue_id LONG,
  -- The id of the process.
  upid JOINID(process.id),
  -- Render subpasses.
  render_subpasses STRING
) AS
SELECT
  s.id,
  s.ts,
  s.dur,
  s.track_id,
  s.category,
  s.name,
  s.depth,
  s.parent_id,
  s.arg_set_id,
  extract_arg(s.arg_set_id, 'context_id') AS context_id,
  extract_arg(s.arg_set_id, 'render_target') AS render_target,
  extract_arg(s.arg_set_id, 'render_target_name') AS render_target_name,
  extract_arg(s.arg_set_id, 'render_pass') AS render_pass,
  extract_arg(s.arg_set_id, 'render_pass_name') AS render_pass_name,
  extract_arg(s.arg_set_id, 'command_buffer') AS command_buffer,
  extract_arg(s.arg_set_id, 'command_buffer_name') AS command_buffer_name,
  extract_arg(s.arg_set_id, 'frame_id') AS frame_id,
  extract_arg(s.arg_set_id, 'submission_id') AS submission_id,
  extract_arg(s.arg_set_id, 'hw_queue_id') AS hw_queue_id,
  extract_arg(s.arg_set_id, 'upid') AS upid,
  extract_arg(s.arg_set_id, 'render_subpasses') AS render_subpasses
FROM slice AS s
JOIN track AS t
  ON s.track_id = t.id
WHERE
  t.type IN ('gpu_render_stage', 'vulkan_events', 'gpu_log');

-- This table contains information on the expected timeline of either a display
-- frame or a surface frame.
CREATE PERFETTO TABLE expected_frame_timeline_slice (
  -- Alias of `slice.id`.
  id ID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(frame_slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id LONG,
  -- Display frame token (vsync id).
  display_frame_token LONG,
  -- Surface frame token (vsync id), null if this is a display frame.
  surface_frame_token LONG,
  -- Unique process id of the app that generates the surface frame.
  upid JOINID(process.id),
  -- Layer name if this is a surface frame.
  layer_name STRING
) AS
SELECT
  s.id,
  s.ts,
  s.dur,
  s.track_id,
  s.category,
  s.name,
  s.depth,
  s.parent_id,
  s.arg_set_id,
  extract_arg(s.arg_set_id, 'Display frame token') AS display_frame_token,
  extract_arg(s.arg_set_id, 'Surface frame token') AS surface_frame_token,
  t.upid,
  extract_arg(s.arg_set_id, 'Layer name') AS layer_name
FROM slice AS s
JOIN process_track AS t
  ON s.track_id = t.id
WHERE
  t.type = 'android_expected_frame_timeline';

-- This table contains information on the actual timeline and additional
-- analysis related to the performance of either a display frame or a surface
-- frame.
CREATE PERFETTO TABLE actual_frame_timeline_slice (
  -- Alias of `slice.id`.
  id ID(slice.id),
  -- Alias of `slice.ts`.
  ts TIMESTAMP,
  -- Alias of `slice.dur`.
  dur DURATION,
  -- Alias of `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias of `slice.category`.
  category STRING,
  -- Alias of `slice.name`.
  name STRING,
  -- Alias of `slice.depth`.
  depth LONG,
  -- Alias of `slice.parent_id`.
  parent_id JOINID(frame_slice.id),
  -- Alias of `slice.arg_set_id`.
  arg_set_id LONG,
  -- Display frame token (vsync id).
  display_frame_token LONG,
  -- Surface frame token (vsync id), null if this is a display frame.
  surface_frame_token LONG,
  -- Unique process id of the app that generates the surface frame.
  upid JOINID(process.id),
  -- Layer name if this is a surface frame.
  layer_name STRING,
  -- Frame's present type (eg. on time / early / late).
  present_type STRING,
  -- Whether the frame finishes on time.
  on_time_finish LONG,
  -- Whether the frame used gpu composition.
  gpu_composition LONG,
  -- Specify the jank types for this frame if there's jank, or none if no jank
  -- occurred.
  jank_type STRING,
  -- Severity of the jank: none if no jank.
  jank_severity_type STRING,
  -- Frame's prediction type (eg. valid / expired).
  prediction_type STRING,
  -- Jank tag based on jank type, used for slice visualization.
  jank_tag STRING
) AS
SELECT
  s.id,
  s.ts,
  s.dur,
  s.track_id,
  s.category,
  s.name,
  s.depth,
  s.parent_id,
  s.arg_set_id,
  extract_arg(s.arg_set_id, 'Display frame token') AS display_frame_token,
  extract_arg(s.arg_set_id, 'Surface frame token') AS surface_frame_token,
  t.upid,
  extract_arg(s.arg_set_id, 'Layer name') AS layer_name,
  extract_arg(s.arg_set_id, 'Present type') AS present_type,
  extract_arg(s.arg_set_id, 'On time finish') AS on_time_finish,
  extract_arg(s.arg_set_id, 'GPU composition') AS gpu_composition,
  extract_arg(s.arg_set_id, 'Jank type') AS jank_type,
  extract_arg(s.arg_set_id, 'Jank severity type') AS jank_severity_type,
  extract_arg(s.arg_set_id, 'Prediction type') AS prediction_type,
  extract_arg(s.arg_set_id, 'Jank tag') AS jank_tag
FROM slice AS s
JOIN process_track AS t
  ON s.track_id = t.id
WHERE
  t.type = 'android_actual_frame_timeline';

-- Stores class information within ART heap graphs. It represents Java/Kotlin
-- classes that exist in the heap, including their names, inheritance
-- relationships, and loading context.
CREATE PERFETTO VIEW heap_graph_class (
  -- Unique identifier for this heap graph class.
  id ID,
  -- (potentially obfuscated) name of the class.
  name STRING,
  -- If class name was obfuscated and deobfuscation map for it provided, the
  -- deobfuscated name.
  deobfuscated_name STRING,
  -- the APK / Dex / JAR file the class is contained in.
  location STRING,
  -- The superclass of this class.
  superclass_id JOINID(heap_graph_class.id),
  -- The classloader that loaded this class.
  classloader_id LONG,
  -- The kind of class.
  kind STRING
) AS
SELECT
  id,
  name,
  deobfuscated_name,
  location,
  superclass_id,
  classloader_id,
  kind
FROM __intrinsic_heap_graph_class;

-- The objects on the Dalvik heap.
--
-- All rows with the same (upid, graph_sample_ts) are one dump.
CREATE PERFETTO VIEW heap_graph_object (
  -- Unique identifier for this heap graph object.
  id ID,
  -- Unique PID of the target.
  upid JOINID(process.id),
  -- Timestamp this dump was taken at.
  graph_sample_ts TIMESTAMP,
  -- Size this object uses on the Java Heap.
  self_size LONG,
  -- Approximate amount of native memory used by this object, as reported by
  -- libcore.util.NativeAllocationRegistry.size.
  native_size LONG,
  -- Join key with heap_graph_reference containing all objects referred in this
  -- object's fields.
  reference_set_id JOINID(heap_graph_reference.id),
  -- Bool whether this object is reachable from a GC root. If false, this object
  -- is uncollected garbage.
  reachable BOOL,
  -- The type of ART heap this object is stored on (app, zygote, boot image)
  heap_type STRING,
  -- Class this object is an instance of.
  type_id JOINID(heap_graph_class.id),
  -- If not NULL, this object is a GC root.
  root_type STRING,
  -- Distance from the root object.
  root_distance LONG
) AS
SELECT
  id,
  upid,
  graph_sample_ts,
  self_size,
  native_size,
  reference_set_id,
  reachable,
  heap_type,
  type_id,
  root_type,
  root_distance
FROM __intrinsic_heap_graph_object;

-- Many-to-many mapping between heap_graph_object.
--
-- This associates the object with given reference_set_id with the objects
-- that are referred to by its fields.
CREATE PERFETTO VIEW heap_graph_reference (
  -- Unique identifier for this heap graph reference.
  id ID,
  -- Join key to heap_graph_object.
  reference_set_id JOINID(heap_graph_object.id),
  -- Id of object that has this reference_set_id.
  owner_id JOINID(heap_graph_object.id),
  -- Id of object that is referred to.
  owned_id JOINID(heap_graph_object.id),
  -- The field that refers to the object. E.g. Foo.name.
  field_name STRING,
  -- The static type of the field. E.g. java.lang.String.
  field_type_name STRING,
  -- The deobfuscated name, if field_name was obfuscated and a deobfuscation
  -- mapping was provided for it.
  deobfuscated_field_name STRING
) AS
SELECT
  id,
  reference_set_id,
  owner_id,
  owned_id,
  field_name,
  field_type_name,
  deobfuscated_field_name
FROM __intrinsic_heap_graph_reference;

-- Table with memory snapshots.
CREATE PERFETTO VIEW memory_snapshot (
  -- Unique identifier for this snapshot.
  id ID,
  -- Time of the snapshot.
  timestamp TIMESTAMP,
  -- Track of this snapshot.
  track_id JOINID(track.id),
  -- Detail level of this snapshot.
  detail_level STRING
) AS
SELECT
  id,
  timestamp,
  track_id,
  detail_level
FROM __intrinsic_memory_snapshot;

-- Table with process memory snapshots.
CREATE PERFETTO VIEW process_memory_snapshot (
  -- Unique identifier for this snapshot.
  id ID,
  -- Snapshot ID for this snapshot.
  snapshot_id JOINID(memory_snapshot.id),
  -- Process for this snapshot.
  upid JOINID(process.id)
) AS
SELECT
  id,
  snapshot_id,
  upid
FROM __intrinsic_process_memory_snapshot;

-- Table with memory snapshot nodes.
CREATE PERFETTO VIEW memory_snapshot_node (
  -- Unique identifier for this node.
  id ID,
  -- Process snapshot ID for to this node.
  process_snapshot_id JOINID(process_memory_snapshot.id),
  -- Parent node for this node, optional.
  parent_node_id JOINID(memory_snapshot_node.id),
  -- Path for this node.
  path STRING,
  -- Size of the memory allocated to this node.
  size LONG,
  -- Effective size used by this node.
  effective_size LONG,
  -- Additional args of the node.
  arg_set_id ARGSETID
) AS
SELECT
  id,
  process_snapshot_id,
  parent_node_id,
  path,
  size,
  effective_size,
  arg_set_id
FROM __intrinsic_memory_snapshot_node;

-- Table with memory snapshot edge
CREATE PERFETTO VIEW memory_snapshot_edge (
  -- Unique identifier for this edge.
  id ID,
  -- Source node for this edge.
  source_node_id JOINID(memory_snapshot_node.id),
  -- Target node for this edge.
  target_node_id JOINID(memory_snapshot_node.id),
  -- Importance for this edge.
  importance LONG
) AS
SELECT
  id,
  source_node_id,
  target_node_id,
  importance
FROM __intrinsic_memory_snapshot_edge;
