--
-- Copyright 2024 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the 'License');
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an 'AS IS' BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

INCLUDE PERFETTO MODULE counters.intervals;

INCLUDE PERFETTO MODULE linux.memory.process;

CREATE PERFETTO TABLE _memory_rss_high_watermark_per_process_table AS
WITH
  with_rss AS (
    SELECT
      ts,
      dur,
      upid,
      coalesce(file_rss, 0) + coalesce(anon_rss, 0) + coalesce(shmem_rss, 0) AS rss
    FROM _memory_rss_and_swap_per_process_table
  ),
  high_watermark_as_counter AS (
    SELECT
      ts,
      max(rss) OVER (PARTITION BY upid ORDER BY ts) AS value,
      -- `id` and `track_id` are hacks to use this table in
      -- `counter_leading_intervals` macro. As `track_id` is using for looking
      -- for duplicates, we are aliasing `upid` with it. `Id` is ignored by the macro.
      upid AS track_id,
      0 AS id
    FROM with_rss
  )
SELECT
  ts,
  dur,
  track_id AS upid,
  cast_int!(value) AS rss_high_watermark
FROM counter_leading_intervals!(high_watermark_as_counter);

-- For each process fetches the memory high watermark until or during
-- timestamp.
CREATE PERFETTO VIEW memory_rss_high_watermark_per_process (
  -- Timestamp
  ts TIMESTAMP,
  -- Duration
  dur DURATION,
  -- Upid of the process
  upid JOINID(process.id),
  -- Pid of the process
  pid LONG,
  -- Name of the process
  process_name STRING,
  -- Maximum `rss` value until now
  rss_high_watermark LONG
) AS
SELECT
  ts,
  dur,
  upid,
  pid,
  name AS process_name,
  rss_high_watermark
FROM _memory_rss_high_watermark_per_process_table
JOIN process
  USING (upid);
