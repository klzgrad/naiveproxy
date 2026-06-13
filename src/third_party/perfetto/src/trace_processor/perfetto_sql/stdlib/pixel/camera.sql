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
--

INCLUDE PERFETTO MODULE slices.with_context;

-- Break down camera Camera graph execution slices per node, port group, and frame.
-- This table extracts key identifiers from Camera graph execution slice names and
-- provides timing information for each processing stage.
CREATE PERFETTO TABLE pixel_camera_frames (
  -- Unique identifier for this slice.
  id ID(slice.id),
  -- Start timestamp of the slice.
  ts TIMESTAMP,
  -- Duration of the slice execution.
  dur DURATION,
  -- Track ID for this slice.
  track_id JOINID(track.id),
  -- Thread ID (utid) executing this slice.
  utid JOINID(thread.id),
  -- Name of the thread executing this slice.
  thread_name STRING,
  -- Name of the processing node in the Camera graph.
  node STRING,
  -- Port group name for the node.
  port_group STRING,
  -- Frame number being processed.
  frame_number LONG,
  -- Camera ID associated with this slice.
  cam_id LONG
) AS
SELECT
  id,
  ts,
  dur,
  track_id,
  utid,
  thread_name,
  -- Slices follow the pattern "camX_Y:Z (frame N)" where X is the camera ID,
  -- Y is the node name, Z is the port group, and N is the frame number
  substr(str_split(name, ':', 0), 6) AS node,
  str_split(str_split(name, ':', 1), ' (', 0) AS port_group,
  cast_int!(STR_SPLIT(STR_SPLIT(name, '(frame', 1), ')', 0)) AS frame_number,
  cast_int!(STR_SPLIT(STR_SPLIT(name, 'cam', 1), '_', 0)) AS cam_id
FROM thread_slice
-- Only include slices matching the Camera graph pattern and with valid durations
WHERE
  name GLOB 'cam*_*:* (frame *)' AND dur != -1;
