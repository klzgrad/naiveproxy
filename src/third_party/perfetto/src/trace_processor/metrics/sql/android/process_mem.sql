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

INCLUDE PERFETTO MODULE android.memory.process;
INCLUDE PERFETTO MODULE linux.memory.process;

SELECT RUN_METRIC('android/process_oom_score.sql');

DROP VIEW IF EXISTS anon_rss_span;
CREATE PERFETTO VIEW anon_rss_span AS
SELECT * FROM _anon_rss;

DROP VIEW IF EXISTS file_rss_span;
CREATE PERFETTO VIEW file_rss_span AS
SELECT * FROM _file_rss;

DROP VIEW IF EXISTS shmem_rss_span;
CREATE PERFETTO VIEW shmem_rss_span AS
SELECT * FROM _shmem_rss;

DROP VIEW IF EXISTS swap_span;
CREATE PERFETTO VIEW swap_span AS
SELECT * FROM _swap;

DROP VIEW IF EXISTS anon_and_swap_span;
CREATE PERFETTO VIEW anon_and_swap_span AS
SELECT
  ts, dur, upid,
  IFNULL(anon_rss_val, 0) + IFNULL(swap_val, 0) AS anon_and_swap_val
FROM _anon_swap_sj;

DROP VIEW IF EXISTS rss_and_swap_span;
CREATE PERFETTO VIEW rss_and_swap_span AS
SELECT
  ts,
  dur,
  upid,
  file_rss AS file_rss_val,
  anon_rss AS anon_rss_val,
  shmem_rss AS shmem_rss_val,
  swap AS swap_val,
  COALESCE(file_rss, 0)
    + COALESCE(anon_rss, 0)
    + COALESCE(shmem_rss, 0) AS rss_val,
  COALESCE(file_rss, 0)
    + COALESCE(anon_rss, 0)
    + COALESCE(shmem_rss, 0)
    + COALESCE(swap, 0) AS rss_and_swap_val
FROM _memory_rss_and_swap_per_process_table;

-- If we have dalvik events enabled (for ART trace points) we can construct the java heap timeline.
SELECT RUN_METRIC('android/process_counter_span_view.sql',
  'table_name', 'java_heap_kb',
  'counter_name', 'Heap size (KB)');

DROP VIEW IF EXISTS java_heap_span;
CREATE PERFETTO VIEW java_heap_span AS
SELECT ts, dur, upid, java_heap_kb_val * 1024 AS java_heap_val
FROM java_heap_kb_span;

DROP TABLE IF EXISTS java_heap_by_oom_span;
CREATE VIRTUAL TABLE java_heap_by_oom_span
USING SPAN_JOIN(java_heap_span PARTITIONED upid, oom_score_span PARTITIONED upid);

DROP TABLE IF EXISTS anon_rss_by_oom_span;
CREATE VIRTUAL TABLE anon_rss_by_oom_span
USING SPAN_JOIN(_anon_rss PARTITIONED upid, oom_score_span PARTITIONED upid);

DROP TABLE IF EXISTS file_rss_by_oom_span;
CREATE VIRTUAL TABLE file_rss_by_oom_span
USING SPAN_JOIN(_file_rss PARTITIONED upid, oom_score_span PARTITIONED upid);

DROP TABLE IF EXISTS swap_by_oom_span;
CREATE VIRTUAL TABLE swap_by_oom_span
USING SPAN_JOIN(_swap PARTITIONED upid, oom_score_span PARTITIONED upid);

DROP TABLE IF EXISTS anon_and_swap_by_oom_span;
CREATE VIRTUAL TABLE anon_and_swap_by_oom_span
USING SPAN_JOIN(anon_and_swap_span PARTITIONED upid, oom_score_span PARTITIONED upid);
