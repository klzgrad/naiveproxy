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

DROP VIEW IF EXISTS rt_runtime_all;

CREATE PERFETTO VIEW rt_runtime_all
AS
SELECT ts, dur, thread.name AS tname
FROM sched_slice
LEFT JOIN thread
  USING (utid)
LEFT JOIN process
  USING (upid)
WHERE priority < 100
ORDER BY dur DESC;

DROP VIEW IF EXISTS android_rt_runtime_output;

CREATE PERFETTO VIEW android_rt_runtime_output
AS
SELECT
  AndroidRtRuntimeMetric(
    'max_runtime',
    (SELECT dur FROM rt_runtime_all LIMIT 1),
    'over_5ms_count',
    (SELECT COUNT(*) FROM rt_runtime_all WHERE dur > 5e6),
    'longest_rt_slices',
    (
      SELECT
        RepeatedField(
          AndroidRtRuntimeMetric_RtSlice(
            'tname', tname, 'ts', ts, 'dur', dur))
      FROM (SELECT ts, dur, tname FROM rt_runtime_all LIMIT 10)
    ));
