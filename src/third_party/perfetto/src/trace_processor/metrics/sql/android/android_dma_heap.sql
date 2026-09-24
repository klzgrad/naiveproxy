--
-- Copyright 2021 The Android Open Source Project
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

INCLUDE PERFETTO MODULE android.memory.dmabuf;

DROP VIEW IF EXISTS dma_heap_timeline;
CREATE PERFETTO VIEW dma_heap_timeline AS
SELECT
  ts,
  LEAD(ts, 1, trace_end())
  OVER(PARTITION BY track_id ORDER BY ts) - ts AS dur,
  track_id,
  value
FROM counter JOIN counter_track
  ON counter.track_id = counter_track.id
WHERE (name = 'mem.dma_heap');

DROP VIEW IF EXISTS dma_heap_stats;
CREATE PERFETTO VIEW dma_heap_stats AS
SELECT
  SUM(value * dur) / SUM(dur) AS avg_size,
  MIN(value) AS min_size,
  MAX(value) AS max_size
FROM dma_heap_timeline;

DROP VIEW IF EXISTS android_dma_heap_output;
CREATE PERFETTO VIEW android_dma_heap_output AS
WITH _process_stats AS (
  SELECT process_name, SUM(buf_size) delta
  FROM android_dmabuf_allocs
  GROUP BY 1
)
SELECT AndroidDmaHeapMetric(
  'avg_size_bytes', (SELECT avg_size FROM dma_heap_stats),
  'min_size_bytes', (SELECT min_size FROM dma_heap_stats),
  'max_size_bytes', (SELECT max_size FROM dma_heap_stats),
  'total_alloc_size_bytes', (
    SELECT CAST(SUM(buf_size) AS DOUBLE) FROM android_dmabuf_allocs WHERE buf_size > 0
  ),
  'total_delta_bytes', (SELECT SUM(buf_size) FROM android_dmabuf_allocs),
  'process_stats', (
    SELECT RepeatedField(AndroidDmaHeapMetric_ProcessStats('process_name', process_name, 'delta_bytes', delta))
    FROM _process_stats
  )
);
