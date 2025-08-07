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
--

SELECT RUN_METRIC('android/process_metadata.sql');

INCLUDE PERFETTO MODULE android.memory.heap_graph.heap_graph_class_aggregation;

DROP VIEW IF EXISTS java_heap_class_stats_output;
CREATE PERFETTO VIEW java_heap_class_stats_output AS
WITH
-- Group by to build the repeated field by upid, ts
heap_class_stats_count_protos AS (
  SELECT
    upid,
    graph_sample_ts,
    RepeatedField(JavaHeapClassStats_TypeCount(
      'type_name', type_name,
      'is_libcore_or_array', is_libcore_or_array,
      'obj_count', obj_count,
      'size_bytes', size_bytes,
      'native_size_bytes', native_size_bytes,
      'reachable_obj_count', reachable_obj_count,
      'reachable_size_bytes', reachable_size_bytes,
      'reachable_native_size_bytes', reachable_native_size_bytes,
      'dominated_obj_count', dominated_obj_count,
      'dominated_size_bytes', dominated_size_bytes,
      'dominated_native_size_bytes', dominated_native_size_bytes
    )) AS count_protos
  FROM android_heap_graph_class_aggregation s
  GROUP BY 1, 2
),
-- Group by to build the repeated field by upid
heap_class_stats_sample_protos AS (
  SELECT
    upid,
    RepeatedField(JavaHeapClassStats_Sample(
      'ts', graph_sample_ts,
      'type_count', count_protos
    )) AS sample_protos
  FROM heap_class_stats_count_protos
  GROUP BY 1
)
SELECT JavaHeapClassStats(
  'instance_stats', RepeatedField(JavaHeapClassStats_InstanceStats(
    'upid', upid,
    'process', process_metadata.metadata,
    'samples', sample_protos
  )))
FROM heap_class_stats_sample_protos JOIN process_metadata USING (upid);
