--
-- Copyright 2024 The Android Open Source Project
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

-- Create the base table (`android_jank_cuj`) containing all completed CUJs
-- found in the trace.
-- This script will use the `android_jank_cuj_main_thread_frame_boundary`,
-- containing bounds of frames within jank CUJs.
SELECT RUN_METRIC('android/process_metadata.sql');

INCLUDE PERFETTO MODULE android.slices;
INCLUDE PERFETTO MODULE android.binder;
INCLUDE PERFETTO MODULE android.frame_blocking_calls.blocking_calls_aggregation;
INCLUDE PERFETTO MODULE android.critical_blocking_calls;
INCLUDE PERFETTO MODULE android.frames.timeline;
INCLUDE PERFETTO MODULE android.cujs.sysui_cujs;

-- The `DrawFrames` slices (one per drawn layer) for each frame within a CUJ,
-- with their render thread timing.
--
-- This is materialized into its own table (rather than left as a CTE) for two
-- reasons:
--   1. It avoids the query planner re-evaluating it multiple times when it is
--      consumed downstream.
--   2. It lets us join against the pre-parsed `android_frames_draw_frame` table
--      (integer equality on frame_id) instead of re-parsing the slice name with
--      `STR_SPLIT` on every row of a `thread_slice` scan.
DROP TABLE IF EXISTS android_blocking_calls_cuj_per_frame_draw_frames;
CREATE PERFETTO TABLE android_blocking_calls_cuj_per_frame_draw_frames AS
SELECT
  frame.frame_id,
  frame.cuj_name,
  df.upid,
  p.name AS process_name,
  s.ts,
  s.ts + s.dur AS ts_end
FROM _extended_frame_boundary frame
JOIN android_frames_draw_frame df
  ON df.frame_id = frame.frame_id
  AND df.render_thread_utid = frame.render_thread_utid
JOIN slice s ON s.id = df.id
JOIN process p ON p.upid = df.upid;

-- The `hwuiTask` running time overlapping each frame within a CUJ. Materialized
-- for the same reasons as above: the interval-overlap join against
-- `thread_state` is expensive and must only run once.
DROP TABLE IF EXISTS android_blocking_calls_cuj_per_frame_hwui_tasks;
CREATE PERFETTO TABLE android_blocking_calls_cuj_per_frame_hwui_tasks AS
SELECT
  df.frame_id,
  df.cuj_name,
  df.upid,
  df.process_name,
  'hwuiTask' AS name,
  MIN(ts.ts + ts.dur, df.ts_end) - MAX(ts.ts, df.ts) AS dur
FROM android_blocking_calls_cuj_per_frame_draw_frames df
JOIN thread t ON t.upid = df.upid AND t.name GLOB 'hwuiTask*'
JOIN thread_state ts ON ts.utid = t.utid AND ts.state = 'Running'
WHERE ts.ts < df.ts_end AND ts.ts + ts.dur > df.ts;

-- Calculate the mean/max values for duration and count for blocking calls per frame.
DROP TABLE IF EXISTS android_blocking_calls_cuj_per_frame_calls;
CREATE PERFETTO TABLE android_blocking_calls_cuj_per_frame_calls AS
WITH blocking_calls_aggregate_values AS (
  -- Aggregate the count and sum for each blocking call by grouping on CUJ name, blocking
  -- call name and frame ID(vsync).
  SELECT
    COUNT(*) AS cnt,
    SUM(dur) AS total_dur_per_frame_ns,
    cuj_name,
    upid,
    process_name,
    name
  FROM _blocking_calls_frame_cuj
  GROUP BY cuj_name, name, frame_id
),
hwui_tasks_aggregate_values AS (
  SELECT
    1 AS cnt,
    SUM(dur) AS total_dur_per_frame_ns,
    cuj_name,
    upid,
    process_name,
    name
  FROM android_blocking_calls_cuj_per_frame_hwui_tasks
  GROUP BY cuj_name, name, frame_id
),
all_blocking_calls_aggregate_values AS (
  SELECT * FROM blocking_calls_aggregate_values
  UNION ALL
  SELECT * FROM hwui_tasks_aggregate_values
),
frame_cnt_per_cuj AS (
  -- Calculate the total number of frames for all CUJs across all instances(eg. multiple
  -- instances for the same CUJ).
  SELECT
    COUNT(*) AS frame_cnt,
    cuj_name
  FROM _android_distinct_frames_in_cuj
  GROUP BY cuj_name
)
SELECT
    cast_double!(SUM(cnt)) / frame_cnt AS mean_cnt_per_frame,
    MAX(cnt) AS max_cnt_per_frame,
    SUM(total_dur_per_frame_ns) / frame_cnt AS mean_dur_per_frame_ns,
    MAX(total_dur_per_frame_ns) AS max_dur_per_frame_ns,
    name,
    upid,
    bc.cuj_name,
    process_name
FROM all_blocking_calls_aggregate_values bc
JOIN frame_cnt_per_cuj fc
USING(cuj_name)
GROUP BY bc.cuj_name, name;

DROP VIEW IF EXISTS android_blocking_calls_cuj_per_frame_metric_output;
CREATE PERFETTO VIEW android_blocking_calls_cuj_per_frame_metric_output AS
SELECT AndroidCujBlockingCallsPerFrameMetric('cuj', (
    SELECT RepeatedField(
        AndroidCujBlockingCallsPerFrameMetric_Cuj(
            'name', cuj_name,
            'process', process_metadata_proto(cuj.upid),
            'blocking_calls', (
                SELECT RepeatedField(
                    AndroidBlockingCallPerFrame(
                        'name', b.name,
                        'max_dur_per_frame_ms', CAST(max_dur_per_frame_ns / 1e6 AS INT),
                        'max_dur_per_frame_ns', b.max_dur_per_frame_ns,
                        'mean_dur_per_frame_ms', CAST(mean_dur_per_frame_ns / 1e6 AS INT),
                        'mean_dur_per_frame_ns', b.mean_dur_per_frame_ns,
                        'max_cnt_per_frame', CAST(b.max_cnt_per_frame AS INT),
                        'mean_cnt_per_frame', b.mean_cnt_per_frame
                    )
                )
                FROM android_blocking_calls_cuj_per_frame_calls b
                WHERE b.cuj_name = cuj.cuj_name and b.upid = cuj.upid
                GROUP BY b.cuj_name
            )
        )
    )
    FROM (SELECT DISTINCT cuj_name, upid FROM android_sysui_jank_cujs) cuj
));
