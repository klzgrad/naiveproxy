--
-- Copyright 2020 The Android Open Source Project
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
--

SELECT RUN_METRIC('android/global_counter_span_view.sql',
  'table_name', 'global_gpu_memory',
  'counter_name', 'GPU Memory');

SELECT RUN_METRIC('android/process_counter_span_view.sql',
  'table_name', 'proc_gpu_memory',
  'counter_name', 'GPU Memory');

DROP VIEW IF EXISTS proc_gpu_memory_view;
CREATE PERFETTO VIEW proc_gpu_memory_view AS
SELECT
  upid,
  MAX(proc_gpu_memory_val) AS mem_max,
  MIN(proc_gpu_memory_val) AS mem_min,
  SUM(proc_gpu_memory_val * dur) AS mem_valxdur,
  SUM(dur) AS mem_dur
FROM proc_gpu_memory_span
GROUP BY upid;

DROP VIEW IF EXISTS agg_proc_gpu_view;
CREATE PERFETTO VIEW agg_proc_gpu_view AS
SELECT
  name,
  MAX(mem_max) AS mem_max,
  MIN(mem_min) AS mem_min,
  SUM(mem_valxdur) / SUM(mem_dur) AS mem_avg
FROM process
JOIN proc_gpu_memory_view
  USING(upid)
GROUP BY name;

DROP VIEW IF EXISTS proc_gpu_view;
CREATE PERFETTO VIEW proc_gpu_view AS
SELECT
  AndroidGpuMetric_Process(
    'name', name,
    'mem_max', CAST(mem_max AS INT64),
    'mem_min', CAST(mem_min AS INT64),
    'mem_avg', CAST(mem_avg AS INT64)
  ) AS proto
FROM agg_proc_gpu_view;

SELECT RUN_METRIC('android/gpu_counter_span_view.sql',
  'table_name', 'gpu_freq',
  'counter_name', 'gpufreq');

DROP VIEW IF EXISTS metrics_per_freq_view;
CREATE PERFETTO VIEW metrics_per_freq_view AS
WITH
total_dur_per_freq AS (
  SELECT
    gpu_id,
    gpu_freq_val AS freq,
    SUM(dur) AS dur_ns
  FROM gpu_freq_span
  GROUP BY gpu_id, gpu_freq_val
),
total_dur_per_gpu AS (
  SELECT
    gpu_id,
    SUM(dur) AS dur_ns
  FROM gpu_freq_span
  GROUP BY gpu_id
)
SELECT
  gpu_id,
  AndroidGpuMetric_FrequencyMetric_MetricsPerFrequency(
    'freq', CAST(freq AS INT64),
    'dur_ms', f.dur_ns / 1e6,
    'percentage', f.dur_ns * 100.0 / g.dur_ns
  ) AS proto
FROM total_dur_per_freq f LEFT JOIN total_dur_per_gpu g USING (gpu_id);

DROP VIEW IF EXISTS gpu_freq_metrics_view;
CREATE PERFETTO VIEW gpu_freq_metrics_view AS
SELECT
  AndroidGpuMetric_FrequencyMetric(
    'gpu_id', gpu_id,
    'freq_max', CAST(MAX(gpu_freq_val) AS INT64),
    'freq_min', CAST(MIN(gpu_freq_val) AS INT64),
    'freq_avg', SUM(gpu_freq_val * dur) / SUM(dur),
    'used_freqs', (SELECT RepeatedField(proto) FROM metrics_per_freq_view
      WHERE metrics_per_freq_view.gpu_id = gpu_freq_span.gpu_id)
  ) AS proto
FROM gpu_freq_span
GROUP BY gpu_id;

DROP VIEW IF EXISTS android_gpu_output;
CREATE PERFETTO VIEW android_gpu_output AS
SELECT AndroidGpuMetric(
  'processes', (SELECT RepeatedField(proto) FROM proc_gpu_view),
  'mem_max', CAST(MAX(global_gpu_memory_val) AS INT64),
  'mem_min', CAST(MIN(global_gpu_memory_val) AS INT64),
  'mem_avg', CAST(SUM(global_gpu_memory_val * dur) / SUM(dur) AS INT64),
  'freq_metrics', (SELECT RepeatedField(proto) FROM gpu_freq_metrics_view)
)
FROM global_gpu_memory_span;
