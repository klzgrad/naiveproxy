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

DROP VIEW IF EXISTS android_jank_cuj_slice;
CREATE PERFETTO VIEW android_jank_cuj_slice AS
SELECT
  cuj_id,
  process.upid,
  process.name AS process_name,
  thread.utid,
  thread.name AS thread_name,
  slice.*,
  slice.ts + slice.dur AS ts_end
FROM android_jank_cuj_boundary boundary
JOIN process USING (upid)
JOIN thread USING (upid)
JOIN thread_track USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
    -- Take slices which overlap even they started before the boundaries
    -- This is to be able to query slices that delayed start of a frame
    AND slice.ts + slice.dur >= boundary.ts AND slice.ts <= boundary.ts_end
WHERE slice.dur > 0;

DROP TABLE IF EXISTS android_jank_cuj_main_thread_slice;
CREATE PERFETTO TABLE android_jank_cuj_main_thread_slice AS
SELECT
  cuj_id,
  upid,
  utid,
  slice.*,
  slice.ts + slice.dur AS ts_end
FROM android_jank_cuj_main_thread_cuj_boundary boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
    -- Take slices which overlap even they started before the boundaries
    -- This is to be able to query slices that delayed start of a frame
    AND slice.ts + slice.dur >= boundary.ts
    AND slice.ts <= boundary.ts_end
WHERE slice.dur > 0;

DROP TABLE IF EXISTS android_jank_cuj_render_thread_slice;
CREATE PERFETTO TABLE android_jank_cuj_render_thread_slice AS
SELECT
  cuj_id,
  upid,
  utid,
  slice.*,
  slice.ts + slice.dur AS ts_end
FROM android_jank_cuj_render_thread_cuj_boundary boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
    -- Take slices which overlap even they started before the boundaries
    -- This is to be able to query slices that delayed start of a frame
    AND slice.ts + slice.dur >= boundary.ts
    AND slice.ts <= boundary.ts_end
WHERE slice.dur > 0;

DROP VIEW IF EXISTS android_jank_cuj_sf_slice;
CREATE PERFETTO VIEW android_jank_cuj_sf_slice AS
SELECT
  cuj_id,
  upid,
  sf_process.name AS process_name,
  thread.utid,
  thread.name AS thread_name,
  slice.*,
  slice.ts + slice.dur AS ts_end
FROM android_jank_cuj_sf_boundary sf_boundary
JOIN android_jank_cuj_sf_process sf_process
JOIN thread USING (upid)
JOIN thread_track USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
    -- Take slices which overlap even they started before the boundaries
    -- This is to be able to query slices that delayed start of a frame
    AND slice.ts + slice.dur >= sf_boundary.ts AND slice.ts <= sf_boundary.ts_end
WHERE slice.dur > 0;

DROP TABLE IF EXISTS android_jank_cuj_sf_main_thread_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_main_thread_slice AS
SELECT
  cuj_id,
  upid,
  utid,
  slice.*,
  slice.ts + slice.dur AS ts_end
FROM android_jank_cuj_sf_main_thread_cuj_boundary boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
    -- Take slices which overlap even they started before the boundaries
    -- This is to be able to query slices that delayed start of a frame
    AND slice.ts + slice.dur >= boundary.ts
    AND slice.ts <= boundary.ts_end
WHERE slice.dur > 0;

-- For RenderEngine thread we use a different approach as it's only used when SF falls back to
-- client composition. Instead of taking all slices during CUJ, we look at each frame explicitly
-- and only take slices that are within RenderEngine frame boundaries.
DROP TABLE IF EXISTS android_jank_cuj_sf_render_engine_slice;
CREATE PERFETTO TABLE android_jank_cuj_sf_render_engine_slice AS
SELECT
  cuj_id,
  upid,
  utid,
  slice.*,
  slice.ts + slice.dur AS ts_end
FROM android_jank_cuj_sf_render_engine_frame_boundary boundary
JOIN thread_track USING (utid)
JOIN thread USING (utid)
JOIN slice
  ON slice.track_id = thread_track.id
    -- Take slices which overlap even they started before the boundaries
    -- This is to be able to query slices that delayed start of a frame
    AND slice.ts + slice.dur >= boundary.ts
    AND slice.ts <= boundary.ts_end
WHERE slice.dur > 0;
