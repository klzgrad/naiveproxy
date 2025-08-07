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

INCLUDE PERFETTO MODULE slices.with_context;

DROP TABLE IF EXISTS android_jank_cuj_main_thread;
CREATE PERFETTO TABLE android_jank_cuj_main_thread AS
SELECT cuj_id, cuj.upid, utid, thread.name, thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj cuj USING (upid)
JOIN thread_track USING (utid)
WHERE
  (cuj.ui_thread IS NULL AND thread.is_main_thread)
  -- Some CUJs use a dedicated thread for Choreographer callbacks
  OR (cuj.ui_thread = thread.utid);

CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_app_thread(thread_name STRING)
RETURNS TABLE(cuj_id INT, upid INT, utid INT, name STRING, track_id INT) AS
SELECT
  cuj_id,
  cuj.upid,
  utid,
  thread.name,
  thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj cuj USING (upid)
JOIN thread_track USING (utid)
WHERE thread.name = $thread_name;

DROP TABLE IF EXISTS android_jank_cuj_render_thread;
CREATE PERFETTO TABLE android_jank_cuj_render_thread AS
SELECT * FROM ANDROID_JANK_CUJ_APP_THREAD('RenderThread');

DROP TABLE IF EXISTS android_jank_cuj_gpu_completion_thread;
CREATE PERFETTO TABLE android_jank_cuj_gpu_completion_thread AS
SELECT * FROM ANDROID_JANK_CUJ_APP_THREAD('GPU completion');

DROP TABLE IF EXISTS android_jank_cuj_hwc_release_thread;
CREATE PERFETTO TABLE android_jank_cuj_hwc_release_thread AS
SELECT * FROM ANDROID_JANK_CUJ_APP_THREAD('HWC release');

DROP TABLE IF EXISTS android_jank_cuj_sf_process;
CREATE PERFETTO TABLE android_jank_cuj_sf_process AS
SELECT * FROM process
WHERE process.name = '/system/bin/surfaceflinger'
LIMIT 1;

DROP TABLE IF EXISTS android_jank_cuj_sf_main_thread;
CREATE PERFETTO TABLE android_jank_cuj_sf_main_thread AS
SELECT upid, utid, thread.name, thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj_sf_process sf_process USING (upid)
JOIN thread_track USING (utid)
WHERE thread.is_main_thread;

CREATE OR REPLACE PERFETTO FUNCTION android_jank_cuj_sf_thread(thread_name STRING)
RETURNS TABLE(upid INT, utid INT, name STRING, track_id INT) AS
SELECT upid, utid, thread.name, thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj_sf_process sf_process USING (upid)
JOIN thread_track USING (utid)
WHERE thread.name = $thread_name;

DROP TABLE IF EXISTS android_jank_cuj_sf_gpu_completion_thread;
CREATE PERFETTO TABLE android_jank_cuj_sf_gpu_completion_thread AS
SELECT * FROM ANDROID_JANK_CUJ_SF_THREAD('GPU completion');

DROP TABLE IF EXISTS android_jank_cuj_sf_render_engine_thread;
CREATE PERFETTO TABLE android_jank_cuj_sf_render_engine_thread AS
SELECT * FROM ANDROID_JANK_CUJ_SF_THREAD('RenderEngine');
