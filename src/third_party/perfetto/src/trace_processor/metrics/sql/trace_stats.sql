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

DROP VIEW IF EXISTS trace_stats_output;
CREATE PERFETTO VIEW trace_stats_output AS
SELECT TraceAnalysisStats(
  'stat', (
    SELECT RepeatedField(TraceAnalysisStats_Stat(
      'name', name,
      'idx', idx,
      'count', value,
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
    FROM stats ORDER BY name ASC
  )
);
