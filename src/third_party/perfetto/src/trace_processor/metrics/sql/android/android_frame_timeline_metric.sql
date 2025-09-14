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

SELECT RUN_METRIC('android/process_metadata.sql');

DROP VIEW IF EXISTS splitted_jank_type_timeline;
CREATE PERFETTO VIEW splitted_jank_type_timeline AS
WITH RECURSIVE split_jank_type AS (
  SELECT
    upid,
    name,
    present_type,
    '' AS jank_type,
    jank_type || ', ' AS unparsed
  FROM actual_frame_timeline_slice
  UNION ALL
  SELECT
    upid,
    name,
    present_type,
    substr(unparsed, 1, instr(unparsed, ',')-1) AS jank_type,
    substr(unparsed, instr(unparsed, ',')+2) AS unparsed
  FROM split_jank_type
  WHERE unparsed != ''
)
SELECT
  upid,
  name AS vsync,
  present_type,
  jank_type
FROM split_jank_type
WHERE jank_type != '';

DROP VIEW IF EXISTS android_frame_timeline_metric_per_process;
CREATE PERFETTO VIEW android_frame_timeline_metric_per_process AS
WITH frames AS (
  SELECT
    process.upid,
    process.name AS process_name,
    timeline.name AS vsync,
    jank_type GLOB '*App Deadline Missed*' AS missed_app_frame,
    jank_type GLOB '*SurfaceFlinger CPU Deadline Missed*'
      OR jank_type GLOB '*SurfaceFlinger GPU Deadline Missed*'
      OR jank_type GLOB '*SurfaceFlinger Scheduling*'
      OR jank_type GLOB '*Prediction Error*'
      OR jank_type GLOB '*Display HAL*' AS missed_sf_frame,
    jank_type GLOB '*App Deadline Missed*'
      OR jank_type GLOB '*SurfaceFlinger CPU Deadline Missed*'
      OR jank_type GLOB '*SurfaceFlinger GPU Deadline Missed*'
      OR jank_type GLOB '*SurfaceFlinger Scheduling*'
      OR jank_type GLOB '*Prediction Error*'
      OR jank_type GLOB '*Display HAL*'
      OR jank_type GLOB '*Dropped Frame*' AS missed_frame,
    jank_type GLOB '*Dropped Frame*' AS dropped_frame,
    -- discard dropped frame duration as it is not a meaningful value
    IIF(jank_type GLOB '*Dropped Frame*', NULL, dur) AS dur,
    IIF(jank_type GLOB '*Dropped Frame*', NULL, dur / 1e6) AS dur_ms
  FROM actual_frame_timeline_slice timeline
  JOIN process USING (upid))
SELECT
  upid,
  process_name,
  process_metadata.metadata AS process_metadata,
  COUNT(DISTINCT(vsync)) AS total_frames,
  COUNT(DISTINCT(IIF(missed_app_frame, vsync, NULL))) AS missed_app_frames,
  COUNT(DISTINCT(IIF(missed_sf_frame, vsync, NULL))) AS missed_sf_frames,
  COUNT(DISTINCT(IIF(missed_frame, vsync, NULL))) AS missed_frames,
  COUNT(DISTINCT(IIF(dropped_frame, vsync, NULL))) AS dropped_frames,
  CAST(PERCENTILE(dur, 50) AS INTEGER) AS frame_dur_p50,
  CAST(PERCENTILE(dur, 90) AS INTEGER) AS frame_dur_p90,
  CAST(PERCENTILE(dur, 95) AS INTEGER) AS frame_dur_p95,
  CAST(PERCENTILE(dur, 99) AS INTEGER) AS frame_dur_p99,
  PERCENTILE(dur_ms, 50) AS frame_dur_ms_p50,
  PERCENTILE(dur_ms, 90) AS frame_dur_ms_p90,
  PERCENTILE(dur_ms, 95) AS frame_dur_ms_p95,
  PERCENTILE(dur_ms, 99) AS frame_dur_ms_p99,
  CAST(AVG(dur) AS INTEGER) AS frame_dur_avg,
  MAX(dur) AS frame_dur_max
FROM frames
JOIN process_metadata USING (upid)
GROUP BY upid, process_name;

