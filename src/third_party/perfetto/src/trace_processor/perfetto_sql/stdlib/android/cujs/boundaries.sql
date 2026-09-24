--
-- Copyright 2026 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.cujs.relevant_slices;

-- Stores the min and max vsync IDs for each of the CUJs which are extracted
-- from the CUJ markers. For backward compatibility (In case the markers don't
-- exist), We calculate that by extracting the vsync ID from the
-- `Choreographer#doFrame` slices that are within the CUJ markers.
CREATE PERFETTO TABLE _android_jank_cuj_vsync_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Process id (stored to simplify further queries).
  upid JOINID(process.id),
  -- Layer id (stored to simplify further queries).
  layer_id LONG,
  -- Minimum vsync ID within the CUJ.
  vsync_min LONG,
  -- Maximum vsync ID within the CUJ.
  vsync_max LONG
)
AS
SELECT
  cuj.cuj_id,
  cuj.upid, -- also store upid to simplify further queries
  cuj.layer_id, -- also store layer_id to simplify further queries
  IFNULL(cuj.begin_vsync, MIN(vsync)) AS vsync_min,
  IFNULL(cuj.end_vsync, MAX(vsync)) AS vsync_max
FROM android_jank_cuj AS cuj
JOIN _android_jank_cuj_do_frames USING (cuj_id)
GROUP BY
  cuj.cuj_id,
  cuj.upid,
  cuj.layer_id;

-- Calculates the frame boundaries based on when we *expected* the work on
-- a given frame to start and when the previous frame finished - not when
-- Choreographer#doFrame actually started.
-- We use MAX(expected time, previous frame ended) as the expected start time.
-- Shifting the start time based on the previous frame is done to avoid having
-- overlapping frame boundaries which would make further analysis more
-- complicated.
-- We also separately store the previous frame ts_end.
-- This will allow us to look into cases where frame start was delayed due to
-- some other work occupying the main thread (e.g. non-drawing callbacks or
-- the previous frame taking much longer than expected).
CREATE PERFETTO TABLE _android_jank_cuj_main_thread_frame_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Thread id of the main/UI thread.
  utid JOINID(thread.id),
  -- Vsync ID of this frame.
  vsync LONG,
  -- Start timestamp of the Choreographer#doFrame slice.
  ts_do_frame_start TIMESTAMP,
  -- End timestamp of the Choreographer#doFrame slice.
  ts_end TIMESTAMP,
  -- End timestamp of the previous Choreographer#doFrame slice.
  ts_prev_do_frame_end TIMESTAMP,
  -- Expected start timestamp from the frame timeline.
  ts_expected TIMESTAMP,
  -- Corrected start timestamp for the frame boundary.
  ts TIMESTAMP,
  -- Duration of the frame boundary.
  dur DURATION
)
AS
-- intermediate table that discards unfinished slices and parses vsync as int.
WITH
  expected_timeline AS (
    SELECT *, CAST(name AS INTEGER) AS vsync
    FROM expected_frame_timeline_slice
    WHERE
      dur > 0
  ),
  -- Matches vsyncs in CUJ to expected frame timeline data.
  -- We also store the actual timeline data to handle a few edge cases where due to clock drift the frame timeline is shifted
  cuj_frame_timeline AS (
    SELECT
      cuj_id,
      vsync,
      e.ts AS ts_expected,
      -- In cases where we are drawing multiple layers, there will be  one
      -- expected frame timeline slice, but multiple actual frame timeline slices.
      -- As a simplification we just take here the min(ts) and max(ts_end) of
      -- the actual frame timeline slices.
      MIN(a.ts) AS ts_actual_min,
      MAX(a.ts + a.dur) AS ts_end_actual_max
    FROM _android_jank_cuj_vsync_boundary AS vsync_boundary
    JOIN expected_timeline AS e
      ON e.upid = vsync_boundary.upid
      AND e.vsync >= vsync_min
      AND e.vsync <= vsync_max
    JOIN actual_frame_timeline_slice AS a
      ON e.upid = a.upid
      AND e.name = a.name
    GROUP BY
      cuj_id,
      e.vsync,
      e.ts
  ),
  -- Orders do_frame slices by vsync to calculate the ts_end of the previous frame
  -- _android_jank_cuj_do_frames only contains frames within the CUJ so
  -- the ts_prev_do_frame_end is always missing for the very first frame
  -- For now this is acceptable as it keeps the query simpler.
  do_frame_ordered AS (
    SELECT
      *,
      -- ts_end of the previous do_frame, or -1 if no previous do_frame found
      COALESCE(LAG(ts_end) OVER (PARTITION BY cuj_id ORDER BY vsync), -1) AS ts_prev_do_frame_end
    FROM _android_jank_cuj_do_frames
  ),
  -- introducing an intermediate table since we want to calculate dur = ts_end - ts
  frame_boundary_base AS (
    SELECT
      do_frame.cuj_id,
      do_frame.utid,
      do_frame.vsync,
      do_frame.ts AS ts_do_frame_start,
      do_frame.ts_end,
      do_frame.ts_prev_do_frame_end,
      timeline.ts_expected,
      CASE
        WHEN timeline.ts_expected IS NULL THEN do_frame.ts
        ELSE MAX(do_frame.ts_prev_do_frame_end, timeline.ts_expected)
      END AS ts
    FROM do_frame_ordered AS do_frame
    LEFT JOIN cuj_frame_timeline AS timeline
      ON timeline.cuj_id = do_frame.cuj_id
      AND do_frame.vsync = timeline.vsync
      -- There are a few special cases we have to handle:
      -- *) In rare cases there is a clock drift after device suspends
      -- This may cause the actual/expected timeline to be misaligned with the rest
      -- of the trace for a short period.
      -- Do not use the timelines if it seems that this happened.
      -- *) Actual timeline start time might also be reported slightly after doFrame
      -- starts. We allow it to start up to 1ms later.
      -- *) If the frame is significantly (~100s of ms) over the deadline,
      -- expected timeline data will be dropped in SF and never recorded. In that case
      -- the actual timeline will only report the end ts correctly. If this happens
      -- fall back to calculating the boundaries based on doFrame slices. Ideally we
      -- would prefer to infer the intended start time of the frame instead.
      AND do_frame.ts >= timeline.ts_actual_min - 1e6
      AND do_frame.ts <= timeline.ts_end_actual_max
  )
