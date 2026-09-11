--
-- Copyright 2021 The Android Open Source Project
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

-- Find all dropped frames, i.e. all PipelineReporters slices whose
-- state is 'STATE_DROPPED'.
DROP TABLE IF EXISTS dropped_pipeline_reporter_slice;
CREATE PERFETTO TABLE dropped_pipeline_reporter_slice AS
SELECT slice.* FROM slice
JOIN args
  ON slice.arg_set_id = args.arg_set_id
WHERE
  slice.name = 'PipelineReporter'
  AND args.string_value = 'STATE_DROPPED';

-- Find the upid of the proccesses where the dropped frames occur.
DROP VIEW IF EXISTS dropped_frames_with_upid;
CREATE PERFETTO VIEW dropped_frames_with_upid AS
SELECT
  dropped_pipeline_reporter_slice.ts,
  process_track.upid
FROM dropped_pipeline_reporter_slice
JOIN process_track
  ON dropped_pipeline_reporter_slice.track_id = process_track.id;

-- Find the name and pid of the processes.
-- If the process name represents a file's pathname, the path part will be
-- removed from the display name of the process.
DROP VIEW IF EXISTS dropped_frames_with_process_info;
CREATE PERFETTO VIEW dropped_frames_with_process_info AS
SELECT
  dropped_frames_with_upid.ts,
  REPLACE(
    process.name,
    RTRIM(
      process.name,
      REPLACE(process.name, '/', '')
    ),
    '') AS process_name,
  process.pid AS process_id
FROM dropped_frames_with_upid
JOIN process
  ON dropped_frames_with_upid.upid = process.upid;

-- Create the dropped frames metric output.
DROP VIEW IF EXISTS chrome_dropped_frames_output;
CREATE PERFETTO VIEW chrome_dropped_frames_output AS
SELECT ChromeDroppedFrames(
  'dropped_frame', (
    SELECT RepeatedField(
      ChromeDroppedFrames_DroppedFrame(
        'ts', ts,
        'process_name', process_name,
        'pid', process_id
      )
    )
    FROM dropped_frames_with_process_info
    ORDER BY ts
  )
);
