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
FROM blocking_calls_aggregate_values bc
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
