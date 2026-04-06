--
-- Copyright 2019 The Android Open Source Project
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

-- Create all the views used to generate the Android Cpu metrics proto.
SELECT RUN_METRIC('android/android_cpu_agg.sql');
SELECT RUN_METRIC('android/android_cpu_raw_metrics_per_core.sql',
  'input_table', 'cpu_freq_sched_per_thread',
  'output_table', 'raw_metrics_per_core');
SELECT RUN_METRIC('android/process_metadata.sql');

DROP VIEW IF EXISTS metrics_per_core_type;
CREATE PERFETTO VIEW metrics_per_core_type AS
SELECT
  utid,
  core_type,
  AndroidCpuMetric_Metrics(
    'mcycles', SUM(mcycles),
    'runtime_ns', SUM(runtime_ns),
    'min_freq_khz', MIN(min_freq_khz),
    'max_freq_khz', MAX(max_freq_khz),
    -- In total here, we need to divide the denominator by 1e9 (to convert
    -- ns to s) and divide the numerator by 1e6 (to convert millicycles to
    -- kcycles). In total, this means we need to multiply the expression as
    -- a whole by 1e3.
    'avg_freq_khz', CAST((SUM(millicycles) / SUM(runtime_ns)) * 1000 AS INT)
  ) AS proto
FROM raw_metrics_per_core
GROUP BY utid, core_type;

-- Aggregate everything per thread.
DROP VIEW IF EXISTS core_proto_per_thread;
CREATE PERFETTO VIEW core_proto_per_thread AS
SELECT
  utid,
  RepeatedField(
    AndroidCpuMetric_CoreData(
      'id', cpu,
      'metrics', AndroidCpuMetric_Metrics(
        'mcycles', mcycles,
        'runtime_ns', runtime_ns,
        'min_freq_khz', min_freq_khz,
        'max_freq_khz', max_freq_khz,
        'avg_freq_khz', avg_freq_khz
      )
    )
  ) AS proto
FROM raw_metrics_per_core
GROUP BY utid;

DROP VIEW IF EXISTS core_type_proto_per_thread;
CREATE PERFETTO VIEW core_type_proto_per_thread AS
SELECT
  utid,
  RepeatedField(
    AndroidCpuMetric_CoreTypeData(
      'type', core_type,
      'metrics', metrics_per_core_type.proto
    )
  ) AS proto
FROM metrics_per_core_type
GROUP BY utid;

DROP VIEW IF EXISTS metrics_proto_per_thread;
CREATE PERFETTO VIEW metrics_proto_per_thread AS
SELECT
  utid,
  AndroidCpuMetric_Metrics(
    'mcycles', SUM(mcycles),
    'runtime_ns', SUM(runtime_ns),
    'min_freq_khz', MIN(min_freq_khz),
    'max_freq_khz', MAX(max_freq_khz),
    -- See above for a breakdown of the maths used to compute the
    -- multiplicative factor.
    'avg_freq_khz', CAST((SUM(millicycles) / SUM(runtime_ns)) * 1000 AS INT)
  ) AS proto
FROM raw_metrics_per_core
GROUP BY utid;

-- Aggregate everything per perocess
DROP VIEW IF EXISTS thread_proto_per_process;
CREATE PERFETTO VIEW thread_proto_per_process AS
SELECT
  upid,
  RepeatedField(
    AndroidCpuMetric_Thread(
      'name', thread.name,
      'metrics', metrics_proto_per_thread.proto,
      'core', core_proto_per_thread.proto,
      'core_type', core_type_proto_per_thread.proto
    )
  ) AS proto
FROM thread
LEFT JOIN core_proto_per_thread USING (utid)
LEFT JOIN core_type_proto_per_thread USING (utid)
LEFT JOIN metrics_proto_per_thread USING(utid)
GROUP BY upid;

