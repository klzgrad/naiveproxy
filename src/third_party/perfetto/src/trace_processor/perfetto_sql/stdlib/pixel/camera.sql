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

INCLUDE PERFETTO MODULE counters.global_tracks;

INCLUDE PERFETTO MODULE linux.memory.process;

INCLUDE PERFETTO MODULE slices.with_context;

-- Break down camera Camera graph execution slices per node, port group, and frame.
-- This table extracts key identifiers from Camera graph execution slice names and
-- provides timing information for each processing stage.
CREATE PERFETTO TABLE pixel_camera_frames(
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
)
AS
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
  name GLOB 'cam*_*:* (frame *)'
  AND dur != -1;

-- RSS of GoogleCamera application.
CREATE PERFETTO VIEW _rss_gca AS
SELECT
  ts,
  dur,
  coalesce(file_rss, 0) + coalesce(anon_rss, 0) + coalesce(shmem_rss, 0) AS gca_rss_val
FROM memory_rss_and_swap_per_process
JOIN (
  SELECT max(rss), upid
  FROM process
  JOIN (
    SELECT
      max(
        coalesce(file_rss, 0) + coalesce(anon_rss, 0) + coalesce(shmem_rss, 0)
      ) AS rss,
      upid
    FROM memory_rss_and_swap_per_process
    GROUP BY
      upid
  ) USING (upid)
  WHERE
    name GLOB '*GoogleCamera'
    OR name GLOB '*googlecamera.fishfood'
    OR name GLOB '*GoogleCameraEng'
  LIMIT 1
) USING (upid);

-- RSS of camera HAL.
CREATE PERFETTO VIEW _rss_camera_hal AS
SELECT
  ts,
  dur,
  coalesce(file_rss, 0) + coalesce(anon_rss, 0) + coalesce(shmem_rss, 0) AS hal_rss_val
FROM memory_rss_and_swap_per_process
JOIN (
  SELECT max(start_ts), upid
  FROM process
  WHERE
    name GLOB '*camera.provider*'
  LIMIT 1
) USING (upid);

-- RSS of cameraserver.
CREATE PERFETTO VIEW _rss_cameraserver AS
SELECT
  ts,
  dur,
  coalesce(file_rss, 0) + coalesce(anon_rss, 0) + coalesce(shmem_rss, 0) AS cameraserver_rss_val
FROM memory_rss_and_swap_per_process
JOIN (
  SELECT max(start_ts), upid
  FROM process
  WHERE
    name GLOB '*cameraserver'
  LIMIT 1
) USING (upid);

-- Spans of DMA heap usage.
CREATE PERFETTO VIEW _dma_span AS
SELECT
  ts,
  LEAD(ts, 1, trace_end()) OVER (PARTITION BY track_id ORDER BY ts) - ts AS dur,
  value AS dma_val
FROM counter AS c
JOIN counter_track AS t
  ON t.id = c.track_id
WHERE
  _counter_track_is_only_name_dimension(t.id)
  AND name = 'mem.dma_heap';

-- Spans for GoogleCamera + Camera HAL.
CREATE VIRTUAL TABLE _rss_gca_hal USING SPAN_OUTER_JOIN(_rss_gca, _rss_camera_hal);

-- Spans for GoogleCamera + Camera HAL + CameraServer.
CREATE VIRTUAL TABLE _rss_all_camera USING SPAN_OUTER_JOIN(_rss_gca_hal, _rss_cameraserver);

-- Spans joining all camera processes RSS and global DMA heap values.
CREATE VIRTUAL TABLE _rss_and_dma_all_camera_join USING SPAN_OUTER_JOIN(_dma_span, _rss_all_camera);

-- Process memory and DMA heap usage timeline for Pixel Camera processes (GCA, HAL, CameraServer).
CREATE PERFETTO TABLE pixel_camera_memory_span(
  -- Timestamp.
  ts TIMESTAMP,
  -- Duration of the span.
  dur DURATION,
  -- RSS of GoogleCamera process.
  gca_rss LONG,
  -- RSS of Camera HAL process (camera.provider).
  hal_rss LONG,
  -- RSS of CameraServer process.
  cameraserver_rss LONG,
  -- Value of DMA heap counters.
  dma LONG,
  -- Sum of all camera RSS and DMA heap memory values.
  rss_and_dma LONG
)
AS
SELECT
  ts,
  dur,
  cast_int!(IFNULL(gca_rss_val, 0)) AS gca_rss,
  cast_int!(IFNULL(hal_rss_val, 0)) AS hal_rss,
  cast_int!(IFNULL(cameraserver_rss_val, 0)) AS cameraserver_rss,
  cast_int!(IFNULL(dma_val, 0)) AS dma,
  cast_int!(IFNULL(gca_rss_val, 0) + IFNULL(hal_rss_val, 0)
    + IFNULL(cameraserver_rss_val, 0)
    + IFNULL(dma_val, 0)) AS rss_and_dma
FROM _rss_and_dma_all_camera_join;
