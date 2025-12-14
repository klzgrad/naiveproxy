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

-- @module prelude.after_eof.counters
-- Performance counters and counter tracks.
--
-- This module provides counter-related tables and views for analyzing
-- performance metrics collected across CPUs, processes, threads, GPUs,
-- and other contexts.

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
  -- The CPU the counter is associated with. Can be null if the counter is not
  -- associated with any CPU.
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
  ct.type IN ('perf_cpu_counter', 'perf_global_counter');

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
