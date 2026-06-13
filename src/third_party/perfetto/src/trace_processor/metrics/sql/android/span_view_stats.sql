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

DROP TABLE IF EXISTS {{table_name}}_delta;
CREATE PERFETTO TABLE {{table_name}}_delta AS
WITH rolling_delta AS (
  -- emits one row per ts point
  SELECT
    upid,
    {{table_name}}_val - MIN({{table_name}}_val) OVER win AS delta
  FROM {{table_name}}_span
  WINDOW win AS (
    PARTITION BY upid
    ORDER BY ts
    ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
  )
)
SELECT
  upid,
  -- max over all ts
  MAX(delta) AS delta
FROM rolling_delta
GROUP BY 1;

DROP TABLE IF EXISTS {{table_name}}_stats;
CREATE PERFETTO TABLE {{table_name}}_stats AS
SELECT
  process.name AS process_name,
  MIN(span.{{table_name}}_val) AS min_value,
  MAX(span.{{table_name}}_val) AS max_value,
  SUM(span.{{table_name}}_val * span.dur) / SUM(span.dur) AS avg_value,
  MAX(delta.delta) AS max_delta_value
FROM {{table_name}}_span AS span
JOIN {{table_name}}_delta AS delta USING(upid)
JOIN process USING(upid)
WHERE process.name IS NOT NULL
GROUP BY 1
ORDER BY 1;

DROP VIEW IF EXISTS {{table_name}}_stats_proto;
CREATE PERFETTO VIEW {{table_name}}_stats_proto AS
SELECT
  process_name,
  AndroidMemoryMetric_Counter(
    'min', min_value,
    'max', max_value,
    'avg', avg_value,
    'delta', max_delta_value
  ) AS proto
FROM {{table_name}}_stats;
