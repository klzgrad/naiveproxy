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

-- Initialize relevant thread, frames and CUJ tables for computing this metric.
SELECT RUN_METRIC('android/jank/android_jank_cuj_init.sql');

-- Creates a table that matches CUJ counters with the correct CUJs.
-- After the CUJ ends FrameTracker emits counters with the number of total
-- frames, missed frames, longest frame duration, etc.
-- The same numbers are also reported by FrameTracker to statsd.
SELECT RUN_METRIC('android/jank/internal/counters.sql');

DROP VIEW IF EXISTS android_jank_cuj_output;
CREATE PERFETTO VIEW android_jank_cuj_output AS
SELECT
  AndroidJankCujMetric(
    'cuj', (
      SELECT RepeatedField(
        AndroidJankCujMetric_Cuj(
          'id', cuj_id,
          'name', cuj_name,
          'process', process_metadata,
          'layer_name', layer_name,
          'ts', COALESCE(boundary.ts, cuj.ts),
          'dur', COALESCE(boundary.dur, cuj.dur),
          'counter_metrics', (
            SELECT AndroidJankCujMetric_Metrics(
              'total_frames', total_frames,
              'missed_frames', missed_frames,
              'missed_app_frames', missed_app_frames,
              'missed_sf_frames', missed_sf_frames,
              'missed_frames_max_successive', missed_frames_max_successive,
              'sf_callback_missed_frames', sf_callback_missed_frames,
              'hwui_callback_missed_frames', hwui_callback_missed_frames,
              'frame_dur_max', frame_dur_max)
            FROM android_jank_cuj_counter_metrics cm
            WHERE cm.cuj_id = cuj.cuj_id),
          'trace_metrics', (
            SELECT AndroidJankCujMetric_Metrics(
              'total_frames', COUNT(*),
              'missed_frames', SUM(app_missed OR sf_missed),
              'missed_app_frames', SUM(app_missed),
              'missed_sf_frames', SUM(sf_missed),
              'sf_callback_missed_frames', SUM(sf_callback_missed),
              'hwui_callback_missed_frames', SUM(hwui_callback_missed),
              'frame_dur_max', MAX(f.dur),
              'frame_dur_avg', CAST(AVG(f.dur) AS INTEGER),
              'frame_dur_p50', CAST(PERCENTILE(f.dur, 50) AS INTEGER),
              'frame_dur_p90', CAST(PERCENTILE(f.dur, 90) AS INTEGER),
              'frame_dur_p95', CAST(PERCENTILE(f.dur, 95) AS INTEGER),
              'frame_dur_p99', CAST(PERCENTILE(f.dur, 99) AS INTEGER),
              'frame_dur_ms_p50', PERCENTILE(f.dur / 1e6, 50),
              'frame_dur_ms_p90', PERCENTILE(f.dur / 1e6, 90),
              'frame_dur_ms_p95', PERCENTILE(f.dur / 1e6, 95),
              'frame_dur_ms_p99', PERCENTILE(f.dur / 1e6, 99))
            FROM android_jank_cuj_frame f
            WHERE f.cuj_id = cuj.cuj_id),
          'timeline_metrics', (
            SELECT AndroidJankCujMetric_Metrics(
              'total_frames', COUNT(*),
              'missed_frames', SUM(app_missed OR sf_missed),
              'missed_app_frames', SUM(app_missed),
              'missed_sf_frames', SUM(sf_missed),
              'sf_callback_missed_frames', SUM(sf_callback_missed),
              'hwui_callback_missed_frames', SUM(hwui_callback_missed),
              'frame_dur_max', MAX(f.dur),
              'frame_dur_avg', CAST(AVG(f.dur) AS INTEGER),
              'frame_dur_p50', CAST(PERCENTILE(f.dur, 50) AS INTEGER),
              'frame_dur_p90', CAST(PERCENTILE(f.dur, 90) AS INTEGER),
              'frame_dur_p95', CAST(PERCENTILE(f.dur, 95) AS INTEGER),
              'frame_dur_p99', CAST(PERCENTILE(f.dur, 99) AS INTEGER),
              'frame_dur_ms_p50', PERCENTILE(f.dur / 1e6, 50),
              'frame_dur_ms_p90', PERCENTILE(f.dur / 1e6, 90),
              'frame_dur_ms_p95', PERCENTILE(f.dur / 1e6, 95),
              'frame_dur_ms_p99', PERCENTILE(f.dur / 1e6, 99))
            FROM android_jank_cuj_frame_timeline f
            WHERE f.cuj_id = cuj.cuj_id),
          'frame', (
            SELECT RepeatedField(
              AndroidJankCujMetric_Frame(
                'frame_number', f.frame_number,
                'vsync', f.vsync,
                'ts', f.ts,
                'dur', f.dur,
                'dur_expected', f.dur_expected,
                'app_missed', f.app_missed,
                'sf_missed', f.sf_missed,
                'sf_callback_missed', f.sf_callback_missed,
                'hwui_callback_missed', f.hwui_callback_missed))
            FROM android_jank_cuj_frame f
            WHERE f.cuj_id = cuj.cuj_id
            ORDER BY frame_number ASC),
          'sf_frame', (
            SELECT RepeatedField(
              AndroidJankCujMetric_Frame(
                'frame_number', f.frame_number,
                'vsync', f.vsync,
                'ts', f.ts,
                'dur', f.dur,
                'dur_expected', f.dur_expected,
                'sf_missed', f.sf_missed))
            FROM android_jank_cuj_sf_frame f
            WHERE f.cuj_id = cuj.cuj_id
            ORDER BY frame_number ASC)
        ))
      FROM android_jank_cuj cuj
      LEFT JOIN android_jank_cuj_boundary boundary USING (cuj_id)
      LEFT JOIN android_jank_cuj_layer_name cuj_layer USING (cuj_id)
      ORDER BY cuj.cuj_id ASC));
