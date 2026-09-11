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

-- This gives us access to the raw spans.
SELECT RUN_METRIC('android/android_camera.sql');

DROP VIEW IF EXISTS android_camera_unagg_output;
CREATE PERFETTO VIEW android_camera_unagg_output AS
SELECT
  AndroidCameraUnaggregatedMetric(
    'gc_rss_and_dma', (
      SELECT RepeatedField(
          AndroidCameraUnaggregatedMetric_Value(
            'ts', ts,
            'gca_rss_val', CAST(gca_rss_val AS real),
            'hal_rss_val', CAST(hal_rss_val AS real),
            'cameraserver_rss_val', CAST(cameraserver_rss_val AS real),
            'dma_val', CAST(dma_val AS real),
            'value', CAST(rss_and_dma_val AS real)
          )
      )
      FROM rss_and_dma_all_camera_span
    )
  );
