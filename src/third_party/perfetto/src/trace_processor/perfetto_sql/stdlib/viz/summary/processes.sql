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

INCLUDE PERFETTO MODULE viz.summary.threads;

CREATE PERFETTO TABLE _process_track_summary AS
SELECT
  upid,
  sum(cnt) AS slice_count
FROM process_track
JOIN _slice_track_summary
  USING (id)
GROUP BY
  upid;

CREATE PERFETTO TABLE _heap_profile_allocation_summary AS
SELECT
  upid,
  count() AS allocation_count
FROM heap_profile_allocation
GROUP BY
  upid;

CREATE PERFETTO TABLE _heap_profile_graph_summary AS
SELECT
  upid,
  count() AS graph_object_count
FROM heap_graph_object
GROUP BY
  upid;

CREATE PERFETTO TABLE _thread_process_grouped_summary AS
SELECT
  upid,
  max(max_running_dur) AS max_running_dur,
  sum(sum_running_dur) AS sum_running_dur,
  sum(running_count) AS running_count,
  sum(slice_count) AS slice_count,
  sum(perf_sample_count) AS perf_sample_count,
  sum(instruments_sample_count) AS instruments_sample_count
FROM _thread_available_info_summary
JOIN thread
  USING (utid)
WHERE
  upid IS NOT NULL
GROUP BY
  upid;

CREATE PERFETTO TABLE _process_available_info_summary AS
WITH
  r AS (
    SELECT
      upid,
      t_summary.upid AS summary_upid,
      t_summary.max_running_dur AS max_running_dur,
      t_summary.sum_running_dur,
      t_summary.running_count,
      t_summary.slice_count AS thread_slice_count,
      t_summary.perf_sample_count AS perf_sample_count,
      t_summary.instruments_sample_count AS instruments_sample_count,
      (
        SELECT
          slice_count
        FROM _process_track_summary
        WHERE
          upid = p.upid
      ) AS process_slice_count,
      (
        SELECT
          allocation_count
        FROM _heap_profile_allocation_summary
        WHERE
          upid = p.upid
      ) AS allocation_count,
      (
        SELECT
          graph_object_count
        FROM _heap_profile_graph_summary
        WHERE
          upid = p.upid
      ) AS graph_object_count
    FROM process AS p
    LEFT JOIN _thread_process_grouped_summary AS t_summary
      USING (upid)
  )
SELECT
  upid,
  coalesce(max_running_dur, 0) AS max_running_dur,
  coalesce(sum_running_dur, 0) AS sum_running_dur,
  coalesce(running_count, 0) AS running_count,
  coalesce(thread_slice_count, 0) AS thread_slice_count,
  coalesce(perf_sample_count, 0) AS perf_sample_count,
  coalesce(instruments_sample_count, 0) AS instruments_sample_count,
  coalesce(process_slice_count, 0) AS process_slice_count,
  coalesce(allocation_count, 0) AS allocation_count,
  coalesce(graph_object_count, 0) AS graph_object_count
FROM r
WHERE
  NOT (
    r.summary_upid IS NULL
    AND process_slice_count IS NULL AND allocation_count IS NULL AND graph_object_count IS NULL
  )
  OR upid IN (
    SELECT
      upid
    FROM process_counter_track
  );
