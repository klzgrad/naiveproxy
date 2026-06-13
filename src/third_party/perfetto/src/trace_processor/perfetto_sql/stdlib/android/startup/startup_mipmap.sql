--
-- Copyright 2026 The Android Open Source Project
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

INCLUDE PERFETTO MODULE intervals.mipmap;

INCLUDE PERFETTO MODULE android.startup.startups;

INCLUDE PERFETTO MODULE android.process_metadata;

INCLUDE PERFETTO MODULE intervals.intersect;

INCLUDE PERFETTO MODULE slices.flat_slices;

INCLUDE PERFETTO MODULE sched.states;

-- Create a table with unique startup events, including thread and process information.
CREATE PERFETTO TABLE _mipmap_startup AS
SELECT
  android_startups.startup_id AS id,
  android_startups.*,
  upid,
  utid
FROM android_startups
JOIN android_startup_processes
  USING (startup_id)
JOIN thread
  USING (upid)
JOIN android_process_metadata
  USING (upid)
WHERE
  dur > 0 AND is_main_thread
ORDER BY
  ts;

-- Flatten slices that occur within the startups.
CREATE PERFETTO TABLE _mipmap_flat_slice AS
SELECT
  _slice_flattened.*
FROM _slice_flattened
JOIN _mipmap_startup
  USING (utid);

-- Span join flattened slices with thread states to get thread state information for each slice.
CREATE VIRTUAL TABLE _mipmap_flat_slice_thread_states_and_slices_sp USING SPAN_LEFT_JOIN (
    thread_state PARTITIONED utid,
    _mipmap_flat_slice PARTITIONED utid);

-- Create a table from the span join results.
CREATE PERFETTO TABLE _mipmap_flat_slice_thread_states_and_slices AS
SELECT
  row_number() OVER () AS id,
  ts,
  dur,
  utid,
  slice_id,
  depth,
  name,
  sched_state_to_human_readable_string(state) AS state,
  cpu,
  io_wait,
  blocked_function,
  coalesce(name, sched_state_to_human_readable_string(state)) AS synth_name,
  irq_context
FROM _mipmap_flat_slice_thread_states_and_slices_sp;

-- Intersect the slices with thread states with the unique startup intervals.
-- This table contains the slices that occurred during each startup.
CREATE PERFETTO TABLE _mipmap_startup_slice AS
SELECT
  ii.ts,
  ii.dur,
  slice.utid,
  slice.id AS slice_id,
  slice.depth,
  slice.name,
  slice.synth_name,
  slice.cpu,
  slice.state,
  slice.io_wait,
  slice.blocked_function,
  slice.irq_context,
  startup.startup_id,
  startup.upid,
  startup.startup_type,
  startup.package,
  startup.dur AS startup_dur
FROM _interval_intersect
    !(
      (
        (SELECT * FROM _mipmap_flat_slice_thread_states_and_slices WHERE dur > -1), (_mipmap_startup)),
      (utid)) AS ii
JOIN _mipmap_flat_slice_thread_states_and_slices AS slice
  ON slice.id = ii.id_0
JOIN _mipmap_startup AS startup
  ON startup.id = ii.id_1;

-- ------------------------------------------------------------------
-- MIPMAP Generation Call
-- ------------------------------------------------------------------

--
-- Creates 1ms buckets for startup intervals.
--
-- This table uses the `_mipmap_buckets_table` macro to generate a series of
-- 1-ms buckets for each startup. These buckets will be used to
-- aggregate and summarize the startup activity.
CREATE PERFETTO TABLE _mipmap_startup_buckets_1ms AS
SELECT
  *
FROM _mipmap_buckets_table!(
  -- Source table for time range
  (SELECT ts, dur, startup_id FROM _mipmap_startup_slice),
  -- Partitioning column
  startup_id,
  -- Bucket duration in nanoseconds
  1e6  -- 1ms buckets
)
ORDER BY
  id;

--
-- Prepares startup slices for mipmapping.
--
-- This table assigns a unique ID and a `group_hash` to each startup slice. The
-- `group_hash` is created from various properties of the slice, such as its
-- name, depth, and thread state. Slices with the same `group_hash` are
-- considered similar and can be merged during the mipmapping process.
CREATE PERFETTO TABLE _mipmap_startup_slices_with_ids AS
SELECT
  row_number() OVER (ORDER BY ts) AS id,
  hash(
    coalesce(name, ''),
    coalesce(state, ''),
    coalesce(depth, ''),
    coalesce(io_wait, ''),
    coalesce(blocked_function, '')
  ) AS group_hash,
  *
FROM _mipmap_startup_slice
ORDER BY
  id;

-- Creates a 1ms resolution mipmap of Android startup slices.
--
-- This table uses the `_mipmap_merged` macro to generate a
-- mipmap of the startup slices. The mipmap provides a summarized view of the
-- startup, with a resolution of 1 ms. The table contains merged slices
-- representing the dominant event in each time bucket.
CREATE PERFETTO TABLE _android_startup_mipmap_1ms (
  -- timestamp of the merged slice
  ts TIMESTAMP,
  -- duration of the merged slice
  dur DURATION,
  -- unique startup id
  startup_id JOINID(android_startups.startup_id),
  -- upid of the startup
  upid LONG,
  -- package name
  package STRING,
  -- startup type
  startup_type STRING,
  -- original startup duration
  startup_dur DURATION,
  -- slice name of the dominant event
  name STRING,
  -- thread state of the dominant event
  state STRING,
  -- slice depth of the dominant event
  depth LONG,
  -- whether the thread was in io_wait
  io_wait LONG,
  -- blocked function
  blocked_function STRING
) AS
SELECT
  mm.ts,
  mm.dur,
  s.startup_id,
  s.upid,
  s.package,
  s.startup_type,
  s.startup_dur,
  -- properties from the representative slice, must be present in the group_hash
  s.name,
  s.state,
  s.depth,
  s.io_wait,
  s.blocked_function
FROM _mipmap_merged!(
  _mipmap_startup_slices_with_ids,
  _mipmap_startup_buckets_1ms,
  startup_id,
  1e6  -- 1ms buckets
) AS mm
JOIN _mipmap_startup_slices_with_ids AS s
  USING (id);
