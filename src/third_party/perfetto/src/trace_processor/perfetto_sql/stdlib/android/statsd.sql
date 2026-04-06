--
-- Copyright 2023 The Android Open Source Project
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
--

-- Statsd atoms.
--
-- A subset of the slice table containing statsd atom instant events.
CREATE PERFETTO VIEW android_statsd_atoms (
  -- Unique identifier for this slice.
  id LONG,
  -- The timestamp at the start of the slice.
  ts TIMESTAMP,
  -- The duration of the slice.
  dur DURATION,
  -- The id of the argument set associated with this slice.
  arg_set_id ARGSETID,
  -- The value of the CPU instruction counter at the start of the slice. This column will only be populated if thread instruction collection is enabled with track_event.
  thread_instruction_count LONG,
  -- The change in value of the CPU instruction counter between the start and end of the slice. This column will only be populated if thread instruction collection is enabled with track_event.
  thread_instruction_delta LONG,
  -- The id of the track this slice is located on.
  track_id JOINID(track.id),
  -- The "category" of the slice. If this slice originated with track_event, this column contains the category emitted. Otherwise, it is likely to be null (with limited exceptions).
  category STRING,
  -- The name of the slice. The name describes what was happening during the slice.
  name STRING,
  -- The depth of the slice in the current stack of slices.
  depth LONG,
  -- The id of the parent (i.e. immediate ancestor) slice for this slice.
  parent_id LONG,
  -- The thread timestamp at the start of the slice. This column will only be populated if thread timestamp collection is enabled with track_event.
  thread_ts TIMESTAMP,
  -- The thread time used by this slice. This column will only be populated if thread timestamp collection is enabled with track_event.
  thread_dur LONG
) AS
SELECT
  slice.id AS id,
  slice.ts AS ts,
  slice.dur AS dur,
  slice.arg_set_id AS arg_set_id,
  slice.thread_instruction_count AS thread_instruction_count,
  slice.thread_instruction_delta AS thread_instruction_delta,
  slice.track_id AS track_id,
  slice.category AS category,
  slice.name AS name,
  slice.depth AS depth,
  slice.parent_id AS parent_id,
  slice.thread_ts AS thread_ts,
  slice.thread_dur AS thread_dur
FROM slice
JOIN track
  ON slice.track_id = track.id
WHERE
  track.name = 'Statsd Atoms';

-- Information about Perfetto triggers, extracted from statsd atoms, which
-- happened during the trace.
--
-- This requires the `android.statsd` data-source to be enabled and the
-- `ATOM_PERFETTO_TRIGGER` push atom to be configured.
CREATE PERFETTO TABLE _android_statsd_perfetto_triggers (
  -- Timestamp of the trigger.
  ts TIMESTAMP,
  -- The name of the trigger.
  trigger_name STRING
) AS
SELECT
  ts,
  extract_arg(arg_set_id, 'perfetto_trigger.trigger_name') AS trigger_name
FROM android_statsd_atoms
WHERE
  name = 'perfetto_trigger';
