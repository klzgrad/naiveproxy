--
-- Copyright 2024 The Android Open Source Project
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

INCLUDE PERFETTO MODULE viz.summary.slices;

CREATE PERFETTO TABLE _sched_summary AS
SELECT
  utid,
  max(dur) AS max_running_dur,
  sum(dur) AS sum_running_dur,
  count() AS running_count
FROM sched
WHERE
  NOT (utid IN (SELECT utid FROM thread WHERE is_idle))
  AND dur != -1
GROUP BY
  utid;

CREATE PERFETTO TABLE _thread_track_summary AS
SELECT utid, sum(cnt) AS slice_count
FROM thread_track
JOIN _slice_track_summary USING (id)
GROUP BY
  utid;

CREATE PERFETTO TABLE _stack_sample_summary AS
SELECT tc.utid, count() AS sample_count
FROM stack_sample AS ss
JOIN stack_sample_task_context AS tc ON tc.id = ss.task_context_id
WHERE
  tc.utid IS NOT NULL
GROUP BY
  tc.utid;

CREATE PERFETTO TABLE _thread_available_info_summary AS
WITH
  raw AS (
    SELECT
      utid,
      ss.max_running_dur,
      ss.sum_running_dur,
      ss.running_count,
      (SELECT slice_count FROM _thread_track_summary WHERE utid = t.utid) AS slice_count,
      (SELECT sample_count FROM _stack_sample_summary WHERE utid = t.utid) AS stack_sample_count
    FROM thread AS t
    LEFT JOIN _sched_summary AS ss USING (utid)
  )
SELECT
  utid,
  coalesce(max_running_dur, 0) AS max_running_dur,
  coalesce(sum_running_dur, 0) AS sum_running_dur,
  coalesce(running_count, 0) AS running_count,
  coalesce(slice_count, 0) AS slice_count,
  coalesce(stack_sample_count, 0) AS stack_sample_count
FROM raw AS r
WHERE
  NOT (r.max_running_dur IS NULL
  AND r.sum_running_dur IS NULL
  AND r.running_count IS NULL
  AND r.slice_count IS NULL
  AND r.stack_sample_count IS NULL)
  OR utid IN (SELECT utid FROM _stack_sample_summary)
  OR utid IN (SELECT utid FROM thread_counter_track);
