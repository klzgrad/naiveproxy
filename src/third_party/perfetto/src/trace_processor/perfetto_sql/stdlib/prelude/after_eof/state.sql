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

-- @module prelude.after_eof.state
-- State tracks and state events.
--
-- This module provides state-related tables and views for analyzing
-- states collected across processes and other contexts.

INCLUDE PERFETTO MODULE prelude.after_eof.views;

-- Tracks containing state events.
CREATE PERFETTO VIEW state_track(
  -- Unique identifier for this state track.
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
  -- Machine identifier
  machine_id JOINID(machine.id)
)
AS
SELECT
  id,
  name,
  parent_id,
  type,
  dimension_arg_set_id,
  source_arg_set_id,
  machine_id
FROM __intrinsic_track
WHERE
  event_type = 'state';

-- Tracks containing state events associated to a process.
CREATE PERFETTO TABLE process_state_track(
  -- Unique identifier for this process state track.
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
  -- Machine identifier
  machine_id JOINID(machine.id),
  -- The upid that the track is associated with.
  upid JOINID(process.id)
)
AS
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
  t.event_type = 'state'
  AND a.key = 'upid';

-- Tracks containing state events associated to a thread.
CREATE PERFETTO TABLE thread_state_track(
  -- Unique identifier for this thread state track.
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
  -- Machine identifier
  machine_id JOINID(machine.id),
  -- The utid that the track is associated with.
  utid JOINID(thread.id)
)
AS
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
  t.event_type = 'state'
  AND a.key = 'utid';
