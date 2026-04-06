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

-- Matches slices with frames within CUJs and aggregates slices durations within each frame.
-- This allows comparing the cumulative durations of a set of slices vs what was the expected
-- duration of each frame. It can be a useful heuristic to figure out what contributed to
-- frames missing their expected deadlines.
--
-- EXAMPLE:
--
--
-- CUJ Frames:
--
--      |==== FRAME 1 ====|
--                  |====== FRAME 2 ======|
--                                             |== FRAME 3 ==|
--                                                          |== FRAME 4 ==|
--
-- Main thread frame boundaries:
--
--      |= FRAME 1 =|==== FRAME 2 ====|        |= FRAME 3 =||= FRAME 4 =|
--
-- `relevant_slice_table_name` slices:
--
--  |== binder ==|      |== binder ==|   |=b=|   |=b=||=b=|
--
--
-- OUTPUT:
--
-- Data in `*_slice_in_frame` - slices from `relevant_slice_table_name` matched
-- to frames and trimmed to their boundaries:
--
-- * FRAME 1:
--      | binder |
-- * FRAME 2:
--                      |== binder ==|
-- * FRAME 3:
--                                               |=b=||=b=|
-- * FRAME 4 - not present in the output
--
--
-- Data in `*_slice_in_frame_agg` is just an aggregation of durations in  *_slice_in_frame.


-- For simplicity we allow `relevant_slice_table_name` to be based on any
-- of the slice tables. This table filters it down to only include slices within
-- the (broadly bounded) CUJ and on the specific process / thread.
-- Using TABLE and not VIEW as this gives better, localized error messages in cases
-- `relevant_slice_table_name` is not correct (e.g. missing cuj_id).
DROP TABLE IF EXISTS {{table_name_prefix}}_query_slice;
CREATE PERFETTO TABLE {{table_name_prefix}}_query_slice AS
SELECT DISTINCT
  slice.cuj_id,
  slice.utid,
  slice.id,
  slice.name,
  slice.ts,
  slice.dur,
  slice.ts_end
FROM {{relevant_slice_table_name}} slice
JOIN {{slice_table_name}} android_jank_cuj_slice_table
  USING (cuj_id, id);

-- Flat view of frames and slices matched and "trimmed" to each frame boundaries.
DROP VIEW IF EXISTS {{table_name_prefix}}_slice_in_frame;
CREATE PERFETTO VIEW {{table_name_prefix}}_slice_in_frame AS
SELECT
  frame.*,
  query_slice.id AS slice_id,
  query_slice.utid AS slice_utid,
  query_slice.name AS slice_name,
  MAX(query_slice.ts, frame_boundary.ts) AS slice_ts,
  MIN(query_slice.ts_end, frame_boundary.ts_end) AS slice_ts_end,
  MIN(query_slice.ts_end, frame_boundary.ts_end) - MAX(query_slice.ts, frame_boundary.ts) AS slice_dur,
  query_slice.ts_end AS ts_end_original
FROM {{frame_table_name}} frame
-- We want to use different boundaries depending on which thread's slices the query is targetting.
JOIN {{frame_boundary_table_name}} frame_boundary USING (cuj_id, vsync)
JOIN {{table_name_prefix}}_query_slice query_slice
  ON frame_boundary.cuj_id = query_slice.cuj_id
    AND android_jank_cuj_slice_overlaps(frame_boundary.ts, frame_boundary.dur, query_slice.ts, query_slice.dur);

-- Aggregated view of frames and slices overall durations within each frame boundaries.
DROP VIEW IF EXISTS {{table_name_prefix}}_slice_in_frame_agg;
CREATE PERFETTO VIEW {{table_name_prefix}}_slice_in_frame_agg AS
SELECT
  cuj_id,
  frame_number,
  vsync,
  dur_expected,
  app_missed,
  sf_missed,
  1.0 * SUM(slice_dur) / dur_expected AS slice_dur_div_frame_dur_expected,
  SUM(slice_dur) AS slice_dur_sum,
  MAX(slice_dur) AS slice_dur_max
FROM {{table_name_prefix}}_slice_in_frame
GROUP BY cuj_id, frame_number, vsync, dur_expected, app_missed, sf_missed;
