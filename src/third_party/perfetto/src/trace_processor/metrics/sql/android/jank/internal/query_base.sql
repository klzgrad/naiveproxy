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

-- Stores sets of tables that make sense together (e.g. slices on the main thread
-- and frame boundaries on the main thread).
-- Used to simplify passing arguments to other functions / metrics.
DROP TABLE IF EXISTS android_jank_cuj_table_set;
CREATE TABLE android_jank_cuj_table_set(
  name TEXT,
  slice_table_name TEXT,
  frame_boundary_table_name TEXT,
  cuj_boundary_table_name TEXT,
  frame_table_name TEXT);

INSERT INTO android_jank_cuj_table_set(
  name,
  slice_table_name,
  frame_boundary_table_name,
  cuj_boundary_table_name,
  frame_table_name)
VALUES
('App threads',
  'android_jank_cuj_slice',
  'android_jank_cuj_frame',
  'android_jank_cuj_boundary',
  'android_jank_cuj_frame'),
('MainThread',
  'android_jank_cuj_main_thread_slice',
  'android_jank_cuj_main_thread_frame_boundary',
  'android_jank_cuj_main_thread_cuj_boundary',
  'android_jank_cuj_frame'),
('RenderThread',
  'android_jank_cuj_render_thread_slice',
  'android_jank_cuj_render_thread_frame_boundary',
  'android_jank_cuj_render_thread_cuj_boundary',
  'android_jank_cuj_frame'),
('SF threads',
  'android_jank_cuj_sf_slice',
  'android_jank_cuj_sf_frame',
  'android_jank_cuj_sf_boundary',
  'android_jank_cuj_sf_frame'),
('SF MainThread',
  'android_jank_cuj_sf_main_thread_slice',
  'android_jank_cuj_sf_main_thread_frame_boundary',
  'android_jank_cuj_sf_main_thread_cuj_boundary',
  'android_jank_cuj_sf_frame'),
('SF RenderEngine',
  'android_jank_cuj_sf_render_engine_slice',
  'android_jank_cuj_sf_render_engine_frame_boundary',
  'android_jank_cuj_sf_boundary',
  'android_jank_cuj_sf_frame');

-- Functions below retrieve specific columns for a given table set.

CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_table_set_slice(table_set STRING)
RETURNS STRING AS
SELECT slice_table_name
FROM android_jank_cuj_table_set ts
WHERE ts.name = $table_set;

CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_table_set_frame_boundary(
  table_set STRING
)
RETURNS STRING AS
SELECT frame_boundary_table_name
FROM android_jank_cuj_table_set ts
WHERE ts.name = $table_set;

CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_table_set_cuj_boundary(
  table_set STRING
)
RETURNS STRING AS
SELECT cuj_boundary_table_name
FROM android_jank_cuj_table_set ts
WHERE ts.name = $table_set;

CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_table_set_frame(table_set STRING)
RETURNS STRING AS
SELECT frame_table_name
FROM android_jank_cuj_table_set ts
WHERE ts.name = $table_set;

-- Checks if two slices, described by ts and dur, ts_second and dur_second, overlap.
-- Does not handle cases where slices are unfinished (dur = -1).
CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_slice_overlaps(ts LONG,
                                                        dur LONG,
                                                        ts_second LONG,
                                                        dur_second LONG)
RETURNS BOOL AS
SELECT
  -- A starts before B ends and A ends after B starts
  ($ts < $ts_second + $dur_second AND $ts + $dur > $ts_second)
  -- or A starts after B starts and A ends before B ends
  OR ($ts > $ts_second AND $ts < $ts_second + $ts_dur);