SELECT *, ts_end - ts AS dur FROM frame_boundary_base;

-- Compute the CUJ boundary on the main thread from the frame boundaries.
CREATE PERFETTO TABLE _android_jank_cuj_main_thread_cuj_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Thread id of the main/UI thread.
  utid JOINID(thread.id),
  -- Start timestamp of the CUJ on the main thread.
  ts TIMESTAMP,
  -- End timestamp of the CUJ on the main thread.
  ts_end TIMESTAMP,
  -- Duration of the CUJ on the main thread.
  dur DURATION
)
AS
SELECT
  cuj_id,
  utid,
  MIN(ts) AS ts,
  MAX(ts_end) AS ts_end,
  MAX(ts_end) - MIN(ts) AS dur
FROM _android_jank_cuj_main_thread_frame_boundary
GROUP BY
  cuj_id,
  utid;

-- Compute the overall CUJ boundary (in the app process) based on the main
-- thread CUJ boundaries and the actual timeline.
CREATE PERFETTO TABLE _android_jank_cuj_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Process id.
  upid JOINID(process.id),
  -- Start timestamp of the CUJ boundary.
  ts TIMESTAMP,
  -- End timestamp of the CUJ boundary.
  ts_end TIMESTAMP,
  -- Duration of the CUJ boundary.
  dur DURATION
)
AS
-- introducing an intermediate table since we want to calculate dur = ts_end - ts
WITH
  boundary_base AS (
    SELECT
      cuj_id,
      cuj.upid,
      main_thread_boundary.ts,
      CASE
        WHEN timeline_slice.ts IS NOT NULL THEN MAX(
          timeline_slice.ts + timeline_slice.dur
        )
        ELSE (
          SELECT MAX(MAX(ts_end), cuj.ts_end)
          FROM _android_jank_cuj_do_frames AS do_frame
          WHERE
            do_frame.cuj_id = cuj.cuj_id
        )
      END AS ts_end
    FROM _android_jank_cuj_main_thread_cuj_boundary AS main_thread_boundary
    JOIN android_jank_cuj AS cuj USING (cuj_id)
    JOIN _android_jank_cuj_vsync_boundary USING (cuj_id)
    LEFT JOIN actual_frame_timeline_slice AS timeline_slice
      ON cuj.upid = timeline_slice.upid
      -- Timeline slices for this exact VSYNC might be missing (e.g. if the last
      -- doFrame did not actually produce anything to draw).
      -- In that case we compute the boundary based on the last doFrame and the
      -- CUJ markers.
      AND vsync_max = CAST(timeline_slice.name AS INTEGER)
    GROUP BY
      cuj_id,
      cuj.upid,
      main_thread_boundary.ts
  )
