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

-- @module prelude.after_eof.events
-- Event data and slices for trace analysis.
--
-- This module provides tables and views for analyzing various types of
-- events and slices including ftrace events, graphics frames, GPU events,
-- and frame timeline information.

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
  t.type = 'android_expected_frame_timeline'
ORDER BY
  s.id;

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
  upid JOINID(process.id),
  -- Unique process id of the app that generates the surface frame.
  surface_frame_token LONG,
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
  jank_tag STRING,
  -- Jank tag (experimental) based on jank type, used for slice visualization.
  jank_tag_experimental STRING
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
  extract_arg(s.arg_set_id, 'Jank tag') AS jank_tag,
  extract_arg(s.arg_set_id, 'Jank tag (experimental)') AS jank_tag_experimental
FROM slice AS s
JOIN process_track AS t
  ON s.track_id = t.id
WHERE
  t.type = 'android_actual_frame_timeline'
ORDER BY
  s.id;
