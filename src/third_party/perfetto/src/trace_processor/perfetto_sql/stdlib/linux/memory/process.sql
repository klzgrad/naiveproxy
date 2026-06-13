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

INCLUDE PERFETTO MODULE linux.memory.general;

-- All memory counters tables.

CREATE PERFETTO VIEW _anon_rss AS
SELECT
  ts,
  dur,
  upid,
  value AS anon_rss_val
FROM _all_counters_per_process
WHERE
  name = 'mem.rss.anon';

CREATE PERFETTO VIEW _file_rss AS
SELECT
  ts,
  dur,
  upid,
  value AS file_rss_val
FROM _all_counters_per_process
WHERE
  name = 'mem.rss.file';

CREATE PERFETTO VIEW _shmem_rss AS
SELECT
  ts,
  dur,
  upid,
  value AS shmem_rss_val
FROM _all_counters_per_process
WHERE
  name = 'mem.rss.shmem';

CREATE PERFETTO VIEW _swap AS
SELECT
  ts,
  dur,
  upid,
  value AS swap_val
FROM _all_counters_per_process
WHERE
  name = 'mem.swap';

-- Span joins

CREATE VIRTUAL TABLE _anon_swap_sj USING SPAN_OUTER_JOIN (
  _anon_rss PARTITIONED upid,
  _swap PARTITIONED upid);

CREATE VIRTUAL TABLE _anon_swap_file_sj USING SPAN_OUTER_JOIN (
  _anon_swap_sj PARTITIONED upid,
  _file_rss PARTITIONED upid
);

CREATE VIRTUAL TABLE _rss_swap_sj USING SPAN_OUTER_JOIN (
  _anon_swap_file_sj PARTITIONED upid,
  _shmem_rss PARTITIONED upid
);

CREATE PERFETTO TABLE _memory_rss_and_swap_per_process_table AS
SELECT
  ts,
  dur,
  upid,
  cast_int!(anon_rss_val) AS anon_rss,
  cast_int!(file_rss_val) AS file_rss,
  cast_int!(shmem_rss_val) AS shmem_rss,
  cast_int!(swap_val) AS swap
FROM _rss_swap_sj;

-- Memory metrics timeline for each process.
CREATE PERFETTO VIEW memory_rss_and_swap_per_process (
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
  -- Anon RSS counter value
  anon_rss LONG,
  -- File RSS counter value
  file_rss LONG,
  -- Shared memory RSS counter value
  shmem_rss LONG,
  -- Total RSS value. Sum of `anon_rss`, `file_rss` and `shmem_rss`. Returns
  -- value even if one of the values is NULL.
  rss LONG,
  -- Swap counter value
  swap LONG,
  -- Sum or `anon_rss` and `swap`. Returns value even if one of the values is
  -- NULL.
  anon_rss_and_swap LONG,
  -- Sum or `rss` and `swap`. Returns value even if one of the values is NULL.
  rss_and_swap LONG
) AS
SELECT
  ts,
  dur,
  upid,
  pid,
  name AS process_name,
  anon_rss,
  file_rss,
  shmem_rss,
  -- We do COALESCE only on `shmem_rss` and `swap`, as it can be expected all
  -- process start to emit anon rss and file rss events (you'll need to at
  -- least read code and have some memory to work with) - so the NULLs are real
  --  values. But it is possible that you will never swap or never use shmem,
  -- so those values are expected to often be NULLs, which shouldn't propagate
  -- into the values like `anon_and_swap` or `rss`.
  file_rss + anon_rss + coalesce(shmem_rss, 0) AS rss,
  swap,
  anon_rss + coalesce(swap, 0) AS anon_rss_and_swap,
  anon_rss + file_rss + coalesce(shmem_rss, 0) + coalesce(swap, 0) AS rss_and_swap
FROM _memory_rss_and_swap_per_process_table
JOIN process
  USING (upid);