DROP VIEW IF EXISTS android_frame_timeline_metric_output;
CREATE PERFETTO VIEW android_frame_timeline_metric_output AS
WITH per_jank_type_metric AS (
  SELECT
    jank_type,
    COUNT(DISTINCT(vsync)) AS total_count,
    COUNT(DISTINCT(IIF(present_type = 'Unspecified Present', vsync, NULL))) AS present_unspecified_count,
    COUNT(DISTINCT(IIF(present_type = 'On-time Present', vsync, NULL))) AS present_on_time_count,
    COUNT(DISTINCT(IIF(present_type = 'Late Present', vsync, NULL))) AS present_late_count,
    COUNT(DISTINCT(IIF(present_type = 'Early Present', vsync, NULL))) AS present_early_count,
    COUNT(DISTINCT(IIF(present_type = 'Dropped Frame', vsync, NULL))) AS present_dropped_count,
    COUNT(DISTINCT(IIF(present_type = 'Unknown Present', vsync, NULL))) AS present_unknown_count
  FROM splitted_jank_type_timeline
  GROUP BY jank_type
),
per_process_jank_type_metric AS (
  SELECT
    upid,
    jank_type,
    COUNT(DISTINCT(vsync)) AS total_count,
    COUNT(DISTINCT(IIF(present_type = 'Unspecified Present', vsync, NULL))) AS present_unspecified_count,
    COUNT(DISTINCT(IIF(present_type = 'On-time Present', vsync, NULL))) AS present_on_time_count,
    COUNT(DISTINCT(IIF(present_type = 'Late Present', vsync, NULL))) AS present_late_count,
    COUNT(DISTINCT(IIF(present_type = 'Early Present', vsync, NULL))) AS present_early_count,
    COUNT(DISTINCT(IIF(present_type = 'Dropped Frame', vsync, NULL))) AS present_dropped_count,
    COUNT(DISTINCT(IIF(present_type = 'Unknown Present', vsync, NULL))) AS present_unknown_count
  FROM splitted_jank_type_timeline
  GROUP BY upid, jank_type
)
SELECT
  AndroidFrameTimelineMetric(
    'total_frames', SUM(total_frames),
    'missed_app_frames', SUM(missed_app_frames),
    'dropped_frames', SUM(dropped_frames),
    'process', (
      SELECT
        RepeatedField(
          AndroidFrameTimelineMetric_ProcessBreakdown(
            'process', process_metadata,
            'total_frames', total_frames,
            'missed_frames', missed_frames,
            'missed_app_frames', missed_app_frames,
            'missed_sf_frames', missed_sf_frames,
            'frame_dur_max', frame_dur_max,
            'frame_dur_avg', frame_dur_avg,
            'frame_dur_p50', frame_dur_p50,
            'frame_dur_p90', frame_dur_p90,
            'frame_dur_p95', frame_dur_p95,
            'frame_dur_p99', frame_dur_p99,
            'frame_dur_ms_p50', frame_dur_ms_p50,
            'frame_dur_ms_p90', frame_dur_ms_p90,
            'frame_dur_ms_p95', frame_dur_ms_p95,
            'frame_dur_ms_p99', frame_dur_ms_p99,
            'dropped_frames', dropped_frames,
            'jank_types', (
              SELECT
                RepeatedField(
                  AndroidFrameTimelineMetric_JankTypeMetric(
                    'type', jank_type,
                    'total_count', total_count,
                    'present_unspecified_count', present_unspecified_count,
                    'present_on_time_count', present_on_time_count,
                    'present_late_count', present_late_count,
                    'present_early_count', present_early_count,
                    'present_dropped_count', present_dropped_count,
                    'present_unknown_count', present_unknown_count
                  )
                )
              FROM per_process_jank_type_metric
              WHERE upid = process.upid
            )
          )
        )
      FROM android_frame_timeline_metric_per_process process
    ),
    'jank_types', (
      SELECT
        RepeatedField(
          AndroidFrameTimelineMetric_JankTypeMetric(
            'type', jank_type,
            'total_count', total_count,
            'present_unspecified_count', present_unspecified_count,
            'present_on_time_count', present_on_time_count,
            'present_late_count', present_late_count,
            'present_early_count', present_early_count,
            'present_dropped_count', present_dropped_count,
            'present_unknown_count', present_unknown_count
          )
        )
      FROM per_jank_type_metric
    )
  )
FROM android_frame_timeline_metric_per_process;
