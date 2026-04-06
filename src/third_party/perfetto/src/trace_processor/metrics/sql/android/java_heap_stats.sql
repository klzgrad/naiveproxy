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

INCLUDE PERFETTO MODULE android.memory.heap_graph.heap_graph_stats;
SELECT RUN_METRIC('android/process_metadata.sql');

DROP VIEW IF EXISTS java_heap_stats_output;
CREATE PERFETTO VIEW java_heap_stats_output AS
WITH
-- Group by upid
heap_graph_sample_protos AS (
  SELECT
    upid,
    RepeatedField(JavaHeapStats_Sample(
      'ts', graph_sample_ts,
      'process_uptime_ms', cast_int!(process_uptime / 1e6),
      'heap_size', total_heap_size,
      'heap_native_size', total_native_alloc_registry_size,
      'obj_count', total_obj_count,
      'reachable_heap_size', reachable_heap_size,
      'reachable_heap_native_size', reachable_native_alloc_registry_size,
      'reachable_obj_count', reachable_obj_count,
      'oom_score_adj', oom_score_adj,
      'anon_rss_and_swap_size', anon_rss_and_swap_size,
      'dmabuf_rss_size', dmabuf_rss_size
    )) AS sample_protos
  FROM android_heap_graph_stats
  GROUP BY 1
)
SELECT JavaHeapStats(
  'instance_stats', RepeatedField(JavaHeapStats_InstanceStats(
    'upid', upid,
    'process', process_metadata.metadata,
    'samples', sample_protos
  )))
FROM heap_graph_sample_protos JOIN process_metadata USING (upid);
