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

SELECT RUN_METRIC('android/process_metadata.sql') AS unused;

DROP VIEW IF EXISTS profiler_smaps_output;
CREATE PERFETTO VIEW profiler_smaps_output AS
WITH base_stat_counts AS (
  SELECT
    ts,
    upid,
    path,
    SUM(size_kb) AS size_kb,
    SUM(private_dirty_kb) AS private_dirty_kb,
    SUM(swap_kb) AS swap_kb
  FROM profiler_smaps
  GROUP BY 1, 2, 3
  ORDER BY 4 DESC
),
mapping_protos AS (
  SELECT
    ts,
    upid,
    RepeatedField(ProfilerSmaps_Mapping(
        'path', path,
        'size_kb', size_kb,
        'private_dirty_kb', private_dirty_kb,
        'swap_kb', swap_kb
      )) AS mappings
  FROM base_stat_counts
  GROUP BY 1, 2
)
SELECT ProfilerSmaps(
    'instance', RepeatedField(
      ProfilerSmaps_Instance(
        'process', process_metadata.metadata,
        'mappings', mappings
      ))
  )
FROM mapping_protos JOIN process_metadata USING (upid);
