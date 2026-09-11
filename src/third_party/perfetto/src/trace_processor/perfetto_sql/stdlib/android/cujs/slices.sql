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

INCLUDE PERFETTO MODULE android.cujs.boundaries;

INCLUDE PERFETTO MODULE android.cujs.threads;

-- Slices overlapping with the CUJ boundaries in the app processes.
CREATE PERFETTO TABLE _android_jank_cuj_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process ID of the process.
  upid JOINID(process.id),
  -- Name of the process.
  process_name STRING,
  -- Thread ID of the thread.
  utid JOINID(thread.id),
  -- Name of the thread.
  thread_name STRING,
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the slice.
  ts_end TIMESTAMP
)
AS
SELECT
  cuj_id,
  process.upid,
  process.name AS process_name,
  thread.utid,
  thread.name AS thread_name,
  slice.id,
  slice.ts,
  slice.dur,
  slice.track_id,
  slice.name,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id,
  slice.thread_ts,
  slice.thread_dur,
  slice.slice_id,
  slice.ts + slice.dur AS ts_end
FROM _android_jank_cuj_boundary AS boundary
JOIN process USING (upid)
JOIN thread USING (upid)
JOIN thread_track USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
  -- Take slices which overlap even they started before the boundaries
  -- This is to be able to query slices that delayed start of a frame
  AND slice.ts + slice.dur >= boundary.ts
  AND slice.ts <= boundary.ts_end
WHERE
  slice.dur > 0;

-- Slices overlapping with the main thread CUJ boundaries in the app processes.
CREATE PERFETTO TABLE _android_jank_cuj_main_thread_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process ID of the process.
  upid JOINID(process.id),
  -- Thread ID of the thread.
  utid JOINID(thread.id),
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the slice.
  ts_end TIMESTAMP
)
AS
SELECT cuj_id, upid, utid, slice.*, slice.ts + slice.dur AS ts_end
FROM _android_jank_cuj_main_thread_cuj_boundary AS boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
  -- Take slices which overlap even they started before the boundaries
  -- This is to be able to query slices that delayed start of a frame
  AND slice.ts + slice.dur >= boundary.ts
  AND slice.ts <= boundary.ts_end
WHERE
  slice.dur > 0;

-- Slices overlapping with the render thread CUJ boundaries in the app processes.
CREATE PERFETTO TABLE _android_jank_cuj_render_thread_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process ID of the process.
  upid JOINID(process.id),
  -- Thread ID of the thread.
  utid JOINID(thread.id),
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Category of the slice.
  category STRING,
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Thread instruction count.
  thread_instruction_count LONG,
  -- Thread instruction delta.
  thread_instruction_delta LONG,
  -- Legacy category alias.
  cat STRING,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the slice.
  ts_end TIMESTAMP
)
AS
SELECT cuj_id, upid, utid, slice.*, slice.ts + slice.dur AS ts_end
FROM _android_jank_cuj_render_thread_cuj_boundary AS boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
  -- Take slices which overlap even they started before the boundaries
  -- This is to be able to query slices that delayed start of a frame
  AND slice.ts + slice.dur >= boundary.ts
  AND slice.ts <= boundary.ts_end
WHERE
  slice.dur > 0;

-- Slices overlapping with the SF CUJ boundaries in SurfaceFlinger.
CREATE PERFETTO TABLE _android_jank_cuj_sf_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process ID of the process.
  upid JOINID(process.id),
  -- Name of the process.
  process_name STRING,
  -- Thread ID of the thread.
  utid JOINID(thread.id),
  -- Name of the thread.
  thread_name STRING,
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the slice.
  ts_end TIMESTAMP
)
AS
SELECT
  cuj_id,
  upid,
  sf_process.name AS process_name,
  thread.utid,
  thread.name AS thread_name,
  slice.id,
  slice.ts,
  slice.dur,
  slice.track_id,
  slice.name,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id,
  slice.thread_ts,
  slice.thread_dur,
  slice.slice_id,
  slice.ts + slice.dur AS ts_end
FROM _android_jank_cuj_sf_boundary AS sf_boundary
JOIN _android_jank_cuj_sf_process AS sf_process
JOIN thread USING (upid)
JOIN thread_track USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
  -- Take slices which overlap even they started before the boundaries
  -- This is to be able to query slices that delayed start of a frame
  AND slice.ts + slice.dur >= sf_boundary.ts
  AND slice.ts <= sf_boundary.ts_end
WHERE
  slice.dur > 0;

-- Slices overlapping with the SF main thread CUJ boundaries in SurfaceFlinger.
CREATE PERFETTO TABLE _android_jank_cuj_sf_main_thread_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process ID of the process.
  upid JOINID(process.id),
  -- Thread ID of the thread.
  utid JOINID(thread.id),
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the slice.
  ts_end TIMESTAMP
)
AS
SELECT
  cuj_id,
  upid,
  utid,
  slice.id,
  slice.ts,
  slice.dur,
  slice.track_id,
  slice.name,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id,
  slice.thread_ts,
  slice.thread_dur,
  slice.slice_id,
  slice.ts + slice.dur AS ts_end
FROM _android_jank_cuj_sf_main_thread_cuj_boundary AS boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
  -- Take slices which overlap even they started before the boundaries
  -- This is to be able to query slices that delayed start of a frame
  AND slice.ts + slice.dur >= boundary.ts
  AND slice.ts <= boundary.ts_end
WHERE
  slice.dur > 0;

-- For RenderEngine thread we use a different approach as it's only used when SF falls back to
-- client composition. Instead of taking all slices during CUJ, we look at each frame explicitly
-- and only take slices that are within RenderEngine frame boundaries.
CREATE PERFETTO TABLE _android_jank_cuj_sf_render_engine_slice(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- Process ID of the process.
  upid JOINID(process.id),
  -- Thread ID of the thread.
  utid JOINID(thread.id),
  -- Slice id.
  id ID(slice.id),
  -- Timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice.
  dur LONG,
  -- Track id of the slice.
  track_id JOINID(track.id),
  -- Name of the slice.
  name STRING,
  -- Depth of the slice in the stack.
  depth LONG,
  -- Parent slice id.
  parent_id LONG,
  -- Arg set id.
  arg_set_id LONG,
  -- Thread timestamp.
  thread_ts TIMESTAMP,
  -- Thread duration.
  thread_dur LONG,
  -- Legacy slice id alias.
  slice_id LONG,
  -- End timestamp of the slice.
  ts_end TIMESTAMP
)
AS
SELECT
  cuj_id,
  upid,
  utid,
  slice.id,
  slice.ts,
  slice.dur,
  slice.track_id,
  slice.name,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id,
  slice.thread_ts,
  slice.thread_dur,
  slice.slice_id,
  slice.ts + slice.dur AS ts_end
FROM _android_jank_cuj_sf_render_engine_frame_boundary AS boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
  -- Take slices which overlap even they started before the boundaries
  -- This is to be able to query slices that delayed start of a frame
  AND slice.ts + slice.dur >= boundary.ts
  AND slice.ts <= boundary.ts_end
WHERE
  slice.dur > 0;
