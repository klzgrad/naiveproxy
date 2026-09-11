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

SELECT RUN_METRIC('android/jank/relevant_threads.sql');

INCLUDE PERFETTO MODULE android.cujs.relevant_slices;

-- NOTE: We preserve the legacy view and table names in this file because external
-- consumers and analytical pipelines outside the Perfetto project rely on them.

-- Re-export the private functions under legacy public names.
CREATE OR REPLACE PERFETTO FUNCTION vsync_from_name(slice_name STRING)
RETURNS STRING AS
SELECT _vsync_from_name($slice_name);

CREATE OR REPLACE PERFETTO FUNCTION gpu_completion_fence_id_from_name(slice_name STRING)
RETURNS STRING AS
SELECT _gpu_completion_fence_id_from_name($slice_name);

DROP TABLE IF EXISTS android_jank_cuj_do_frame_slice;
CREATE PERFETTO TABLE android_jank_cuj_do_frame_slice AS
SELECT * FROM _android_jank_cuj_do_frame_slice;

DROP TABLE IF EXISTS android_jank_cuj_draw_frame_slice;
CREATE PERFETTO TABLE android_jank_cuj_draw_frame_slice AS
SELECT * FROM _android_jank_cuj_draw_frame_slice;

DROP TABLE IF EXISTS android_jank_cuj_gpu_completion_fence;
CREATE PERFETTO TABLE android_jank_cuj_gpu_completion_fence AS
SELECT * FROM _android_jank_cuj_gpu_completion_fence;

DROP TABLE IF EXISTS android_jank_cuj_hwc_release_fence;
CREATE PERFETTO TABLE android_jank_cuj_hwc_release_fence AS
SELECT * FROM _android_jank_cuj_hwc_release_fence;

DROP TABLE IF EXISTS android_jank_cuj_hwc_release_slice;
CREATE PERFETTO TABLE android_jank_cuj_hwc_release_slice AS
SELECT * FROM _android_jank_cuj_hwc_release_slice;

DROP TABLE IF EXISTS android_jank_cuj_gpu_completion_slice;
CREATE PERFETTO TABLE android_jank_cuj_gpu_completion_slice AS
SELECT * FROM _android_jank_cuj_gpu_completion_slice;

CREATE OR REPLACE PERFETTO FUNCTION find_android_jank_cuj_sf_main_thread_slice(
  slice_name_glob STRING)
RETURNS TABLE(
  cuj_id INT, utid INT, vsync INT, id INT,
  name STRING, ts LONG, dur LONG, ts_end LONG)
AS
SELECT * FROM _find_android_jank_cuj_sf_main_thread_slice($slice_name_glob);

DROP TABLE IF EXISTS android_jank_cuj_sf_commit_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_commit_slice AS
SELECT * FROM _android_jank_cuj_sf_commit_slice;

DROP TABLE IF EXISTS android_jank_cuj_sf_composite_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_composite_slice AS
SELECT * FROM _android_jank_cuj_sf_composite_slice;

DROP TABLE IF EXISTS android_jank_cuj_sf_on_message_invalidate_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_on_message_invalidate_slice AS
SELECT * FROM _android_jank_cuj_sf_on_message_invalidate_slice;

DROP VIEW IF EXISTS android_jank_cuj_sf_root_slice;
CREATE PERFETTO VIEW android_jank_cuj_sf_root_slice AS
SELECT * FROM _android_jank_cuj_sf_root_slice;

DROP TABLE IF EXISTS android_jank_cuj_sf_gpu_completion_fence;
CREATE PERFETTO TABLE android_jank_cuj_sf_gpu_completion_fence AS
SELECT * FROM _android_jank_cuj_sf_gpu_completion_fence;

DROP TABLE IF EXISTS android_jank_cuj_sf_gpu_completion_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_gpu_completion_slice AS
SELECT * FROM _android_jank_cuj_sf_gpu_completion_slice;

DROP TABLE IF EXISTS android_jank_cuj_sf_draw_layers_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_draw_layers_slice AS
SELECT * FROM _android_jank_cuj_sf_draw_layers_slice;
