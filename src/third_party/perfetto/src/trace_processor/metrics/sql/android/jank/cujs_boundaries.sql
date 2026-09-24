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

INCLUDE PERFETTO MODULE android.cujs.boundaries;

-- NOTE: We preserve the legacy table names in this file because external
-- consumers and analytical pipelines outside the Perfetto project rely on them.
-- Tables that could be fully migrated to stdlib are re-exported from
-- android.cujs.boundaries. Tables that depend on metrics-only tables
-- (from relevant_slices.sql) remain defined here.

DROP TABLE IF EXISTS android_jank_cuj_vsync_boundary;
CREATE PERFETTO TABLE android_jank_cuj_vsync_boundary AS
SELECT * FROM _android_jank_cuj_vsync_boundary;

DROP TABLE IF EXISTS android_jank_cuj_sf_vsync_boundary;
CREATE PERFETTO TABLE android_jank_cuj_sf_vsync_boundary AS
SELECT * FROM _android_jank_cuj_sf_vsync_boundary;

DROP TABLE IF EXISTS android_jank_cuj_main_thread_frame_boundary;
CREATE PERFETTO TABLE android_jank_cuj_main_thread_frame_boundary AS
SELECT * FROM _android_jank_cuj_main_thread_frame_boundary;

DROP TABLE IF EXISTS android_jank_cuj_main_thread_cuj_boundary;
CREATE PERFETTO TABLE android_jank_cuj_main_thread_cuj_boundary AS
SELECT * FROM _android_jank_cuj_main_thread_cuj_boundary;

DROP TABLE IF EXISTS android_jank_cuj_render_thread_frame_boundary;
CREATE PERFETTO TABLE android_jank_cuj_render_thread_frame_boundary AS
SELECT * FROM _android_jank_cuj_render_thread_frame_boundary;

DROP TABLE IF EXISTS android_jank_cuj_render_thread_cuj_boundary;
CREATE PERFETTO TABLE android_jank_cuj_render_thread_cuj_boundary AS
SELECT * FROM _android_jank_cuj_render_thread_cuj_boundary;

DROP TABLE IF EXISTS android_jank_cuj_boundary;
CREATE PERFETTO TABLE android_jank_cuj_boundary AS
SELECT * FROM _android_jank_cuj_boundary;

DROP TABLE IF EXISTS android_jank_cuj_sf_main_thread_frame_boundary;
CREATE PERFETTO TABLE android_jank_cuj_sf_main_thread_frame_boundary AS
SELECT * FROM _android_jank_cuj_sf_main_thread_frame_boundary;

DROP TABLE IF EXISTS android_jank_cuj_sf_main_thread_cuj_boundary;
CREATE PERFETTO TABLE android_jank_cuj_sf_main_thread_cuj_boundary AS
SELECT * FROM _android_jank_cuj_sf_main_thread_cuj_boundary;

DROP TABLE IF EXISTS android_jank_cuj_sf_render_engine_frame_boundary;
CREATE PERFETTO TABLE android_jank_cuj_sf_render_engine_frame_boundary AS
SELECT * FROM _android_jank_cuj_sf_render_engine_frame_boundary;

DROP TABLE IF EXISTS android_jank_cuj_sf_boundary;
CREATE PERFETTO TABLE android_jank_cuj_sf_boundary AS
SELECT * FROM _android_jank_cuj_sf_boundary;
