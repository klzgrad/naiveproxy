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

DROP VIEW IF EXISTS irq_runtime_all;

CREATE PERFETTO VIEW irq_runtime_all
AS
SELECT ts, dur, name
FROM slice
WHERE category = 'irq';

DROP VIEW IF EXISTS hw_irq_runtime;

CREATE PERFETTO VIEW hw_irq_runtime
AS
SELECT ts, dur, name
FROM irq_runtime_all
WHERE name GLOB 'IRQ (*)'
ORDER BY dur DESC;

DROP VIEW IF EXISTS hw_irq_runtime_statistics;

CREATE PERFETTO VIEW hw_irq_runtime_statistics
AS
SELECT
  MAX(dur) AS max_runtime,
  COUNT(IIF(dur > 1e6, 1, NULL)) AS over_threshold_count,
  COUNT(*) AS total_count
FROM hw_irq_runtime;

DROP VIEW IF EXISTS sw_irq_runtime;
CREATE PERFETTO VIEW sw_irq_runtime
AS
SELECT ts, dur, name
FROM irq_runtime_all
WHERE name NOT GLOB 'IRQ (*)'
ORDER BY dur DESC;

DROP VIEW IF EXISTS sw_irq_runtime_statistics;
CREATE PERFETTO VIEW sw_irq_runtime_statistics
AS
SELECT
  MAX(dur) AS max_runtime,
  COUNT(IIF(dur > 5e6, 1, NULL)) AS over_threshold_count,
  COUNT(*) AS total_count
FROM sw_irq_runtime;

DROP VIEW IF EXISTS android_irq_runtime_output;

CREATE PERFETTO VIEW android_irq_runtime_output
AS
SELECT
  AndroidIrqRuntimeMetric(
    'hw_irq',
    (
      SELECT
        AndroidIrqRuntimeMetric_IrqRuntimeMetric(
          'max_runtime',
          max_runtime,
          'total_count',
          total_count,
          'threshold_metric',
          AndroidIrqRuntimeMetric_ThresholdMetric(
            'threshold',
            '1ms',
            'over_threshold_count',
            over_threshold_count,
            'anomaly_ratio',
            CAST(
              over_threshold_count AS DOUBLE)
            / CAST(
              total_count AS DOUBLE)),
          'longest_irq_slices',
          (
            SELECT
              RepeatedField(
                AndroidIrqRuntimeMetric_IrqSlice(
                  'irq_name', name, 'ts', ts, 'dur', dur))
            FROM (SELECT ts, dur, name FROM hw_irq_runtime LIMIT 10)
          ))
      FROM hw_irq_runtime_statistics
    ),
    'sw_irq',
    (
      SELECT
        AndroidIrqRuntimeMetric_IrqRuntimeMetric(
          'max_runtime',
          max_runtime,
          'total_count',
          total_count,
          'threshold_metric',
          AndroidIrqRuntimeMetric_ThresholdMetric(
            'threshold',
            '5ms',
            'over_threshold_count',
            over_threshold_count,
            'anomaly_ratio',
            CAST(
              over_threshold_count AS DOUBLE)
            / CAST(
              total_count AS DOUBLE)),
          'longest_irq_slices',
          (
            SELECT
              RepeatedField(
                AndroidIrqRuntimeMetric_IrqSlice(
                  'irq_name', name, 'ts', ts, 'dur', dur))
            FROM (SELECT ts, dur, name FROM sw_irq_runtime LIMIT 10)
          ))
      FROM sw_irq_runtime_statistics
    ));
