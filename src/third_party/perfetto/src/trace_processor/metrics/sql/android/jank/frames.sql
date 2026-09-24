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
--

INCLUDE PERFETTO MODULE android.cujs.frames;

-- NOTE: We preserve the legacy table names in this file because external
-- consumers and analytical pipelines outside the Perfetto project rely on them.

DROP TABLE IF EXISTS android_jank_cuj_frame_timeline;
CREATE PERFETTO TABLE android_jank_cuj_frame_timeline AS
SELECT * FROM _android_jank_cuj_frame_timeline;

DROP TABLE IF EXISTS android_jank_cuj_frame;
CREATE PERFETTO TABLE android_jank_cuj_frame AS
SELECT * FROM _android_jank_cuj_frame;

DROP TABLE IF EXISTS android_jank_cuj_sf_frame;
CREATE PERFETTO TABLE android_jank_cuj_sf_frame AS
SELECT * FROM _android_jank_cuj_sf_frame;
