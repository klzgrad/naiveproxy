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

INCLUDE PERFETTO MODULE android.garbage_collection;

DROP VIEW IF EXISTS android_garbage_collection_stats_output;
CREATE PERFETTO VIEW android_garbage_collection_stats_output AS
SELECT AndroidGarbageCollectionStats(
  'ts', ts,
  'dur', dur,
  'heap_size_mbs', heap_size_mbs,
  'heap_size_mb', heap_size_mb,
  'heap_allocated_mb', heap_allocated_mb,
  'heap_allocation_rate', heap_allocation_rate,
  'heap_live_mbs', heap_live_mbs,
  'heap_total_mbs', heap_total_mbs,
  'heap_utilization', heap_utilization,
  'gc_running_dur', gc_running_dur,
  'gc_running_rate', gc_running_rate,
  'gc_running_efficiency', gc_running_efficiency,
  'gc_during_android_startup_dur', gc_during_android_startup_dur,
  'total_android_startup_dur', total_android_startup_dur,
  'gc_during_android_startup_rate', gc_during_android_startup_rate,
  'gc_during_android_startup_efficiency', gc_during_android_startup_efficiency,
  'processes', (
    SELECT RepeatedField(
      AndroidGarbageCollectionStats_ProcessStats(
        'process', metadata,
        'heap_size_mbs', heap_size_mbs,
        'heap_size_mb', heap_size_mb,
        'heap_allocated_mb', heap_allocated_mb,
        'heap_allocation_rate', heap_allocation_rate,
        'heap_live_mbs', heap_live_mbs,
        'heap_total_mbs', heap_total_mbs,
        'heap_utilization', heap_utilization,
        'gc_running_dur', gc_running_dur,
        'gc_running_rate', gc_running_rate,
        'gc_running_efficiency', gc_running_efficiency,
        'gc_during_android_startup_dur', gc_during_android_startup_dur,
        'gc_during_android_startup_rate', gc_during_android_startup_rate,
        'gc_during_android_startup_efficiency', gc_during_android_startup_efficiency
      )
    )
    FROM _android_garbage_collection_process_stats
    LEFT JOIN process_metadata using (upid)
  ))
FROM _android_garbage_collection_stats;