DROP VIEW IF EXISTS core_metrics_per_process;
CREATE PERFETTO VIEW core_metrics_per_process AS
SELECT
  upid,
  cpu,
  AndroidCpuMetric_Metrics(
    'mcycles', SUM(mcycles),
    'runtime_ns', SUM(runtime_ns),
    'min_freq_khz', MIN(min_freq_khz),
    'max_freq_khz', MAX(max_freq_khz),
    -- In total here, we need to divide the denominator by 1e9 (to convert
    -- ns to s) and divide the numerator by 1e6 (to convert millicycles to
    -- kcycles). In total, this means we need to multiply the expression as
    -- a whole by 1e3.
    'avg_freq_khz', CAST((SUM(millicycles) / SUM(runtime_ns)) * 1000 AS INT)
  ) AS proto
FROM raw_metrics_per_core
JOIN thread USING (utid)
GROUP BY upid, cpu;

DROP VIEW IF EXISTS core_proto_per_process;
CREATE PERFETTO VIEW core_proto_per_process AS
SELECT
  upid,
  RepeatedField(
    AndroidCpuMetric_CoreData(
      'id', cpu,
      'metrics', core_metrics_per_process.proto
    )
  ) AS proto
FROM core_metrics_per_process
GROUP BY upid;

DROP VIEW IF EXISTS core_type_metrics_per_process;
CREATE PERFETTO VIEW core_type_metrics_per_process AS
SELECT
  upid,
  core_type,
  AndroidCpuMetric_Metrics(
    'mcycles', SUM(mcycles),
    'runtime_ns', SUM(runtime_ns),
    'min_freq_khz', MIN(min_freq_khz),
    'max_freq_khz', MAX(max_freq_khz),
    -- In total here, we need to divide the denominator by 1e9 (to convert
    -- ns to s) and divide the numerator by 1e6 (to convert millicycles to
    -- kcycles). In total, this means we need to multiply the expression as
    -- a whole by 1e3.
    'avg_freq_khz', CAST((SUM(millicycles) / SUM(runtime_ns)) * 1000 AS INT)
  ) AS proto
FROM raw_metrics_per_core
JOIN thread USING (utid)
GROUP BY upid, core_type;

DROP VIEW IF EXISTS core_type_proto_per_process;
CREATE PERFETTO VIEW core_type_proto_per_process AS
SELECT
  upid,
  RepeatedField(
    AndroidCpuMetric_CoreTypeData(
      'type', core_type,
      'metrics', core_type_metrics_per_process.proto
    )
  ) AS proto
FROM core_type_metrics_per_process
GROUP BY upid;

DROP VIEW IF EXISTS metrics_proto_per_process;
CREATE PERFETTO VIEW metrics_proto_per_process AS
SELECT
  upid,
  AndroidCpuMetric_Metrics(
    'mcycles', SUM(mcycles),
    'runtime_ns', SUM(runtime_ns),
    'min_freq_khz', MIN(min_freq_khz),
    'max_freq_khz', MAX(max_freq_khz),
    -- See above for a breakdown of the maths used to compute the
    -- multiplicative factor.
    'avg_freq_khz', CAST((SUM(millicycles) / SUM(runtime_ns)) * 1000 AS INT)
  ) AS proto
FROM raw_metrics_per_core
JOIN thread USING (utid)
GROUP BY upid;

DROP VIEW IF EXISTS android_cpu_output;
CREATE PERFETTO VIEW android_cpu_output AS
SELECT AndroidCpuMetric(
  'process_info', (
    SELECT RepeatedField(
      AndroidCpuMetric_Process(
        'name', process.name,
        'process', process_metadata.metadata,
        'metrics', metrics_proto_per_process.proto,
        'threads', thread_proto_per_process.proto,
        'core', core_proto_per_process.proto,
        'core_type', core_type_proto_per_process.proto
      )
    )
    FROM process
    JOIN metrics_proto_per_process USING(upid)
    JOIN thread_proto_per_process USING (upid)
    JOIN core_proto_per_process USING (upid)
    JOIN core_type_proto_per_process USING (upid)
    JOIN process_metadata USING (upid)
  )
);
