--
-- Copyright 2022 The Android Open Source Project
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

-- Find all long EventLatency slices > 100ms and also get the
-- type of the event stored as 'debug.event' argument.
-- In order to group all events
-- Note that a long latency event is represented by the ending time
-- of an EventLatency slice, i.e. the timestamp of the frame presentation
-- that reflects the event.
DROP VIEW IF EXISTS long_eventlatency_slice;
CREATE PERFETTO VIEW long_eventlatency_slice AS
SELECT
  ts + dur AS ts,
  dur,
  id,
  track_id,
  EXTRACT_ARG(arg_set_id, 'debug.event') AS event_type
FROM slice WHERE name = 'EventLatency' AND dur > 100000000;

-- Find the upid of the proccesses where the long latency occur.
DROP VIEW IF EXISTS long_latency_with_upid;
CREATE PERFETTO VIEW long_latency_with_upid AS
SELECT
  long_eventlatency_slice.ts,
  long_eventlatency_slice.event_type,
  process_track.upid
FROM long_eventlatency_slice
JOIN process_track
  ON long_eventlatency_slice.track_id = process_track.id;

-- Find the name and pid of the processes.
-- Long latency events with the same timestamp and from the same process
-- are considered one single long latency occurrence.
-- If the process name represents a file's pathname, the path part will be
-- removed from the display name of the process.
DROP VIEW IF EXISTS long_latency_with_process_info;
CREATE PERFETTO VIEW long_latency_with_process_info AS
SELECT
  long_latency_with_upid.ts,
  GROUP_CONCAT(DISTINCT long_latency_with_upid.event_type) AS event_type,
  REPLACE(
    process.name,
    RTRIM(
      process.name,
      REPLACE(process.name, '/', '')
    ),
    '') AS process_name,
  process.pid AS process_id
FROM long_latency_with_upid
JOIN process
  ON long_latency_with_upid.upid = process.upid
GROUP BY ts, process.pid;

-- Create the long latency metric output.
DROP VIEW IF EXISTS chrome_long_latency_output;
CREATE PERFETTO VIEW chrome_long_latency_output AS
SELECT ChromeLongLatency(
  'long_latency', (
    SELECT RepeatedField(
      ChromeLongLatency_LongLatency(
        'ts', ts,
        'event_type', event_type,
        'process_name', process_name,
        'pid', process_id
      )
    )
    FROM long_latency_with_process_info
    ORDER BY ts
  )
);
