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

-- The "reliable range" is defined as follows:
-- 1. If a thread_track has a first_packet_on_sequence flag, the thread data is reliable for the
--    entire duration of the trace.
-- 2. Otherwise, the thread data is reliable from the first thread event till the end of the trace.
-- 3. The "reliable range" is an intersection of reliable thread ranges for all threads such that:
--   a. The number of events on the thread is at or above 25p.
--   b. The event rate for the thread is at or above 75p.
-- Note: this metric considers only chrome processes and their threads, i.e. the ones coming
-- from track_event's.

-- Extracts an int value with the given name from the metadata table.
CREATE OR REPLACE PERFETTO FUNCTION _extract_int_metadata(
  -- The name of the metadata entry.
  name STRING)
-- int_value for the given name. NULL if there's no such entry.
RETURNS LONG AS
SELECT int_value FROM metadata WHERE name = ($name);

DROP VIEW IF EXISTS chrome_event_stats_per_thread;

CREATE PERFETTO VIEW chrome_event_stats_per_thread
AS
SELECT
  COUNT(*) AS cnt, CAST(COUNT(*) AS DOUBLE) / (MAX(ts + dur) - MIN(ts)) AS rate, utid
FROM thread_track
JOIN slice
  ON thread_track.id = slice.track_id
WHERE EXTRACT_ARG(source_arg_set_id, 'source') = 'descriptor'
GROUP BY utid;

DROP VIEW IF EXISTS chrome_event_cnt_cutoff;

-- Ignore the bottom 25% of threads by event count. 25% is a somewhat arbitrary number. It creates a
-- cutoff at around 10 events for a typical trace, and threads with fewer events are usually:
-- 1. Not particularly interesting for the reliable range definition.
-- 2. Create a lot of noise for other metrics, such as event rate.
CREATE PERFETTO VIEW chrome_event_cnt_cutoff
AS
SELECT cnt
FROM
  chrome_event_stats_per_thread
ORDER BY
  cnt
LIMIT
  1
  OFFSET(
    (SELECT COUNT(*) FROM chrome_event_stats_per_thread) / 4);

DROP VIEW IF EXISTS chrome_event_rate_cutoff;

-- Choose the top 25% event rate. 25% is a somewhat arbitrary number. The goal is to strike
-- balance between not cropping too many events and making sure that the chance of data loss in the
-- range declared "reliable" is low.
CREATE PERFETTO VIEW chrome_event_rate_cutoff
AS
SELECT rate
FROM
  chrome_event_stats_per_thread
ORDER BY
  rate
LIMIT
  1
  OFFSET(
    (SELECT COUNT(*) FROM chrome_event_stats_per_thread) * 3 / 4);

DROP VIEW IF EXISTS chrome_reliable_range_per_thread;

-- This view includes only threads with event count and rate above the cutoff points defined
-- above.
-- See b/239830951 for the analysis showing why we don't want to include all threads here
-- (TL;DR - it makes the "reliable range" too short for a typical trace).
CREATE PERFETTO VIEW chrome_reliable_range_per_thread
AS
SELECT
  utid,
  MIN(ts) AS start,
  MAX(IFNULL(EXTRACT_ARG(source_arg_set_id, 'has_first_packet_on_sequence'), 0))
  AS has_first_packet_on_sequence
FROM thread_track
JOIN slice
  ON thread_track.id = slice.track_id
WHERE
  utid IN (
    SELECT utid
    FROM chrome_event_stats_per_thread
    LEFT JOIN chrome_event_cnt_cutoff
      ON 1
    LEFT JOIN chrome_event_rate_cutoff
      ON 1
    WHERE
      chrome_event_stats_per_thread.cnt >= chrome_event_cnt_cutoff.cnt
      AND chrome_event_stats_per_thread.rate >= chrome_event_rate_cutoff.rate
  )
GROUP BY utid;

-- Finds Browser and Renderer processes with a missing main thread. If there
-- is such a process, the trace definitely has thread data loss, and no part
-- of the trace is trustworthy/reliable.
-- As of Jan 2023, all tracing scenarios emit some data from the Browser and
-- Renderer main thread (assuming that the corresponding process is present).
DROP VIEW IF EXISTS chrome_processes_with_missing_main;

CREATE PERFETTO VIEW chrome_processes_with_missing_main
AS
SELECT
  upid
FROM (
  SELECT upid, utid
  FROM process
  LEFT JOIN
    -- We can't use is_main_thread column for Chrome traces - Renderer
    -- processes have is_main_thread = 0 for the logical main thread.
    (SELECT utid, upid FROM thread WHERE thread.name GLOB '*[Mm]ain*')
  USING (upid)
  WHERE
    EXTRACT_ARG(process.arg_set_id, 'chrome.process_type')
      IN ('Browser', 'Renderer', 'Gpu')
)
WHERE utid is NULL;

DROP VIEW IF EXISTS chrome_processes_data_loss_free_period;

CREATE PERFETTO VIEW chrome_processes_data_loss_free_period
AS
SELECT
  upid AS limiting_upid,
  -- If reliable_from is NULL, the process has data loss until the end of the trace.
  IFNULL(reliable_from, (SELECT MAX(ts + dur) FROM slice)) AS start
FROM
  (
    SELECT upid, reliable_from
    FROM experimental_missing_chrome_processes
    UNION ALL
    -- A missing main thread means that the process data is unreliable for the
    -- entire duration of the trace.
    SELECT upid, NULL AS reliable_from
    FROM chrome_processes_with_missing_main
  )
ORDER BY start DESC
LIMIT 1;

DROP VIEW IF EXISTS chrome_reliable_range;

CREATE PERFETTO VIEW chrome_reliable_range
AS
SELECT
  -- If the trace has a cropping packet, we don't want to recompute the reliable
  -- based on cropped track events - the result might be incorrect.
  IFNULL(_extract_int_metadata('range_of_interest_start_us') * 1000,
         MAX(thread_start, data_loss_free_start)) AS start,
  IIF(_extract_int_metadata('range_of_interest_start_us') IS NOT NULL,
      'Range of interest packet',
      IIF(limiting_upid IN (SELECT upid FROM chrome_processes_with_missing_main),
          'Missing main thread for upid=' || limiting_upid,
          IIF(thread_start >= data_loss_free_start,
              'First slice for utid=' || limiting_utid,
               'Missing process data for upid=' || limiting_upid))) AS reason,
  limiting_upid AS debug_limiting_upid,
  limiting_utid AS debug_limiting_utid
FROM
  (SELECT
    COALESCE(MAX(start), 0) AS thread_start,
    utid AS limiting_utid,
    COALESCE((SELECT start FROM chrome_processes_data_loss_free_period), 0) AS data_loss_free_start,
    (SELECT limiting_upid FROM chrome_processes_data_loss_free_period) AS limiting_upid
    FROM chrome_reliable_range_per_thread
    WHERE has_first_packet_on_sequence = 0);
