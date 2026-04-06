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

INCLUDE PERFETTO MODULE android.oom_adjuster;

INCLUDE PERFETTO MODULE linux.memory.process;

-- OOM score tables

CREATE VIRTUAL TABLE _mem_ooms_sj USING SPAN_OUTER_JOIN (
  android_oom_adj_intervals PARTITIONED upid,
  _memory_rss_and_swap_per_process_table PARTITIONED upid);

-- Process memory and it's OOM adjuster scores. Detects transitions, each new
-- interval means that either the memory or OOM adjuster score of the process changed.
CREATE PERFETTO TABLE memory_oom_score_with_rss_and_swap_per_process (
  -- Timestamp the oom_adj score or memory of the process changed
  ts TIMESTAMP,
  -- Duration until the next oom_adj score or memory change of the process.
  dur DURATION,
  -- oom adjuster score of the process.
  score LONG,
  -- oom adjuster bucket of the process.
  bucket STRING,
  -- Upid of the process having an oom_adj update.
  upid JOINID(process.id),
  -- Name of the process having an oom_adj update.
  process_name STRING,
  -- Pid of the process having an oom_adj update.
  pid LONG,
  -- Slice of the latest oom_adj update in the system_server.
  oom_adj_id JOINID(slice.id),
  -- Timestamp of the latest oom_adj update in the system_server.
  oom_adj_ts TIMESTAMP,
  -- Duration of the latest oom_adj update in the system_server.
  oom_adj_dur DURATION,
  -- Track of the latest oom_adj update in the system_server. Alias of
  -- `track.id`.
  oom_adj_track_id JOINID(track.id),
  -- Thread name of the latest oom_adj update in the system_server.
  oom_adj_thread_name STRING,
  -- Reason for the latest oom_adj update in the system_server.
  oom_adj_reason STRING,
  -- Trigger for the latest oom_adj update in the system_server.
  oom_adj_trigger STRING,
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
  score,
  bucket,
  upid,
  process_name,
  pid,
  oom_adj_id,
  oom_adj_ts,
  oom_adj_dur,
  oom_adj_track_id,
  oom_adj_thread_name,
  oom_adj_reason,
  oom_adj_trigger,
  anon_rss,
  file_rss,
  shmem_rss,
  file_rss + anon_rss + coalesce(shmem_rss, 0) AS rss,
  swap,
  anon_rss + coalesce(swap, 0) AS anon_rss_and_swap,
  anon_rss + file_rss + coalesce(shmem_rss, 0) + coalesce(swap, 0) AS rss_and_swap
FROM _mem_ooms_sj
JOIN process
  USING (upid);
