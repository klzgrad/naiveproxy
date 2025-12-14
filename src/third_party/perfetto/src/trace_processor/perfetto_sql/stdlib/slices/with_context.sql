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

-- All thread slices with data about thread, thread track and process.
CREATE PERFETTO VIEW thread_slice (
  -- Slice
  id ID(slice.id),
  -- Alias for `slice.ts`.
  ts TIMESTAMP,
  -- Alias for `slice.dur`.
  dur DURATION,
  -- Alias for `slice.category`.
  category STRING,
  -- Alias for `slice.name`.
  name STRING,
  -- Alias for `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias for `thread_track.name`.
  track_name STRING,
  -- Alias for `thread.name`.
  thread_name STRING,
  -- Alias for `thread.utid`.
  utid JOINID(thread.id),
  -- Alias for `thread.tid`.
  tid LONG,
  -- Alias for `thread.is_main_thread`.
  is_main_thread BOOL,
  -- Alias for `process.name`.
  process_name STRING,
  -- Alias for `process.upid`.
  upid JOINID(process.id),
  -- Alias for `process.pid`.
  pid LONG,
  -- Alias for `slice.depth`.
  depth LONG,
  -- Alias for `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias for `slice.arg_set_id`.
  arg_set_id ARGSETID,
  -- Alias for `slice.thread_ts`.
  thread_ts TIMESTAMP,
  -- Alias for `slice.thread_dur`.
  thread_dur LONG
) AS
SELECT
  slice.id,
  slice.ts,
  slice.dur,
  slice.category,
  slice.name,
  slice.track_id,
  thread_track.name AS track_name,
  thread.name AS thread_name,
  thread.utid,
  thread.tid,
  thread.is_main_thread,
  process.name AS process_name,
  process.upid,
  process.pid,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id,
  slice.thread_ts,
  slice.thread_dur
FROM slice
JOIN thread_track
  ON slice.track_id = thread_track.id
JOIN thread
  USING (utid)
LEFT JOIN process
  USING (upid);

-- All process slices with data about process track and process.
CREATE PERFETTO VIEW process_slice (
  -- Slice
  id ID(slice.id),
  -- Alias for `slice.ts`.
  ts TIMESTAMP,
  -- Alias for `slice.dur`.
  dur DURATION,
  -- Alias for `slice.category`.
  category STRING,
  -- Alias for `slice.name`.
  name STRING,
  -- Alias for `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias for `process_track.name`.
  track_name STRING,
  -- Alias for `process.name`.
  process_name STRING,
  -- Alias for `process.upid`.
  upid JOINID(process.id),
  -- Alias for `process.pid`.
  pid LONG,
  -- Alias for `slice.depth`.
  depth LONG,
  -- Alias for `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias for `slice.arg_set_id`.
  arg_set_id ARGSETID,
  -- Alias for `slice.thread_ts`.
  thread_ts TIMESTAMP,
  -- Alias for `slice.thread_dur`.
  thread_dur LONG
) AS
SELECT
  slice.id,
  slice.ts,
  slice.dur,
  slice.category,
  slice.name,
  slice.track_id,
  process_track.name AS track_name,
  process.name AS process_name,
  process.upid,
  process.pid,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id,
  slice.thread_ts,
  slice.thread_dur
FROM slice
JOIN process_track
  ON slice.track_id = process_track.id
JOIN process
  USING (upid);

-- All the slices in the trace associated to a thread or a process along
-- with contextual information about them (e.g. thread name, process name, tid etc).
CREATE PERFETTO VIEW thread_or_process_slice (
  -- Slice
  id JOINID(slice.id),
  -- Alias for `slice.ts`.
  ts TIMESTAMP,
  -- Alias for `slice.dur`.
  dur DURATION,
  -- Alias for `slice.category`.
  category STRING,
  -- Alias for `slice.name`.
  name STRING,
  -- Alias for `slice.track_id`.
  track_id JOINID(track.id),
  -- Alias for `track.name`.
  track_name STRING,
  -- Alias for `thread.name`.
  thread_name STRING,
  -- Alias for `thread.utid`.
  utid JOINID(thread.id),
  -- Alias for `thread.tid`
  tid LONG,
  -- Alias for `process.name`.
  process_name STRING,
  -- Alias for `process.upid`.
  upid JOINID(process.id),
  -- Alias for `process.pid`.
  pid LONG,
  -- Alias for `slice.depth`.
  depth LONG,
  -- Alias for `slice.parent_id`.
  parent_id JOINID(slice.id),
  -- Alias for `slice.arg_set_id`.
  arg_set_id ARGSETID
) AS
SELECT
  slice.id,
  slice.ts,
  slice.dur,
  slice.category,
  slice.name,
  slice.track_id,
  thread_track.name AS track_name,
  thread.name AS thread_name,
  thread.utid,
  thread.tid,
  process.name AS process_name,
  process.upid AS upid,
  process.pid AS pid,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id
FROM slice
JOIN thread_track
  ON slice.track_id = thread_track.id
JOIN thread
  USING (utid)
LEFT JOIN process
  USING (upid)
UNION ALL
SELECT
  slice.id,
  slice.ts,
  slice.dur,
  slice.category,
  slice.name,
  slice.track_id,
  process_track.name AS track_name,
  NULL AS thread_name,
  NULL AS utid,
  NULL AS tid,
  process.name AS process_name,
  process.upid AS upid,
  process.pid AS pid,
  slice.depth,
  slice.parent_id,
  slice.arg_set_id
FROM slice
JOIN process_track
  ON slice.track_id = process_track.id
JOIN process
  USING (upid);