SELECT *, ts_end - ts AS dur FROM boundary_base;

-- Similar to `_android_jank_cuj_main_thread_frame_boundary` but for the render
-- thread the expected start time is the time of the first `postAndWait` slice
-- on the main thread.
-- The query is slightly simpler because we don't have to handle the clock drift
-- and data loss like with the frame timeline.
-- One difference vs main thread is that there might be multiple DrawFrames
-- slices for a single vsync - this happens when we are drawing multiple layers
-- (e.g. status bar & notifications).
CREATE PERFETTO TABLE _android_jank_cuj_render_thread_frame_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Thread id of the render thread.
  utid JOINID(thread.id),
  -- Vsync ID of this frame.
  vsync LONG,
  -- Expected start timestamp (first postAndWait slice).
  ts_expected TIMESTAMP,
  -- Start timestamp of the DrawFrame slice.
  ts_draw_frame_start TIMESTAMP,
  -- End timestamp of the previous DrawFrame slice.
  ts_prev_draw_frame_end TIMESTAMP,
  -- Corrected start timestamp for the frame boundary.
  ts TIMESTAMP,
  -- End timestamp of the DrawFrame slice.
  ts_end TIMESTAMP,
  -- Duration of the frame boundary.
  dur DURATION
)
AS
-- see do_frame_ordered above
-- we also order by `ts` to handle multiple DrawFrames for a single vsync
WITH
  draw_frame_ordered AS (
    SELECT
      *,
      -- ts_end of the previous draw frame, or -1 if no previous draw frame found
      COALESCE(LAG(ts_end) OVER (PARTITION BY cuj_id ORDER BY vsync, ts), -1) AS ts_prev_draw_frame_end
    FROM _android_jank_cuj_draw_frame_slice
  ),
  -- introducing an intermediate table since we want to calculate dur = ts_end - ts
  frame_boundary_base AS (
    SELECT
      draw_frame.cuj_id,
      draw_frame.utid,
      draw_frame.vsync,
      MIN(post_and_wait.ts) AS ts_expected,
      MIN(draw_frame.ts) AS ts_draw_frame_start,
      MIN(draw_frame.ts_prev_draw_frame_end) AS ts_prev_draw_frame_end,
      MIN(
        MAX(MIN(post_and_wait.ts), MIN(draw_frame.ts_prev_draw_frame_end)),
        MIN(draw_frame.ts)
      ) AS ts,
      MAX(draw_frame.ts_end) AS ts_end
    FROM draw_frame_ordered AS draw_frame
    JOIN _android_jank_cuj_do_frames AS do_frame USING (cuj_id, vsync)
    JOIN descendant_slice(do_frame.id) AS post_and_wait
    WHERE
      post_and_wait.name = 'postAndWait'
    GROUP BY
      draw_frame.cuj_id,
      draw_frame.utid,
      draw_frame.vsync
  )
SELECT *, ts_end - ts AS dur FROM frame_boundary_base;

