--
-- Copyright 2023 The Android Open Source Project
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

-- TODO(b/329344794): Rewrite to fetch data from other tables than `raw`.

-- Aggregates f2fs IO and latency stats by counter name.
CREATE PERFETTO VIEW _android_io_f2fs_counter_stats (
  -- Counter name on which all the other values are aggregated on.
  name STRING,
  -- Sum of all counter values for the counter name.
  sum DOUBLE,
  -- Max of all counter values for the counter name.
  max DOUBLE,
  -- Min of all counter values for the counter name.
  min DOUBLE,
  -- Duration between the first and last counter value for the counter name.
  dur DURATION,
  -- Count of all the counter values for the counter name.
  count LONG,
  -- Avergate of all the counter values for the counter name.
  avg DOUBLE
) AS
SELECT
  str_split(counter_track.name, '].', 1) AS name,
  sum(counter.value) AS sum,
  max(counter.value) AS max,
  min(counter.value) AS min,
  max(ts) - min(ts) AS dur,
  count(ts) AS count,
  avg(counter.value) AS avg
FROM counter
JOIN counter_track
  ON counter_track.id = counter.track_id AND counter_track.name GLOB '*f2fs*'
GROUP BY
  name
ORDER BY
  sum DESC;

-- Aggregates f2fs_write stats by inode and thread.
CREATE PERFETTO VIEW _android_io_f2fs_write_stats (
  -- Utid of the thread.
  utid JOINID(thread.id),
  -- Tid of the thread.
  tid LONG,
  -- Name of the thread.
  thread_name STRING,
  -- Upid of the process.
  upid JOINID(process.id),
  -- Pid of the process.
  pid LONG,
  -- Name of the thread.
  process_name STRING,
  -- Inode number of the file being written.
  ino LONG,
  -- Device node number of the file being written.
  dev LONG,
  -- Total number of bytes written on this file by the |utid|.
  bytes LONG,
  -- Total count of write requests for this file.
  write_count LONG
) AS
WITH
  f2fs_write_end AS (
    SELECT
      *,
      extract_arg(arg_set_id, 'len') AS len,
      extract_arg(arg_set_id, 'dev') AS dev,
      extract_arg(arg_set_id, 'ino') AS ino,
      extract_arg(arg_set_id, 'copied') AS copied
    FROM ftrace_event
    WHERE
      name GLOB 'f2fs_write_end*'
  )
SELECT
  thread.utid,
  thread.tid,
  thread.name AS thread_name,
  process.upid,
  process.pid,
  process.name AS process_name,
  f.ino,
  f.dev,
  sum(copied) AS bytes,
  count(len) AS write_count
FROM f2fs_write_end AS f
JOIN thread
  USING (utid)
JOIN process
  USING (upid)
GROUP BY
  utid,
  ino,
  dev
ORDER BY
  bytes DESC;

-- Aggregates f2fs write stats. Counts distinct datapoints, total write operations,
-- and bytes written
CREATE PERFETTO VIEW _android_io_f2fs_aggregate_write_stats (
  -- Total number of writes in the trace.
  total_write_count LONG,
  -- Number of distinct processes.
  distinct_processes LONG,
  -- Total number of bytes written.
  total_bytes_written LONG,
  -- Count of distinct devices written to.
  distinct_device_count LONG,
  -- Count of distinct inodes written to.
  distinct_inode_count LONG,
  -- Count of distinct threads writing.
  distinct_thread_count LONG
) AS
SELECT
  sum(write_count) AS total_write_count,
  count(DISTINCT pid) AS distinct_processes,
  sum(bytes) AS total_bytes_written,
  count(DISTINCT dev) AS distinct_device_count,
  count(DISTINCT ino) AS distinct_inode_count,
  count(DISTINCT tid) AS distinct_thread_count
FROM _android_io_f2fs_write_stats;
