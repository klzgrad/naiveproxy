--
-- Copyright 2021 The Android Open Source Project
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

DROP VIEW IF EXISTS InteractionEvents;
CREATE PERFETTO VIEW InteractionEvents AS
SELECT
  ts, dur, ts AS ts_ir, dur AS dur_ir
FROM slice WHERE name GLOB 'Interaction.*';

DROP VIEW IF EXISTS GestureLegacyEvents;
CREATE PERFETTO VIEW GestureLegacyEvents AS
SELECT
  ts,
  EXTRACT_ARG(arg_set_id, 'legacy_event.phase') AS phase
FROM __intrinsic_chrome_raw
WHERE EXTRACT_ARG(arg_set_id, 'legacy_event.name') = 'SyntheticGestureController::running';

-- Convert pairs of 'S' and 'F' events into slices with ts and dur.
DROP VIEW IF EXISTS GestureEvents;
CREATE PERFETTO VIEW GestureEvents AS
SELECT
  ts, dur, ts AS ts_ge, dur AS dur_ge
FROM (
  SELECT
    ts,
    phase,
    LEAD(ts) OVER (ORDER BY ts) - ts AS dur
  FROM GestureLegacyEvents
)
WHERE phase = 'S';

DROP TABLE IF EXISTS InteractionEventsJoinGestureEvents;
CREATE VIRTUAL TABLE InteractionEventsJoinGestureEvents
USING SPAN_LEFT_JOIN(InteractionEvents, GestureEvents);

--------------------------------------------------------------------------------
-- Interesting segments are:
-- 1) If there's a gesture overlapping with interaction, then gesture's range.
-- 2) Else, interaction's range.

DROP VIEW IF EXISTS InterestingSegments;
CREATE PERFETTO VIEW InterestingSegments AS
WITH pre_cast AS (
  SELECT  -- 1) Gestures overlapping interactions.
    ts_ge AS ts,
    dur_ge AS dur
  FROM InteractionEventsJoinGestureEvents
  WHERE ts_ge IS NOT NULL
  GROUP BY ts_ge
  UNION ALL
  SELECT  -- 2) Interactions without gestures.
    ts_ir AS ts,
    dur_ir AS dur
  FROM InteractionEventsJoinGestureEvents
  WHERE ts_ge IS NULL
  GROUP BY ts_ir
  HAVING COUNT(*) = 1
)
SELECT
  CAST(ts AS BIGINT) AS ts,
  CAST(dur AS BIGINT) AS dur
FROM pre_cast;

--------------------------------------------------------------------------------
-- On ChromeOS, DRM events, if they exist, are the source of truth. Otherwise,
-- look for display rendering stats.
-- On Android, the TBMv2 version relied on Surface Flinger events that are
-- currently unavailable in proto traces. So results may be different from
-- the TBMv2 version on this platform.

DROP TABLE IF EXISTS DisplayCompositorPresentationEvents;
CREATE TABLE DisplayCompositorPresentationEvents AS
SELECT ts, FALSE AS exp
FROM slice
WHERE name = 'DrmEventFlipComplete'
GROUP BY ts;

INSERT INTO DisplayCompositorPresentationEvents
SELECT ts, FALSE AS exp
FROM slice
WHERE
  name = 'vsync_before'
  AND NOT EXISTS (SELECT * FROM DisplayCompositorPresentationEvents)
GROUP BY ts;

INSERT INTO DisplayCompositorPresentationEvents
SELECT ts, FALSE AS exp
FROM slice
WHERE
  name = 'BenchmarkInstrumentation::DisplayRenderingStats'
  AND NOT EXISTS (SELECT * FROM DisplayCompositorPresentationEvents)
GROUP BY ts;

INSERT INTO DisplayCompositorPresentationEvents
SELECT ts, TRUE AS exp
FROM slice
WHERE name = 'Display::FrameDisplayed'
GROUP BY ts;

DROP VIEW IF EXISTS FrameSegments;
CREATE PERFETTO VIEW FrameSegments AS
SELECT
  ts,
  LEAD(ts) OVER wnd - ts AS dur,
  ts AS ts_fs,
  LEAD(ts) OVER wnd - ts AS dur_fs,
  exp
FROM DisplayCompositorPresentationEvents
WINDOW wnd AS (PARTITION BY exp ORDER BY ts);

DROP TABLE IF EXISTS FrameSegmentsJoinInterestingSegments;
CREATE VIRTUAL TABLE FrameSegmentsJoinInterestingSegments USING
SPAN_JOIN(FrameSegments, InterestingSegments);

DROP VIEW IF EXISTS FrameTimes;
CREATE PERFETTO VIEW FrameTimes AS
SELECT dur / 1e6 AS dur_ms, exp
FROM FrameSegmentsJoinInterestingSegments
WHERE ts = ts_fs AND dur = dur_fs;

--------------------------------------------------------------------------------
-- Determine frame rate

DROP VIEW IF EXISTS RefreshPeriodAndroid;
CREATE PERFETTO VIEW RefreshPeriodAndroid AS
-- Not implemented yet.
SELECT NULL AS interval_ms;

DROP VIEW IF EXISTS RefreshPeriodNonAndroid;
CREATE PERFETTO VIEW RefreshPeriodNonAndroid AS
SELECT EXTRACT_ARG(arg_set_id, 'debug.args.interval_us') / 1e3 AS interval_ms
FROM slice
JOIN thread_track ON (slice.track_id = thread_track.id)
JOIN thread ON (thread_track.utid = thread.utid)
WHERE thread.name = 'Compositor' AND slice.name = 'Scheduler::BeginFrame'
LIMIT 1;

DROP VIEW IF EXISTS RefreshPeriodDefault;
CREATE PERFETTO VIEW RefreshPeriodDefault AS
SELECT 1000.0 / 60 AS interval_ms;

DROP TABLE IF EXISTS RefreshPeriod;
CREATE PERFETTO TABLE RefreshPeriod AS
SELECT COALESCE(
  (SELECT interval_ms FROM RefreshPeriodAndroid),
  (SELECT interval_ms FROM RefreshPeriodNonAndroid),
  (SELECT interval_ms FROM RefreshPeriodDefault)
) AS interval_ms;

--------------------------------------------------------------------------------
-- Compute average FPS

DROP VIEW IF EXISTS ValidFrameTimes;
CREATE PERFETTO VIEW ValidFrameTimes AS
SELECT
  dur_ms / (SELECT interval_ms FROM RefreshPeriod) AS length,
  exp
FROM FrameTimes
WHERE dur_ms / (SELECT interval_ms FROM RefreshPeriod) >= 0.5;

DROP VIEW IF EXISTS AvgSurfaceFps;
CREATE PERFETTO VIEW AvgSurfaceFps AS
SELECT
  exp,
  1e3 * COUNT(*) / (SELECT SUM(dur_ms) FROM FrameTimes WHERE exp = valid.exp) AS fps
FROM ValidFrameTimes valid
GROUP BY exp;

DROP VIEW IF EXISTS frame_times_output;
CREATE PERFETTO VIEW frame_times_output AS
SELECT FrameTimes(
  'frame_time', (SELECT RepeatedField(dur_ms) FROM FrameTimes WHERE NOT exp),
  'exp_frame_time', (SELECT RepeatedField(dur_ms) FROM FrameTimes WHERE exp),
  'avg_surface_fps', (SELECT fps FROM AvgSurfaceFps WHERE NOT exp),
  'exp_avg_surface_fps', (SELECT fps FROM AvgSurfaceFps WHERE exp)
);
