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

-- Categorizes whether the jank was caused by Surface Flinger
CREATE PERFETTO FUNCTION android_is_sf_jank_type(
    -- the jank type
    -- from args.display_value with key = "Jank type"
    jank_type STRING
)
-- True when the jank type represents sf jank
RETURNS BOOL AS
SELECT
  $jank_type GLOB '*SurfaceFlinger CPU Deadline Missed*'
  OR $jank_type GLOB '*SurfaceFlinger GPU Deadline Missed*'
  OR $jank_type GLOB '*SurfaceFlinger Scheduling*'
  OR $jank_type GLOB '*Prediction Error*'
  OR $jank_type GLOB '*Display HAL*';

-- Categorizes whether the jank was caused by the app
CREATE PERFETTO FUNCTION android_is_app_jank_type(
    -- the jank type
    -- from args.display_value with key = "Jank type"
    jank_type STRING
)
-- True when the jank type represents app jank
RETURNS BOOL AS
SELECT
  $jank_type GLOB '*App Deadline Missed*'
  OR $jank_type GLOB '*App Resynced Jitter*';

-- Categorizes whether the jank was caused by the sf, app or "Dropped Frame"
CREATE PERFETTO FUNCTION android_is_missed_frame_type(
    -- the jank type
    -- from args.display_value with key = "Jank type"
    jank_type STRING
)
-- True if jank_type represents missed frame jank
RETURNS BOOL AS
SELECT
  android_is_sf_jank_type($jank_type)
  OR android_is_app_jank_type($jank_type)
  OR $jank_type GLOB '*Dropped Frame*';
