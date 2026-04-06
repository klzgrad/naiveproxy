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
  NOT utid IN (
    SELECT
      utid
    FROM thread
    WHERE
      is_idle
  ) AND dur != -1
GROUP BY
  utid;

CREATE PERFETTO TABLE _thread_track_summary AS
SELECT
  utid,
  sum(cnt) AS slice_count
FROM thread_track
JOIN _slice_track_summary
  USING (id)
GROUP BY
  utid;

CREATE PERFETTO TABLE _perf_sample_summary AS
SELECT
  utid,
  count() AS perf_sample_cnt
FROM perf_sample
WHERE
  callsite_id IS NOT NULL
GROUP BY
  utid;

CREATE PERFETTO TABLE _instruments_sample_summary AS
SELECT
  utid,
  count() AS instruments_sample_cnt
FROM instruments_sample
WHERE
  callsite_id IS NOT NULL
GROUP BY
  utid;

CREATE PERFETTO TABLE _thread_available_info_summary AS
WITH
  raw AS (
    SELECT
      utid,
      ss.max_running_dur,
      ss.sum_running_dur,
      ss.running_count,
      (
        SELECT
          slice_count
        FROM _thread_track_summary
        WHERE
          utid = t.utid
      ) AS slice_count,
      (
        SELECT
          perf_sample_cnt
        FROM _perf_sample_summary
        WHERE
          utid = t.utid
      ) AS perf_sample_count,
      (
        SELECT
          instruments_sample_cnt
        FROM _instruments_sample_summary
        WHERE
          utid = t.utid
      ) AS instruments_sample_count
    FROM thread AS t
    LEFT JOIN _sched_summary AS ss
      USING (utid)
  )
SELECT
  utid,
  coalesce(max_running_dur, 0) AS max_running_dur,
  coalesce(sum_running_dur, 0) AS sum_running_dur,
  coalesce(running_count, 0) AS running_count,
  coalesce(slice_count, 0) AS slice_count,
  coalesce(perf_sample_count, 0) AS perf_sample_count,
  coalesce(instruments_sample_count, 0) AS instruments_sample_count
FROM raw AS r
WHERE
  NOT (
    r.max_running_dur IS NULL
    AND r.sum_running_dur IS NULL
    AND r.running_count IS NULL
    AND r.slice_count IS NULL AND r.perf_sample_count IS NULL AND r.instruments_sample_count IS NULL
  )
  OR utid IN (
    SELECT
      utid
    FROM cpu_profile_stack_sample
  )
  OR utid IN (
    SELECT
      utid
    FROM thread_counter_track
  );
