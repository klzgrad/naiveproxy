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
INCLUDE PERFETTO MODULE android.cujs.slices;


DROP VIEW IF EXISTS android_jank_cuj_slice;
CREATE PERFETTO VIEW android_jank_cuj_slice AS
SELECT * FROM _android_jank_cuj_slice;

DROP TABLE IF EXISTS android_jank_cuj_main_thread_slice;
CREATE PERFETTO TABLE android_jank_cuj_main_thread_slice AS
SELECT * FROM _android_jank_cuj_main_thread_slice;

DROP TABLE IF EXISTS android_jank_cuj_render_thread_slice;
CREATE PERFETTO TABLE android_jank_cuj_render_thread_slice AS
SELECT * FROM _android_jank_cuj_render_thread_slice;

DROP VIEW IF EXISTS android_jank_cuj_sf_slice;
CREATE PERFETTO VIEW android_jank_cuj_sf_slice AS
SELECT * FROM _android_jank_cuj_sf_slice;

DROP TABLE IF EXISTS android_jank_cuj_sf_main_thread_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_main_thread_slice AS
SELECT * FROM _android_jank_cuj_sf_main_thread_slice;

DROP TABLE IF EXISTS android_jank_cuj_sf_render_engine_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_render_engine_slice AS
SELECT * FROM _android_jank_cuj_sf_render_engine_slice;
