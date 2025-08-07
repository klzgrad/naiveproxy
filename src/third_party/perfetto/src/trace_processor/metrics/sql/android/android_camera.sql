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

-- This gives us access to the RSS breakdowns.
SELECT RUN_METRIC('android/process_mem.sql');

-- Compute DMA spans.
SELECT RUN_METRIC('android/global_counter_span_view.sql',
  'table_name', 'dma',
  'counter_name', 'mem.dma_heap');

-- RSS of GCA.
DROP VIEW IF EXISTS rss_gca;
CREATE PERFETTO VIEW rss_gca AS
SELECT ts, dur, rss_val AS gca_rss_val
FROM rss_and_swap_span
JOIN (
  SELECT max(rss), upid
  FROM process
  JOIN (
    SELECT max(rss_val) as rss, upid FROM rss_and_swap_span GROUP BY upid
  ) USING (upid)
  WHERE name GLOB '*GoogleCamera'
    OR name GLOB '*googlecamera.fishfood'
    OR name GLOB '*GoogleCameraEng'
  LIMIT 1
) AS gca USING (upid);

-- RSS of camera HAL.
DROP VIEW IF EXISTS rss_camera_hal;
CREATE PERFETTO VIEW rss_camera_hal AS
SELECT ts, dur, rss_val AS hal_rss_val
FROM rss_and_swap_span
JOIN (
  SELECT max(start_ts), upid
  FROM process
  WHERE name GLOB '*camera.provider*'
  LIMIT 1
) AS hal USING (upid);

-- RSS of cameraserver.
DROP VIEW IF EXISTS rss_cameraserver;
CREATE PERFETTO VIEW rss_cameraserver AS
SELECT ts, dur, rss_val AS cameraserver_rss_val
FROM rss_and_swap_span
JOIN (
  SELECT max(start_ts), upid
  FROM process
  WHERE name GLOB '*cameraserver'
  LIMIT 1
) AS cameraserver USING (upid);

-- RSS of GCA + HAL.
DROP TABLE IF EXISTS rss_gca_hal;
CREATE VIRTUAL TABLE rss_gca_hal
USING SPAN_OUTER_JOIN(rss_gca, rss_camera_hal);

-- RSS of GCA + HAL + cameraserver.
DROP TABLE IF EXISTS rss_all_camera;
CREATE VIRTUAL TABLE rss_all_camera
USING SPAN_OUTER_JOIN(rss_gca_hal, rss_cameraserver);

-- RSS of GCA + HAL + cameraserver + DMA.
DROP TABLE IF EXISTS rss_and_dma_all_camera_join;
CREATE VIRTUAL TABLE rss_and_dma_all_camera_join
USING SPAN_OUTER_JOIN(dma_span, rss_all_camera);

DROP VIEW IF EXISTS rss_and_dma_all_camera_span;
CREATE PERFETTO VIEW rss_and_dma_all_camera_span AS
SELECT
  ts,
  dur,
  IFNULL(gca_rss_val, 0) as gca_rss_val,
  IFNULL(hal_rss_val, 0) as hal_rss_val,
  IFNULL(cameraserver_rss_val, 0) as cameraserver_rss_val,
  IFNULL(dma_val, 0) as dma_val,
  CAST(
    IFNULL(gca_rss_val, 0)
    + IFNULL(hal_rss_val, 0)
    + IFNULL(cameraserver_rss_val, 0)
    + IFNULL(dma_val, 0) AS int) AS rss_and_dma_val
FROM rss_and_dma_all_camera_join;

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
