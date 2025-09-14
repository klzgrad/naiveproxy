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

CREATE PERFETTO TABLE _android_sf_process AS
SELECT
  *
FROM process
WHERE
  process.name = '/system/bin/surfaceflinger'
LIMIT 1;

CREATE PERFETTO TABLE _android_sf_main_thread AS
SELECT
  upid,
  utid,
  thread.name,
  thread_track.id AS track_id
FROM thread
JOIN _android_sf_process AS sf_process
  USING (upid)
JOIN thread_track
  USING (utid)
WHERE
  thread.is_main_thread;

-- Extract thread and track information for a given thread in the surfaceflinger process.
CREATE PERFETTO FUNCTION _android_sf_thread(
    -- thread_name to fetch information for.
    thread_name STRING
)
RETURNS TABLE (
  -- upid of the process.
  upid JOINID(process.id),
  -- utid of the process.
  utid JOINID(thread.id),
  -- name of the thread.
  name STRING,
  -- track_id for the thread.
  track_id JOINID(track.id)
) AS
SELECT
  upid,
  utid,
  thread.name,
  thread_track.id AS track_id
FROM thread
JOIN _android_sf_process AS sf_process
  USING (upid)
JOIN thread_track
  USING (utid)
WHERE
  thread.name = $thread_name;

-- Match the frame timeline on the app side with the frame timeline on the SF side.
-- In cases where there are multiple layers drawn, there would be separate frame timeline
-- slice for each of the layers. GROUP BY is used to deduplicate these rows.
CREATE PERFETTO TABLE android_app_to_sf_frame_timeline_match (
  -- upid of the app.
  app_upid JOINID(process.upid),
  -- vsync id of the app.
  app_vsync LONG,
  -- upid of surfaceflinger process.
  sf_upid JOINID(process.upid),
  -- vsync id for surfaceflinger.
  sf_vsync LONG
) AS
SELECT
  app_timeline.upid AS app_upid,
  CAST(app_timeline.name AS INTEGER) AS app_vsync,
  sf_process.upid AS sf_upid,
  CAST(sf_timeline.name AS INTEGER) AS sf_vsync
FROM actual_frame_timeline_slice AS app_timeline
JOIN flow
  ON flow.slice_out = app_timeline.id
JOIN actual_frame_timeline_slice AS sf_timeline
  ON flow.slice_in = sf_timeline.id
JOIN _android_sf_process AS sf_process
  ON sf_timeline.upid = sf_process.upid
GROUP BY
  app_upid,
  app_vsync,
  sf_upid,
  sf_vsync;
