--
-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.cujs.base;

INCLUDE PERFETTO MODULE android.surfaceflinger;

-- Returns a table with all CUJs and an additional column for the track id of thread_name
-- passed as parameter, if present in the same process of the cuj.
CREATE PERFETTO FUNCTION android_jank_cuj_app_thread(
  -- Name of the thread for which information needs to be extracted.
  thread_name STRING
)
RETURNS TABLE(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- thread id of the input thread.
  utid LONG,
  -- name of the thread.
  name STRING,
  -- track id associated with the thread.
  track_id LONG
)
AS
SELECT cuj_id, cuj.upid, utid, thread.name, thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj AS cuj USING (upid)
JOIN thread_track USING (utid)
WHERE
  thread.name = $thread_name;

-- Table captures thread information for 'RenderThread' for all CUJs.
CREATE PERFETTO TABLE android_jank_cuj_render_thread(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- thread id of the main/UI thread.
  utid JOINID(thread.id),
  -- thread name.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
)
AS
SELECT * FROM android_jank_cuj_app_thread('RenderThread');

-- Private table capturing thread information for main/UI thread for all CUJs.
CREATE PERFETTO TABLE _android_jank_cuj_main_thread(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- thread id of the main/UI thread.
  utid JOINID(thread.id),
  -- thread name.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
)
AS
SELECT cuj_id, cuj.upid, utid, thread.name, thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj AS cuj USING (upid)
JOIN thread_track USING (utid)
WHERE
  (cuj.ui_thread IS NULL AND thread.is_main_thread)
  -- Some CUJs use a ui thread different than the main thread. We get the ui
  -- thread thanks to an instant event. If that is not available, we use the app
  -- main thread (typically pid == tid).
  OR (cuj.ui_thread = thread.utid);

-- Private table capturing thread information for 'GPU completion' thread for all CUJs.
CREATE PERFETTO TABLE _android_jank_cuj_gpu_completion_thread(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- thread id of the thread.
  utid JOINID(thread.id),
  -- thread name.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
)
AS
SELECT * FROM android_jank_cuj_app_thread('GPU completion');

-- Private table capturing thread information for 'HWC release' thread for all CUJs.
CREATE PERFETTO TABLE _android_jank_cuj_hwc_release_thread(
  -- Unique incremental ID for each CUJ.
  cuj_id LONG,
  -- process id.
  upid JOINID(process.id),
  -- thread id of the thread.
  utid JOINID(thread.id),
  -- thread name.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
)
AS
SELECT * FROM android_jank_cuj_app_thread('HWC release');

-- Private table capturing the SurfaceFlinger process.
CREATE PERFETTO TABLE _android_jank_cuj_sf_process(
  -- process id.
  upid JOINID(process.id),
  -- process name.
  name STRING
)
AS
SELECT upid, name FROM _android_sf_process;

-- Private table capturing the SurfaceFlinger 'GPU completion' thread.
CREATE PERFETTO TABLE _android_jank_cuj_sf_gpu_completion_thread(
  -- process id.
  upid JOINID(process.id),
  -- thread id of the thread.
  utid JOINID(thread.id),
  -- thread name.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
)
AS
SELECT * FROM _android_sf_thread('GPU completion');

-- Private table capturing the SurfaceFlinger 'RenderEngine' thread.
CREATE PERFETTO TABLE _android_jank_cuj_sf_render_engine_thread(
  -- process id.
  upid JOINID(process.id),
  -- thread id of the thread.
  utid JOINID(thread.id),
  -- thread name.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
)
AS
SELECT * FROM _android_sf_thread('RenderEngine');
