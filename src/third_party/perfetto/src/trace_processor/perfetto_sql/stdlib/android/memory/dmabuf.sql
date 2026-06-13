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

-- Raw ftrace events
CREATE PERFETTO TABLE _raw_dmabuf_events AS
SELECT
  extract_arg(arg_set_id, 'inode') AS inode,
  tt.utid,
  c.ts,
  cast_int!(c.value) AS buf_size
FROM thread_counter_track AS tt
JOIN counter AS c
  ON c.track_id = tt.id
WHERE
  tt.name = 'mem.dma_heap_change';

-- gralloc binder reply slices
CREATE PERFETTO TABLE _gralloc_binders AS
WITH
  gralloc_threads AS (
    SELECT
      utid
    FROM process
    JOIN thread
      USING (upid)
    WHERE
      process.name GLOB '/vendor/bin/hw/android.hardware.graphics.allocator*'
      OR process.name GLOB '/vendor/bin/hw/*gralloc.allocator*'
  )
SELECT
  flow.slice_out AS client_slice_id,
  gralloc_slice.ts,
  gralloc_slice.dur,
  gralloc_tt.utid
FROM slice AS gralloc_slice
JOIN thread_track AS gralloc_tt
  ON gralloc_slice.track_id = gralloc_tt.id
JOIN gralloc_threads
  USING (utid)
JOIN flow
  ON gralloc_slice.id = flow.slice_in
WHERE
  gralloc_slice.name = 'binder reply';

-- Match gralloc thread allocations to inbound binders
CREATE PERFETTO TABLE _attributed_dmabufs AS
SELECT
  r.inode,
  r.ts,
  r.buf_size,
  coalesce(client_tt.utid, r.utid) AS attr_utid
FROM _raw_dmabuf_events AS r
LEFT JOIN _gralloc_binders AS gb
  ON r.utid = gb.utid AND r.ts BETWEEN gb.ts AND gb.ts + gb.dur
LEFT JOIN slice AS client_slice
  ON client_slice.id = gb.client_slice_id
LEFT JOIN thread_track AS client_tt
  ON client_slice.track_id = client_tt.id
ORDER BY
  r.inode,
  r.ts;

CREATE PERFETTO FUNCTION _alloc_source(
    is_alloc BOOL,
    inode LONG,
    ts TIMESTAMP
)
RETURNS LONG AS
SELECT
  attr_utid
FROM _attributed_dmabufs
WHERE
  inode = $inode
  AND (
    (
      $is_alloc AND ts = $ts
    ) OR (
      NOT $is_alloc AND ts < $ts
    )
  )
ORDER BY
  ts DESC
LIMIT 1;

-- Track dmabuf allocations, re-attributing gralloc allocations to their source
-- (if binder transactions to gralloc are recorded).
CREATE PERFETTO TABLE android_dmabuf_allocs (
  -- timestamp of the allocation
  ts TIMESTAMP,
  -- allocation size (will be negative for release)
  buf_size LONG,
  -- dmabuf inode
  inode LONG,
  -- utid of thread responsible for the allocation
  -- if a dmabuf is allocated by gralloc we follow the binder transaction
  -- to the requesting thread (requires binder tracing)
  utid JOINID(thread.id),
  -- tid of thread responsible for the allocation
  tid LONG,
  -- thread name
  thread_name STRING,
  -- upid of process responsible for the allocation (matches utid)
  upid JOINID(process.id),
  -- pid of process responsible for the allocation
  pid LONG,
  -- process name
  process_name STRING
) AS
WITH
  _thread_allocs AS (
    SELECT
      inode,
      ts,
      buf_size,
      _alloc_source(buf_size > 0, inode, ts) AS utid
    FROM _attributed_dmabufs
  )
SELECT
  ts,
  buf_size,
  inode,
  utid,
  tid,
  thread.name AS thread_name,
  upid,
  pid,
  process.name AS process_name
FROM _thread_allocs AS allocs
JOIN thread
  USING (utid)
LEFT JOIN process
  USING (upid)
ORDER BY
  ts;

-- Provides a timeseries of dmabuf allocations for each process.
-- To populate this table, tracing must be enabled with the "dmabuf_allocs" ftrace event.
CREATE PERFETTO TABLE android_memory_cumulative_dmabuf (
  -- upid of process responsible for the allocation (matches utid)
  upid JOINID(process.id),
  -- process name
  process_name STRING,
  -- utid of thread responsible for the allocation
  -- if a dmabuf is allocated by gralloc we follow the binder transaction
  -- to the requesting thread (requires binder tracing)
  utid JOINID(thread.id),
  -- thread name
  thread_name STRING,
  -- timestamp of the allocation
  ts TIMESTAMP,
  -- total allocation size per process and thread
  value LONG
) AS
SELECT
  upid,
  process_name,
  utid,
  thread_name,
  ts,
  sum(buf_size) OVER (PARTITION BY coalesce(upid, utid) ORDER BY ts) AS value
FROM android_dmabuf_allocs;
