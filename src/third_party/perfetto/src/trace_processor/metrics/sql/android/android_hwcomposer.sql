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

SELECT RUN_METRIC(
  'android/composition_layers.sql',
  'track_name', 'HWComposer: Total Layer',
  'output', 'total_layers'
);

SELECT RUN_METRIC(
  'android/composition_layers.sql',
  'track_name', 'HWComposer: DPU Layer',
  'output', 'dpu_layers'
);

SELECT RUN_METRIC(
  'android/composition_layers.sql',
  'track_name', 'HWComposer: GPU Layer',
  'output', 'gpu_layers'
);

SELECT RUN_METRIC(
  'android/composition_layers.sql',
  'track_name', 'HWComposer: DPU Cached Layer',
  'output', 'dpu_cached_layers'
);

SELECT RUN_METRIC(
  'android/composition_layers.sql',
  'track_name', 'HWComposer: SF Cached Layer',
  'output', 'sf_cached_layers'
);

SELECT RUN_METRIC(
  'android/composition_layers.sql',
  'track_name', 'HWComposer: RCD Layer',
  'output', 'rcd_layers'
);

SELECT RUN_METRIC(
  'android/composer_execution.sql',
  'output', 'hwc_execution_spans'
);


DROP VIEW IF EXISTS display_ids;
CREATE PERFETTO VIEW display_ids AS
SELECT DISTINCT display_id
FROM (
  SELECT display_id FROM total_layers
  UNION
  SELECT display_id FROM dpu_layers
  UNION
  SELECT display_id FROM gpu_layers
  UNION
  SELECT display_id FROM dpu_cached_layers
  UNION
  SELECT display_id FROM sf_cached_layers
  UNION
  SELECT display_id FROM rcd_layers
  UNION
  SELECT display_id FROM hwc_execution_spans
);

DROP VIEW IF EXISTS metrics_per_display;
CREATE PERFETTO VIEW metrics_per_display AS
SELECT AndroidHwcomposerMetrics_MetricsPerDisplay(
  'display_id', display_id,
  'composition_total_layers',
  (SELECT AVG(value) FROM total_layers WHERE display_id = d.display_id),
  'composition_dpu_layers',
  (SELECT AVG(value) FROM dpu_layers WHERE display_id = d.display_id),
  'composition_gpu_layers',
  (SELECT AVG(value) FROM gpu_layers WHERE display_id = d.display_id),
  'composition_dpu_cached_layers',
  (SELECT AVG(value) FROM dpu_cached_layers WHERE display_id = d.display_id),
  'composition_sf_cached_layers',
  (SELECT AVG(value) FROM sf_cached_layers WHERE display_id = d.display_id),
  'composition_rcd_layers',
  (SELECT AVG(value) FROM rcd_layers WHERE display_id = d.display_id),
  'skipped_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'skipped_validation'),
  'unskipped_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'unskipped_validation'),
  'separated_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'separated_validation'),
  'unknown_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'unknown'),
  'avg_all_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type != 'unknown'),
  'avg_skipped_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'skipped_validation'),
  'avg_unskipped_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'unskipped_validation'),
  'avg_separated_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE display_id = d.display_id AND validation_type = 'separated_validation')
) AS proto
FROM display_ids d;

SELECT RUN_METRIC('android/process_counter_span_view.sql',
  'table_name', 'dpu_vote_clock',
  'counter_name', 'dpu_vote_clock'
);

SELECT RUN_METRIC('android/process_counter_span_view.sql',
  'table_name', 'dpu_vote_avg_bw',
  'counter_name', 'dpu_vote_avg_bw'
);

SELECT RUN_METRIC('android/process_counter_span_view.sql',
  'table_name', 'dpu_vote_peak_bw',
  'counter_name', 'dpu_vote_peak_bw'
);

SELECT RUN_METRIC('android/process_counter_span_view.sql',
  'table_name', 'dpu_vote_rt_bw',
  'counter_name', 'dpu_vote_rt_bw'
);

DROP VIEW IF EXISTS dpu_vote_process;
CREATE PERFETTO VIEW dpu_vote_process AS
SELECT DISTINCT p.upid, p.pid
FROM (
  SELECT upid FROM dpu_vote_clock_span
  UNION
  SELECT upid FROM dpu_vote_avg_bw_span
  UNION
  SELECT upid FROM dpu_vote_peak_bw_span
) s JOIN process p USING (upid);

-- These systrace counters are coming from dedicated kernel threads, so we can
-- assume pid = tid.
DROP VIEW IF EXISTS dpu_vote_metrics;
CREATE PERFETTO VIEW dpu_vote_metrics AS
SELECT AndroidHwcomposerMetrics_DpuVoteMetrics(
  'tid', pid,
  'avg_dpu_vote_clock',
  (SELECT SUM(dpu_vote_clock_val * dur) / SUM(dur)
    FROM dpu_vote_clock_span s WHERE s.upid = p.upid),
  'avg_dpu_vote_avg_bw',
  (SELECT SUM(dpu_vote_avg_bw_val * dur) / SUM(dur)
    FROM dpu_vote_avg_bw_span s WHERE s.upid = p.upid),
  'avg_dpu_vote_peak_bw',
  (SELECT SUM(dpu_vote_peak_bw_val * dur) / SUM(dur)
    FROM dpu_vote_peak_bw_span s WHERE s.upid = p.upid),
  'avg_dpu_vote_rt_bw',
  (SELECT SUM(dpu_vote_rt_bw_val * dur) / SUM(dur)
    FROM dpu_vote_rt_bw_span s WHERE s.upid = p.upid)
) AS proto
FROM dpu_vote_process p
ORDER BY pid;

DROP VIEW IF EXISTS android_hwcomposer_output;
CREATE PERFETTO VIEW android_hwcomposer_output AS
SELECT AndroidHwcomposerMetrics(
  'composition_total_layers', (SELECT AVG(value) FROM total_layers),
  'composition_dpu_layers', (SELECT AVG(value) FROM dpu_layers),
  'composition_gpu_layers', (SELECT AVG(value) FROM gpu_layers),
  'composition_dpu_cached_layers', (SELECT AVG(value) FROM dpu_cached_layers),
  'composition_sf_cached_layers', (SELECT AVG(value) FROM sf_cached_layers),
  'composition_rcd_layers', (SELECT AVG(value) FROM rcd_layers),
  'skipped_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE validation_type = 'skipped_validation'),
  'unskipped_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE validation_type = 'unskipped_validation'),
  'separated_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE validation_type = 'separated_validation'),
  'unknown_validation_count',
  (SELECT COUNT(*) FROM hwc_execution_spans
    WHERE validation_type = 'unknown'),
  'avg_all_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE validation_type != 'unknown'),
  'avg_skipped_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE validation_type = 'skipped_validation'),
  'avg_unskipped_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE validation_type = 'unskipped_validation'),
  'avg_separated_execution_time_ms',
  (SELECT AVG(execution_time_ns) / 1e6 FROM hwc_execution_spans
    WHERE validation_type = 'separated_validation'),
  'dpu_vote_metrics', (SELECT RepeatedField(proto) FROM dpu_vote_metrics),
  'metrics_per_display', (SELECT RepeatedField(proto) FROM metrics_per_display)
);
