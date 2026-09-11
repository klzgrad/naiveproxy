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
--

-- TraceAnalysisStats carries one Stat entry per (name, idx). The `stats`
-- view emits one row per (name, idx, machine_id, trace_id), so we sum
-- `value` across the (machine_id, trace_id).
DROP VIEW IF EXISTS trace_stats_output;
CREATE PERFETTO VIEW trace_stats_output AS
SELECT TraceAnalysisStats(
  'stat', (
    SELECT RepeatedField(TraceAnalysisStats_Stat(
      'name', name,
      'idx', idx,
      'count', total_value,
      'source', CASE source
        WHEN 'trace' THEN 'SOURCE_TRACE'
        WHEN 'analysis' THEN 'SOURCE_ANALYSIS'
        ELSE 'SOURCE_UNKNOWN'
      END,
      'severity', CASE severity
        WHEN 'info' THEN 'SEVERITY_INFO'
        WHEN 'data_loss' THEN 'SEVERITY_DATA_LOSS'
        WHEN 'error' THEN 'SEVERITY_ERROR'
        ELSE 'SEVERITY_UNKNOWN'
      END
      ))
    FROM (
      SELECT name, idx, source, severity, SUM(value) AS total_value
      FROM stats
      GROUP BY name, idx, source, severity
      ORDER BY name ASC
    )
  )
);
