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

INCLUDE PERFETTO MODULE android.memory.lmk;

DROP VIEW IF EXISTS android_lmk_output;
CREATE PERFETTO VIEW android_lmk_output AS
WITH lmk_counts AS (
  SELECT oom_score_adj, COUNT(1) AS count
  FROM android_lmk_events
  GROUP BY 1
)
SELECT AndroidLmkMetric(
  'total_count', (SELECT COUNT(1) FROM android_lmk_events),
  'by_oom_score', (
    SELECT
      RepeatedField(AndroidLmkMetric_ByOomScore(
        'oom_score_adj', oom_score_adj,
        'count', count
      ))
    FROM lmk_counts
    WHERE oom_score_adj IS NOT NULL
  ),
  'oom_victim_count', (
    SELECT COUNT(1)
    FROM instant
    WHERE name = 'mem.oom_kill'
  )
);
