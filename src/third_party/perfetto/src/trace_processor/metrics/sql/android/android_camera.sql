--
-- Copyright 2019 The Android Open Source Project
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

-- This imports the Pixel-specific camera memory tracking tables.
INCLUDE PERFETTO MODULE pixel.camera;
DROP VIEW IF EXISTS rss_and_dma_all_camera_span;
CREATE PERFETTO VIEW rss_and_dma_all_camera_span AS
SELECT
  ts,
  dur,
  gca_rss AS gca_rss_val,
  hal_rss AS hal_rss_val,
  cameraserver_rss AS cameraserver_rss_val,
  dma AS dma_val,
  rss_and_dma AS rss_and_dma_val
FROM pixel_camera_memory_span;

-- we are dividing and casting to real when calculating avg_value
-- to avoid issues such as the one in b/203613535
DROP VIEW IF EXISTS rss_and_dma_all_camera_stats;
CREATE PERFETTO VIEW rss_and_dma_all_camera_stats AS
SELECT
  MIN(rss_and_dma_val) AS min_value,
  MAX(rss_and_dma_val) AS max_value,
  SUM(rss_and_dma_val * dur / 1e3) / SUM(dur / 1e3) AS avg_value
FROM rss_and_dma_all_camera_span;

DROP VIEW IF EXISTS android_camera_output;
CREATE PERFETTO VIEW android_camera_output AS
SELECT
  AndroidCameraMetric(
    'gc_rss_and_dma', AndroidCameraMetric_Counter(
      'min', CAST(min_value AS real),
      'max', CAST(max_value AS real),
      'avg', CAST(avg_value AS real)
    )
  )
FROM rss_and_dma_all_camera_stats;
