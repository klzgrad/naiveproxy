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

-- @module prelude.after_eof.tracks
-- Track infrastructure for organizing trace events.
--
-- This module provides the track concept and specialized track tables for
-- organizing events by thread, process, CPU, and GPU contexts.

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
  machine_id LONG,
  -- An opaque key indicating that this track belongs to a group of tracks which
  -- are "conceptually" the same track.
  --
  -- Tracks in trace processor don't allow overlapping events to allow for easy
  -- analysis (i.e. SQL window functions, SPAN JOIN and other similar
  -- operators). However, in visualization settings (e.g. the UI), the
  -- distinction doesn't matter and all tracks with the same `track_group_id`
  -- should be merged together into a single logical "UI track".
  track_group_id LONG
) AS
SELECT
  id,
  name,
  type,
  dimension_arg_set_id,
  parent_id,
  source_arg_set_id,
  machine_id,
  track_group_id
FROM __intrinsic_track;

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