-- Compute the CUJ boundary on the render thread from the frame boundaries.
CREATE PERFETTO TABLE _android_jank_cuj_render_thread_cuj_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Thread id of the render thread.
  utid JOINID(thread.id),
  -- Start timestamp of the CUJ on the render thread.
  ts TIMESTAMP,
  -- End timestamp of the CUJ on the render thread.
  ts_end TIMESTAMP,
  -- Duration of the CUJ on the render thread.
  dur DURATION
)
AS
SELECT
  cuj_id,
  utid,
  MIN(ts) AS ts,
  MAX(ts_end) AS ts_end,
  MAX(ts_end) - MIN(ts) AS dur
FROM _android_jank_cuj_render_thread_frame_boundary
GROUP BY
  cuj_id,
  utid;

-- Similarly, extract the min/max vsync for the SF from
-- commit/compose/onMessageInvalidate slices on its main thread.
CREATE PERFETTO TABLE _android_jank_cuj_sf_vsync_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Minimum vsync ID within the CUJ.
  vsync_min LONG,
  -- Maximum vsync ID within the CUJ.
  vsync_max LONG
)
AS
SELECT cuj_id, MIN(vsync) AS vsync_min, MAX(vsync) AS vsync_max
FROM _android_jank_cuj_sf_root_slice
GROUP BY
  cuj_id;

-- Compute the CUJ boundary on the main thread from the frame boundaries.
CREATE PERFETTO TABLE _android_jank_cuj_sf_main_thread_cuj_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Thread id of the SF main thread.
  utid JOINID(thread.id),
  -- Start timestamp of the CUJ on the SF main thread.
  ts TIMESTAMP,
  -- End timestamp of the CUJ on the SF main thread.
  ts_end TIMESTAMP,
  -- Duration of the CUJ on the SF main thread.
  dur DURATION
)
AS
SELECT
  cuj_id,
  utid,
  MIN(ts) AS ts,
  MAX(ts_end) AS ts_end,
  MAX(ts_end) - MIN(ts) AS dur
FROM _android_jank_cuj_sf_main_thread_frame_boundary
GROUP BY
  cuj_id,
  utid;

-- RenderEngine will only work on a frame if SF falls back to client composition.
-- Because of that we do not calculate overall CUJ boundaries so there is no
-- `_android_jank_cuj_sf_render_engine_cuj_boundary` table created.
-- RenderEngine frame boundaries are calculated based on `composeSurfaces` slice start
-- on the main thread (this calls into RenderEngine), and when `REThreaded::drawLayers`
-- ends.
CREATE PERFETTO TABLE _android_jank_cuj_sf_render_engine_frame_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Thread id of the RenderEngine thread.
  utid JOINID(thread.id),
  -- Vsync ID of this frame.
  vsync LONG,
  -- Timestamp of the composeSurfaces slice bounding this drawLayers.
  ts TIMESTAMP,
  -- Start timestamp of the drawLayers slice.
  ts_draw_layers_start TIMESTAMP,
  -- End timestamp of the drawLayers slice.
  ts_end TIMESTAMP,
  -- Duration of the frame boundary.
  dur DURATION
)
AS
SELECT
  cuj_id,
  utid,
  vsync,
  draw_layers.ts_compose_surfaces AS ts,
  draw_layers.ts AS ts_draw_layers_start,
  draw_layers.ts_end,
  draw_layers.ts_end - draw_layers.ts_compose_surfaces AS dur
FROM _android_jank_cuj_sf_draw_layers_slice AS draw_layers;

CREATE PERFETTO TABLE _android_jank_cuj_sf_boundary(
  -- CUJ id.
  cuj_id LONG,
  -- Start timestamp of the CUJ.
  ts TIMESTAMP,
  -- End timestamp of the CUJ.
  ts_end TIMESTAMP,
  -- Duration of the CUJ.
  dur DURATION
)
AS
SELECT cuj_id, ts, ts_end, dur
FROM _android_jank_cuj_sf_main_thread_cuj_boundary;
