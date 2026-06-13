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

-- Returns a table with all CUJs and an additional column for the track id of thread_name
-- passed as parameter, if present in the same process of the cuj.
CREATE PERFETTO FUNCTION android_jank_cuj_app_thread(
    -- Name of the thread for which information needs to be extracted.
    thread_name STRING
)
RETURNS TABLE (
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
) AS
SELECT
  cuj_id,
  cuj.upid,
  utid,
  thread.name,
  thread_track.id AS track_id
FROM thread
JOIN android_jank_cuj AS cuj
  USING (upid)
JOIN thread_track
  USING (utid)
WHERE
  thread.name = $thread_name;

-- Table captures thread information for 'RenderThread' for all CUJs.
CREATE PERFETTO TABLE android_jank_cuj_render_thread (
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
) AS
SELECT
  *
FROM android_jank_cuj_app_thread('RenderThread');
