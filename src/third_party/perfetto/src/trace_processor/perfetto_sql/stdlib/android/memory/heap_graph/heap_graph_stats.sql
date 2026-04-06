--
-- Copyright 2025 The Android Open Source Project
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

INCLUDE PERFETTO MODULE linux.memory.process;

INCLUDE PERFETTO MODULE android.oom_adjuster;

INCLUDE PERFETTO MODULE android.memory.dmabuf_spans;

-- Returns either the value for the span matching ts exactly, or a data point
-- up to 500ms in the future.
CREATE PERFETTO MACRO _closest_value(
    upid Expr,
    ts Expr,
    t TableOrSubquery,
    value ColumnName
)
RETURNS Expr AS
(
  SELECT
    result
  FROM (
    SELECT
      $t.ts AS vts,
      $t.ts + $t.dur AS vts_end,
      $value AS result,
      abs($t.ts - $ts) AS distance
    FROM $t
    WHERE
      $t.upid = $upid
  )
  WHERE
    $ts BETWEEN vts AND vts_end OR (
      vts > $ts AND distance <= 500 * 1e6
    )
  ORDER BY
    distance
  LIMIT 1
);

-- Table summarizing java heap graphs collected with the ART perfetto module.
-- Contains one row per heap graph, with summary statistics (e.g. total / reachable objects)
-- and memory stats for the corresponding process at the time of the heap dump.
CREATE PERFETTO TABLE android_heap_graph_stats (
  -- The upid of the process.
  upid JOINID(process.id),
  -- The timestamp the heap graph was dumped at.
  graph_sample_ts TIMESTAMP,
  -- The uptime of the process at the time of the heap graph dump.
  process_uptime LONG,
  -- The total (reachable + unreachable) size of the Java heap in bytes.
  total_heap_size LONG,
  -- The total size of native allocations (registered with NativeAllocationRegistry) in bytes.
  -- Does *not* overlap with total_heap_size.
  total_native_alloc_registry_size LONG,
  -- The number of objects in the heap.
  total_obj_count LONG,
  -- The reachable size of the Java heap in bytes.
  reachable_heap_size LONG,
  -- The size of reachable native allocations (registered with NativeAllocationRegistry) in bytes.
  reachable_native_alloc_registry_size LONG,
  -- The number of reachable objects in the heap.
  reachable_obj_count LONG,
  -- The OOM score adj of the process at the time of the heap graph dump.
  oom_score_adj LONG,
  -- The anon RSS + swap size of the process (in bytes) at the time of the heap graph dump.
  anon_rss_and_swap_size LONG,
  -- The dmabuf size of the process (in bytes) at the time of the heap graph dump.
  dmabuf_rss_size LONG
) AS
WITH
  base_stats AS (
    SELECT
      upid,
      graph_sample_ts,
      sum(self_size) AS total_heap_size,
      sum(native_size) AS total_native_alloc_registry_size,
      count(1) AS total_obj_count,
      sum(iif(reachable, self_size, 0)) AS reachable_heap_size,
      sum(iif(reachable, native_size, 0)) AS reachable_native_alloc_registry_size,
      sum(iif(reachable, 1, 0)) AS reachable_obj_count
    FROM heap_graph_object
    GROUP BY
      1,
      2
  )
SELECT
  upid,
  graph_sample_ts,
  graph_sample_ts - process.start_ts AS process_uptime,
  total_heap_size,
  total_native_alloc_registry_size,
  total_obj_count,
  reachable_heap_size,
  reachable_native_alloc_registry_size,
  reachable_obj_count,
  _closest_value!(base_stats.upid, graph_sample_ts, android_oom_adj_intervals, score) AS oom_score_adj,
  _closest_value!(base_stats.upid, graph_sample_ts, memory_rss_and_swap_per_process, anon_rss_and_swap) AS anon_rss_and_swap_size,
  _closest_value!(base_stats.upid, graph_sample_ts, _dmabuf_spans, dmabuf_rss) AS dmabuf_rss_size
FROM base_stats
JOIN process
  USING (upid);
